/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* mcuio generic soft host controller functions */

#include <linux/mcuio.h>
#include <linux/circ_buf.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/circ_buf.h>
#include <linux/mcuio_ids.h>

#include <linux/mcuio.h>
#include <linux/mcuio-proto.h>
#include <linux/mcuio-hc.h>
#include <linux/mcuio-soft-hc.h>
#include "mcuio-internal.h"

static int __check_alignment(struct mcuio_request *r)
{
	switch (r->type) {
	case mcuio_type_rdw:
	case mcuio_type_wrw:
		return !(r->offset % sizeof(u16)) ? 0 : -1;
	case mcuio_type_rddw:
	case mcuio_type_wrdw:
		return !(r->offset % sizeof(u32)) ? 0 : -1;
	default:
		return 0;
	}
}

static int __do_read_register(struct mcuio_request *r,
			      struct mcuio_soft_hc *shc)
{
	/* Only 32bit reads allowed for registers */
	if (r->type != mcuio_type_rddw)
		return -EPERM;
	switch (r->offset) {
	case MCUIO_RX_CNT:
	{
		struct circ_buf *buf = &shc->rx_circ_buf;
		u32 v = CIRC_CNT(buf->head, buf->tail, sizeof(shc->rx_buf));
		r->data[0] = v;
		break;
	}
	case MCUIO_IRQ:
		r->data[0] = shc->irqno;
		break;
	case MCUIO_IRQ_STAT:
		r->data[0] = shc->irqstat;
		break;
	default:
		return -EPERM;
	}
	return 0;
}

static int __read_inbuf(struct mcuio_request *r, struct mcuio_soft_hc *shc)
{
	int i, s = sizeof(shc->rx_buf);
	u8 *out = r->extended_data;
	struct circ_buf *buf = &shc->rx_circ_buf;

	if (CIRC_CNT(buf->head, buf->tail, s) < sizeof(u32))
		return -EAGAIN;
	for (i = 0; i < r->extended_datalen; i++) {
		out[i] = buf->buf[buf->tail++];
		buf->tail &= (s - 1);
	}
	return 0;
}

static int __do_read_buffer(struct mcuio_request *r, struct mcuio_soft_hc *shc)
{
	if (r->offset >= MCUIO_HC_INBUF && r->offset < MCUIO_RX_CNT)
		return __read_inbuf(r, shc);
	return -EPERM;
}

static int __do_read_request(struct mcuio_request *r, struct mcuio_soft_hc *shc)
{
	if (r->type == mcuio_type_rdmb)
		return __do_read_buffer(r, shc);
	return __do_read_register(r, shc);
}

static int __do_write_buffer(struct mcuio_request *r, struct mcuio_soft_hc *shc)
{
	if (r->offset >= MCUIO_HC_OUTBUF && r->offset < MCUIO_HC_INBUF)
		return shc->ops->write(shc, r->extended_data,
				       r->extended_datalen);
	return -EPERM;
}

static int __do_write_register(struct mcuio_request *r,
			       struct mcuio_soft_hc *shc)
{
	/* Only 32bit writes allowed for registers */
	if (r->type != mcuio_type_wrdw)
		return -EPERM;
	switch (r->offset) {
	case MCUIO_IRQ_CLR:
		shc->irqstat &= ~r->data[0];
		break;
	default:
		return -EPERM;
	}
	return 0;
}

static int __do_write_request(struct mcuio_request *r,
			      struct mcuio_soft_hc *shc)
{
	if (r->type == mcuio_type_wrmb)
		return __do_write_buffer(r, shc);
	return __do_write_register(r, shc);
}

static int mcuio_soft_hc_do_request(struct mcuio_request *r, void *__shc)
{
	int stat;
	struct mcuio_soft_hc *shc = __shc;

	stat = __check_alignment(r);
	if (stat)
		return stat;
	if (mcuio_type_is_read(r->type))
		return __do_read_request(r, shc);
	return __do_write_request(r, shc);
}

int mcuio_soft_hc_push_chars(struct mcuio_soft_hc *shc, const u8 *in, int len)
{
	int s = sizeof(shc->rx_buf), available, actual;
	struct circ_buf *buf = &shc->rx_circ_buf;
	available = CIRC_SPACE_TO_END(buf->head, buf->tail, s);
	if (available < sizeof(u32)) {
		pr_debug("%s %d\n", __func__, __LINE__);
		return -EAGAIN;
	}
	actual = min(len, available);
	memcpy(&buf->buf[buf->head], in, actual);
	buf->head = (buf->head + actual) & (s - 1);
	/* set irq status register RX_RDY bit */
	shc->irqstat |= RX_RDY;
	if (shc->irq_enabled)
		handle_nested_irq(shc->irqno);
	return actual;
}
EXPORT_SYMBOL(mcuio_soft_hc_push_chars);

static void mcuio_soft_hc_irq_mask(struct irq_data *d)
{
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	struct mcuio_soft_hc *shc =
		container_of(chip, struct mcuio_soft_hc, chip);

	shc->irq_enabled = 0;
}

static void mcuio_soft_hc_irq_unmask(struct irq_data *d)
{
	struct irq_chip *chip = irq_data_get_irq_chip(d);
	struct mcuio_soft_hc *shc =
		container_of(chip, struct mcuio_soft_hc, chip);

	shc->irq_enabled = 1;
}

static struct mcuio_soft_hc *__setup_shc(const struct mcuio_soft_hc_ops *ops,
					 void *priv)
{
	struct mcuio_soft_hc *shc = kzalloc(sizeof(*shc), GFP_KERNEL);
	if (!shc)
		return ERR_PTR(-ENOMEM);
	shc->ops = ops;
	shc->priv = priv;
	shc->rx_circ_buf.head = shc->rx_circ_buf.tail = 0;
	shc->rx_circ_buf.buf = shc->rx_buf;
	shc->chip.name = "MCUIO-SHC";
	shc->chip.irq_mask = mcuio_soft_hc_irq_mask;
	shc->chip.irq_unmask = mcuio_soft_hc_irq_unmask;
	shc->irqno = irq_alloc_desc(0);
	irq_set_chip(shc->irqno, &shc->chip);
	irq_set_handler(shc->irqno, &handle_simple_irq);
	irq_modify_status(shc->irqno,
			  IRQ_NOREQUEST | IRQ_NOAUTOEN,
			  IRQ_NOPROBE);
	return shc;
}

static struct mcuio_device_id default_soft_hc_id = {
	.device = 0,
	.vendor = 0,
	.class = MCUIO_CLASS_SOFT_HOST_CONTROLLER,
};

struct device *mcuio_add_soft_hc(struct mcuio_device_id *id,
				 const struct mcuio_soft_hc_ops *ops,
				 void *priv)
{
	struct mcuio_soft_hc *shc = __setup_shc(ops, priv);
	if (!shc)
		return ERR_PTR(-ENOMEM);
	return mcuio_add_hc_device(id ? id : &default_soft_hc_id,
				   mcuio_soft_hc_do_request, shc);
}
EXPORT_SYMBOL(mcuio_add_soft_hc);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO soft host controller code");
MODULE_LICENSE("GPL v2");
