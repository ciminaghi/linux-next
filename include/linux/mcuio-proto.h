#ifndef __MCUIO_PROTO_H__
#define __MCUIO_PROTO_H__

#ifdef __KERNEL__
#include <linux/types.h>

#define mcuio_ntohl(a) le32_to_cpu(a)
#define mcuio_htonl(a) cpu_to_le32(a)
#define mcuio_ntohs(a) le16_to_cpu(a)
#define mcuio_htons(a) cpu_to_le16(a)

#else /* !__KERNEL__ */

#include <stdint.h>

#define BITS_PER_LONG
#define BIT(a) (1UL << (a))
#define BIT_MASK(a) (1UL << ((nr) % BITS_PER_LONG))

#if defined _BSD_SOURCE
#include <endian.h>

#define mcuio_ntohl(a) le32toh(a)
#define mcuio_htonl(a) htole32(a)
#define mcuio_ntohs(a) le16toh(a)
#define mcuio_htons(a) htole16(a)

#else /* !_BSD_SOURCE */

#ifndef mcuio_nhtol
#error please define mcuio_nhtol
#endif
#ifndef mcuio_htonl
#error please define mcuio_htonl
#endif
#ifndef mcuio_nhtos
#error please define mcuio_nhtos
#endif
#ifndef mcuio_htons
#error please define mcuio_htons
#endif

#endif /* !__BSD_SOURCE */
#endif /* !__KERNEL__ */

#define MASK(b)	(BIT(b) - 1)


#define mcuio_type_rdb			0
#define mcuio_type_wrb			1
#define mcuio_type_rdw			2
#define mcuio_type_wrw			3
#define mcuio_type_rddw			4
#define mcuio_type_wrdw			5
#define mcuio_type_rdmb			6
#define mcuio_type_wrmb			7

#define mcuio_error_bit			BIT(5)
#define mcuio_reply_bit			BIT(6)
#define mcuio_fill_data_bit		BIT(7)
#define mcuio_actual_type_mask		0x07

#define mcuio_addr_offset_bits		16
#define mcuio_addr_offset_shift		0
#define mcuio_addr_offset_mask		MASK(mcuio_addr_offset_bits)

#define mcuio_addr_func_bits		5
#define mcuio_addr_func_shift		mcuio_addr_offset_bits
#define mcuio_addr_func_mask		MASK(mcuio_addr_func_bits)
#define MCUIO_FUNCS_PER_DEV		BIT(mcuio_addr_func_bits)

#define mcuio_addr_dev_bits		3
#define mcuio_addr_dev_shift		(mcuio_addr_func_bits + \
					 mcuio_addr_offset_bits)
#define mcuio_addr_dev_mask		MASK(mcuio_addr_dev_bits)
#define MCUIO_DEVS_PER_BUS		BIT(mcuio_addr_dev_bits)

#define mcuio_addr_type_bits		8
#define mcuio_addr_type_shift		(mcuio_addr_func_bits + \
					 mcuio_addr_offset_bits + \
					 mcuio_addr_dev_bits)
#define mcuio_addr_type_mask		MASK(mcuio_addr_type_bits)

/*
 * Max data length for an extended packet
 * The first sub-frame can carry 8 data bytes, the second to the
 * last-minus-one 16 bytes, the last sub-frame 14 bytes
 * We assume that a single extended frame can take 4k of memory.x
 *
 * 8 + 16*n + 14 = 4096 -> n = (4096 - 22)/16 = 254
 * 8 + 16*254 + 14 = 4086
 */
#define MCUIO_MAX_EXT_DATALEN 4086

/* An mcuio base packet or 1st subframe, sync chars excluded */
struct mcuio_base_packet {
	uint8_t sync[2];
	uint32_t addr_type;
	union {
		struct {
			uint32_t data[2];
			uint16_t crc;
		} base;
		struct {
			uint16_t datalen;
			uint8_t data[8];
		} ext;
		uint8_t raw[10];
	} body;
} __attribute__((packed));

static inline unsigned mcuio_packet_type(struct mcuio_base_packet *p)
{
	return mcuio_ntohl(p->addr_type) >> mcuio_addr_type_shift;
}

