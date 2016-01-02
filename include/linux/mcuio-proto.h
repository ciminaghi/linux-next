#ifndef __MCUIO_PROTO_H__
#define __MCUIO_PROTO_H__

#include <linux/types.h>

#define mcuio_ntohl(a) le32_to_cpu(a)
#define mcuio_htonl(a) cpu_to_le32(a)
#define mcuio_ntohs(a) le16_to_cpu(a)
#define mcuio_htons(a) cpu_to_le16(a)


#endif /* __MCUIO_PROTO_H__ */
