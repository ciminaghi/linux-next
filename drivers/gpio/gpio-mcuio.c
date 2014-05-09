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

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>

static int mcuio_gpio_probe(struct mcuio_device *mdev)
{
	return -ENODEV;
}

static int mcuio_gpio_remove(struct mcuio_device *mdev)
{
	return 0;
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
