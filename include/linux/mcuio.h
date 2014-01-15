#ifndef __MCUIO_H__
#define __MCUIO_H__

#ifdef __KERNEL__

#include <linux/device.h>
#include <linux/module.h>

struct mcuio_packet;

/*
 * Id of an mcuio device.
 */
struct mcuio_device_id {
	unsigned int device;
	unsigned int vendor;
	unsigned int class;
	unsigned int class_mask;
};

struct mcuio_request;
typedef int (*rfun)(struct mcuio_request *, void *data);

/*
 * An mcuio device.
 * @id: device id, as defined above
 * @bus: bus number
 * @device: device number (0 for host controllers)
 * @fn: function number (0 for host controllers)
 * @dev: the relevant device
 * @do_request: pointer to request processing function (sends request through
 * host controller for remote devices or executes request for local devices)
 * @do_request_data: pointer to private data passed to do_request callback
 */
struct mcuio_device {
	struct mcuio_device_id id;
	unsigned bus, device, fn;
	struct device dev;
	rfun do_request;
	void *do_request_data;
};

#define to_mcuio_dev(_dev) container_of(_dev, struct mcuio_device, dev)

/*
 * mcuio_driver -- an mcuio driver struc
 */
struct mcuio_driver {
	const struct mcuio_device_id	*id_table;
	int (*probe)(struct mcuio_device *dev);
	int (*remove)(struct mcuio_device *dev);
	struct device_driver		driver;
};

#define to_mcuio_drv(_drv) container_of(_drv, struct mcuio_driver, driver)

/*
 * The parent of all mcuio controllers on this machine
 */
extern struct device mcuio_bus;

int mcuio_driver_register(struct mcuio_driver *drv, struct module *owner);
void mcuio_driver_unregister(struct mcuio_driver *drv);
int mcuio_device_register(struct mcuio_device *dev,
			  struct device_type *type,
			  struct device *parents);
void mcuio_device_unregister(struct mcuio_device *dev);

struct mcuio_request;

typedef void (*request_cb)(struct mcuio_request *);

/*
 * This represents an mcuio request
 * @mdev: pointer to mcuio device
 * @offset: offset within function address space
 * @type: request type
 * @cb: pointer to callback function
 * @cb_data: callback data.
 * @status: status of request (0 completed OK, -EXXXX errors)
 * @data: request data (data for non extended packets)
 * @extended_data_length: length of data for extended packets.
 * @extended_data: pointer to data arrat for rd/wr many bytes packets
 * @list: used for enqueueing requests
 * @to_work: delayed_work struct for request timeout management
 * @priv: private data. FIX THIS
 * @dont_free: this flag is !0 when request shall not be kfree'd
 * @fill: if this is !0 the resulting request packet shall have its fill data
 *        flag set
 */
struct mcuio_request {
	struct mcuio_device *mdev;
	unsigned offset;
	unsigned type;
	request_cb cb;
	void *cb_data;
	int status;
	uint32_t data[2];
	int extended_datalen;
	void *extended_data;
	struct list_head list;
	struct delayed_work to_work;
	void *priv;
	int dont_free;
	int fill;
};

/*
 * Submit a request, block until request done
 *
 * Shall be implemented later on, return error at the moment
 *
 * @r: pointer to request
 */
static inline int mcuio_submit_request(struct mcuio_request *r)
{
	return -EOPNOTSUPP;
}


#endif /* __KERNEL__ */

#endif /* __MCUIO_H__ */
