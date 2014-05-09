/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* MCUIO irq controller driver */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mutex.h>
#include <linux/gpio.h>
#include <linux/delay.h>

#include <linux/mcuio.h>
#include <linux/mcuio-hc.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>

#include "mcuio-internal.h"

#define STATUS_OFFSET 0x8
#define MASK_OFFSET 0xc
#define UNMASK_OFFSET 0x10
#define ACK_OFFSET 0x14

static const struct regmap_config mcuio_irqc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = 0x24,
	.cache_type = REGCACHE_NONE,
};

struct mcuio_irq_controller_data {
	struct regmap *map;
	struct irq_chip chip;
	int base_irq;
};

static irqreturn_t irq_ctrl_irq_handler(int irq, void *__data)
{
	struct mcuio_device *mdev = __data;
	struct mcuio_irq_controller_data *priv = dev_get_drvdata(&mdev->dev);
	int i, stat;
	u32 status = 0;

	if (!priv || !priv->map) {
		dev_err(&mdev->dev, "no private data or no regmap\n");
		return IRQ_NONE;
	}
	stat = regmap_read(priv->map, STATUS_OFFSET, &status);
	if (stat < 0) {
		dev_err(&mdev->dev, "error reading irq status\n");
		return IRQ_NONE;
	}
	if (!status) {
		dev_err(&mdev->dev, "irq status is 0\n");
		return IRQ_NONE;
	}
	dev_dbg(&mdev->dev, "%s: irq status = 0x%08x\n", __func__, status);
	for (i = 0; i < 32; i++)
		if (status & (1 << i))
			handle_nested_irq(priv->base_irq + i);
	dev_dbg(&mdev->dev, "%s: handled, unmasking\n", __func__);
	stat = regmap_write(priv->map, UNMASK_OFFSET, status);
	if (stat < 0)
		dev_err(&mdev->dev, "error unmasking irqs\n");
	dev_dbg(&mdev->dev, "%s: unmasked\n", __func__);
	return IRQ_HANDLED;
}

static void __mcuio_ctrl_write_reg(struct irq_data *d, unsigned offset)
{
	int stat;
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	struct mcuio_irq_controller_data *priv =
		container_of(chip, struct mcuio_irq_controller_data, chip);
	u32 mask = (1 << (d->irq - priv->base_irq));
	stat = regmap_write(priv->map, offset, mask);
	if (stat < 0)
		pr_err("%s: error writing to offset %u\n", __func__, offset);
}

static void mcuio_ctrl_irq_ack(struct irq_data *d)
{
	__mcuio_ctrl_write_reg(d, ACK_OFFSET);
}

static void mcuio_ctrl_irq_mask(struct irq_data *d)
{
	__mcuio_ctrl_write_reg(d, MASK_OFFSET);
}

static void mcuio_ctrl_irq_unmask(struct irq_data *d)
{
	__mcuio_ctrl_write_reg(d, UNMASK_OFFSET);
}

static int mcuio_irq_controller_probe(struct mcuio_device *mdev)
{
	unsigned int gpio_number;
	int gpio_irq, ret, i;
	struct mcuio_device *hc = to_mcuio_dev(mdev->dev.parent);
	struct mcuio_irq_controller_data *priv;
	int dev_irqs[MCUIO_FUNCS_PER_DEV];

	if (!hc) {
		dev_err(&mdev->dev, "no parent for device\n");
		return -EINVAL;
	}
	priv = devm_kzalloc(&mdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&mdev->dev, "cannot allocate irq controller priv\n");
		return -ENOMEM;
	}
	dev_set_drvdata(&mdev->dev, priv);
	priv->map = devm_regmap_init_mcuio(mdev, &mcuio_irqc_regmap_config);
	if (IS_ERR(priv->map)) {
		dev_err(&mdev->dev, "cannot setup regmap for device\n");
		return PTR_ERR(priv->map);
	}
	/* Read gpio number @0x18 */
	ret = regmap_read(priv->map, 0x18, &gpio_number);
	if (ret < 0) {
		dev_err(&mdev->dev, "error reading irq wire gpio number\n");
		return ret;
	}
	dev_dbg(&mdev->dev, "using gpio %u as irq wire\n", gpio_number);
	gpio_irq = gpio_to_irq(gpio_number);
	if (gpio_irq < 0) {
		dev_err(&mdev->dev, "cannot translate gpio number to irq\n");
		return ret;
	}
	ret = devm_request_threaded_irq(&mdev->dev, gpio_irq, NULL,
					irq_ctrl_irq_handler,
					IRQF_ONESHOT|IRQF_TRIGGER_HIGH,
					dev_name(&mdev->dev), mdev);
	if (ret < 0) {
		dev_err(&mdev->dev, "request_irq regurns error\n");
		return ret;
	}
	priv->base_irq = irq_alloc_descs(-1, 0, MCUIO_FUNCS_PER_DEV, 0);
	if (priv->base_irq < 0) {
		dev_err(&mdev->dev, "cannot allocate irq descriptors\n");
		return priv->base_irq;
	}
	priv->chip.name = "MCUIO-IRQC";
	priv->chip.irq_mask = mcuio_ctrl_irq_mask;
	priv->chip.irq_unmask = mcuio_ctrl_irq_unmask;
	priv->chip.irq_ack = mcuio_ctrl_irq_ack;
	for (i = 0; i < MCUIO_FUNCS_PER_DEV; i++) {
		int irq = priv->base_irq + i;
		irq_set_chip(irq, &priv->chip);
		irq_set_handler(irq, &handle_simple_irq);
		irq_modify_status(irq,
				  IRQ_NOREQUEST | IRQ_NOAUTOEN,
				  IRQ_NOPROBE);
		dev_irqs[i] = irq;
	}
	return mcuio_hc_set_irqs(hc, mdev->device, dev_irqs);
}

static int mcuio_irq_controller_remove(struct mcuio_device *mdev)
{
	struct mcuio_irq_controller_data *priv = dev_get_drvdata(&mdev->dev);
	irq_free_descs(priv->base_irq, MCUIO_FUNCS_PER_DEV);
	return 0;
}

static const struct mcuio_device_id irq_ctrl_drv_ids[] = {
	{
		.class = MCUIO_CLASS_IRQ_CONTROLLER_WIRE,
		.class_mask = 0xffff,
	},
	/* Terminator */
	{
		.device = MCUIO_NO_DEVICE,
		.class = MCUIO_CLASS_UNDEFINED,
	},
};

static struct mcuio_driver mcuio_irq_controller_driver = {
	.driver = {
		.name = "mcuio-irqc",
	},
	.id_table = irq_ctrl_drv_ids,
	.probe = mcuio_irq_controller_probe,
	.remove = mcuio_irq_controller_remove,
};

static int __init mcuio_irq_controller_init(void)
{
	return mcuio_driver_register(&mcuio_irq_controller_driver,
				     THIS_MODULE);
}

static void __exit mcuio_irq_controller_exit(void)
{
	return mcuio_driver_unregister(&mcuio_irq_controller_driver);
}

subsys_initcall(mcuio_irq_controller_init);
module_exit(mcuio_irq_controller_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO irq controller driver");
MODULE_LICENSE("GPL v2");
