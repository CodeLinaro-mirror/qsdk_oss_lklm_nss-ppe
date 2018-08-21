/*
 **************************************************************************
 * Copyright (c) 2017-2018, The Linux Foundation. All rights reserved.
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

/* nss_ipsecmgr.c
 *	IPSec Manager
 */
#include <linux/version.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/of.h>
#include <linux/ipv6.h>
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
#include <net/route.h>
#include <net/ip6_route.h>
#include <net/esp.h>
#include <net/xfrm.h>
#include <net/icmp.h>

#include <crypto/aead.h>
#include <crypto/skcipher.h>
#include <crypto/internal/hash.h>

#include <nss_api_if.h>
#include <nss_ipsec.h>
#include <nss_cryptoapi.h>
#include <nss_ipsecmgr.h>

#ifdef NSS_IPSECMGR_PPE_SUPPORT
#include <ref/ref_vsi.h>
#endif

#include "nss_ipsecmgr_priv.h"

bool enable_ipsec_inline = false;
module_param(enable_ipsec_inline, bool, S_IRUGO);
MODULE_PARM_DESC(enable_ipsec_inline, "Enable IPsec Inline mode");

struct nss_ipsecmgr_drv *ipsecmgr_drv;

static const struct net_device_ops nss_ipsecmgr_dummy_ndev_ops;

/*
 * file operation structure instance
 */
static const struct file_operations node_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_tunnel_stats_read,
};

/*
 * nss_ipsecmgr_dummy_setup()
 *	Setup function for dummy netdevice.
 */
static void nss_ipsecmgr_dummy_setup(struct net_device *dev)
{
	/*
	 * Since, we want to start with fragmentation post IPsec
	 * transform.
	 */
	dev->mtu = ETH_DATA_LEN;
}

/*
 * nss_ipsecmgr_configure()
 *	Send the configure node message
 */
static void nss_ipsecmgr_configure(struct work_struct *work)
{
	enum nss_ipsec_error_type resp = NSS_IPSEC_ERROR_TYPE_NONE;
	uint32_t data_ifnum = ipsecmgr_drv->data_ifnum;
	struct nss_ipsec_configure_node *cfg_node;
	struct nss_ipsec_msg nim = {0};
	nss_tx_status_t status;
	uint32_t vsi_num = 0;

	/*
	 * By making sure that cryptoapi is registered,
	 * we are confirming that IPsec FW is initialized
	 * and ready to be configured.
	 */
	if (!nss_cryptoapi_is_registered()) {
		schedule_delayed_work(&ipsecmgr_drv->cfg_work, NSS_IPSECMGR_CONFIGURE_NODE_RETRY_TIMEOUT);
		return;
	}

	cfg_node = &nim.msg.node;
	cfg_node->dma_lookaside = true;
	cfg_node->dma_redirect = ipsecmgr_drv->ipsec_inline;

	/*
	 * Send DMA IPsec message to initialize the DMA rings.
	 */
	status = nss_ipsec_tx_msg_sync(ipsecmgr_drv->nss_ctx,
					data_ifnum,
					NSS_IPSEC_MSG_TYPE_CONFIGURE_NODE,
					sizeof(*cfg_node),
					&nim,
					&resp);

	if (status != NSS_TX_SUCCESS) {
		nss_ipsecmgr_trace("%p: Failed to send message to NSS(%u)", ipsecmgr_drv, status);
		schedule_delayed_work(&ipsecmgr_drv->cfg_work, NSS_IPSECMGR_CONFIGURE_NODE_RETRY_TIMEOUT);
		return;
	}

	/*
	 * Program PPE for inline mode; if inline is enabled.
	 * TODO: Need to update with ipsec device's MTU and
	 * keep the max MTU across tunnels as the MTU.
	 */
	if (ipsecmgr_drv->ipsec_inline) {
#ifdef NSS_IPSECMGR_PPE_SUPPORT
		/*
		 * Get port's default VSI.
		 */
		if (ppe_port_vsi_get(0, NSS_PPE_PORT_IPSEC, &vsi_num)) {
			nss_ipsecmgr_warn("%p: Failed to get port VSI", ipsecmgr_drv);
			ipsecmgr_drv->ipsec_inline = false;
			return;
		}

		/*
		 * Configure PPE's inline port
		 */
		if (!nss_ipsec_ppe_port_config(ipsecmgr_drv->nss_ctx, ipsecmgr_drv->dev, data_ifnum, vsi_num)) {
			nss_ipsecmgr_warn("%p: Failed to configure PPE inline mode", ipsecmgr_drv);
			ipsecmgr_drv->ipsec_inline = false;
			return;
		}
#endif
	}

	nss_ipsecmgr_trace("%p: Configure node msg successful", ipsecmgr_drv);
	return;
}

/*
 * nss_ipsecmgr_init()
 *	module init
 */
