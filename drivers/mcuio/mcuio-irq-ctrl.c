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
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mutex.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>

#include "mcuio-internal.h"

static int mcuio_irq_controller_probe(struct mcuio_device *mdev)
{
	return -ENODEV;
}

static int mcuio_irq_controller_remove(struct mcuio_device *mdev)
{
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
