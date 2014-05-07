#ifndef __MCUIO_INTERNAL_H__
#define __MCUIO_INTERNAL_H__

#include <linux/version.h>

extern struct bus_type mcuio_bus_type;
extern struct device mcuio_bus;

int mcuio_get_bus(void);
void mcuio_put_bus(unsigned bus);

#endif /* __MCUIO_INTERNAL_H__ */
