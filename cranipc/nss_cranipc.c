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

/* nss_cranipc.c
 *	NSS to DANIPC
 */
#include <linux/version.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/of.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/bitops.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <linux/etherdevice.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/atomic.h>
#include <net/protocol.h>
#include <net/icmp.h>

#include <nss_def.h>
#include <nss_cmn.h>

#include <nss_cranipc.h>

#include <nss_api_if.h>
#include "nss_cranipc_def.h"

/*
 **********************
 * Helper Functions
 **********************
 */

/*
 * nss_cranipc_update_stats()
 *	Update cranipc interface stats
 */
static void nss_cranipc_update_stats(struct nss_cranipc_priv *priv, struct nss_cranipc_msg *nim)
{
	struct rtnl_link_stats64 *stats;
	struct nss_cmn_node_stats *pkts;
	struct nss_cranipc_node_sync *sync_stats;

	stats = &priv->stats;
	pkts = &nim->msg.stats.node_stats;
	sync_stats = &nim->msg.stats;

	stats->tx_bytes += pkts->tx_bytes;
	stats->tx_packets += pkts->tx_packets;

	stats->rx_bytes += pkts->rx_bytes;
	stats->rx_packets += pkts->rx_packets;

	/* Updating stats for all the drops */
	stats->rx_dropped += pkts->rx_dropped;
	stats->rx_dropped += sync_stats->dl_queue_dropped;
	stats->rx_dropped += sync_stats->dl_dropped_not_ready;
}

static struct net_device_stats *nss_cranipc_ndev_stats(struct net_device *ndev)
{
	return &ndev->stats;
}

static void nss_cranipc_if_up(struct net_device *netdev)
{
	struct nss_cranipc_priv *priv = netdev_priv(netdev);

	if (priv->is_first_init_done)
		return;

	netdev->flags |= IFF_UP;
	netdev->flags |= IFF_RUNNING;
	priv->is_first_init_done = 1;
}

/*
 * nss_cran_event_cb()
 *	Handle CRAN event from the NSS driver
 */
static void nss_cran_event_cb(void *if_ctx, struct nss_cranipc_msg *msg)
{
	struct nss_cmn_msg *ncm = &msg->cm;
	struct nss_ctx_instance *nss_ctx;
	struct nss_cranipc_priv *priv;
	struct net_device *dev;

	nss_ctx = nss_cranipc_get_context();
	BUG_ON(!nss_ctx);

	/*
	 * Get the net device from the interface number
	 */
	dev = nss_cmn_get_interface_dev(nss_ctx, ncm->interface);
	if (!dev) {
		nss_cran_warn("event received on deallocated I/F (%d)\n", ncm->interface);
		return;
	}

	priv = netdev_priv(dev);
	switch (ncm->type) {
	case NSS_CRANIPC_MSG_TYPE_STATS_SYNC:
		/*
		 * Update cranipc interface statistics.
		 */
		nss_cranipc_update_stats(priv, msg);
		break;
	case NSS_CRANIPC_MSG_TYPE_CONFIG:
		/*
		 * Bring up the netdev interface
		 */
		if (ncm->response == NSS_CMN_RESPONSE_ACK)
			nss_cranipc_if_up(dev);
		break;
	default:
		nss_cran_warn("Invalid msg type (%d)\n", ncm->type);
		break;
	}
}

/*
 * nss_cran_data_cb()
 *	Handle CRAN data from the NSS driver.
 *	We essentially drop the packet and update the stats
 */
static void nss_cran_data_cb(struct net_device *dev, struct sk_buff *skb, struct napi_struct *napi)
{
	struct nss_cranipc_priv *priv = netdev_priv(dev);
	struct rtnl_link_stats64 *stats = &priv->stats;

	dev_kfree_skb_any(skb);
	stats->tx_dropped++;
}

/*
 * nss_cranipc_config()
 *	Send config message to NSS Firmware
 */
