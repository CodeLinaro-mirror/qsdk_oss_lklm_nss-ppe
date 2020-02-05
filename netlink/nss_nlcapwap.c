/*
 **************************************************************************
 * Copyright (c) 2015-2016,2018-2020 The Linux Foundation. All rights reserved.
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

#include <linux/if.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/netlink.h>
#include <linux/types.h>
#include <linux/version.h>

#include <net/genetlink.h>
#include <net/sock.h>

#include <nss_api_if.h>
#include <nss_capwap.h>
#include <nss_capwapmgr.h>
#include <nss_capwap_user.h>
#include <nss_cmn.h>
#include <nss_dtlsmgr.h>
#include <nss_dtls_cmn.h>
#include <nss_nlcmn_if.h>
#include <nss_nl_if.h>
#include "nss_crypto_defines.h"
#include "nss_nl.h"
#include "nss_nlcapwap.h"
#include "nss_nlcapwap_if.h"
#include "nss_nlcmn_if.h"

/*
 * Lock variable for synchronization
 */
static DEFINE_SPINLOCK(lock);

/*
 * Bitmap to keep track of tunnel state
 */
static DECLARE_BITMAP(tun_data, NSS_CAPWAPMGR_MAX_TUNNELS);

/*
 * Global capwap context variable
 */
static struct nss_nlcapwap_global_ctx capwap_gbl_ctx;

/*
 * Atomic variable to keep track of peformance
 */
static atomic_t enable_perf = ATOMIC_INIT(0);

/*
 * nss_nlcapwap_family_mcgrp
 *	Multicast group for sending message status & events
 */
static const struct genl_multicast_group nss_nlcapwap_family_mcgrp[] = {
	{.name = NSS_NLCAPWAP_MCAST_GRP},
};

/*
 * nss_nlcapwap_family
 *	Capwap family definition
 */
struct genl_family nss_nlcapwap_family = {
	.id = GENL_ID_GENERATE,				/* Auto generate ID */
	.name = NSS_NLCAPWAP_FAMILY,			/* family name string */
	.hdrsize = sizeof(struct nss_nlcapwap_rule),	/* NSS NETLINK capwap rule */
	.version = NSS_NL_VER,				/* Set it to NSS_NL_VER version */
	.maxattr = NSS_NLCAPWAP_CMD_TYPE_MAX,		/* maximum commands supported */
	.netnsok = true,
	.pre_doit = NULL,
	.post_doit = NULL,
};

/*
 * nss_nlcapwap_update_tun_status()
 *	Sets tun status to true or false based on parameter passed
 *	0: The tunnel id is unused
 *	1: The tunnel id is already taken and is in use
 */
static inline bool nss_nlcapwap_update_tun_status(int index, int status)
{
	if ((index < 0) || (index >= NSS_CAPWAPMGR_MAX_TUNNELS)) {
		nss_nl_error("index value out of bound: %d\n", index);
		return false;
	}

	/*
	 * Test if tunnel is not set to status already
	 */
	spin_lock(&lock);
	if (test_bit(index, tun_data) == status) {
		spin_unlock(&lock);
		nss_nl_error("Tunnel %d status is already set to: %d\n", index, status);
		return false;
	}

	spin_unlock(&lock);
	change_bit(index, tun_data);
	return true;
}

/*
 * nss_nlcapwap_get_meta_header()
 *	Returns meta header
 */
static inline struct nss_nlcapwap_meta_header nss_nlcapwap_get_meta_header(void)
{
	struct nss_nlcapwap_meta_header meta_hdr;
	spin_lock(&lock);
	meta_hdr = capwap_gbl_ctx.meta_header;
	spin_unlock(&lock);
	return meta_hdr;
}

/*
 * nss_nlcapwap_set_meta_header()
 *	Sets the value of meta header
 */
static inline void nss_nlcapwap_set_meta_header(struct nss_nlcapwap_meta_header meta_hdr)
{
	spin_lock(&lock);
	capwap_gbl_ctx.meta_header = meta_hdr;
	spin_unlock(&lock);
}

/*
 * nss_nlcapwap_rx_handler()
 *	Capwap netdev rx handler
 */
static rx_handler_result_t nss_nlcapwapmgr_rx_handler(struct sk_buff **pskb)
{
	struct nss_capwap_metaheader *pre;
	uint32_t pattern, no_pattern, i;
	struct sk_buff *skb = *pskb;
	uint8_t *data;

	if (atomic_read(&enable_perf)) {
		goto done;
	}

	data = skb->data;
	pattern = no_pattern = 0;
	for (i = 0; i < skb->len; i++) {
		(data[i] == NSS_NLCAPWAP_DATA) ? pattern++ : no_pattern++;
	}

done:
	pre = (struct nss_capwap_metaheader *)skb->data;
	nss_nl_info("tunnel %d: NETDEV RX: len:%d (%d,%d)\n", pre->tunnel_id,
	      skb->len, pattern, no_pattern);
	dev_kfree_skb_any(skb);
	return RX_HANDLER_CONSUMED;
}

