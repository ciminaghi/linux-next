/*
 * Copyright 2011 Dog Hunter SA
 * Author: Davide Ciminaghi <ciminaghi@gnudd.com>
 *
 * GNU GPLv2 or later
 */

/* mcuio host controller driver */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/regmap.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>
#include <linux/kthread.h>
#include <linux/mutex.h>

#include <linux/mcuio.h>
#include <linux/mcuio_ids.h>
#include <linux/mcuio-proto.h>
#include <linux/mcuio-hc.h>

#include "mcuio-internal.h"

/* Max number of read descr timeout before skipping to next device */
#define MAX_ENUM_RETRIES 2

struct mcuio_request;

typedef void (*___request_cb)(struct mcuio_request *);

/* Host controller data */
struct mcuio_hc_data {
	unsigned bus;
	struct mutex lock;
	struct list_head request_queue;
	struct list_head pending_requests;
	atomic_t removing;

	struct kthread_worker tx_kworker;
	struct task_struct *tx_kworker_task;
	struct kthread_work send_messages;

	struct task_struct *rx_thread;
	wait_queue_head_t rd_wq;

	struct mcuio_device *mdev;
	struct kthread_worker enum_kworker;
	struct task_struct *enum_kworker_task;
	struct kthread_work do_enum;
};

typedef int (*mcuio_copy)(uint32_t *dts, const uint32_t *src, int length,
			  int ntoh);

int __mcuio_copyb(uint32_t *dst, const uint32_t *src, int length, int ntoh)
{
	memcpy(dst, src, length);
	return length;
}

int __mcuio_copyw(uint32_t *__dst, const uint32_t *__src, int length, int ntoh)
{
	uint16_t *dst = (uint16_t *)__dst;
	uint16_t *src = (uint16_t *)__src;
	int i, n = length / sizeof(uint16_t);
	for (i = 0; i < n; i++)
		*dst++ = ntoh ? mcuio_ntohs(*src++) : mcuio_htons(*src++);
	return length;
}

int __mcuio_copydw(uint32_t *dst, const uint32_t *src, int length, int ntoh)
{
	int i, n = length / sizeof(uint32_t);
	for (i = 0; i < n; i++)
		*dst++ = ntoh ? mcuio_ntohl(*src++) : mcuio_htonl(*src++);
	return length;
}

static const mcuio_copy __copy_table[] = {
	[ mcuio_type_rdb ] = __mcuio_copyb,
	[ mcuio_type_wrb ] = __mcuio_copyb,
	[ mcuio_type_rdw ] = __mcuio_copyw,
	[ mcuio_type_wrw ] = __mcuio_copyw,
	[ mcuio_type_rddw ] = __mcuio_copydw,
	[ mcuio_type_wrdw ] = __mcuio_copydw,
	[ mcuio_type_rdmb ] = __mcuio_copyb,
	[ mcuio_type_wrmb ] = __mcuio_copyb,
};

/*
 * Copy data to a base packet or to the first subframe of an extended packet
 */
static int __copy_data(struct mcuio_request *r,
		       struct mcuio_base_packet *p, int ntoh)
{
	uint32_t *addr;
	mcuio_copy cp = __copy_table[mcuio_packet_type(p) &
				     mcuio_actual_type_mask];
	uint32_t *__dst;
	uint32_t *__src;
	int len = mcuio_packet_is_extended(p) ?
		FIRST_SUBF_DLEN : mcuio_base_packet_datalen(p);

	if (!cp)
		return -ENOSYS;
	addr = r->extended_data ? r->extended_data : r->data;
	__dst = ntoh ? addr : mcuio_packet_data(p);
	__src = ntoh ? mcuio_packet_data(p) : addr;
	return cp(__dst, __src, len,
		  ntoh ? mcuio_packet_is_read(p) : !mcuio_packet_is_read(p));
}

