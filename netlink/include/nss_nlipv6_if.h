/*
 **************************************************************************
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
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
 * @file nss_nlipv6_if.h
 *	NSS Netlink IPv6 headers
 */
#ifndef __NSS_NLIPV6_IF_H
#define __NSS_NLIPV6_IF_H

/**
 * IPv6 forwarding Family
 */
#define NSS_NLIPV6_FAMILY "nss_nlipv6"

#define NSS_NLIPV6_ADDR_BITS (sizeof(uint32_t) * 4)

/**
 * @brief IPv6 rule
 */
struct nss_nlipv6_rule {
	struct nss_nlcmn cm;		/**< common message header */

	char flow_ifname[IFNAMSIZ];	/**< ingress interface name */
	char return_ifname[IFNAMSIZ];	/**< egress interface name */

	struct nss_ipv6_msg nim;	/**< rule message */
};

/**
 * @brief NETLINK IPv6 message init
 *
 * @param rule[IN] NSS NETLINK IPv6 rule
 * @param type[IN] IPv6 message type
 */
static inline void nss_nlipv6_rule_init(struct nss_nlipv6_rule *rule, enum nss_ipv6_message_types type)
{
	nss_nlcmn_set_ver(&rule->cm, NSS_NL_VER);
	nss_nlcmn_init_cmd(&rule->cm, sizeof(struct nss_nlipv6_rule), type);
}

#endif /* __NSS_NLIPV6_IF_H */
