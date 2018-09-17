/*
 **************************************************************************
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
 * @file nss_nlcranipc_if.h
 *	NSS Netlink CRANIPC headers
 */
#ifndef __NSS_NLCRANIPC_IF_H
#define __NSS_NLCRANIPC_IF_H

/**
 * CRANIPC forwarding Family
 */
#define NSS_NLCRANIPC_FAMILY "nss_nlcranipc"
#define NSS_NLCRANIPC_MCAST_GRP "nss_nlcran_mc"


/**
 * @brief CRANIPC Config
 */
struct nss_nlcranipc_msg {
	struct nss_nlcmn cm;		/**< common message header */

	struct nss_cranipc_msg nim;	/**< config message */
};

/**
 * @brief NETLINK CRANIPC message init
 *
 * @param rule[IN] NSS NETLINK CRANIPC Config
 * @param type[IN] CRANIPC message type
 */
static inline void nss_nlcranipc_rule_init(struct nss_nlcranipc_msg *msg, enum nss_cranipc_msg_type type)
{
	nss_nlcmn_set_ver(&msg->cm, NSS_NL_VER);
	nss_nlcmn_init_cmd(&msg->cm, sizeof(struct nss_nlcranipc_msg), type);
}

#endif /* __NSS_NLCRANIPC_IF_H */