static inline void mcuio_set_packet_type(struct mcuio_base_packet *p,
					 uint32_t t)
{
	p->addr_type &= (mcuio_addr_type_mask << mcuio_addr_type_shift);
	p->addr_type |= mcuio_htonl(t) << mcuio_addr_type_shift;
}

static inline unsigned mcuio_base_datalen(uint32_t t)
{
	if (t & mcuio_fill_data_bit)
		return sizeof(uint64_t);
	return (1 << ((t & ~1) >> 1));
}

static inline int mcuio_type_is_read(uint32_t t)
{
	return !(t & 0x1);
}

static inline int mcuio_packet_is_read(struct mcuio_base_packet *p)
{
	return mcuio_type_is_read(mcuio_packet_type(p));
}

static inline int mcuio_packet_is_write(struct mcuio_base_packet *p)
{
	return !mcuio_type_is_read(mcuio_packet_type(p));
}

static inline int mcuio_type_is_reply(uint32_t t)
{
	return t & mcuio_reply_bit;
}

static inline int mcuio_packet_is_reply(struct mcuio_base_packet *p)
{
	return mcuio_type_is_reply(mcuio_packet_type(p));
}

static inline int mcuio_type_is_extended(uint32_t t)
{
	return ((((t & mcuio_actual_type_mask) == mcuio_type_wrmb) &&
		 !mcuio_type_is_reply(t)) ||
		(((t & mcuio_actual_type_mask) == mcuio_type_rdmb) &&
		 mcuio_type_is_reply(t)));
}

static inline int mcuio_packet_is_extended(struct mcuio_base_packet *p)
{
	return mcuio_type_is_extended(mcuio_packet_type(p));
}

static inline int mcuio_packet_is_base(struct mcuio_base_packet *p)
{
	return !mcuio_packet_is_extended(p);
}

static inline int mcuio_type_is_fill_data(uint32_t t)
{
	return t & mcuio_fill_data_bit;
}

static inline int mcuio_packet_is_fill_data(struct mcuio_base_packet *p)
{
	return mcuio_type_is_fill_data(mcuio_packet_type(p));
}

static inline void *mcuio_packet_data(struct mcuio_base_packet *p)
{
	/* FIXME: get rid of the cast */
	return mcuio_packet_is_extended(p) ?
		(void *)p->body.ext.data : (void *)p->body.base.data;
}

static inline unsigned mcuio_base_packet_datalen(struct mcuio_base_packet *p)
{
	return mcuio_base_datalen(mcuio_packet_type(p));
}

static inline unsigned mcuio_packet_datalen(struct mcuio_base_packet *p)
{
	if (mcuio_packet_is_extended(p))
		return mcuio_ntohl(p->body.ext.datalen);
	return mcuio_base_packet_datalen(p);
}

static inline int mcuio_ext_packet_datalen_to_nsub(int datalen)
{
#define LAST_SUBF_DLEN (sizeof(struct mcuio_base_packet) - sizeof(uint16_t))
#define MID_SUBF_DLEN sizeof(struct mcuio_base_packet)
#define FIRST_SUBF_DLEN 8
	int out = -1;

	if (datalen <= sizeof(uint64_t) || datalen > MCUIO_MAX_EXT_DATALEN)
		return out;
	return 1 + ((datalen - FIRST_SUBF_DLEN)/MID_SUBF_DLEN) +
		(((datalen - FIRST_SUBF_DLEN)%MID_SUBF_DLEN) > LAST_SUBF_DLEN ?
		  1 : 0) + 1;
}

static inline int mcuio_packet_datalen_to_nsub(int datalen)
{
	if (datalen <= sizeof(uint64_t))
		return 1;
	return mcuio_ext_packet_datalen_to_nsub(datalen);
}

static inline int mcuio_packet_nsub(struct mcuio_base_packet *p)
{
	return mcuio_packet_datalen_to_nsub(mcuio_packet_datalen(p));
}

static inline unsigned mcuio_packet_offset(struct mcuio_base_packet *p)
{
	return (mcuio_ntohl(p->addr_type) >> mcuio_addr_offset_shift) &
		mcuio_addr_offset_mask;
}

