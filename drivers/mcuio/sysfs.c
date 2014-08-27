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
#include <linux/sysfs.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include "mcuio-internal.h"


static ssize_t show_device(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "0x%04x", mdev->id.device);
}

static ssize_t show_vendor(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "0x%04x", mdev->id.vendor);
}

static ssize_t show_class(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "0x%08x", mdev->id.class);
}

static ssize_t show_bus(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "%u", mdev->bus);
}

static ssize_t show_dev(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "%u", mdev->device);
}

static ssize_t show_func(struct device *dev, struct device_attribute *attr,
			char *buf)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	return sprintf(buf, "%u", mdev->fn);
}

static DEVICE_ATTR(device, 0444, show_device, NULL);
static DEVICE_ATTR(vendor, 0444, show_vendor, NULL);
static DEVICE_ATTR(class, 0444, show_class, NULL);
static DEVICE_ATTR(bus, 0444, show_bus, NULL);
static DEVICE_ATTR(dev, 0444, show_dev, NULL);
static DEVICE_ATTR(func, 0444, show_func, NULL);

static struct attribute *dev_attrs[] = {
	&dev_attr_device.attr,
	&dev_attr_vendor.attr,
	&dev_attr_class.attr,
	&dev_attr_bus.attr,
	&dev_attr_dev.attr,
	&dev_attr_func.attr,
	NULL,
};

struct attribute_group mcuio_default_dev_attr_group = {
	.attrs = dev_attrs,
};
EXPORT_SYMBOL(mcuio_default_dev_attr_group);
