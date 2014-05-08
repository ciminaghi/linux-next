/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* mcuio generic soft host controller functions and data structs, header file */

#ifndef __MCUIO_SOFT_HOST_CONTROLLER_H__
#define __MCUIO_SOFT_HOST_CONTROLLER_H__

#include <linux/circ_buf.h>
#include <linux/irq.h>

struct mcuio_soft_hc;

struct mcuio_soft_hc_ops {
	int (*write)(struct mcuio_soft_hc *, const u8 *ptr, unsigned int len);
};

/*
 * A soft mcuio host controller
 *
 * @id: pointer to corresponding mcuio device's id.
 * @irqstat: current irq status register
 * @irqno: number of [virtual] irq
 * @irq_enabled: irq enabled when !0
 * @rx_circ_buf: circular buffer structure for rx data management
 * @rx_buf: actual rx data buffer
 * @ops: pointer to hc operations
 * @chip: related irqchip
 * @priv: client driver private data
 */
struct mcuio_soft_hc {
	struct mcuio_device_id *id;
	u32 irqstat;
	int irqno;
	int irq_enabled;
	struct circ_buf rx_circ_buf;
	char rx_buf[256];
	const struct mcuio_soft_hc_ops *ops;
	struct irq_chip chip;
	void *priv;
};

/* Instantiate a soft host controller */
/*
 * mcuio_add_soft_hc
 *
 * @id: pointer to corresponding mcuio device's id.
 * @ops: pointer to operations structure
 * @priv: pointer to private data
 *
 * Returns pointer to corresponding device
 */
struct device *mcuio_add_soft_hc(struct mcuio_device_id *id,
				 const struct mcuio_soft_hc_ops *ops,
				 void *priv);

/* Push chars from soft host controller client driver */
/*
 * mcuio_soft_hc_push_chars()
 *
 * @shc: pointer to soft host controller data structure
 * @buf: pointer to input buffer
 * @len: length of buffer
 */
int mcuio_soft_hc_push_chars(struct mcuio_soft_hc *shc, const u8 *buf, int len);


#endif /* __MCUIO_SOFT_HOST_CONTROLLER_H__ */