static void mcuio_free_request(struct mcuio_request *r)
{
	struct mcuio_hc_data *data;
	struct mcuio_device *hc = to_mcuio_dev(r->mdev->dev.parent);

	data = dev_get_drvdata(&hc->dev);
	mutex_lock(&data->lock);
	list_del(&r->list);
	mutex_unlock(&data->lock);
	if (!r->dont_free)
		devm_kfree(&hc->dev, r);
}

static void __request_timeout(struct work_struct *work)
{
	struct mcuio_request *r =
		container_of(work, struct mcuio_request, to_work.work);
	if (r->cb)
		r->cb(r);
	mcuio_free_request(r);
}

static int __write_packet(struct regmap *map, const void *src)
{
	return regmap_raw_write(map, MCUIO_HC_OUTBUF, src,
				sizeof(struct mcuio_base_packet));
}

static struct mcuio_request *__make_request(struct mcuio_device *mdev,
					    unsigned dev, unsigned func,
					    unsigned type,
					    int fill,
					    unsigned offset, ___request_cb cb)
{
	struct mcuio_request *out;

	out = devm_kzalloc(&mdev->dev, sizeof(*out), GFP_KERNEL);
	if (!out)
		return NULL;
	out->extended_data = NULL;
	out->extended_datalen = 0;
	out->mdev = mdev;
	out->type = type;
	out->offset = offset;
	out->status = -ETIMEDOUT;
	out->cb = cb;
	out->fill = fill;
	return out;
}

static int __write_request(struct regmap *map, struct mcuio_request *r)
{
	int stat, nsub = 1, togo = sizeof(uint64_t);
	struct mcuio_base_packet p;
	uint8_t *ptr;

	mcuio_packet_set_addr_type(&p, r->mdev->device, r->mdev->fn, r->offset,
				   r->type, r->fill);
	if (mcuio_packet_is_extended(&p)) {
		if (!r->extended_datalen || !r->extended_data)
			return -EINVAL;
		p.body.ext.datalen = r->extended_datalen;
		nsub = 	mcuio_packet_datalen_to_nsub(r->extended_datalen);
		if (nsub < 2)
			return nsub < 0 ? nsub : -EINVAL;
		togo = r->extended_datalen;
	}
	/* Copy data to packet or first subframe */
	__copy_data(r, &p, 0);
	stat = __write_packet(map, &p);
	if (stat < 0 || !mcuio_packet_is_extended(&p))
		return stat;
	/* Extended packet, write data */
	for (ptr = r->extended_data ;
	     togo > MID_SUBF_DLEN && stat >= 0;
	     togo -= MID_SUBF_DLEN, ptr += MID_SUBF_DLEN) {
		stat = __write_packet(map, ptr);
	}
	if (stat < 0)
		return stat;
	/* Extended packet, write last subframe */
	if (togo > LAST_SUBF_DLEN || togo <= 0) {
		WARN_ON(1);
		return -EINVAL;
	}
	stat = __write_packet(map, ptr);
	return stat;
}

static int __do_request(struct mcuio_hc_data *data)
{
	struct mcuio_request *r;
	struct mcuio_device *hc;
	struct regmap *map;

	mutex_lock(&data->lock);
	if (list_empty(&data->request_queue)) {
		mutex_unlock(&data->lock);
		return 0;
	}
	r = list_entry(data->request_queue.next, struct mcuio_request, list);
	hc = to_mcuio_dev(r->mdev->dev.parent);
	map = dev_get_regmap(&hc->dev, NULL);
	if (!map) {
		mutex_unlock(&data->lock);
		WARN_ON(1);
		return -EIO;
	}
	list_move(&r->list, &data->pending_requests);
	mutex_unlock(&data->lock);
	/* Schedule timeout */
	INIT_DELAYED_WORK(&r->to_work, __request_timeout);
	/* FIXME: WHAT IS THE CORRECT DELAY ? */
	schedule_delayed_work(&r->to_work, HZ/10);
	if (__write_request(map, r) < 0) {
		dev_err(&hc->dev, "error writing to output fifo");
		goto regmap_error;
	}
	return 1;

regmap_error:
	cancel_delayed_work_sync(&r->to_work);
	mcuio_free_request(r);
	return -EIO;
}

