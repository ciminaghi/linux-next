/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* mcuio host controller driver */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>

static int mcuio_host_controller_probe(struct mcuio_device *dev)
{
	return -ENODEV;
}

static int mcuio_host_controller_remove(struct mcuio_device *dev)
{
	return 0;
}

static const struct mcuio_device_id hc_drv_ids[] = {
	{
		.class = MCUIO_CLASS_HOST_CONTROLLER,
		.class_mask = 0xffff,
	},
	{
		.class = MCUIO_CLASS_SOFT_HOST_CONTROLLER,
		.class_mask = 0xffff,
	},
	/* Terminator */
	{
		.device = MCUIO_NO_DEVICE,
		.class = MCUIO_CLASS_UNDEFINED,
	},
};

static struct mcuio_driver mcuio_host_controller_driver = {
	.driver = {
		.name = "mcuio-hc",
	},
	.id_table = hc_drv_ids,
	.probe = mcuio_host_controller_probe,
	.remove = mcuio_host_controller_remove,
};

static int __init mcuio_host_controller_init(void)
{
	return mcuio_driver_register(&mcuio_host_controller_driver,
				     THIS_MODULE);
}

static void __exit mcuio_host_controller_exit(void)
{
	return mcuio_driver_unregister(&mcuio_host_controller_driver);
}

subsys_initcall(mcuio_host_controller_init);
module_exit(mcuio_host_controller_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO host controller driver");
MODULE_LICENSE("GPL v2");
