/*
 * Copyright 2013 Dog Hunter SA
 *
 * Author Davide Ciminaghi
 * GNU GPLv2
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/mcuio.h>
#include "mcuio-internal.h"

static int __init mcuio_init(void)
{
	int ret;
	ret = device_register(&mcuio_bus);
	if (ret)
		return ret;
	/* Register mcuio bus */
	return bus_register(&mcuio_bus_type);
}

static void __exit mcuio_exit(void)
{
	/* Remove mcuio bus */
	device_unregister(&mcuio_bus);
	bus_unregister(&mcuio_bus_type);
	return;
}

postcore_initcall(mcuio_init);
module_exit(mcuio_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO subsys core module");
MODULE_LICENSE("GPL v2");
