/*
 **************************************************************************
 * Copyright (c) 2014-2015,2018-2020 The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/*
 * nss_nlcapwap.h
 *	NSS Netlink Capwap API definitions
 */
#ifndef __NSS_NLCAPWAP_H
#define __NSS_NLCAPWAP_H

#include "nss_nlcapwap_if.h"

#define NSS_NLCAPWAP_SKB_TAILROOM 192			/**< Tailroom for skb */
#define NSS_NLCAPWAP_IP_VERS_4 4			/**< Ip version 4 */
#define NSS_NLCAPWAP_IP_VERS_6 6			/**< Ip version 6 */
#define NSS_NLCAPWAP_VLAN_TAG_INVALID 0xFFF		/**< Invalid vlan tag */
#define NSS_NLCAPWAP_WAN_IFNUM 0			/**< WAN interface number */
#define NSS_NLCAPWAP_SKB_RESERVE_SZ_TWO 2		/**< skb reserve size */
#define NSS_NLCAPWAP_SKB_RESERVE_SZ_NINTY_EIGHT 98	/**< skb reserve size */
#define NSS_NLCAPWAP_SKB_RESERVE_SZ_HUNDRED 100		/**< skb reserve size */
#define NSS_NLCAPWAP_80211E_ZERO 0			/**< 80211e type */
#define NSS_NLCAPWAP_80211E_ONE 1			/**< 80211e type */
#define NSS_NLCAPWAP_DATA 0xcc				/**< Dummy data */

/*
 * nss_nlcapwap_meta_header_type
 *	Capwap meta header type
 */
enum nss_nlcapwap_meta_header_type {
	NSS_NLCAPWAP_META_HEADER_TYPE_UNKNOWN = -1,	/**< Unknown meta header type */
	NSS_NLCAPWAP_META_HEADER_TYPE_ZERO,		/**< capwap meta header type 0 */
	NSS_NLCAPWAP_META_HEADER_TYPE_ONE,		/**< capwap meta header type 1 */
	NSS_NLCAPWAP_META_HEADER_TYPE_MAX		/**< Max meta header type */
};

/*
 * nss_nlcapwap_global_ctx
 *	Global context for capwap
 */
struct nss_nlcapwap_global_ctx {
	struct nss_nlcapwap_meta_header meta_header;	/**< Stores meta header of tunnel */
	int nss_nlcapwap_80211e;			/**< Enable or disable wireless qos */
};

/*
 * nss_nlcapwap_ndev_priv
 *	Netdevice private data
 */
struct nss_nlcapwap_ndev_priv {
	uint32_t capwap_seq;				/**< Sequence number */
};

bool nss_nlcapwap_init(void);
bool nss_nlcapwap_exit(void);

#if (CONFIG_NSS_NLCAPWAP == 1)
#define NSS_NLCAPWAP_INIT nss_nlcapwap_init
#define NSS_NLCAPWAP_EXIT nss_nlcapwap_exit
#else
#define NSS_NLCAPWAP_INIT 0
#define NSS_NLCAPWAP_EXIT 0
#endif /* !CONFIG_NSS_NLCAPWAP */

#endif /* __NSS_NLCAPWAP_H */
