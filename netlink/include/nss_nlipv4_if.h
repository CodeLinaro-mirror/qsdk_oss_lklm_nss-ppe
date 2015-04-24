/*
 **************************************************************************
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
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
 * @file nss_nlipv4_if.h
 *	NSS Netlink IPv4 headers
 */
#ifndef __NSS_NLIPV4_IF_H
#define __NSS_NLIPV4_IF_H

/**
 * IPv4 forwarding Family
 */
#define NSS_NLIPV4_FAMILY "nss_nlipv4"

/**
 * @brief IPv4 rule
 */
struct nss_nlipv4_rule {
	struct nss_nlcmn cm;		/**< common message header */

	char flow_ifname[IFNAMSIZ];	/**< ingress interface name */
	char return_ifname[IFNAMSIZ];	/**< egress interface name */

	struct nss_ipv4_msg nim;	/**< rule message */
};

/**
 * @brief NETLINK IPv4 message init
 *
 * @param rule[IN] NSS NETLINK IPv4 rule
 * @param type[IN] IPv4 message type
 * @param user_data[IN] user data per message
 *
 */
static inline void nss_nlipv4_rule_init(struct nss_nlipv4_rule *rule, enum nss_ipv4_message_types type, uint32_t user_data)
{
	nss_nlcmn_init(&rule->cm, type, sizeof(struct nss_nlipv4_rule), user_data);
}

#endif /* __NSS_NLIPV4_IF_H */
