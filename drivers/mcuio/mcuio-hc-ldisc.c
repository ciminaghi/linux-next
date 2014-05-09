/*
 * Copyright 2013 Dog Hunter SA
 *
 * Author Davide Ciminaghi
 * GNU GPLv2
 */

/* Line discipline based mcuio host controller */

#include <linux/module.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/circ_buf.h>

#include <linux/mcuio.h>

#include <linux/mcuio-proto.h>
#include <linux/mcuio-hc.h>
#include <linux/mcuio-soft-hc.h>

#include "mcuio-internal.h"

/*
 * FIXME
 */
#define N_MCUIO 29

struct ldisc_priv_data {
	struct device *dev;
	struct circ_buf cbuf;
	char buf[sizeof(struct mcuio_base_packet) * 8];
};



static int mcuio_ldisc_shc_write(struct mcuio_soft_hc *shc,
				 const u8 *ptr, unsigned int len)
{
	int stat;
	struct tty_struct *tty = shc->priv;
	/* FIXME: CHECK FOR FREE SPACE IN BUFFER */
	stat = tty->ops->write(tty, (char *)ptr, len);
	return stat == len ? 0 : stat;
}

static const struct mcuio_soft_hc_ops ops = {
	.write = mcuio_ldisc_shc_write,
};

/*
 * Open ldisc: register an mcuio controller
 */
static int mcuio_ldisc_open(struct tty_struct *tty)
{
	struct ldisc_priv_data *priv;
	struct device *dev;

	dev = mcuio_add_soft_hc(NULL, &ops, tty);
	if (IS_ERR(dev))
		return (PTR_ERR(dev));
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;
	priv->dev = dev;
	priv->cbuf.buf = priv->buf;
	tty->disc_data = priv;
	return 0;
}

static void mcuio_ldisc_close(struct tty_struct *tty)
{
	struct ldisc_priv_data *priv = tty->disc_data;
	if (!priv)
		return;
	if (!priv->dev)
		return;
	mcuio_del_hc_device(priv->dev);
	tty->disc_data = NULL;
}

static int mcuio_ldisc_hangup(struct tty_struct *tty)
{
	mcuio_ldisc_close(tty);
	return 0;
}

static void mcuio_ldisc_receive_buf(struct tty_struct *tty,
				    const unsigned char *cp,
				    char *fp, int count)
{
	struct ldisc_priv_data *priv = tty->disc_data;
	struct mcuio_device *mdev;
	struct mcuio_soft_hc *shc;
	int i, space, cnt;

	if (!priv)
		return;
	mdev = to_mcuio_dev(priv->dev);
	shc = mdev->do_request_data;
	if (!shc) {
		WARN_ON(1);
		return;
	}
	space = CIRC_SPACE(priv->cbuf.head, priv->cbuf.tail,
			   sizeof(priv->buf));
	if (count > space)
		pr_debug("not enough space\n");
	for (i = 0; i < min(count, space); i++) {
		priv->buf[priv->cbuf.head] = cp[i];
		priv->cbuf.head = (priv->cbuf.head + 1) &
			(sizeof(priv->buf) - 1);
	}
	for (i = 0; ; i += sizeof(struct mcuio_base_packet)) {
		cnt = CIRC_CNT(priv->cbuf.head, priv->cbuf.tail,
			       sizeof(priv->buf));
		if (cnt < sizeof(struct mcuio_base_packet))
			break;
		mcuio_soft_hc_push_chars(shc,
					 &priv->buf[priv->cbuf.tail],
					 sizeof(struct mcuio_base_packet));
		priv->cbuf.tail =
			(priv->cbuf.tail + sizeof(struct mcuio_base_packet)) &
			(sizeof(priv->buf) - 1);
	}
}

static struct tty_ldisc_ops mcuio_ldisc = {
	.owner 		= THIS_MODULE,
	.magic 		= TTY_LDISC_MAGIC,
	.name 		= "mcuio",
	.open 		= mcuio_ldisc_open,
	.close	 	= mcuio_ldisc_close,
	.hangup	 	= mcuio_ldisc_hangup,
	.receive_buf	= mcuio_ldisc_receive_buf,
};

static int __init mcuio_ldisc_init(void)
{
	return tty_register_ldisc(N_MCUIO, &mcuio_ldisc);
}

static void __exit mcuio_ldisc_exit(void)
{
	tty_unregister_ldisc(N_MCUIO);
}

module_init(mcuio_ldisc_init);
module_exit(mcuio_ldisc_exit);

MODULE_LICENSE("GPL v2");
MODULE_ALIAS_LDISC(N_MCUIO);
MODULE_AUTHOR("Davide Ciminaghi, derived from slip ldisc implementation");