static irqreturn_t hc_irq_handler(int irq, void *__data)
{
	struct mcuio_device *mdev = __data;
	struct regmap *map = dev_get_regmap(&mdev->dev, NULL);
	struct mcuio_hc_data *data = dev_get_drvdata(&mdev->dev);
	int ret;
	u32 status;

	if (!data) {
		dev_err(&mdev->dev, "no drv data in irq handler\n");
		return IRQ_NONE;
	}
	ret = regmap_read(map, MCUIO_IRQ_STAT, &status);
	if (ret < 0)
		return IRQ_NONE;
	if (status & RX_RDY)
		wake_up_interruptible(&data->rd_wq);
	ret = regmap_write(map, MCUIO_IRQ_CLR, status);
	if (ret < 0)
		dev_err(&mdev->dev, "error clearing irq flag\n");
	return IRQ_HANDLED;
}

static inline u32 __get_available(struct regmap *map)
{
	u32 out;
	int stat = regmap_read(map, MCUIO_RX_CNT, &out);
	if (stat < 0)
		return 0;
	return out;
}

static int __read_from_hc(struct mcuio_device *hc, void *out, int count)
{
	int stat;
	struct mcuio_hc_data *data = dev_get_drvdata(&hc->dev);
	struct regmap *map;

	if (!data) {
		WARN_ON(1);
		return -EINVAL;
	}
	map = dev_get_regmap(&hc->dev, NULL);
	if (!map) {
		WARN_ON(1);
		return -ENODEV;
	}

	stat = wait_event_interruptible(data->rd_wq,
					__get_available(map) >= count ||
					kthread_should_stop());
	/* FIXME: handle signals */
	if (stat < 0 || kthread_should_stop()) {
		if (stat < 0)
			dev_err(&hc->dev, "error %d in wait_event\n",
				stat);
		return stat;
	}
	return regmap_raw_read(map, MCUIO_HC_INBUF, out, count);
}

static int __read_base_packet(struct mcuio_device *hc,
			      struct mcuio_base_packet *p)
{
	return __read_from_hc(hc, p, sizeof(*p));
}

static int __finish_reading_packet(struct mcuio_device *hc,
				   struct mcuio_base_packet *p,
				   struct mcuio_request *r)
{
	int size, stat;

	if (!mcuio_packet_is_extended(p))
		return 0;
	if (r && (!r->extended_datalen || !r->extended_data)) {
		WARN_ON(1);
		return -ENOMEM;
	}
	/* First subframe has already been read */
	size = (mcuio_packet_nsub(p) - 1) * sizeof(*p);
	if (!size) {
		WARN_ON(1);
		return -EINVAL;
	}
	if (size < 0) {
		dev_err(&hc->dev, "invalid packet size\n");
		return -EINVAL;
	}
	if (!r) {
		int i;
		struct mcuio_base_packet __p;

		/* Just throw away current packet */
		for (i = 0; i < mcuio_packet_nsub(p) - 1; i++) {
			stat = __read_from_hc(hc, &__p, sizeof(__p));
			if (stat < 0)
				dev_err(&hc->dev, "throwing away packet\n");
		}

	}
	/* FIXME: CHECK CRC */
	return __read_from_hc(hc, r->extended_data + FIRST_SUBF_DLEN, size);
}

static struct mcuio_request *__find_request(struct mcuio_device *hc,
					    struct mcuio_base_packet *p)
{
	struct mcuio_request *r;
	struct mcuio_hc_data *data = dev_get_drvdata(&hc->dev);

	mutex_lock(&data->lock);
	list_for_each_entry(r, &data->pending_requests, list) {
		if ((mcuio_packet_type(p) & mcuio_actual_type_mask) ==
		    (r->type & mcuio_actual_type_mask) &&
		    mcuio_packet_dev(p) == r->mdev->device &&
		    mcuio_packet_func(p) == r->mdev->fn &&
		    mcuio_packet_offset(p) == r->offset) {
			mutex_unlock(&data->lock);
			return r;
		}
	}
	mutex_unlock(&data->lock);
	return NULL;
}

