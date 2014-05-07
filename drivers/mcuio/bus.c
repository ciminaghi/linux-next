/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/types.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include "mcuio-internal.h"

static inline int mcuio_device_is_host_controller(struct mcuio_device *mdev)
{
	return mdev->id.class == MCUIO_CLASS_HOST_CONTROLLER ||
	    mdev->id.class == MCUIO_CLASS_SOFT_HOST_CONTROLLER;
}

/*
 * mcuio_match_device
 * @drv driver to match
 * @dev device to match
 *
 */
static int mcuio_match_device(struct device *dev, struct device_driver *drv)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	struct mcuio_driver *mdrv = to_mcuio_drv(drv);
	const struct mcuio_device_id *id;
	pr_debug("%s:%d\n", __func__, __LINE__);
	for (id = mdrv->id_table;
	     !(id->device == MCUIO_NO_DEVICE &&
	       id->class == MCUIO_CLASS_UNDEFINED);
	     id++) {
		/* Device and vendor match first */
		if (mdev->id.device == id->device &&
		    mdev->id.vendor == id->vendor)
			return 1;
		/* Next try class match */
		if (mdev->id.class == (id->class & id->class_mask))
			return 1;
	}
	return 0;
}

struct bus_type mcuio_bus_type = {
	.name = "mcuio",
	.match = mcuio_match_device,
};

static int mcuio_drv_probe(struct device *_dev)
{
	struct mcuio_driver *drv = to_mcuio_drv(_dev->driver);
	struct mcuio_device *dev = to_mcuio_dev(_dev);

	if (!drv->probe)
		return -ENODEV;
	return drv->probe(dev);
}

static int mcuio_drv_remove(struct device *_dev)
{
	struct mcuio_driver *drv = to_mcuio_drv(_dev->driver);
	struct mcuio_device *dev = to_mcuio_dev(_dev);

	if (drv->remove)
		return drv->remove(dev);
	_dev->driver = NULL;
	return 0;
}

int mcuio_driver_register(struct mcuio_driver *drv, struct module *owner)
{
	drv->driver.owner = owner;
	drv->driver.bus = &mcuio_bus_type;
	if (drv->probe)
		drv->driver.probe = mcuio_drv_probe;
	if (drv->remove)
		drv->driver.remove = mcuio_drv_remove;
	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(mcuio_driver_register);

void mcuio_driver_unregister(struct mcuio_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(mcuio_driver_unregister);

struct device mcuio_bus = {
	.init_name	= "mcuio",
};
EXPORT_SYMBOL_GPL(mcuio_bus);

struct device_type mcuio_default_device_type = {
	.name = "mcuiodev",
};

int mcuio_device_register(struct mcuio_device *mdev,
			  struct device_type *type,
			  struct device *parent)
{
	int ret;
	if (!mdev)
		return -EINVAL;
	mdev->dev.parent = parent ? parent : &mcuio_bus;
	mdev->dev.bus = &mcuio_bus_type;
	mdev->dev.type = type ? type : &mcuio_default_device_type;
	dev_set_name(&mdev->dev, "%d:%d.%d", mdev->bus, mdev->device, mdev->fn);
	ret = device_register(&mdev->dev);
	if (!ret)
		return ret;
	put_device(&mdev->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(mcuio_device_register);

static int __mcuio_device_unregister(struct device *dev, void *dummy)
{
	device_unregister(dev);
	return 0;
}

static void mcuio_unregister_children(struct mcuio_device *mdev)
{
	device_for_each_child(&mdev->dev, NULL, __mcuio_device_unregister);
}

void mcuio_device_unregister(struct mcuio_device *mdev)
{
	if (mcuio_device_is_host_controller(mdev))
		mcuio_unregister_children(mdev);
	__mcuio_device_unregister(&mdev->dev, NULL);
}
EXPORT_SYMBOL_GPL(mcuio_device_unregister);