/*
 * nss_nlcapwap_register_netdev_handler()
 *	Handler to register capwap netdev rx handler
 */
static void nss_nlcapwap_register_netdev_handler(void)
{
	struct net_device *capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * Register a netdevice rx handler
	 */
	rtnl_lock();
	if (netdev_rx_handler_register(capwap_ndev, nss_nlcapwapmgr_rx_handler, NULL)) {
		nss_nl_error("Couldn't register CAPWAP RX handler\n");
	}

	rtnl_unlock();
}

/*
 * nss_nlcapwap_data_cb()
 *	Data callback for capwap
 */
static void nss_nlcapwap_data_cb(void *app_data, struct sk_buff *skb)
{
	nss_nl_error("%p: RX DTLS exception packet len:%d\n", skb, skb->len);
}

/*
 * nss_nlcapwap_dtls_configure()
 *	Common handler for v4 and v6 capwap-dtls configuration
 */
static void nss_nlcapwap_dtls_configure(struct nss_dtlsmgr_config *dcfg,
		struct nss_nlcapwap_rule *nl_rule)
{
	uint8_t algo = nl_rule->msg.dtls.encap.crypto.algo;

	dcfg->flags |= NSS_DTLSMGR_HDR_CAPWAP;
	if (algo == NSS_DTLSMGR_ALGO_AES_GCM) {
		dcfg->flags |= NSS_DTLSMGR_CIPHER_MODE_GCM;
	}

	/*
	 * Encap configuration
	 */
	dcfg->app_data = NULL;
	dcfg->notify = NULL;
	dcfg->data = nss_nlcapwap_data_cb;
	memcpy((void *)&dcfg->encap, (void *)&nl_rule->msg.dtls.encap, sizeof(struct nss_dtlsmgr_encap_config));
	/*
	 * Decap configuration
	 */
	memcpy((void *)&dcfg->decap, (void *)&nl_rule->msg.dtls.decap, sizeof(struct nss_dtlsmgr_decap_config));
}

/*
 * nss_nlcapwap_create_tun_ipv4_config()
 *	Configures the common rule for rx and tx.
 */
static void nss_nlcapwap_create_tun_ipv4_config(struct nss_nlcapwap_rule *nl_rule,
		struct nss_capwap_rule_msg *capwap_rule, struct nss_dtlsmgr_config *dtls_ipv4,
		struct nss_ipv4_create *ipv4)
{
	struct net_device *wan_ndev;
	uint32_t features = 0;
	uint32_t if_num;

	/*
	 * Initialize the msgs
	 */
	memset(ipv4, 0, sizeof(*ipv4));
	memset(capwap_rule, 0, sizeof(*capwap_rule));

	/*
	 * Configure CAPWAP rule
	 */
	capwap_rule->encap.path_mtu = htonl(nl_rule->msg.create.rule.encap.path_mtu);
	capwap_rule->decap.reassembly_timeout = htonl(nl_rule->msg.create.rule.decap.reassembly_timeout);
	capwap_rule->decap.max_fragments = htonl(nl_rule->msg.create.rule.decap.max_fragments);
	capwap_rule->decap.max_buffer_size = htonl(nl_rule->msg.create.rule.decap.max_buffer_size);
	capwap_rule->stats_timer = htonl(nl_rule->msg.create.rule.stats_timer);
	capwap_rule->l3_proto = NSS_CAPWAP_TUNNEL_IPV4;
	if (nl_rule->msg.create.rule.which_udp == IPPROTO_UDP) {
		capwap_rule->which_udp = NSS_CAPWAP_TUNNEL_UDP;
		ipv4->protocol = IPPROTO_UDP;
	} else {
		capwap_rule->which_udp = NSS_CAPWAP_TUNNEL_UDPLite;
		ipv4->protocol = IPPROTO_UDPLITE;
	}

	if (nl_rule->msg.create.inner_trustsec_en) {
		nss_nl_info("Enabling INNER TRUSTSEC in rule\n");
		features |= NSS_CAPWAPMGR_FEATURE_INNER_TRUSTSEC_ENABLED;
	}

	if (nl_rule->msg.create.outer_trustsec_en) {
		nss_nl_info("Enabling OUTER TRUSTSEC in rule\n");
		features |= NSS_CAPWAPMGR_FEATURE_OUTER_TRUSTSEC_ENABLED;
	}

	if (nl_rule->msg.create.wireless_qos_en) {
		nss_nl_info("Enabling WIRELESS QOS\n");
		features |= NSS_CAPWAPMGR_FEATURE_WIRELESS_QOS_ENABLED;
	}

	capwap_rule->enabled_features = features;