static int __receive_messages(void *__data)
{
	struct mcuio_device *hc = __data;

	while (!kthread_should_stop()) {
		struct mcuio_base_packet p;
		struct mcuio_request *r;
		int stat;

		/*
		 * Just read a base packet, which could be the first
		 * subframe of an extended packet
		 */
		stat = __read_base_packet(hc, &p);
		if (stat) {
			schedule();
			continue;
		}
		if (!mcuio_packet_is_reply(&p)) {
			/*
			  Packet is a request, we do not handle requests at
			  the moment
			*/
			__finish_reading_packet(hc, &p, NULL);
			continue;
		}
		r = __find_request(hc, &p);
		if (!r) {
			dev_err(&hc->dev, "unexpected reply");
			__finish_reading_packet(hc, &p, NULL);
			continue;
		}
		r->status = mcuio_packet_is_error(&p);
		cancel_delayed_work_sync(&r->to_work);
		if (mcuio_packet_is_read(&p)) {
			__copy_data(r, &p, 1);
			if (mcuio_packet_is_extended(&p)) {
				stat = __finish_reading_packet(hc, &p, r);
				if (stat)
					r->status = stat;
			}
		}
		if (r->cb)
			r->cb(r);
		mcuio_free_request(r);
	}
	return 0;
}

static void __send_messages(struct kthread_work *work)
{
	struct mcuio_hc_data *data =
		container_of(work, struct mcuio_hc_data, send_messages);
	while (__do_request(data) > 0);
}

static void __enqueue_request(struct mcuio_device *mdev,
			      struct mcuio_hc_data *data,
			      struct mcuio_request *r)
{
	mutex_lock(&data->lock);
	list_add_tail(&r->list, &data->request_queue);
	mutex_unlock(&data->lock);
	queue_kthread_work(&data->tx_kworker, &data->send_messages);
}

static int mcuio_hc_enqueue_request(struct mcuio_request *r)
{
	struct mcuio_hc_data *data;
	struct mcuio_device *hc;

	if (!r || !r->mdev || !r->mdev->dev.parent)
		return -EINVAL;
	hc = to_mcuio_dev(r->mdev->dev.parent);
	data = dev_get_drvdata(&hc->dev);
	if (!data)
		return -EINVAL;
	if (atomic_read(&data->removing))
		return -ENODEV;
	__enqueue_request(hc, data, r);
	return 0;
}

static void __request_cb(struct mcuio_request *r)
{
	struct completion *c = r->cb_data;
	complete(c);
}

/*
 * Submit request to a remote device through the host controller
 * This shall be the do_request method for remote devices
 */
static int __mcuio_submit_remote_request(struct mcuio_request *r, void *dummy)
{
	int ret;
	DECLARE_COMPLETION_ONSTACK(request_complete);

	r->cb = __request_cb;
	r->cb_data = &request_complete;
	r->status = -ETIMEDOUT;
	ret = mcuio_hc_enqueue_request(r);
	if (!ret)
		ret = wait_for_completion_interruptible(&request_complete);
	if (ret)
		return ret;
	return r->status;
}

static int __do_one_enum(struct mcuio_device *mdev, unsigned edev,
			 unsigned efunc, struct mcuio_request **out)
{
	struct mcuio_request *r;
	int ret;

	r = __make_request(mdev, edev, efunc,
			   mcuio_type_rddw, 1, 0, NULL);
	if (!r)
		return -ENOMEM;
	ret = mcuio_submit_request(r);
	*out = r;
	return ret;
}

