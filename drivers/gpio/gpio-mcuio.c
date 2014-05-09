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
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>

struct mcuio_gpio {
	struct regmap		*map;
	struct gpio_chip	chip;
	struct irq_chip		irqchip;
	int			irq_base;
	u32			out[2];
	char			label[20];
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

static int mcuio_gpio_to_irq(struct gpio_chip *chip, unsigned offset)
{
	struct mcuio_gpio *gpio = container_of(chip, struct mcuio_gpio, chip);
	return gpio->irq_base > 0 ? gpio->irq_base + offset : -ENXIO;
}


static const struct regmap_config mcuio_gpio_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0x930,
	.cache_type = REGCACHE_NONE,
};

static irqreturn_t mcuio_gpio_irq_handler(int irq, void *devid)
{
	int i, stat;
	/* read events status */
	uint32_t status[2];
	struct mcuio_gpio *gpio = devid;
	struct regmap *map = gpio->map;

	/* mask __before__ reading status */
	pr_debug("%s entered\n", __func__);

	pr_debug("%s: reading status[0]\n", __func__);
	stat = regmap_read(map, 0x928, &status[0]);
	if (stat < 0) {
		dev_err(gpio->chip.dev,
			"%s: error reading gpio status[0]\n", __func__);
		goto end;
	}
	pr_debug("%s: reading status[1]\n", __func__);
	stat = regmap_read(map, 0x92c, &status[1]);
	if (stat < 0) {
		dev_err(gpio->chip.dev,
			"%s: error reading gpio status[1]\n", __func__);
		goto end;
	}
	for (i = 0; i < gpio->chip.ngpio; i++)
		if (test_bit(i, (unsigned long *)status)) {
			pr_debug("%s: invoking handler for irq %d\n",
				 __func__, gpio->irq_base + i);
			handle_nested_irq(gpio->irq_base + i);
		}
end:
	pr_debug("leaving %s\n", __func__);
	return IRQ_HANDLED;
}

static int __mcuio_gpio_set_irq_flags(struct irq_data *d, u8 flags, u8 mask)
{
	struct mcuio_gpio *g = irq_data_get_irq_chip_data(d);
	unsigned gpio = d->irq - g->irq_base;
	struct regmap *map = g->map;
	u32 s;
	unsigned addr = gpio + 0x710;
	int shift = (8 * (addr % sizeof(u32))), ret;

	ret = regmap_read(map, (addr / sizeof(u32)) * sizeof(u32), &s);
	if (ret < 0) {
		dev_err(g->chip.dev, "could not read curr evts flags\n");
		return ret;
	}
	s &= ~(((u32)mask) << shift);
	ret = regmap_write(map, (addr / sizeof(u32)) * sizeof(u32),
			   s | ((u32)flags) << shift);
	if (ret < 0)
		dev_err(g->chip.dev, "could not set new evts flags\n");
	return ret < 0 ? ret : 0;
}

static void mcuio_gpio_irq_unmask(struct irq_data *d)
{
	(void)__mcuio_gpio_set_irq_flags(d, 0x80, 0x80);
}

static void mcuio_gpio_irq_mask(struct irq_data *d)
{
	(void)__mcuio_gpio_set_irq_flags(d, 0x00, 0x80);
}

static int mcuio_gpio_irq_set_type(struct irq_data *d, unsigned int flow_type)
{
	u8 v = 0;
	unsigned int t = flow_type & IRQF_TRIGGER_MASK;
	if ((t & IRQF_TRIGGER_HIGH) || (t & IRQF_TRIGGER_LOW))
		return -EINVAL;
	if (t & IRQF_TRIGGER_RISING)
		v |= 1;
	if (t & IRQF_TRIGGER_FALLING)
		v |= 2;
	return __mcuio_gpio_set_irq_flags(d, v, 0x7f);
}

static int mcuio_gpio_probe(struct mcuio_device *mdev)
{
	struct mcuio_gpio *g;
	struct regmap *map;
	unsigned int ngpios;
	char *names, **names_ptr;
	int i, ret;
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
	g->chip.to_irq			= mcuio_gpio_to_irq;
	g->chip.ngpio			= ngpios;
	g->irqchip.irq_set_type		= mcuio_gpio_irq_set_type;
	g->irqchip.irq_mask		= mcuio_gpio_irq_mask;
	g->irqchip.irq_unmask		= mcuio_gpio_irq_unmask;
	regmap_read(map, 0x910, &g->out[0]);
	regmap_read(map, 0x914, &g->out[1]);
	pr_debug("%s: initial state = 0x%08x-0x%08x\n", __func__, g->out[0],
		 g->out[1]);
	snprintf(g->label, sizeof(g->label), "mcuio-%u:%u.%u", mdev->bus,
		 mdev->device, mdev->fn);

	/*
	 * Get 8 bytes per gpio label. Labels are actually 4 bytes long, plus
	 * a terminator
	 */
	names = devm_kzalloc(&mdev->dev, 8 * g->chip.ngpio, GFP_KERNEL);
	if (!names) {
		dev_err(&mdev->dev, "no memory for gpio names\n");
		return -ENOMEM;
	}
	names_ptr = devm_kzalloc(&mdev->dev, g->chip.ngpio * sizeof(char *),
				 GFP_KERNEL);
	if (!names_ptr) {
		dev_err(&mdev->dev, "no memory for gpio names ptrs array\n");
		return -ENOMEM;
	}
	for (i = 0; i < g->chip.ngpio; i++) {
		regmap_read(map, 0x10 + i*4, (u32 *)&names[i*8]);
		dev_dbg(&mdev->dev, "found gpio %s\n", &names[i*8]);
		names_ptr[i] = &names[i*8];
	}
	g->chip.names = (const char *const *)names_ptr;

	pr_debug("%s: max gpios = %d\n", __func__, ARCH_NR_GPIOS);
	ret = gpiochip_add(&g->chip);
	if (ret) {
		pr_err("Error %d adding gpiochip\n", ret);
		return ret;
	}
	g->irq_base = irq_alloc_descs(-1, 0, g->chip.ngpio, 0);
	if (g->irq_base < 0) {
		dev_err(&mdev->dev, "could not allocate irq descriptors\n");
		return -ENOMEM;
	}
	for (i = 0; i < g->chip.ngpio; i++) {
		int irq = i + g->irq_base;
		irq_clear_status_flags(irq, IRQ_NOREQUEST);
		irq_set_chip_data(irq, g);
		irq_set_chip(irq, &g->irqchip);
		irq_set_nested_thread(irq, true);
		irq_set_noprobe(irq);
	}
	ret = request_threaded_irq(mdev->irq, NULL, mcuio_gpio_irq_handler,
				   IRQF_TRIGGER_LOW | IRQF_ONESHOT,
				   "mcuio-gpio", g);
	if (ret < 0) {
		dev_err(&mdev->dev, "error requesting mcuio gpio irq\n");
		goto fail;
	}

	dev_set_drvdata(&mdev->dev, g);
	return 0;

fail:
	irq_free_descs(g->irq_base, g->chip.ngpio);
	return ret;
}

static int mcuio_gpio_remove(struct mcuio_device *mdev)
{
	struct mcuio_gpio *g = dev_get_drvdata(&mdev->dev);
	if (!g) {
		dev_err(&mdev->dev, "%s: no drvdata", __func__);
		return -EINVAL;
	}
	free_irq(mdev->irq, g);
	irq_free_descs(g->irq_base, g->chip.ngpio);
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
