#ifndef __MCUIO_INTERNAL_H__
#define __MCUIO_INTERNAL_H__

#include <linux/version.h>

struct mcuio_device;

extern struct bus_type mcuio_bus_type;
extern struct device mcuio_bus;
extern struct attribute_group mcuio_default_dev_attr_group;

int mcuio_get_bus(void);
void mcuio_put_bus(unsigned bus);
int mcuio_hc_force_enum(struct mcuio_device *mdev,
			unsigned start, unsigned end);

/*
 * Find an mcuio device: WARNING: gets a reference to the device (if found),
 * you might need a call to put_device().
 */
struct mcuio_device *mcuio_find_device(struct mcuio_device *parent,
				       unsigned bus, unsigned device,
				       unsigned fn);

#endif /* __MCUIO_INTERNAL_H__ */