	/*
	 * Configure IPv4 rule
	 */
	wan_ndev = dev_get_by_name(&init_net, &nl_rule->msg.create.gmac_ifname[0]);
	if (!wan_ndev) {
		nss_nl_info("Can't find %s netdev\n", nl_rule->msg.create.gmac_ifname);
		return;
	}

	if_num = nss_cmn_get_interface_number_by_dev(wan_ndev);
	nss_nl_info("CAPWAP on %s, if_num is %d\n", nl_rule->msg.create.gmac_ifname, if_num);
	ipv4->src_interface_num = if_num;
	ipv4->dest_interface_num = NSS_NLCAPWAP_WAN_IFNUM;
	ipv4->from_mtu = wan_ndev->mtu;
	ipv4->to_mtu = wan_ndev->mtu;

	ipv4->src_ip = nl_rule->msg.create.rule.encap.src_ip.ip.ipv4;
	ipv4->src_port = nl_rule->msg.create.rule.encap.src_port;
	ipv4->src_ip_xlate = nl_rule->msg.create.rule.encap.src_ip.ip.ipv4;
	ipv4->src_port_xlate = nl_rule->msg.create.rule.encap.src_port;

	ipv4->dest_ip = nl_rule->msg.create.rule.encap.dest_ip.ip.ipv4;
	ipv4->dest_port = nl_rule->msg.create.rule.encap.dest_port;
	ipv4->dest_ip_xlate = nl_rule->msg.create.rule.encap.dest_ip.ip.ipv4;
	ipv4->dest_port_xlate = nl_rule->msg.create.rule.encap.dest_port;

	memcpy(ipv4->src_mac, nl_rule->msg.create.gmac_ifmac, sizeof(ipv4->src_mac));

	ipv4->in_vlan_tag[0] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv4->in_vlan_tag[1] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv4->out_vlan_tag[0] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv4->out_vlan_tag[1] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	dev_put(wan_ndev);
}

/*
 * nss_nlcapwap_create_tun_ipv6_config()
 *	Configures the common rule for rx and tx.
 */
static void nss_nlcapwap_create_tun_ipv6_config(struct nss_nlcapwap_rule *nl_rule,
		struct nss_capwap_rule_msg *capwap_rule, struct nss_dtlsmgr_config *dtls_ipv6,
		struct nss_ipv6_create *ipv6)
{
	struct net_device *wan_ndev;
	uint32_t features = 0;
	uint32_t if_num;

	/*
	 * Initialize the msgs
	 */
	memset(ipv6, 0, sizeof (struct nss_ipv6_create));
	memset(capwap_rule, 0, sizeof(struct nss_capwap_rule_msg));

	/*
	 * Configuring capwap rule
	 */
	capwap_rule->encap.path_mtu = htonl(nl_rule->msg.create.rule.encap.path_mtu);
	capwap_rule->decap.reassembly_timeout = htonl(nl_rule->msg.create.rule.decap.reassembly_timeout);
	capwap_rule->decap.max_fragments = htonl(nl_rule->msg.create.rule.decap.max_fragments);
	capwap_rule->decap.max_buffer_size = htonl(nl_rule->msg.create.rule.decap.max_buffer_size);
	capwap_rule->stats_timer = htonl(nl_rule->msg.create.rule.stats_timer);
	capwap_rule->l3_proto = NSS_CAPWAP_TUNNEL_IPV6;

	if (nl_rule->msg.create.rule.which_udp == IPPROTO_UDP) {
		capwap_rule->which_udp = NSS_CAPWAP_TUNNEL_UDP;
		ipv6->protocol = IPPROTO_UDP;
	} else {
		capwap_rule->which_udp = NSS_CAPWAP_TUNNEL_UDPLite;
		ipv6->protocol = IPPROTO_UDPLITE;
	}

	if (nl_rule->msg.create.inner_trustsec_en) {
		nss_nl_info("Enabling INNER TRUSTSEC in rule\n");
		features |= NSS_CAPWAPMGR_FEATURE_INNER_TRUSTSEC_ENABLED;
	}

	if (nl_rule->msg.create.outer_trustsec_en) {
		nss_nl_info("Enabling OUTER TRUSTSEC in rule\n");
		features |= NSS_CAPWAPMGR_FEATURE_OUTER_TRUSTSEC_ENABLED;
	}

	if (nl_rule->msg.create.wireless_qos_en) {
		nss_nl_info("Enabling WIRELESS QOS\n");
		features |= NSS_CAPWAPMGR_FEATURE_WIRELESS_QOS_ENABLED;
	}

	capwap_rule->enabled_features = features;
	capwap_rule->outer_sgt_value = nl_rule->msg.create.rule.outer_sgt_value;

	/*
	 * Configure IPv6 rule
	 */
	wan_ndev = dev_get_by_name(&init_net, &nl_rule->msg.create.gmac_ifname[0]);
	if (!wan_ndev) {
		nss_nl_info("Can't find %s netdev\n", nl_rule->msg.create.gmac_ifname);
		return;
	}

