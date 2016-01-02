/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* mcuio host controller functions */

#include <linux/mcuio.h>
#include <linux/circ_buf.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/circ_buf.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-hc.h>
#include <linux/mcuio-soft-hc.h>
#include <linux/mcuio-proto.h>
#include <uapi/linux/mcuio-proto.h>
#include "mcuio-internal.h"

static struct mcuio_device_id default_hc_id = {
	.device = 0,
	.vendor = 0,
	.class = MCUIO_CLASS_HOST_CONTROLLER,
};

void mcuio_hc_dev_default_release(struct device *dev)
{
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	mcuio_put_bus(mdev->bus);
	kfree(mdev);
}
EXPORT_SYMBOL(mcuio_hc_dev_default_release);

static ssize_t store_enum(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	char *sep, *line;
	struct mcuio_device *mdev = to_mcuio_dev(dev);
	int ret, err = 0;
	unsigned long start = 1, end;

	line = kstrdup(buf, GFP_KERNEL);
	sep = strchr(line, '-');
	if (sep)
		*sep = 0;
	ret = kstrtoul(line, 0, &start);
	if (ret < 0 || start >= MCUIO_DEVS_PER_BUS || !start)
		err = 1;
	if (!sep)
		end = start;
	if (sep && !err) {
		ret = kstrtoul(sep + 1, 0, &end);
		if (ret < 0 || end >= MCUIO_DEVS_PER_BUS || end < start)
			err = 1;
	}
	kfree(line);
	if (err) {
		pr_err("mcuio hc %s: invalid value %s\n", __func__, buf);
		return -EINVAL;
	}
	ret = mcuio_hc_force_enum(mdev, start, end);
	if (ret < 0)
		return ret;
	return count;
}

static DEVICE_ATTR(enum, 0222, NULL, store_enum);

static struct attribute *hc_attrs[] = {
	&dev_attr_enum.attr,
	NULL,
};

static struct attribute_group mcuio_hc_dev_attr_group = {
	.attrs = hc_attrs,
};

static const struct attribute_group *hc_dev_attr_groups[] = {
	&mcuio_default_dev_attr_group,
	&mcuio_hc_dev_attr_group,
	NULL,
};

static struct device_type hc_device_type = {
	.name = "mcuio-host-controller",
	.groups = hc_dev_attr_groups,
};

struct device *mcuio_add_hc_device(struct mcuio_device_id *id, rfun rf,
				   void *data, void (*release)(struct device *))
{
	int b, ret = -ENOMEM;
	struct mcuio_device *d = kzalloc(sizeof(*d), GFP_KERNEL);
	if (!d)
		return ERR_PTR(-ENOMEM);
	b = mcuio_get_bus();
	if (b < 0) {
		ret = b;
		goto err0;
	}
	d->bus = b;
	d->device = 0;
	d->fn = 0;
	d->id = id ? *id : default_hc_id;
	d->do_request = rf;
	d->do_request_data = data;
	hc_device_type.release = release ? release :
		mcuio_hc_dev_default_release;
	ret = mcuio_device_register(d, &hc_device_type, NULL);
	if (ret < 0)
		goto err1;
	return &d->dev;

err1:
	mcuio_put_bus(b);
err0:
	kfree(d);
	return ERR_PTR(ret);
}
EXPORT_SYMBOL(mcuio_add_hc_device);

void mcuio_del_hc_device(struct device *dev)
{
	mcuio_device_unregister(to_mcuio_dev(dev));
}
EXPORT_SYMBOL(mcuio_del_hc_device);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO host controller code");
MODULE_LICENSE("GPL v2");