static inline unsigned mcuio_packet_func(struct mcuio_base_packet *p)
{
	return (mcuio_ntohl(p->addr_type) >> mcuio_addr_func_shift) &
		mcuio_addr_func_mask;
}

static inline unsigned mcuio_packet_dev(struct mcuio_base_packet *p)
{
	return (mcuio_ntohl(p->addr_type) >> mcuio_addr_dev_shift) &
		mcuio_addr_dev_mask;
}

static inline void mcuio_packet_set_addr_type(struct mcuio_base_packet *p,
					      unsigned dev,
					      unsigned func,
					      unsigned offset,
					      unsigned type,
					      int fill)
{
	p->addr_type = mcuio_htonl(((offset & mcuio_addr_offset_mask) <<
				    mcuio_addr_offset_shift) |
				   ((func & mcuio_addr_func_mask) <<
				    mcuio_addr_func_shift) |
				   ((dev & mcuio_addr_dev_mask) <<
				    mcuio_addr_dev_shift) |
				   (((type & mcuio_addr_type_mask) |
				     (fill ? mcuio_fill_data_bit : 0)) <<
				    mcuio_addr_type_shift));
}

static inline int mcuio_packet_is_reply_to(struct mcuio_base_packet *p,
					   struct mcuio_base_packet *request)
{
	unsigned t = mcuio_packet_type(p);
	unsigned rt = mcuio_packet_type(request);
	unsigned o = mcuio_packet_offset(p);
	unsigned ro = mcuio_packet_offset(request);
	unsigned f = mcuio_packet_func(p);
	unsigned rf = mcuio_packet_func(request);
	unsigned d = mcuio_packet_dev(p);
	unsigned rd = mcuio_packet_dev(request);

	pr_debug("type = 0x%02x, request type = 0x%02x\n", t, rt);
	return (t & mcuio_actual_type_mask) == rt && o == ro && f == rf &&
		d == rd;
}


static inline void mcuio_packet_set_reply(struct mcuio_base_packet *p)
{
	mcuio_set_packet_type(p, mcuio_packet_type(p) | mcuio_reply_bit);
}

static inline int mcuio_packet_is_error(struct mcuio_base_packet *p)
{
	return mcuio_packet_type(p) & mcuio_error_bit;
}

static inline void mcuio_packet_set_error(struct mcuio_base_packet *p)
{
	mcuio_set_packet_type(p, mcuio_packet_type(p) | mcuio_error_bit);
}

static inline void mcuio_packet_set_fill_data(struct mcuio_base_packet *p)
{
	mcuio_set_packet_type(p, mcuio_packet_type(p) | mcuio_fill_data_bit);
}

static inline const char *mcuio_packet_type_to_str(int t)
{
	switch(t & mcuio_actual_type_mask) {
	case mcuio_type_rdb:
		return "rdb";
	case mcuio_type_wrb:
		return "wrb";
	case mcuio_type_rdw:
		return "rdw";
	case mcuio_type_wrw:
		return "wrw";
	case mcuio_type_rddw:
		return "rddw";
	case mcuio_type_wrdw:
		return "wrdw";
	case mcuio_type_rdmb:
		return "rdmb";
	case mcuio_type_wrmb:
		return "rwmb";
	}
	return "unknown";
}

struct mcuio_func_descriptor {
	uint32_t device_vendor;
	uint32_t rev_class;
} __attribute__((packed));

static inline uint16_t mcuio_get_vendor(struct mcuio_func_descriptor *d)
{
	return mcuio_ntohs(mcuio_ntohl(d->device_vendor) & 0xffff);
}

static inline uint16_t mcuio_get_device(struct mcuio_func_descriptor *d)
{
	return mcuio_ntohs(mcuio_ntohl(d->device_vendor) >> 16);
}

static inline uint32_t mcuio_get_class(struct mcuio_func_descriptor *d)
{
	return mcuio_ntohl(d->rev_class) >> 8;
}

static inline uint32_t mcuio_get_rev(struct mcuio_func_descriptor *d)
{
	return mcuio_ntohl(d->rev_class) & ((1 << 8) - 1);
}

#endif /* __MCUIO_PROTO_H__ */