	if_num = nss_cmn_get_interface_number_by_dev(wan_ndev);
	nss_nl_info("CAPWAP on %s, if_num is %d\n", nl_rule->msg.create.gmac_ifname, if_num);
	ipv6->src_interface_num = if_num;
	ipv6->dest_interface_num = NSS_NLCAPWAP_WAN_IFNUM;
	ipv6->from_mtu = wan_ndev->mtu;
	ipv6->to_mtu = wan_ndev->mtu;
	memcpy(ipv6->src_ip, nl_rule->msg.create.rule.encap.src_ip.ip.ipv6, sizeof(ipv6->src_ip));
	memcpy(ipv6->dest_ip, nl_rule->msg.create.rule.encap.dest_ip.ip.ipv6, sizeof(ipv6->dest_ip));
	memcpy(&ipv6->src_mac[0], &nl_rule->msg.create.gmac_ifmac[0], sizeof(ipv6->src_mac));
	ipv6->src_port = nl_rule->msg.create.rule.encap.src_port;
	ipv6->dest_port = nl_rule->msg.create.rule.encap.dest_port;
	ipv6->in_vlan_tag[0] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv6->in_vlan_tag[1] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv6->out_vlan_tag[0] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	ipv6->out_vlan_tag[1] = NSS_NLCAPWAP_VLAN_TAG_INVALID;
	dev_put(wan_ndev);
}

/*
 * nss_nlcapwap_ops_create_tun()
 *	Handler for creating tunnel
 */
static int nss_nlcapwap_ops_create_tun(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_capwap_rule_msg capwap_rule;
	struct nss_dtlsmgr_config dtls_ipv4;
	struct nss_dtlsmgr_config dtls_ipv6;
	struct nss_nlcapwap_rule *nl_rule;
	struct nss_ipv4_create ipv4_rule;
	struct nss_ipv6_create ipv6_rule;
	struct net_device *capwap_ndev;
	struct nss_nlcmn *nl_cm;
	int tun_id;
	int ret = 0;

	/*
	 * Get the tunnel_id. We only create a new tunnel if max limit is not exceeded
	 */
	spin_lock(&lock);
	tun_id = find_first_zero_bit(tun_data, NSS_CAPWAPMGR_MAX_TUNNELS);
	spin_unlock(&lock);
	if (tun_id >= NSS_CAPWAPMGR_MAX_TUNNELS) {
		nss_nl_error("Max number of tunnels limit exceeded\n");
		return -EPERM;
	}

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * Extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_CREATE_TUN);
	if (!nl_cm) {
		nss_nl_error("Unable to extract create tunnel data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Needed for 802.3 to 802.11 conversion
	 */
	memcpy(capwap_rule.bssid, nl_rule->msg.create.rule.bssid, sizeof(capwap_rule.bssid));
	capwap_rule.outer_sgt_value = nl_rule->msg.create.rule.outer_sgt_value;

	spin_lock(&lock);
	if (nl_rule->msg.create.wireless_qos_en) {
		capwap_gbl_ctx.nss_nlcapwap_80211e = NSS_NLCAPWAP_80211E_ONE;
	} else {
		capwap_gbl_ctx.nss_nlcapwap_80211e = NSS_NLCAPWAP_80211E_ZERO;
	}

	spin_unlock(&lock);

	/*
	 * Create tunnel based on ip version
	 */
	if (nl_rule->msg.create.rule.l3_proto == NSS_NLCAPWAP_IP_VERS_4) {
		nss_nlcapwap_create_tun_ipv4_config(nl_rule, &capwap_rule, &dtls_ipv4, &ipv4_rule);
		/*
		 * Create CAPWAP IPv4 tunnel
		 */
		ret = nss_capwapmgr_ipv4_tunnel_create(capwap_ndev, tun_id, &ipv4_rule,
				&capwap_rule, &dtls_ipv4);
		if (ret != 0) {
			nss_nl_error("Unable to create tunnel: %d\n", tun_id);
			return -EAGAIN;
		}

		nss_nl_info("Created IPv4 tunnel: src:%pI4h(%d) dst:%pI4h(%d) p:%d\n\n",
		      &ipv4_rule.src_ip, ipv4_rule.src_port, &ipv4_rule.dest_ip,
		      ipv4_rule.dest_port, ipv4_rule.protocol);
	} else {
		nss_nlcapwap_create_tun_ipv6_config(nl_rule, &capwap_rule, &dtls_ipv6, &ipv6_rule);
		/*
		 * Create CAPWAP IPv6 tunnel
		 */
		ret = nss_capwapmgr_ipv6_tunnel_create(capwap_ndev, tun_id, &ipv6_rule,
				&capwap_rule, &dtls_ipv6);
		if (ret != 0) {
			nss_nl_error("Unable to create tunnel: %d\n", tun_id);
			return -EAGAIN;
		}

		nss_nl_info("Created IPv6 tunnel + proto:%d\n", ipv6_rule.protocol);
		nss_nl_info("src:%pI6(%d)\n", ipv6_rule.src_ip, ipv6_rule.src_port);
		nss_nl_info("dst:%pI6(%d)\n\n", ipv6_rule.dest_ip, ipv6_rule.dest_port);
	}

	nss_capwapmgr_change_version(capwap_ndev, tun_id, NSS_CAPWAP_VERSION_V2);
	nss_capwapmgr_enable_tunnel(capwap_ndev, tun_id);

	/*
	 * Increment the tunnel_id
	 */
	if (!nss_nlcapwap_update_tun_status(tun_id, 1)) {
		nss_nl_error("Unable to set the tunnel status: %d\n", tun_id);
		return -EAGAIN;
	}

	nss_nl_info("Successfully created tunnel %d\n", tun_id + 1);
	return 0;
}

/*
 * nss_nlcapwap_destroy_tun()
 *	Destroys tunnel based on tun_id
 */
static bool nss_nlcapwap_destroy_tun(int tun_id)
{
	struct net_device *capwap_ndev;
	nss_capwapmgr_status_t status;

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * Disable the tunnel and then destroy
	 */
	spin_lock(&lock);
	if (!test_bit(tun_id, tun_data)) {
		nss_nl_error("Tunnel is already disabled: %d\n", tun_id);
		spin_unlock(&lock);
		return false;
	}

	spin_unlock(&lock);
	status = nss_capwapmgr_disable_tunnel(capwap_ndev, tun_id);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Unable to disable the tunnel: %d\n", tun_id);
		return false;
	}

	status = nss_capwapmgr_tunnel_destroy(capwap_ndev, tun_id);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Unable to destroy the tunnel: %d\n", tun_id);
		nss_capwapmgr_enable_tunnel(capwap_ndev, tun_id);
		return false;
	}

	/*
	 * Update the tunnel status in global array
	 */
	if (!nss_nlcapwap_update_tun_status(tun_id, 0)) {
		nss_nl_error("Unable to set the tunnel status to 0: %d\n", tun_id);
		return false;
	}

	return true;
}