static void __register_device(struct mcuio_request *r)
{
	struct mcuio_func_descriptor d;
	struct mcuio_device *hc = to_mcuio_dev(r->mdev->dev.parent);
	struct mcuio_device *new;

	new = kzalloc(sizeof(*new), GFP_KERNEL);
	if (!new) {
		dev_err(&hc->dev,
			"error allocating device %u:%u.%u\n",
			hc->bus, r->mdev->device, r->mdev->fn);
		return;
	}
	memcpy(&d, r->data, sizeof(d));
	new->id.device = mcuio_get_device(&d);
	new->id.vendor = mcuio_get_vendor(&d);
	new->id.class = d.rev_class;
	new->id.class_mask = 0xffffffff;
	new->bus = hc->bus;
	new->device = r->mdev->device;
	new->fn = r->mdev->fn;
	new->do_request = __mcuio_submit_remote_request;
	new->do_request_data = NULL;
	pr_debug("%s %d, device = 0x%04x, vendor = 0x%04x, "
		 "class = 0x%04x\n", __func__, __LINE__, new->id.device,
		 new->id.vendor, new->id.class);
	if (mcuio_device_register(new, NULL, &hc->dev) < 0) {
		dev_err(&hc->dev, "error registering device %u:%u.%u\n",
			hc->bus, r->mdev->device, r->mdev->fn);
		kfree(new);
	}
}

static int __next_enum(unsigned *edev, unsigned *efunc, int *retry)
{
	if ((*retry) > 0) {
		/* Doing retries */
		(*retry)--;
		return 0;
	}
	if (!(*retry)) {
		/* No reply and no more attempts left, skip to next device */
		*retry = -1;
		if ((*edev)++ >= MCUIO_DEVS_PER_BUS - 1)
			return 1;
		*efunc = 0;
		return 0;
	}
	if ((*efunc)++ >= MCUIO_FUNCS_PER_DEV - 1) {
		*efunc = 0;
		if ((*edev)++ >= MCUIO_DEVS_PER_BUS - 1)
			return 1;
	}
	return 0;
}

static void __do_enum(struct kthread_work *work)
{
	struct mcuio_hc_data *data =
		container_of(work, struct mcuio_hc_data, do_enum);
	struct mcuio_device *mdev = data->mdev;
	struct mcuio_request *r = NULL;
	unsigned edev, efunc;
	int stop_enum, stat, retry = -1;

	for (edev = 1, efunc = 0, stop_enum = 0; !stop_enum;
	     stop_enum = __next_enum(&edev, &efunc, &retry)) {
		stat = __do_one_enum(mdev, edev, efunc, &r);
		if (stat < 0) {
			if (!r)
				continue;
			dev_err(&mdev->dev,
				"error %d on enum of %u.%u\n",
				r->status == -ETIMEDOUT ? r->status :
				r->data[0], edev, efunc);
			if (r->status == -ETIMEDOUT) {
				/* No reply from target */
				retry = retry == -1 ? MAX_ENUM_RETRIES :
					retry - 1;
			}
			continue;
		}
		retry = -1;
		/* Found a new device, let's add it */
		__register_device(r);
	}
}

static const struct regmap_config mcuio_hc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.max_register = MCUIO_HC_MAX_REGISTER,
	.cache_type = REGCACHE_NONE,
};