int nss_cranipc_config(struct nss_cranipc_msg *msg)
{
	nss_tx_status_t nss_tx_status;
	struct nss_ctx_instance *nss_ctx;

	if (!nss_cranipc_is_cran_enabled())
		return -1;

	nss_ctx = nss_cranipc_get_context();

	nss_cranipc_msg_init(msg, NSS_CRANIPC_INTERFACE, NSS_CRANIPC_MSG_TYPE_CONFIG, NSS_CRANIPC_MSG_LEN, nss_cran_event_cb, NULL);

	nss_tx_status = nss_cranipc_tx(nss_ctx, msg);
	if (nss_tx_status != NSS_TX_SUCCESS) {
		nss_cran_warn("%p: Unable to send config message to NSS FW\n", nss_ctx);
		return -1;
	}

	return 0;
}
EXPORT_SYMBOL( nss_cranipc_config);

/*
 * nss_cranipc_open()
 *	function to be called when ifconfig up is invoked
 */
static int nss_cranipc_open(struct net_device *dev)
{
	netif_start_queue(dev);
	return 0;
}

/*
 * NSS CRANIPC operation struct
 */
static const struct net_device_ops nss_cranipc_client_ops = {
	.ndo_open = nss_cranipc_open,
	.ndo_start_xmit = NULL,
	.ndo_get_stats =nss_cranipc_ndev_stats,
	.ndo_change_mtu = NULL,
};

/*
 * nss_cranipc_ndev_setup()
 *	net device setup function for alloc
 */
static void nss_cranipc_ndev_setup(struct net_device *dev)
{
	dev->addr_len = ETH_ALEN;
	dev->mtu = NSS_CRANIPC_MTU;
}

/*
 * nss_cranipc_register_netdev()
 *	function to register netdevice
 */
struct net_device * nss_cranipc_register_netdev()
{
	struct net_device *ndev;
	uint32_t err = 0;

#if (LINUX_VERSION_CODE <= KERNEL_VERSION(3, 16, 0))
	ndev = alloc_netdev(sizeof(struct nss_cranipc_priv), "danipc-cran", nss_cranipc_ndev_setup);
#else
	ndev = alloc_netdev(sizeof(struct nss_cranipc_priv), "danipc-cran", NET_NAME_ENUM, nss_cranipc_ndev_setup);
#endif
	if (!ndev) {
		nss_cran_warn("Unable to create danipc-cran net_device \n");
		return NULL;
	}

	ndev->netdev_ops = &nss_cranipc_client_ops;

	err = register_netdev(ndev);
	if (err) {
		nss_cran_err("%d: Could not register danipc-cran net_device \n", err);
		free_netdev(ndev);
		return NULL;
	}

	return ndev;
}

/*
 * nss_cranipc_init()
 *	module init
 */
static int __init nss_cranipc_init(void)
{
	struct nss_cranipc_priv *priv;
	struct net_device *netdev;

	/*
	 * If cran is not enabled, we will still load the module
	 * with no functionality, as netlink driver depends on the
	 * cranipc module for the symbols
	 */
	if (!nss_cranipc_is_cran_enabled())
		goto ret;

	/* Register danipc-cran netdevice */
	netdev = nss_cranipc_register_netdev();

	if (!netdev) {
		nss_cran_warn("Failed to create CRAN netdev interface \n");
		return -1;
	}

	/* Register C-RAN IPC interface with the NSS Driver */
	priv = netdev_priv(netdev);

	priv->nss_ctx = nss_register_cranipc_if(NSS_CRANIPC_INTERFACE, nss_cran_data_cb, nss_cran_event_cb, netdev);
	if (!priv->nss_ctx) {
		/* If we are unable to get context, we unregister our netdev */
		unregister_netdev(netdev);
		nss_cran_info("Failed to register CRAN interface with NSS \n");
		return -1;
	}

	priv->is_first_init_done = 0;
	nss_cran_info("Cranipc client initialized\n");

ret:
	return 0;
}

/*
 * nss_danipc_exit()
 * 	module exit
 */
static void __exit nss_cranipc_exit(void)
{
	struct nss_ctx_instance *nss_ctx = nss_cranipc_get_context();
	struct net_device *ndev = nss_cmn_get_interface_dev(nss_ctx, NSS_CRANIPC_INTERFACE);

	unregister_netdev(ndev);
	nss_unregister_cranipc_if(NSS_CRANIPC_INTERFACE);
	free_netdev(ndev);
	nss_cran_info("NSS CRANIPC client unloaded\n");
}

module_init(nss_cranipc_init);
module_exit(nss_cranipc_exit);

MODULE_LICENSE("Dual BSD/GPL");