/*
 * nss_nlcapwap_ops_destroy_tun()
 *	Handler to destroy tunnel
 */
static int nss_nlcapwap_ops_destroy_tun(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcapwap_rule *nl_rule;
	struct nss_nlcmn *nl_cm;
	int tun_id, ret;

	/*
	 * Extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_DESTROY_TUN);
	if (!nl_cm) {
		nss_nl_error("Unable to extract destroy tunnel data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Get the tunnel id
	 */
	tun_id = nl_rule->msg.destroy.tun_id;
	if (tun_id < 0 || (tun_id >= NSS_CAPWAPMGR_MAX_TUNNELS)) {
		nss_nl_error("Not a valid tunnel_id: %d\n", tun_id);
		return -EINVAL;
	}

	ret = nss_nlcapwap_destroy_tun(tun_id);
	if (!ret) {
		nss_nl_error("%p: Unable to destroy tunnel: %d\n", skb, tun_id);
		return -EINVAL;
	}

	nss_nl_info("Successfully destroyed %d tunnel\n", nl_rule->msg.destroy.tun_id);
	return 0;
}

/*
 * nss_nlcapwap_ops_update_mtu()
 *	Handler for updating mtu
 */
static int nss_nlcapwap_ops_update_mtu(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcapwap_rule *nl_rule;
	struct net_device *capwap_ndev;
	nss_capwapmgr_status_t status;
	struct nss_nlcmn *nl_cm;
	int tun_id;

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_UPDATE_MTU);
	if (!nl_cm) {
		nss_nl_error("Unable to extract update mtu data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Update the path_mtu of the corresponding tunnel
	 */
	tun_id = nl_rule->msg.update_mtu.tun_id;
	if (tun_id < 0 || (tun_id >= NSS_CAPWAPMGR_MAX_TUNNELS)) {
		nss_nl_error("Not a valid tunnel_id: %d\n", tun_id);
		return -EINVAL;
	}

	status = nss_capwapmgr_update_path_mtu(capwap_ndev, tun_id,
			nl_rule->msg.update_mtu.mtu.path_mtu);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Unable to update the mtu of the %d tunnel: %d\n", tun_id,
				nl_rule->msg.update_mtu.mtu.path_mtu);
		return -EAGAIN;
	}

	nss_nl_info("Successfully updated the mtu of the %d tunnel.\n", tun_id);
	return 0;
}

/*
 * nss_nlcapwap_ops_dtls()
 *	Handler for dtls enable or disable command
 */