static int __init nss_ipsecmgr_init(void)
{
	struct nss_ipsecmgr_priv *priv;
	struct net_device *dev;
	uint32_t features = 0;
	int status;

	ipsecmgr_drv = vzalloc(sizeof(*ipsecmgr_drv));
	if (!ipsecmgr_drv) {
		nss_ipsecmgr_warn("Failed to allocate IPsec manager context");
		return -1;
	}

	ipsecmgr_drv->nss_ctx = nss_ipsec_get_context();
	if (!ipsecmgr_drv->nss_ctx) {
		nss_ipsecmgr_warn("%p: Failed to retrieve NSS context", ipsecmgr_drv);
		goto free;
	}

#ifdef NSS_IPSECMGR_PPE_SUPPORT
	ipsecmgr_drv->ipsec_inline = enable_ipsec_inline;
#endif

	dev = alloc_netdev(sizeof(*priv), NSS_IPSECMGR_DEFAULT_TUN_NAME, NET_NAME_UNKNOWN, nss_ipsecmgr_dummy_setup);
	if (!dev) {
		nss_ipsecmgr_warn("%p: Failed to allocate dummy netdevice", ipsecmgr_drv);
		goto free;
	}

	priv = netdev_priv(dev);
	priv->dev = dev;
	INIT_LIST_HEAD(&priv->list);

	dev->netdev_ops = &nss_ipsecmgr_dummy_ndev_ops;

	status = register_netdev(dev);
	if (status) {
		nss_ipsecmgr_info("%p: Failed to register dummy netdevice(%p)", ipsecmgr_drv, dev);
		goto netdev_free;
	}

	ipsecmgr_drv->dev = dev;
	ipsecmgr_drv->data_ifnum = nss_ipsec_get_data_interface();
	ipsecmgr_drv->encap_ifnum = nss_ipsec_get_encap_interface();
	ipsecmgr_drv->decap_ifnum = nss_ipsec_get_decap_interface();

	rwlock_init(&ipsecmgr_drv->lock);
	nss_ipsecmgr_init_sa_db(ipsecmgr_drv->sa_db);
	nss_ipsecmgr_init_flow_db(ipsecmgr_drv->flow_db);
	nss_ipsecmgr_init_tun_db(&ipsecmgr_drv->tun_db);

	nss_ipsec_data_register(ipsecmgr_drv->data_ifnum, nss_ipsecmgr_tunnel_rx, ipsecmgr_drv->dev, features);
	nss_ipsec_notify_register(ipsecmgr_drv->encap_ifnum, nss_ipsecmgr_tunnel_rx_notify, ipsecmgr_drv);
	nss_ipsec_notify_register(ipsecmgr_drv->decap_ifnum, nss_ipsecmgr_tunnel_rx_notify, ipsecmgr_drv);

	INIT_DELAYED_WORK(&ipsecmgr_drv->cfg_work, nss_ipsecmgr_configure);

	/*
	 * Initialize debugfs.
	 */
	ipsecmgr_drv->dentry = debugfs_create_dir("qca-nss-ipsecmgr", NULL);
	if (!ipsecmgr_drv->dentry) {
		nss_ipsecmgr_warn("%p: Failed to create root debugfs entry", ipsecmgr_drv);
		goto unregister_dev;

	}

	/*
	 * Adding node stats debugfs entry.
	 */
	if (!debugfs_create_file("node", S_IRUGO, ipsecmgr_drv->dentry, NULL, &node_stats_op)) {
		nss_ipsecmgr_warn("%p: Failed to create node stats debugfs entry", ipsecmgr_drv);
		goto unregister_dev;
	}

	/*
	 * Configure inline mode and the DMA rings.
	 */
	nss_ipsecmgr_configure(&ipsecmgr_drv->cfg_work.work);

	write_lock(&ipsecmgr_drv->lock);
	list_add(&priv->list, &ipsecmgr_drv->tun_db);

	ipsecmgr_drv->max_mtu = dev->mtu;
	write_unlock(&ipsecmgr_drv->lock);

	nss_ipsecmgr_info("NSS IPsec manager loaded: %s\n", NSS_CLIENT_BUILD_ID);
	return 0;

unregister_dev:
	unregister_netdev(ipsecmgr_drv->dev);

netdev_free:
	free_netdev(ipsecmgr_drv->dev);

free:
	vfree(ipsecmgr_drv);
	ipsecmgr_drv = NULL;

	return -1;
}

/*
 * nss_ipsecmgr_exit()
 *	module exit
 */
static void __exit nss_ipsecmgr_exit(void)
{
	struct nss_ipsecmgr_priv *priv;

	if (!ipsecmgr_drv) {
		nss_ipsecmgr_warn("IPsec manager driver context empty");
		return;
	}

	if (!ipsecmgr_drv->nss_ctx) {
		nss_ipsecmgr_warn("%p: NSS Context empty", ipsecmgr_drv);
		goto free;
	}

	priv = netdev_priv(ipsecmgr_drv->dev);

	write_lock(&ipsecmgr_drv->lock);
	list_del(&priv->list);

	ipsecmgr_drv->max_mtu = U16_MAX;
	write_unlock(&ipsecmgr_drv->lock);

	BUG_ON(!list_empty(&ipsecmgr_drv->tun_db));

	/*
	 * Unregister the callbacks from the HLOS as we are no longer
	 * interested in exception data & async messages
	 */
	nss_ipsec_data_unregister(ipsecmgr_drv->nss_ctx, ipsecmgr_drv->data_ifnum);

	nss_ipsec_notify_unregister(ipsecmgr_drv->nss_ctx, ipsecmgr_drv->encap_ifnum);
	nss_ipsec_notify_unregister(ipsecmgr_drv->nss_ctx, ipsecmgr_drv->decap_ifnum);

	unregister_netdev(ipsecmgr_drv->dev);

	/*
	 * Remove debugfs directory and entries below that.
	 */
	debugfs_remove_recursive(ipsecmgr_drv->dentry);

free:
	/*
	 * Free the ipsecmgr ctx
	 */
	vfree(ipsecmgr_drv);
	ipsecmgr_drv = NULL;

	nss_ipsecmgr_info("NSS IPsec manager unloaded\n");

}

MODULE_LICENSE("Dual BSD/GPL");

module_init(nss_ipsecmgr_init);
module_exit(nss_ipsecmgr_exit);
