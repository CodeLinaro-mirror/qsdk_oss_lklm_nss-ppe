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
 * nss_nlcranipc.c
 *	NSS Netlink CRANIPC Handler
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/netlink.h>
#include <linux/rcupdate.h>
#include <linux/etherdevice.h>
#include <linux/if_addr.h>
#include <linux/version.h>
#include <linux/vmalloc.h>
#include <linux/if_vlan.h>
#include <linux/completion.h>
#include <linux/semaphore.h>
#include <linux/in.h>

#include <net/genetlink.h>
#include <net/route.h>
#include <net/arp.h>
#include <net/neighbour.h>
#include <net/genetlink.h>
#include <net/net_namespace.h>
#include <net/sock.h>

#include <nss_api_if.h>
#include <nss_cmn.h>
#include <nss_nl_if.h>
#include "nss_nl.h"
#include "nss_nlcmn_if.h"
#include "nss_nlcranipc_if.h"

/*
 * prototypes
 */

static int nss_nlcranipc_ops_config(struct sk_buff *skb, struct genl_info *info);


/*
 * CRANIPC family definition
 */
static struct genl_family nss_nlcranipc_family = {
	.id = GENL_ID_GENERATE,				/* Auto generate ID */
	.name = NSS_NLCRANIPC_FAMILY,			/* family name string */
	.hdrsize = sizeof(struct nss_nlcranipc_msg),	/* NSS NETLINK CRANIPC msg */
	.version = NSS_NL_VER,				/* Set it to NSS_NLCRANIPC version */
	.maxattr = NSS_CRANIPC_MSG_TYPE_MAX,		/* maximum commands supported */
	.netnsok = true,
	.pre_doit = NULL,
	.post_doit = NULL,
};

/*
 * multicast group for sending message status & events
 */
static struct genl_multicast_group nss_nlcranipc_mcgrp = {
	.name = NSS_NLCRANIPC_MCAST_GRP,
};

/*
 * operation table called by the generic netlink layer based on the command
 */
static struct genl_ops nss_nlcranipc_ops[] = {
	{.cmd = NSS_CRANIPC_MSG_TYPE_CONFIG, .doit = nss_nlcranipc_ops_config,},	/* config */
};

#define NSS_NLCRANIPC_OPS_SZ ARRAY_SIZE(nss_nlcranipc_ops)

/*
 * nss_nlcranipc_verify_tuple()
 * 	verify and override 4-tuple entries
 */
static int nss_nlcranipc_verify_tuple(struct nss_cranipc_config *tuple)
{
	/*
	 * Check sanity of the tuple.
	 */
	if (!tuple->sip || !tuple->dip || !tuple->sport || !tuple->dport) {
		nss_nl_info("Empty sip or dip or sport or dport for 4-tuple\n");
		return -EINVAL;
	}
	return 0;
}

/*
 * nss_nlcranipc_ops_config()
 * 	config handler
 */
static int nss_nlcranipc_ops_config(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcranipc_msg *nl_msg;
	struct nss_nlcmn *nl_cm;
	struct nss_cranipc_msg *nim;
	uint32_t pid;
	int error;

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcranipc_family, info, NSS_CRANIPC_MSG_TYPE_CONFIG);
	if (!nl_cm) {
		nss_nl_error("unable to extract config data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_msg = container_of(nl_cm, struct nss_nlcranipc_msg, cm);
	nim = &nl_msg->nim;
	pid = nl_cm->pid;

	/*
	 * check 4-tuple
	 */
	error = nss_nlcranipc_verify_tuple(&nim->msg.config);
	if (error < 0) {
		nss_nl_error("invalid 4-tuple information passed\n");
		return error;
	}

	error = nss_cranipc_config(nim);
	return error;
}

/*
 * nss_nlcranipc_init()
 * 	handler init
 */
bool nss_nlcranipc_init(void)
{
	int error;

	nss_nl_info_always("Init NSS netlink CRANIPC handler\n");

	/*
	 * register NETLINK ops with the family
	 */
	error = genl_register_family_with_ops(&nss_nlcranipc_family, nss_nlcranipc_ops, NSS_NLCRANIPC_OPS_SZ);
	if (error != 0) {
		nss_nl_info_always("Error: unable to register CRANIPC family\n");
		return false;
	}

	/*
	 * register NETLINK MCAST group for notifications
	 */
	error = genl_register_mc_group(&nss_nlcranipc_family, &nss_nlcranipc_mcgrp);
	if (error != 0) {
		nss_nl_info_always("Error: unable to register CRANIPC Netlink multicast group\n");
		goto unreg_family;
	}

	return true;

	/*
	 * undo all registeration
	 */
unreg_family:
	genl_unregister_family(&nss_nlcranipc_family);
	return false;
}

/*
 * nss_nlcranipc_exit()
 *	handler exit
 */
bool nss_nlcranipc_exit(void)
{
	int error;

	nss_nl_info_always("Exit NSS netlink CRANIPC handler\n");

	/*
	 * unregister the ops family
	 */
	error = genl_unregister_family(&nss_nlcranipc_family);
	if (error != 0) {
		nss_nl_info_always("Unable to unregister CRANIPC NETLINK family\n");
		return false;
	}

	/*
	 * unregister the multicast family
	 */
	genl_unregister_mc_group(&nss_nlcranipc_family, &nss_nlcranipc_mcgrp);
	return true;
}

