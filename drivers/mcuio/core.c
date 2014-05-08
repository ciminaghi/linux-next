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
#include <linux/completion.h>
#include <linux/spinlock.h>
#include <linux/bitops.h>
#include "mcuio-internal.h"

unsigned long int busnum;
spinlock_t busnum_lock;

int mcuio_get_bus(void)
{
	int out;
	spin_lock(&busnum_lock);
	if (busnum == 0xffffffff) {
		out = -ENOMEM;
		goto end;
	}
	out = find_last_bit(&busnum, sizeof(busnum));
	if (out == sizeof(busnum))
		out = 0;
	set_bit(out, &busnum);
end:
	spin_unlock(&busnum_lock);
	return out;
}
EXPORT_SYMBOL(mcuio_get_bus);

void mcuio_put_bus(unsigned n)
{
	clear_bit(n, &busnum);
}
EXPORT_SYMBOL(mcuio_put_bus);

static int __init mcuio_init(void)
{
	int ret;
	spin_lock_init(&busnum_lock);
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

int mcuio_submit_request(struct mcuio_request *r)
{
	if (!r->mdev) {
		WARN_ON(1);
		return -ENODEV;
	}
	if (!r->mdev->do_request) {
		dev_err(&r->mdev->dev, "cannot execute request\n");
		return -EOPNOTSUPP;
	}
	return r->mdev->do_request(r, r->mdev->do_request_data);
}
EXPORT_SYMBOL(mcuio_submit_request);

postcore_initcall(mcuio_init);
module_exit(mcuio_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO subsys core module");
MODULE_LICENSE("GPL v2");
