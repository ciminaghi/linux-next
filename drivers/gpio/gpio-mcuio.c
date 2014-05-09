/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

#define DEBUG

/* mcuio host controller driver */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/gpio.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>

struct mcuio_gpio {
	struct regmap		*map;
	struct gpio_chip	chip;
	u32			out[2];
};

static int mcuio_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct mcuio_gpio *gpio = container_of(chip, struct mcuio_gpio, chip);
	struct regmap *map = gpio->map;
	int roffset = offset / 32;
	u32 mask = 1 << (offset - (roffset * 32));
	u32 in;

	pr_debug("%s invoked, offset = %u\n", __func__, offset);
	regmap_read(map, 0x910 + roffset * sizeof(u32), &in);
	pr_debug("%s: in = 0x%08x\n", __func__, in);
	return in & mask ? 1 : 0;
}

static void mcuio_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcuio_gpio *gpio = container_of(chip, struct mcuio_gpio, chip);
	struct regmap *map = gpio->map;
	int roffset = offset / 32;
	u32 mask = 1 << (offset - (roffset * 32));
	gpio->out[roffset] &= ~mask;
	if (value)
		gpio->out[roffset] |= mask;
	pr_debug("%s invoked, offset = 0x%08x, value = 0x%08x\n",
		 __func__, offset, value);
	regmap_write(map, 0x910 + roffset * sizeof(u32), gpio->out[roffset]);
}

static int mcuio_gpio_input(struct gpio_chip *chip, unsigned offset)
{
	struct mcuio_gpio *gpio = container_of(chip, struct mcuio_gpio, chip);
	struct regmap *map = gpio->map;
	int ret, shift;
	unsigned curr_addr;
	u32 curr;

	curr_addr = 0x510 + (offset / sizeof(u32)) * sizeof(u32);
	pr_debug("%s: invoking regmap_read @0x%04x\n", __func__, curr_addr);
	ret = regmap_read(map, curr_addr, &curr);
	if (ret < 0) {
		pr_err("%s: error reading curr config\n", __func__);
		return ret;
	}
	shift = (offset % sizeof(u32)) * 8;

	pr_debug("%s: curr = 0x%08x, shift = %d\n", __func__, curr, shift);

	curr &= ~(0x3 << shift);
	curr |= (0x01 << shift);

	pr_debug("%s invoked, offset = 0x%08x, writing 0x%08x to 0x%08x\n",
		 __func__, offset, curr, curr_addr);
	return regmap_write(map, curr_addr, curr);
}

static int mcuio_gpio_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct mcuio_gpio *gpio = container_of(chip, struct mcuio_gpio, chip);
	struct regmap *map = gpio->map;
	int ret, shift;
	unsigned curr_addr;
	u32 curr;

	/* Set value first */
	mcuio_gpio_set(chip, offset, value);

	/* Next configure the gpio as an output */
	curr_addr = 0x510 + (offset / sizeof(u32)) * sizeof(u32);
	pr_debug("%s: invoking regmap_read @0x%04x\n", __func__, curr_addr);
	ret = regmap_read(map, curr_addr, &curr);
	if (ret < 0) {
		pr_err("%s: error reading curr config\n", __func__);
		return ret;
	}
	shift = (offset % sizeof(u32)) * 8;

	pr_debug("%s: curr = 0x%08x, shift = %d\n", __func__, curr, shift);

	curr &= ~(0x3 << shift);
	curr |= (0x02 << shift);

	pr_debug("%s invoked, offset = 0x%08x, writing 0x%08x to 0x%08x\n",
		 __func__, offset, curr, curr_addr);
	return regmap_write(map, curr_addr, curr);
}

static const struct regmap_config mcuio_gpio_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0x930,
	.cache_type = REGCACHE_NONE,
};

static int mcuio_gpio_probe(struct mcuio_device *mdev)
{
	struct mcuio_gpio *g;
	struct regmap *map;
	unsigned int ngpios;
	int ret;
	struct mcuio_device *hc = to_mcuio_dev(mdev->dev.parent);

	if (!hc) {
		dev_err(&mdev->dev, "no parent for device\n");
		return -EINVAL;
	}
	map = devm_regmap_init_mcuio(mdev, &mcuio_gpio_regmap_config);
	if (IS_ERR(map)) {
		dev_err(&mdev->dev, "cannot setup regmap for device\n");
		return PTR_ERR(map);
	}
	ret = regmap_read(map, 0xc, &ngpios);
	if (ret < 0) {
		dev_err(&mdev->dev, "error reading number of gpios\n");
		return ret;
	}
	pr_debug("%s %d, ngpios = %d\n", __func__, __LINE__, ngpios);
	g = devm_kzalloc(&mdev->dev, sizeof(*g), GFP_KERNEL);
	if (!g)
		return -ENOMEM;

	g->map = map;
	g->chip.base			= 100;
	g->chip.can_sleep		= 1;
	g->chip.dev			= &mdev->dev;
	g->chip.owner			= THIS_MODULE;
	g->chip.get			= mcuio_gpio_get;
	g->chip.set			= mcuio_gpio_set;
	g->chip.direction_input		= mcuio_gpio_input;
	g->chip.direction_output	= mcuio_gpio_output;
	g->chip.ngpio			= ngpios;
	regmap_read(map, 0x910, &g->out[0]);
	regmap_read(map, 0x914, &g->out[1]);
	pr_debug("%s: initial state = 0x%08x-0x%08x\n", __func__, g->out[0],
		 g->out[1]);

	pr_debug("%s: max gpios = %d\n", __func__, ARCH_NR_GPIOS);
	ret = gpiochip_add(&g->chip);
	if (ret) {
		pr_err("Error %d adding gpiochip\n", ret);
		return ret;
	}

	dev_set_drvdata(&mdev->dev, g);
	return 0;
}

static int mcuio_gpio_remove(struct mcuio_device *mdev)
{
	struct mcuio_gpio *g = dev_get_drvdata(&mdev->dev);
	if (!g) {
		dev_err(&mdev->dev, "%s: no drvdata", __func__);
		return -EINVAL;
	}
	return gpiochip_remove(&g->chip);
}

static const struct mcuio_device_id gpio_drv_ids[] = {
	{
		.class = MCUIO_CLASS_GPIO,
		.class_mask = 0xffff,
	},
	/* Terminator */
	{
		.device = MCUIO_NO_DEVICE,
		.class = MCUIO_CLASS_UNDEFINED,
	},
};

static struct mcuio_driver mcuio_gpio_driver = {
	.driver = {
		.name = "mcuio-gpio",
	},
	.id_table = gpio_drv_ids,
	.probe = mcuio_gpio_probe,
	.remove = mcuio_gpio_remove,
};

static int __init mcuio_gpio_init(void)
{
	return mcuio_driver_register(&mcuio_gpio_driver, THIS_MODULE);
}

static void __exit mcuio_gpio_exit(void)
{
	return mcuio_driver_unregister(&mcuio_gpio_driver);
}

subsys_initcall(mcuio_gpio_init);
module_exit(mcuio_gpio_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO gpio generic driver");
MODULE_LICENSE("GPL v2");