static int nss_nlcapwap_ops_dtls(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_dtlsmgr_config dtls_config;
	struct nss_nlcapwap_rule *nl_rule;
	struct net_device *capwap_ndev;
	nss_capwapmgr_status_t status;
	struct nss_nlcmn *nl_cm;

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_DTLS);
	if (!nl_cm) {
		nss_nl_error("Unable to extract dtls data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Disabling dtls for capwap
	 */
	if (!nl_rule->msg.dtls.enable_dtls) {
		status = nss_capwapmgr_disable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
		if (status != NSS_CAPWAPMGR_SUCCESS) {
			nss_nl_error("Not able to disable tunnel %d\n",
					nl_rule->msg.dtls.tun_id);
			return -EAGAIN;
		}

		status = nss_capwapmgr_configure_dtls(capwap_ndev, nl_rule->msg.dtls.tun_id, 0, NULL);
		if (status != NSS_CAPWAPMGR_SUCCESS) {
			nss_nl_error("Not able to disable dtls for tunnel %d\n",
					nl_rule->msg.dtls.tun_id);
			nss_capwapmgr_enable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
			return -EAGAIN;
		}

		status = nss_capwapmgr_enable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
		if (status != NSS_CAPWAPMGR_SUCCESS) {
			nss_nl_error("Not able to enable tunnel %d\n",
					nl_rule->msg.dtls.tun_id);
			return -EAGAIN;
		}

		nss_nl_info("Succesfully disabled dtls for capwap tunnel %d\n",
				nl_rule->msg.dtls.tun_id);
		return 0;
	}

	nss_nl_info("Enabling DTLS for tunnel %d\n", nl_rule->msg.dtls.tun_id);

	/*
	 * Initializing the dtls message
	 */
	dtls_config.flags = nl_rule->msg.dtls.flags;

	/*
	 * Fill rest of DTLS data
	 */
	nss_nlcapwap_dtls_configure(&dtls_config, nl_rule);

	/*
	 * Enabling dtls for capwap tunnel
	 */
	status = nss_capwapmgr_disable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Not able to disable tunnel %d\n",
				nl_rule->msg.dtls.tun_id);
		return -EAGAIN;
	}

	status = nss_capwapmgr_configure_dtls(capwap_ndev, nl_rule->msg.dtls.tun_id,
			1, &dtls_config);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Not able to enable dtls for tunnel %d\n",
				nl_rule->msg.dtls.tun_id);
		nss_capwapmgr_enable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
		return -EAGAIN;
	}

	status = nss_capwapmgr_enable_tunnel(capwap_ndev, nl_rule->msg.dtls.tun_id);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Not able to enable tunnel %d\n",
				nl_rule->msg.dtls.tun_id);
		return -EAGAIN;
	}

	nss_nl_info("Successfully enabled dtls for the capwap tunnel: %d\n",
			nl_rule->msg.dtls.tun_id);
	return 0;
}

/*
 * nss_nlcapwap_ops_perf()
 *	Handler for enabling or disabling perf
 */
static int nss_nlcapwap_ops_perf(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcapwap_rule *nl_rule;
	struct nss_nlcmn *nl_cm;

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_PERF);
	if (!nl_cm) {
		nss_nl_error("Unable to extract perf parameter.\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	if (nl_rule->msg.perf.perf_en) {
		atomic_set(&enable_perf, 1);
		nss_nl_info("Successfully enabled performance.\n");
		return 0;
	}

	atomic_set(&enable_perf, 0);
	nss_nl_info("Successfully disabled performance.\n");

	return 0;
}

/*
 * nss_nlcapwap_ops_ip_flow()
 *	Handler for adding or deleting ip flow rule
 */
