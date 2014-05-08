/*
 * Regmap for remote mcuio devices (not living on this machine)
 * Presently, all mcuio devices are remote devices except for the ho
 * controller. Code comes from regmap-mmio
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include <linux/mcuio.h>
#include <linux/mcuio-proto.h>

/**
 * mcuio bus context
 * @hc: pointer to target device
 * @dev: device number of mcuio device
 * @func: function number of mcuio device
 */
struct regmap_mcuio_context {
	struct mcuio_device *mdev;
	unsigned val_bytes;
};

typedef void (*copyf)(void *dst, const void *src, int n);

static void copyb(void *dst, const void *src, int n)
{
	memcpy(dst, src, n);
}

static void copyw(void *dst, const void *src, int n)
{
	int i;
	for (i = 0; i < n; i++)
		*(u16 *)dst = *(u16 *)src;
}

static void copydw(void *dst, const void *src, int n)
{
	int i;
	for (i = 0; i < n; i++)
		*(u32 *)dst = *(u32 *)src;
}

static int regmap_mcuio_gather_write(void *context,
				     const void *reg, size_t reg_size,
				     const void *val, size_t val_size)
{
	return -EOPNOTSUPP;
}

static int regmap_mcuio_write(void *context, const void *data, size_t count)
{
	struct regmap_mcuio_context *ctx = context;
	struct mcuio_request r;
	u32 offset;
	unsigned t;
	int ret = 0, fill = 0, n = 1;
	copyf f;
	void *dst = r.data;

	offset = *(u32 *)data;
	r.extended_data = NULL;
	r.extended_datalen = 0;

	switch (ctx->val_bytes) {
	case 1:
		f = copyb;
		t = mcuio_type_wrb;
		if (count == 1)
			break;
		if (count == sizeof(u64)) {
			fill = 1;
			break;
		}
		/* No data has to be copied ! */
		f = NULL;
		n = count;
		t = mcuio_type_wrmb;
		/* FIXME: AVOID THIS CAST !!! */
		r.extended_data = (void *)data;
		r.extended_datalen = n;
		break;
	case 2:
		if (count % sizeof(u16))
			return -EINVAL;
		t = mcuio_type_wrw;
		if (!(count % sizeof(u64)))
			fill = 1;
		f = copyw;
		break;
	case 4:
		if (count % sizeof(u32))
			return -EINVAL;
		t = mcuio_type_wrdw;
		if (!(count % sizeof(u64)))
			fill = 1;
		f = copydw;
		break;
	default:
		return -EINVAL;
	}

	if (fill)
		n = sizeof(u64) / ctx->val_bytes;

	r.mdev = ctx->mdev;
	r.type = t;
	r.dont_free = 1;
	r.fill = fill;

	while (count) {
		r.offset = offset;
		if (f)
			f(dst, data, n);
		ret = mcuio_submit_request(&r);
		if (ret)
			break;
		data += (n * ctx->val_bytes);
		count -= (n * ctx->val_bytes);
		offset += (n * ctx->val_bytes);
	}
	return ret;
}

static int regmap_mcuio_read(void *context,
			     const void *reg, size_t reg_size,
			     void *val, size_t val_size)
{
	struct regmap_mcuio_context *ctx = context;
	struct mcuio_request r;
	u32 offset = *(u32 *)reg;
	int ret = 0, fill = 0, n = 1;
	copyf f;
	unsigned t;

	BUG_ON(reg_size != 4);

	switch (ctx->val_bytes) {
	case 1:
		t = mcuio_type_rdb;
		f = copyb;
		if (val_size == 1)
			break;
		if (val_size == sizeof(u64)) {
			fill = 1;
			break;
		}
		f = NULL;
		n = val_size;
		t = mcuio_type_rdmb;
		r.extended_data = val;
		r.extended_datalen = val_size;
		break;
	case 2:
		if (val_size % sizeof(u16))
			return -EINVAL;
		t = mcuio_type_rdw;
		if (!(val_size % sizeof(u64)))
			fill = 1;
		f = copyw;
		break;
	case 4:
		if (val_size % sizeof(u32))
			return -EINVAL;
		t = mcuio_type_rddw;
		if (!(val_size % sizeof(u64)))
			fill = 1;
		f = copydw;
		break;
	default:
		return -EINVAL;
	}

	if (fill)
		n = sizeof(u64) / ctx->val_bytes;

	r.type = t;
	r.mdev = ctx->mdev;
	r.dont_free = 1;
	r.fill = 1;

	while (val_size) {
		r.offset = offset;
		ret = mcuio_submit_request(&r);
		if (ret)
			break;
		if (f)
			f(val, r.data, n);
		val_size -= (n * ctx->val_bytes);
		val += (n * ctx->val_bytes);
		offset += (n * ctx->val_bytes);
	}
	return ret;
}


static void regmap_mcuio_free_context(void *context)
{
	struct regmap_mcuio_context *ctx = context;
	kfree(ctx);
}

static struct regmap_bus regmap_mcuio = {
	.write = regmap_mcuio_write,
	.read = regmap_mcuio_read,
	.gather_write = regmap_mcuio_gather_write,
	.free_context = regmap_mcuio_free_context,
	.reg_format_endian_default = REGMAP_ENDIAN_NATIVE,
	.val_format_endian_default = REGMAP_ENDIAN_NATIVE,
};

static struct regmap_mcuio_context *
regmap_mcuio_setup_context(struct mcuio_device *mdev,
			   const struct regmap_config *config)
{
	struct regmap_mcuio_context *ctx;
	int min_stride;

	if (config->reg_bits != 32)
		return ERR_PTR(-EINVAL);

	switch (config->val_bits) {
	case 8:
		/* The core treats 0 as 1 */
		min_stride = 0;
		break;
	case 16:
		min_stride = 2;
		break;
	case 32:
		min_stride = 4;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return ERR_PTR(-ENOMEM);

	ctx->mdev = mdev;
	ctx->val_bytes = config->val_bits / 8;
	return ctx;
}


/**
 * regmap_init_mcuio(): Initialise mcuio register map
 *
 * @dev: Device that will be interacted with
 * @hc: mcuio system controller
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
struct regmap *regmap_init_mcuio(struct mcuio_device *mdev,
				 const struct regmap_config *config)
{
	struct regmap_mcuio_context *ctx;
	ctx = regmap_mcuio_setup_context(mdev, config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return regmap_init(&mdev->dev, &regmap_mcuio, ctx, config);
}
EXPORT_SYMBOL_GPL(regmap_init_mcuio);

/**
 * devm_regmap_init_mcuio(): Initialise mcuio register map, device manage
 * version
 *
 * @dev: Device that will be interacted with
 * @hc: mcuio system controller
 * @config: Configuration for register map
 *
 * The return value will be an ERR_PTR() on error or a valid pointer to
 * a struct regmap.
 */
struct regmap *devm_regmap_init_mcuio(struct mcuio_device *mdev,
				      const struct regmap_config *config)
{
	struct regmap_mcuio_context *ctx;
	ctx = regmap_mcuio_setup_context(mdev, config);
	if (IS_ERR(ctx))
		return ERR_CAST(ctx);

	return devm_regmap_init(&mdev->dev, &regmap_mcuio, ctx, config);
}
EXPORT_SYMBOL_GPL(devm_regmap_init_mcuio);

MODULE_AUTHOR("Davide Ciminaghi");
MODULE_DESCRIPTION("MCUIO bus regmap implementation");
MODULE_LICENSE("GPL v2");
