/*
 * ********************************************************************************
 * Copyright (c) 2016-2019, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 **********************************************************************************
 */

#ifndef __NSS_IPSECMGR_PRIV_H
#define __NSS_IPSECMGR_PRIV_H

#include <net/ipv6.h>
#include <nss_api_if.h>
#include "nss_ipsecmgr_tunnel.h"

#define NSS_IPSECMGR_DEBUG_LVL_ERROR 1		/**< Turn on debug for an error. */
#define NSS_IPSECMGR_DEBUG_LVL_WARN 2		/**< Turn on debug for a warning. */
#define NSS_IPSECMGR_DEBUG_LVL_INFO 3		/**< Turn on debug for information. */
#define NSS_IPSECMGR_DEBUG_LVL_TRACE 4		/**< Turn on debug for trace. */

#define nss_ipsecmgr_info_always(s, ...) pr_info("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__)

#define nss_ipsecmgr_error(s, ...) do {	\
	if (net_ratelimit()) {	\
		pr_alert("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__);	\
	}	\
} while (0)

#define nss_ipsecmgr_warn(s, ...) do {	\
	if (net_ratelimit()) {	\
		pr_warn("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__);	\
	}	\
} while (0)

#if defined(CONFIG_DYNAMIC_DEBUG)
/*
 * Compile messages for dynamic enable/disable
 */
#define nss_ipsecmgr_info(s, ...) pr_debug("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__)
#define nss_ipsecmgr_trace(s, ...) pr_debug("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__)

#else
/*
 * Statically compile messages at different levels
 */
#define nss_ipsecmgr_info(s, ...) {	\
	if (NSS_IPSECMGR_DEBUG_LEVEL > NSS_IPSECMGR_DEBUG_LVL_INFO) {	\
		pr_notice("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__);	\
	}	\
}
#define nss_ipsecmgr_trace(s, ...) {	\
	if (NSS_IPSECMGR_DEBUG_LEVEL > NSS_IPSECMGR_DEBUG_LVL_TRACE) {	\
		pr_info("%s[%d]:" s "\n", __func__, __LINE__, ##__VA_ARGS__);	\
	}	\
}

#endif /* !CONFIG_DYNAMIC_DEBUG */
#define NSS_IPSECMGR_CHK_POW2(x) (__builtin_constant_p(x) && !(~(x - 1) & (x >> 1)))

#define NSS_IPSECMGR_MAX_NAME (NSS_IPSECMGR_MAX_KEY_NAME + 64)

#define NSS_IPSECMGR_SA_MAX  64 /* Max SAs */
#if (~(NSS_IPSECMGR_SA_MAX - 1) & (NSS_IPSECMGR_SA_MAX >> 1))
#error "NSS_IPSECMGR_SA_MAX is not a power of 2"
#endif

#define NSS_IPSECMGR_NODE_STATS_SZ 512

#define NSS_IPSECMGR_DEFAULT_TUN_NAME "ipsecdummy"
#define NSS_IPSECMGR_ESP_TRAIL_SZ 2 /* esp trailer size */
#define NSS_IPSECMGR_ESP_PAD_SZ 14 /* maximum amount of padding */

/*
 * IPsec manager drv instance
 */
struct nss_ipsecmgr_drv {
	struct dentry *dentry;			/* Debugfs entry per ipsecmgr module. */
	struct net_device *dev;			/* IPsec dummy net device. */

	rwlock_t lock;					/* lock for all DB operations. */
	struct list_head sa_db[NSS_IPSECMGR_SA_MAX];	/* SA database. */
	struct list_head flow_db[NSS_IPSECMGR_FLOW_MAX];/* Flow database. */
	struct list_head tun_db;			/* Tunnel database */

	int encap_ifnum;			/* NSS encap interface. */
	int decap_ifnum;			/* NSS decap interface. */
	int data_ifnum;				/* NSS data interface. */

	struct nss_ctx_instance *nss_ctx;	/* NSS context. */
	struct delayed_work cfg_work;		/* Configure node work */
	bool ipsec_inline;			/* IPsec inline mode */
	uint16_t max_mtu;			/* Maximum MTU supported */

	struct nss_ipsecmgr_node_stats node_stats;	/* Node stats */
};

/*
 * nss_ipsecmgr_tuple2index()
 * 	Change tuple to hash index
 */
static inline uint32_t nss_ipsecmgr_tuple2index(struct nss_ipsec_tuple *tuple, uint32_t max)
{
	uint32_t val = 0;

	val ^= tuple->dst_addr[0];
	val ^= tuple->src_addr[0];

	val ^= tuple->dst_addr[1];
	val ^= tuple->src_addr[1];

	val ^= tuple->dst_addr[2];
	val ^= tuple->src_addr[2];

	val ^= tuple->dst_addr[3];
	val ^= tuple->src_addr[3];

	val ^= tuple->esp_spi;

	val ^= tuple->dst_port;
	val ^= tuple->src_port;

	val ^= tuple->proto_next_hdr;
	val ^= tuple->ip_ver;
	val ^= tuple->pattern;

	return val & (max - 1);
}

/*
 * nss_ipsecmgr_tuple_match()
 * 	Match the tuple
 */
static inline bool nss_ipsecmgr_tuple_match(struct nss_ipsec_tuple *tuple, struct nss_ipsec_tuple *match)
{
	uint8_t status = 0;

	status += !!(tuple->dst_addr[0] ^ match->dst_addr[0]);
	status += !!(tuple->dst_addr[1] ^ match->dst_addr[1]);
	status += !!(tuple->dst_addr[2] ^ match->dst_addr[2]);
	status += !!(tuple->dst_addr[3] ^ match->dst_addr[3]);

	status += !!(tuple->src_addr[0] ^ match->src_addr[0]);
	status += !!(tuple->src_addr[1] ^ match->src_addr[1]);
	status += !!(tuple->src_addr[2] ^ match->src_addr[2]);
	status += !!(tuple->src_addr[3] ^ match->src_addr[3]);

	status += !!(tuple->esp_spi ^ match->esp_spi);
	status += !!(tuple->dst_port ^ match->dst_port);
	status += !!(tuple->src_port ^ match->src_port);

	status += !!(tuple->proto_next_hdr ^ match->proto_next_hdr);
	status += !!(tuple->ip_ver ^ match->ip_ver);
	status += !!(tuple->pattern ^ match->pattern);

	return !status;
}

/*
 * nss_ipsecmgr_ntoh_v6addr()
 *	Network to host order and swap
 */
static inline void nss_ipsecmgr_ntoh_v6addr(uint32_t *dest, uint32_t *src)
{
	dest[3] = ntohl(src[0]);
	dest[2] = ntohl(src[1]);
	dest[1] = ntohl(src[2]);
	dest[0] = ntohl(src[3]);
}

/*
 * nss_ipsecmgr_hton_v6addr()
 *	Host to network order and swap
 */
static inline void nss_ipsecmgr_hton_v6addr(uint32_t *dest, uint32_t *src)
{
	dest[3] = htonl(src[0]);
	dest[2] = htonl(src[1]);
	dest[1] = htonl(src[2]);
	dest[0] = htonl(src[3]);
}

#endif