static int mcuio_host_controller_probe(struct mcuio_device *mdev)
{
	struct mcuio_hc_data *data;
	struct regmap *map;
	u32 irq;
	int ret = -ENOMEM;

	/* Only manage local host controllers */
	if (mdev->device)
		return -ENODEV;
	map = devm_regmap_init_mcuio(mdev, &mcuio_hc_regmap_config);
	if (IS_ERR(map)) {
		dev_err(&mdev->dev, "Error setting up regmap for hc\n");
		return PTR_ERR(map);
	}
	data = devm_kzalloc(&mdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return ret;
	dev_set_drvdata(&mdev->dev, data);
	atomic_set(&data->removing, 0);
	mutex_init(&data->lock);
	data->mdev = mdev;
	init_kthread_worker(&data->tx_kworker);
	init_waitqueue_head(&data->rd_wq);
	ret = regmap_read(map, MCUIO_IRQ, &irq);
	if (ret < 0) {
		dev_err(&mdev->dev, "Error %d reading irq number\n", ret);
		return ret;
	}
	ret = devm_request_threaded_irq(&mdev->dev, irq, NULL,
					hc_irq_handler,
					IRQF_ONESHOT,
					dev_name(&mdev->dev), mdev);
	if (ret < 0) {
		dev_err(&mdev->dev, "Error %d requesting irq\n", ret);
		return ret;
	}
	data->tx_kworker_task = kthread_run(kthread_worker_fn,
					    &data->tx_kworker,
					    "%s_%s",
					    dev_name(&mdev->dev), "tx");
	if (IS_ERR(data->tx_kworker_task)) {
		dev_err(&mdev->dev, "failed to create message tx task\n");
		return -ENOMEM;
	}
	init_kthread_work(&data->send_messages, __send_messages);
	INIT_LIST_HEAD(&data->request_queue);
	INIT_LIST_HEAD(&data->pending_requests);
	data->rx_thread = kthread_run(__receive_messages, mdev, "%s_%s",
				      dev_name(&mdev->dev), "rx");
	if (IS_ERR(data->rx_thread)) {
		dev_err(&mdev->dev, "failed to create message rx task\n");
		kthread_stop(data->tx_kworker_task);
		return PTR_ERR(data->rx_thread);
	}
	init_kthread_worker(&data->enum_kworker);
	data->enum_kworker_task = kthread_run(kthread_worker_fn,
					      &data->enum_kworker,
					      "%s_%s",
					      dev_name(&mdev->dev), "enum");
	if (IS_ERR(data->enum_kworker_task)) {
		dev_err(&mdev->dev, "failed to create enum task\n");
		return -ENOMEM;
	}
	init_kthread_work(&data->do_enum, __do_enum);
	/* Immediately start enum */
	queue_kthread_work(&data->enum_kworker, &data->do_enum);
	return 0;
}

static void __cleanup_outstanding_requests(struct mcuio_hc_data *data)
{
	struct mcuio_request *r, *tmp;
	list_for_each_entry_safe(r, tmp, &data->pending_requests, list) {
		pr_debug("%s %d: freeing request %p\n", __func__,
			 __LINE__, r);
		cancel_delayed_work_sync(&r->to_work);
		if (r->cb)
			r->cb(r);
		mcuio_free_request(r);
	}
}

static int mcuio_host_controller_remove(struct mcuio_device *mdev)
{
	struct mcuio_hc_data *data = dev_get_drvdata(&mdev->dev);
	atomic_set(&data->removing, 1);
	barrier();
	flush_kthread_worker(&data->tx_kworker);
	kthread_stop(data->tx_kworker_task);
	__cleanup_outstanding_requests(data);
	devm_kfree(&mdev->dev, data);
	return 0;
}

static const struct mcuio_device_id hc_drv_ids[] = {
	{
		.class = MCUIO_CLASS_HOST_CONTROLLER,
		.class_mask = 0xffff,
	},
	{
		.class = MCUIO_CLASS_SOFT_HOST_CONTROLLER,
		.class_mask = 0xffff,
	},
	/* Terminator */
	{
		.device = MCUIO_NO_DEVICE,
		.class = MCUIO_CLASS_UNDEFINED,
	},
};

static struct mcuio_driver mcuio_host_controller_driver = {
	.driver = {
		.name = "mcuio-hc",
	},
	.id_table = hc_drv_ids,
	.probe = mcuio_host_controller_probe,
	.remove = mcuio_host_controller_remove,
};

static int __init mcuio_host_controller_init(void)
{
	return mcuio_driver_register(&mcuio_host_controller_driver,
				     THIS_MODULE);
}

static void __exit mcuio_host_controller_exit(void)
{
	return mcuio_driver_unregister(&mcuio_host_controller_driver);
}

subsys_initcall(mcuio_host_controller_init);
module_exit(mcuio_host_controller_exit);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO host controller driver");
MODULE_LICENSE("GPL v2");