static int nss_nlcapwap_ops_ip_flow(struct sk_buff *skb, struct genl_info *info)
{
	nss_capwapmgr_status_t status;
	struct nss_nlcapwap_rule *nl_rule;
	struct net_device *capwap_ndev;
	struct nss_nlcmn *nl_cm;

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_IP_FLOW);
	if (!nl_cm) {
		nss_nl_error("Unable to extract ip flow rule.\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Add flow rule
	 */
	if (nl_rule->msg.ip_flow.ip_flow_mode == NSS_NLCAPWAP_IP_FLOW_MODE_ADD) {
		status = nss_capwapmgr_add_flow_rule(capwap_ndev, nl_rule->msg.ip_flow.tun_id,
				nl_rule->msg.ip_flow.flow.ip_version, nl_rule->msg.ip_flow.flow.protocol,
				nl_rule->msg.ip_flow.flow.src_ip, nl_rule->msg.ip_flow.flow.dst_ip,
				nl_rule->msg.ip_flow.flow.src_port, nl_rule->msg.ip_flow.flow.dst_port,
				nl_rule->msg.ip_flow.flow.flow_id);
		if (status != NSS_CAPWAPMGR_SUCCESS) {
			nss_nl_error("Unable to add flow rule\n");
			return -EAGAIN;
		}

		nss_nl_info("Succesfully added flow rule for tunnel %d\n",
				nl_rule->msg.ip_flow.tun_id);
		return 0;
	}

	/*
	 * Delete existing flow rule
	 */
	status = nss_capwapmgr_del_flow_rule(capwap_ndev, nl_rule->msg.ip_flow.tun_id,
			nl_rule->msg.ip_flow.flow.ip_version, nl_rule->msg.ip_flow.flow.protocol,
			nl_rule->msg.ip_flow.flow.src_ip, nl_rule->msg.ip_flow.flow.dst_ip,
			nl_rule->msg.ip_flow.flow.src_port, nl_rule->msg.ip_flow.flow.dst_port);
	if (status != NSS_CAPWAPMGR_SUCCESS) {
		nss_nl_error("Unable to del flow rule\n");
		return -EAGAIN;
	}

	nss_nl_info("Succesfully deleted flow rule for tunnel %d\n", nl_rule->msg.ip_flow.tun_id);
	return 0;
}

/*
 * nss_nlcapwap_tx_packets_host_to_host()
 *	Handler for sending traffic from one DUT to other
 */
static int nss_nlcapwap_tx_packets_host_to_host(struct nss_nlcapwap_rule *nl_rule)
{
	struct nss_capwap_metaheader *pre;
	struct net_device *capwap_ndev;
	uint32_t pattern, no_pattern;
	struct sk_buff *skb;
	uint8_t *data;
	int i, len;

	/*
	 * Get the capwap netdev reference
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();
	skb = dev_alloc_skb(nl_rule->msg.tx_packets.pkt_size + NSS_NLCAPWAP_SKB_TAILROOM);
	if (!skb) {
		nss_nl_error("Could not allocate a sk_buff\n");
		return -1;
	}

	/*
	 * Reserve 2 bytes extra if meta header type is 802.3
	 */
	spin_lock(&lock);
	if (capwap_gbl_ctx.meta_header.type == NSS_CAPWAP_PKT_TYPE_802_3) {
		skb_reserve(skb, NSS_NLCAPWAP_SKB_RESERVE_SZ_HUNDRED);
	} else {
		skb_reserve(skb, NSS_NLCAPWAP_SKB_RESERVE_SZ_NINTY_EIGHT +
				NSS_NLCAPWAP_SKB_RESERVE_SZ_TWO * capwap_gbl_ctx.nss_nlcapwap_80211e);
	}

	skb_put(skb, nl_rule->msg.tx_packets.pkt_size + sizeof(struct nss_capwap_metaheader));

	/*
	 * Fill pattern to check for packet integrity
	 */
	if (!atomic_read(&enable_perf)) {
		memset(&skb->data[sizeof(struct nss_capwap_metaheader)], NSS_NLCAPWAP_DATA,
			nl_rule->msg.tx_packets.pkt_size);
	}

	if (capwap_gbl_ctx.meta_header.type == NSS_NLCAPWAP_META_HEADER_TYPE_ZERO) {
		/*
		 * TYPE ipv4. Fill the iptype and ipheader info.
		 */
		skb->data[sizeof(struct nss_capwap_metaheader) + 12] = 0x8;
		skb->data[sizeof(struct nss_capwap_metaheader) + 13] = 0;
		skb->data[sizeof(struct nss_capwap_metaheader) + 14] = 0x45;
		skb->data[sizeof(struct nss_capwap_metaheader) + 15] = 0;
	} else if (capwap_gbl_ctx.meta_header.type == NSS_NLCAPWAP_META_HEADER_TYPE_ONE) {
		/*
		 * Type EAPOL. Fill the iptype for ethernet header.
		 */
		skb->data[sizeof(struct nss_capwap_metaheader) + 12] = 0x88;
		skb->data[sizeof(struct nss_capwap_metaheader) + 13] = 0x8e;
	} else {
		/*
		 * Type management. Recognized by 0x0004 after metadata.
		 */
		skb->data[sizeof(struct nss_capwap_metaheader) + 0] = 0x00;
		skb->data[sizeof(struct nss_capwap_metaheader) + 1] = 0x04;
	}

	pre = (struct nss_capwap_metaheader *)skb->data;
	memcpy(pre, &capwap_gbl_ctx.meta_header, sizeof(struct nss_capwap_metaheader));
	spin_unlock(&lock);

	if (atomic_read(&enable_perf)) {
		goto process;
	}

	pattern = no_pattern = 0;
	data = skb->data;
	len = skb->len;
	for (i = 0; i < len; i++) {
		(data[i] == NSS_NLCAPWAP_DATA) ? pattern++ : no_pattern++;
	}

process:
	capwap_ndev->netdev_ops->ndo_start_xmit(skb, capwap_ndev);
	nss_nl_info("tunnel %d: TX: len:%d (%d,%d)\n", pre->tunnel_id,
	      len, pattern, no_pattern);

	return 0;
}

/*
 * nss_nlcapwap_ops_tx_packets()
 *	Handler for sending traffic
 */
static int nss_nlcapwap_ops_tx_packets(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcapwap_rule *nl_rule;
	struct nss_nlcmn *nl_cm;
	int ret, i;

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_TX_PACKETS);
	if (!nl_cm) {
		nss_nl_error("Unable to extract tx_packets data\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Send traffic from host to host
	 */
	for (i = 0; i < nl_rule->msg.tx_packets.num_of_packets; i++) {
		ret = nss_nlcapwap_tx_packets_host_to_host(nl_rule);
		if (ret < 0) {
			nss_nl_error("Error in transmission of skb\n");
			return ret;
		}
	}

	nss_nl_info("Traffic transmission successful\n");
	return 0;
}

/*
 * nss_nlcapwap_ops_meta_header()
 *	Handler for creating meta header
 */
static int nss_nlcapwap_ops_meta_header(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_nlcapwap_meta_header meta_hdr;
	struct nss_nlcapwap_rule *nl_rule;
	struct nss_nlcmn *nl_cm;

	/*
	 * extract the message payload
	 */
	nl_cm = nss_nl_get_msg(&nss_nlcapwap_family, info, NSS_NLCAPWAP_CMD_TYPE_META_HEADER);
	if (!nl_cm) {
		nss_nl_error("Unable to extract meta header values.\n");
		return -EINVAL;
	}

	/*
	 * Message validation required before accepting the configuration
	 */
	nl_rule = container_of(nl_cm, struct nss_nlcapwap_rule, cm);

	/*
	 * Set meta header values
	 */
	meta_hdr = nl_rule->msg.meta_header;
	nss_nlcapwap_set_meta_header(meta_hdr);
	nss_nl_info("Successfully created meta header.\n");
	return 0;
}

/*
 * nss_nlcapwap_cmd_ops
 *	Operation table called by the generic netlink layer based on the command
 */
struct genl_ops nss_nlcapwap_cmd_ops[] = {
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_CREATE_TUN, .doit = nss_nlcapwap_ops_create_tun,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_DESTROY_TUN, .doit = nss_nlcapwap_ops_destroy_tun,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_UPDATE_MTU, .doit = nss_nlcapwap_ops_update_mtu,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_DTLS, .doit = nss_nlcapwap_ops_dtls,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_PERF, .doit = nss_nlcapwap_ops_perf,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_TX_PACKETS, .doit = nss_nlcapwap_ops_tx_packets,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_META_HEADER, .doit = nss_nlcapwap_ops_meta_header,},
	{.cmd = NSS_NLCAPWAP_CMD_TYPE_IP_FLOW, .doit = nss_nlcapwap_ops_ip_flow,},
};

/*
 * nss_nlcapwap_get_ifnum()
 *	Get the interface number corresponding to netdev
 */
int nss_nlcapwap_get_ifnum(struct net_device *dev, enum nss_dynamic_interface_type type)
{
	int ifnum;

	/*
	 * Get the interface number depending upon the dev and type
	 */
	ifnum = nss_cmn_get_interface_number_by_dev_and_type(dev, type);
	if (ifnum < 0) {
		nss_nl_error("%p: Failed to find interface number (dev:%s, type:%d)\n",
				dev, dev->name, type);
		return -ENODEV;
	}

	return ifnum;
}

/*
 * nss_nlcapwap_init()
 *	Init handler for capwap
 */
bool nss_nlcapwap_init(void)
{
	int err;

	nss_nl_info_always("Init NSS netlink capwap handler\n");

	/*
	 * register NETLINK ops with the family
	 */
	err = genl_register_family_with_ops_groups(&nss_nlcapwap_family, nss_nlcapwap_cmd_ops,
			nss_nlcapwap_family_mcgrp);
	if (err) {
		nss_nl_info_always("Error: %d unable to register capwap family\n", err);
		genl_unregister_family(&nss_nlcapwap_family);
		return false;
	}

	/*
	 * Register the capwap netdev rx handler
	 */
	nss_nlcapwap_register_netdev_handler();
	return true;
}

/*
 * nss_nlcapwap_exit()
 *	Exit handler for capwap
 */
bool nss_nlcapwap_exit(void)
{
	struct net_device *capwap_ndev;
	int err, i;

	nss_nl_info_always("Exit NSS netlink capwap handler\n");

	/*
	 * Unregister the capwap netdev rx handler
	 */
	capwap_ndev = nss_capwapmgr_get_netdev();
	rtnl_lock();
	netdev_rx_handler_unregister(capwap_ndev);
	rtnl_unlock();

	/*
	 * Destroy all the active tunnels
	 */
	for (i = 0; i < NSS_CAPWAPMGR_MAX_TUNNELS; i++) {
		nss_nlcapwap_destroy_tun(i);
	}

	/*
	 * unregister the ops family
	 */
	err = genl_unregister_family(&nss_nlcapwap_family);
	if (err) {
		nss_nl_info_always("Error: %d unable to unregister capwap NETLINK family\n", err);
		return false;
	}

	return true;
}
