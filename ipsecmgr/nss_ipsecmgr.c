/*
 **************************************************************************
 * Copyright (c) 2014-2018, The Linux Foundation. All rights reserved.
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
 *	NSS to HLOS IPSec Manager
 */
#include <linux/cache.h>
#include <linux/version.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/of.h>
#include <linux/ipv6.h>
#include <linux/if_vlan.h>
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

#include <nss_api_if.h>
#include <nss_ipsec.h>
#include <nss_ipsecmgr.h>

#include "nss_ipsecmgr_priv.h"
#include <nss_tstamp.h>

extern bool nss_cmn_get_nss_enabled(void);

struct nss_ipsecmgr_drv *ipsecmgr_ctx;

static bool gen_pmtu_error = true;
module_param(gen_pmtu_error, bool, 0644);
MODULE_PARM_DESC(gen_pmtu_error, "Support generation of PMTU error packet");

/*
 **********************
 * Helper Functions
 **********************
 */

/*
 * nss_ipsecmgr_ref_no_update()
 *     dummy functions for object owner when there is no update
 */
static void nss_ipsecmgr_ref_no_update(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *child, struct nss_ipsec_msg *nim)
{
	nss_ipsecmgr_trace("ref_no_update triggered for child (%p)\n", child);
	return;
}

/*
 * nss_ipsecmgr_ref_no_free()
 * 	dummy functions for object owner when there is no free
 */
static void nss_ipsecmgr_ref_no_free(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref)
{
	nss_ipsecmgr_trace("%p:ref_no_free triggered\n", ref);
	return;
}

/*
 * nss_ipsecmgr_ref_no_overhead()
 *	dummy functions for object owner when there is no overhead
 */
static uint32_t nss_ipsecmgr_ref_no_overhead(struct nss_ipsecmgr_ref *ref)
{
	nss_ipsecmgr_trace("%p:ref_get_no_overhead triggered\n", ref);
	return 0;
}

/*
 * nss_ipsecmgr_ref_set_overhead()
 *	set the overhead function for reference object
 */
void nss_ipsecmgr_ref_set_overhead(struct nss_ipsecmgr_ref *ref, nss_ipsecmgr_ref_overhead_t overhead)
{
	ref->overhead = overhead;
}

/*
 * nss_ipsecmgr_ref_init()
 * 	initiaize the reference object
 */
void nss_ipsecmgr_ref_init(struct nss_ipsecmgr_ref *ref, nss_ipsecmgr_ref_update_t update, nss_ipsecmgr_ref_free_t free)
{
	INIT_LIST_HEAD(&ref->head);
	INIT_LIST_HEAD(&ref->node);

	ref->id = 0;
	ref->parent = NULL;
	ref->update = update ? update : nss_ipsecmgr_ref_no_update;
	ref->free = free ? free : nss_ipsecmgr_ref_no_free;
	ref->overhead = nss_ipsecmgr_ref_no_overhead;
}

/*
 * nss_ipsecmgr_ref_add()
 * 	add child reference to parent chain
 */
void nss_ipsecmgr_ref_add(struct nss_ipsecmgr_ref *child, struct nss_ipsecmgr_ref *parent)
{
	/*
	 * if child is already part of an existing chain then remove it before
	 * adding it to the new one. In case this is a new entry then the list
	 * init during alloc would ensure that the "del_init" operation results
	 * in a no-op
	 */
	list_del_init(&child->node);
	list_add(&child->node, &parent->head);

	child->parent = parent;
}

/*
 * nss_ipsecmgr_ref_update()
 * 	update the "ref" object and link it to the parent
 */
void nss_ipsecmgr_ref_update(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *child, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_ref *entry;

	child->id++;
	child->update(priv, child, nim);

	/*
	 * If, there are references to associated with this
	 * object then notify them about the change. This allows
	 * the "ref" objects to trigger notifications to NSS for
	 * updates to SA
	 */
	list_for_each_entry(entry, &child->head, node) {
		nss_ipsecmgr_ref_update(priv, entry, nim);
	}
}

/*
 * nss_ipsecmgr_ref_overhead()
 *	Get the SA overhead of the reference passed
 *
 * note: ideally this will trigger a chain of callbacks till
 * the SA
 */
uint32_t nss_ipsecmgr_ref_overhead(struct nss_ipsecmgr_ref *ref)
{
	if (!ref->parent)
		return ref->overhead(ref);

	return nss_ipsecmgr_ref_overhead(ref->parent);
}

/*
 * nss_ipsecmgr_ref_free()
 * 	Free all references from the "ref" object
 *
 * Note: If, the "ref" has child references then it
 * will walk the child reference chain first and issue
 * free for each of the associated "child ref" objects.
 * At the end it will invoke free for the "parent" ref
 * object.
 *
 * +-------+   +-------+   +-------+
 * |  SA1  +--->   SA2 +--->  SA3  |
 * +---+---+   +---+---+   +-------+
 *     |
 * +---V---+   +-------+   +-------+
 * | Flow1 +---> Sub1  +---> Flow4 |
 * +-------+   +---+---+   +-------+
 *                 |
 *             +---v---+
 *             | Flow2 |
 *             +---+---+
 *                 |
 *             +---v---+
 *             | Flow3 |
 *             +-------+
 */
void nss_ipsecmgr_ref_free(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref)
{
	struct nss_ipsecmgr_ref *entry;

	while (!list_empty(&ref->head)) {
		entry = list_first_entry(&ref->head, struct nss_ipsecmgr_ref, node);
		nss_ipsecmgr_ref_free(priv, entry);
	}

	list_del_init(&ref->node);
	ref->free(priv, ref);
}

/*
 * nss_ipsecmgr_ref_is_child()
 * 	return true if the child is direct sibling of parent
 */
bool nss_ipsecmgr_ref_is_child(struct nss_ipsecmgr_ref *child, struct nss_ipsecmgr_ref *parent)
{
	struct nss_ipsecmgr_ref *entry;

	list_for_each_entry(entry, &parent->head, node) {
		if (entry == child) {
			return true;
		}
	}

	return false;
}

/*
 **********************
 * Netdev ops
 **********************
 */

/*
 * nss_ipsecmgr_tunnel_open()
 * 	open the tunnel for usage
 */
static int nss_ipsecmgr_tunnel_open(struct net_device *dev)
{
	struct nss_ipsecmgr_priv *priv;

	priv = netdev_priv(dev);

	netif_start_queue(dev);

	return 0;
}

/*
 * nss_ipsecmgr_tunnel_stop()
 * 	stop the IPsec tunnel
 */
static int nss_ipsecmgr_tunnel_stop(struct net_device *dev)
{
	struct nss_ipsecmgr_priv *priv;

	priv = netdev_priv(dev);

	netif_stop_queue(dev);

	return 0;
}

/*
 * nss_ipsecmgr_create_meta_data()
 *	Create the meta data
 */
static void nss_ipsecmgr_create_vlan_metadata(struct sk_buff *skb, uint16_t prio)
{
	/*
	 * Assume that size of metadata never goes beyond L1_CACHE_BYTES.
	 */
	uint8_t *data_align;
	struct nss_ipsec_metadata *meta_data;
	uint8_t iphdr_offset;

	BUG_ON(skb_headroom(skb) < (2 * L1_CACHE_BYTES));

	data_align = (uint8_t *)round_down((nss_ptr_t)skb->data, 2 * L1_CACHE_BYTES);
	iphdr_offset = skb->data - data_align;

	/*
	 * We can pull the skb unconditionaly because we already ensured that
	 * the SKB has 128 bytes of headroom.
	 */
	meta_data = (struct nss_ipsec_metadata *)skb_push(skb, iphdr_offset);

	meta_data->version = NSS_IPSEC_METADATA_VER;
	meta_data->type = NSS_IPSEC_METADATA_TYPE_VLAN;
	meta_data->len = sizeof(struct nss_ipsec_metadata_vlan);
	meta_data->iphdr_offset = iphdr_offset;

	meta_data->data.vlan.prio = prio;
}

/*
 * nss_ipsecmgr_tunnel_tx()
 * 	tunnel transmit function
 */
static netdev_tx_t nss_ipsecmgr_tunnel_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct nss_ipsecmgr_flow_data flow_data = {0};
	bool process_mtu = gen_pmtu_error;
	struct nss_ipsecmgr_priv *priv;
	struct net_device *outer_dev;
	bool expand_skb = false;
	uint16_t vlan_prio = 0;
	int nhead, ntail;
	bool tstamp_skb;

	priv = netdev_priv(dev);
	nhead = dev->needed_headroom;
	ntail = dev->needed_tailroom;

	tstamp_skb = skb_shinfo(skb)->tx_flags & SKBTX_HW_TSTAMP;

	/*
	 * Check if skb is non-linear
	 */
	if (skb_is_nonlinear(skb)) {
		nss_ipsecmgr_trace("%s: NSS IPSEC does not support fragments %p\n", dev->name, skb);
		goto free;
	}

	/*
	 * Check if skb is shared
	 */
	if (unlikely(skb_shared(skb))) {
		nss_ipsecmgr_trace("%s: Shared skb is not supported: %p\n", dev->name, skb);
		goto free;
	}

	/*
	 * Check if packet is given starting from network header
	 */
	if (skb->data != skb_network_header(skb)) {
		nss_ipsecmgr_trace("%s: 'Skb data is not starting from IP header\n", dev->name);
		goto free;
	}

	/*
	 * For all these cases
	 * - create a writable copy of buffer
	 * - increase the head room
	 * - increase the tail room
	 */
	if (skb_cloned(skb) || (skb_headroom(skb) < nhead) || (skb_tailroom(skb) < ntail)) {
		expand_skb = true;
	}

	if (expand_skb && pskb_expand_head(skb, nhead, ntail, GFP_KERNEL)) {
		nss_ipsecmgr_trace("%s: unable to expand buffer\n", dev->name);
		goto free;
	}

	/*
	 * Before proceeding check for the following conditions
	 * For IPv4 packet if DF bit is set then process_mtu
	 */
	if (process_mtu && (skb->protocol == htons(ETH_P_IP)))
		process_mtu = !!(ip_hdr(skb)->frag_off & htons(IP_DF));

	/*
	 * check whether the IPsec encapsulation can be offloaded to NSS
	 *	- if the flow matches a subnet rule, then a new flow rule
	 *		is added to NSS.
	 *	- if the flow doesn't match any subnet, then the packet
	 * 		is dropped
	 */
	if (!nss_ipsecmgr_flow_offload(priv, skb, &flow_data)) {
		nss_ipsecmgr_warn("%p:failed to accelerate flow\n", dev);
		goto free;
	}

	/*
	 * Check if pre-fragmentation is not enabled or already a fragment.
	 * then send the buffer on its way to NSS
	 */
	if (process_mtu && nss_ipsecmgr_flow_process_pmtu(priv, skb, &flow_data))
		return NETDEV_TX_OK;

	/*
	 * If the packet needs to timestamped. Send to
	 * timestamping NSS module.
	 */
	if (unlikely(tstamp_skb)) {
		if (nss_tstamp_tx_buf(ipsecmgr_ctx->nss_ctx, skb, NSS_IPSEC_ENCAP_IF_NUMBER))
			goto free;

		return NETDEV_TX_OK;
	}

	/*
	 * Check whether the skb->priority has to be
	 * translated to 8021p.
	 */
	if (unlikely(atomic_read(&priv->is_vlan))) {
		outer_dev = dev_get_by_index(&init_net, atomic_read(&priv->outer_ifindex));
		if (outer_dev) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 13, 0)
			vlan_prio = vlan_dev_get_egress_qos_mask(outer_dev, skb);
#else
			vlan_prio = vlan_dev_get_egress_qos_mask(outer_dev, skb->priority);
#endif

			/*
			 * If the mapped priority is zero, will be taken care by default
			 * condition in NSS.
			 */
			if (vlan_prio)
				nss_ipsecmgr_create_vlan_metadata(skb, vlan_prio >> VLAN_PRIO_SHIFT);

			dev_put(outer_dev);
		}
	}

	/*
	 * Send the packet down
	 */
	if (nss_ipsec_tx_buf(skb, ipsecmgr_ctx->encap_ifnum) != 0) {
		/*
		 * TODO: NEED TO STOP THE QUEUE
		 */
		goto free;
	}

	return NETDEV_TX_OK;

free:
	dev_kfree_skb_any(skb);
	return NETDEV_TX_OK;
}

/*
 * nss_ipsecmgr_tunnel_stats()
 * 	get tunnel statistics
 */
static struct rtnl_link_stats64 *nss_ipsecmgr_tunnel_stats64(struct net_device *dev, struct rtnl_link_stats64 *stats)
{
	struct nss_ipsecmgr_priv *priv = netdev_priv(dev);

	memset(stats, 0, sizeof(struct rtnl_link_stats64));

	/*
	 * trigger a stats update chain
	 */
	read_lock_bh(&ipsecmgr_ctx->lock);
	memcpy(stats, &priv->stats, sizeof(struct rtnl_link_stats64));
	read_unlock_bh(&ipsecmgr_ctx->lock);

	return stats;
}

/*
 * nss_ipsecmgr_change_mtu()
 * 	change MTU size of IPsec tunnel device
 */
static int32_t nss_ipsecmgr_change_mtu(struct net_device *dev, int32_t mtu)
{
	dev->mtu = mtu;
	return 0;
}

/* NSS IPsec tunnel operation */
static const struct net_device_ops nss_ipsecmgr_tunnel_ops = {
	.ndo_open = nss_ipsecmgr_tunnel_open,
	.ndo_stop = nss_ipsecmgr_tunnel_stop,
	.ndo_start_xmit = nss_ipsecmgr_tunnel_tx,
	.ndo_get_stats64 = nss_ipsecmgr_tunnel_stats64,
	.ndo_change_mtu = nss_ipsecmgr_change_mtu,
};

/*
 * nss_ipsecmgr_tunnel_free()
 * 	free an existing IPsec tunnel interface
 */
static void nss_ipsecmgr_tunnel_free(struct net_device *dev)
{
	nss_ipsecmgr_info("IPsec tunnel device(%s) freed\n", dev->name);

	free_netdev(dev);
}

/*
 * nss_ipsecmr_setup_tunnel()
 * 	setup the IPsec tunnel
 */
static void nss_ipsecmgr_tunnel_setup(struct net_device *dev)
{
	dev->addr_len = ETH_ALEN;
	dev->mtu = NSS_IPSECMGR_TUN_MTU(ETH_DATA_LEN);

	dev->hard_header_len = NSS_IPSECMGR_TUN_MAX_HDR_LEN;
	dev->needed_headroom = NSS_IPSECMGR_TUN_HEADROOM;
	dev->needed_tailroom = NSS_IPSECMGR_TUN_TAILROOM;

	dev->type = NSS_IPSEC_ARPHRD_IPSEC;

	dev->ethtool_ops = NULL;
	dev->header_ops = NULL;
	dev->netdev_ops = &nss_ipsecmgr_tunnel_ops;

	dev->destructor = nss_ipsecmgr_tunnel_free;

	/*
	 * get the MAC address from the ethernet device
	 */
	random_ether_addr(dev->dev_addr);

	memset(dev->broadcast, 0xff, dev->addr_len);
	memcpy(dev->perm_addr, dev->dev_addr, dev->addr_len);
}

/*
 * nss_ipsecmgr_tunnel_dev_index2entry()
 * 	get the tunnel entry for the dev index
 *
 * Note: this is typically expected in the RX BH path
 */
static struct nss_ipsecmgr_tunnel_entry *nss_ipsecmgr_tunnel_dev_index2entry(int dev_index)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct nss_ipsecmgr_tunnel_entry *entry;

	if (!atomic_read(&tun_db->num_entries))
		return NULL;

	BUG_ON(!in_atomic());

	/*
	 * search the callback database to find if there
	 * are any registered callback for the net_device
	 */
	read_lock(&ipsecmgr_ctx->lock);				/* lock */
	list_for_each_entry(entry, &tun_db->entries, node) {
		if (entry->dev_index == dev_index) {
			read_unlock(&ipsecmgr_ctx->lock);	/* unlock */
			return entry;
		}
	}

	read_unlock(&ipsecmgr_ctx->lock);			/* unlock */

	return NULL;
}

/*
 * nss_ipsecmgr_check_rx_loop()
 * 	check whether the SKB is a looped back from NSS as exception.
 */
static inline bool nss_ipsecmgr_check_rx_loop(struct sk_buff *skb)
{
	uint16_t *magic = (uint16_t *)skb->cb;
	if (*magic == NSS_IPSECMGR_ESP_LOOP_MAGIC) {
		*magic = 0;
		return true;
	}

	return false;
}

/*
 * nss_ipsecmgr_rx_init_loop()
 * 	Stamp SKB with magic number. We will check the magic in the exception
 * 	path, whether the packet is successfully decrypted or not.
 */
static inline void nss_ipsecmgr_rx_init_loop(struct sk_buff *skb)
{
	uint16_t *magic = (uint16_t *)skb->cb;
	*magic = NSS_IPSECMGR_ESP_LOOP_MAGIC;
}

/*
 * nss_ipsecmgr_rx_reset_loop()
 * 	check whether packet has magic set in SKB CB and clear it
 */
static inline void nss_ipsecmgr_rx_reset_loop(struct sk_buff *skb)
{
	uint16_t *magic = (uint16_t *)skb->cb;
	if (*magic == NSS_IPSECMGR_ESP_LOOP_MAGIC)
		*magic = 0;
}

/*
 * nss_ipsecmgr_tunnel_rx_local()
 * 	ESP exception packets from NSS should be delivered through local_out.
 *
 * note: This function will return false, if the packet is not consumed.
 */
static bool nss_ipsecmgr_tunnel_rx_local(struct sk_buff *skb)
{
	switch (ip_hdr(skb)->version) {
	case IPVERSION:
	{
		struct dst_entry *dst = NULL;
		struct iphdr *iph = ip_hdr(skb);
		uint8_t proto = iph->protocol;

		if ((proto != IPPROTO_ESP) && (proto != IPPROTO_UDP))
			return false;

		skb_set_transport_header(skb, sizeof(*iph));
		if ((iph->protocol == IPPROTO_UDP) && (udp_hdr(skb)->dest != ntohs(NSS_IPSECMGR_NATT_PORT_DATA)))
			return false;

		/*
		 * Packets sent from IPSec Manager ESP protocol handler can
		 * land here, if there are no SAs present in NSS. Check for
		 * SKB_CB magic and drop the packets here.
		 */
		if (nss_ipsecmgr_check_rx_loop(skb)) {
			nss_ipsecmgr_info("received ingress ESP packet from NSS\n");

			write_lock(&ipsecmgr_ctx->lock);
			ipsecmgr_ctx->dec_stats.fail_loop++;
			write_unlock(&ipsecmgr_ctx->lock);

			dev_kfree_skb_any(skb);
			return true;
		}

		/*
		 * ESP packet received here, since routing rule is absent.
		 *
		 * Before sending to higher layer, make basic sanity of packets.
		 * - IP Header Checksum should be set
		 * - Connection Tracker reference should not be retained from
		 *   clear text packet.
		 */
		if (!iph->check)
			ip_send_check(iph);

		if (skb->nfct)
			nf_reset(skb);

		/*
		 * set dst for the packet, before injecting into the stack
		 */
		dst = (struct dst_entry *)ip_route_output(&init_net, iph->daddr, 0, 0, 0);
		if (IS_ERR(dst)) {
			nss_ipsecmgr_info("dst could not be retrived\n");
			dev_kfree_skb_any(skb);
			return true;
		}

		skb_dst_drop(skb);
		skb_dst_set(skb, dst);
		ip_local_out(skb);
		return true;
	}

	case 6:
	{
		struct ipv6hdr *ip6h = ipv6_hdr(skb);
		struct dst_entry *dst = NULL;
		struct flowi6 fl6;

		if (ipv6_hdr(skb)->nexthdr != IPPROTO_ESP)
			return false;

		/*
		 * Packets sent from IPSec Manager ESP protocol handler can
		 * land here, if there are no SAs present in NSS. Check for
		 * SKB_CB magic and drop the packets here.
		 */
		if (nss_ipsecmgr_check_rx_loop(skb)) {
			nss_ipsecmgr_info("received ingress ESP packet from NSS\n");

			write_lock(&ipsecmgr_ctx->lock);
			ipsecmgr_ctx->dec_stats.fail_loop++;
			write_unlock(&ipsecmgr_ctx->lock);

			dev_kfree_skb_any(skb);
			return true;
		}

		/*
		 * - Connection Tracker reference should not be retained from
		 *   clear text packet.
		 */
		if (skb->nfct)
			nf_reset(skb);

		memset(&fl6, 0, sizeof(fl6));
		memcpy(&fl6.daddr, &ip6h->daddr, sizeof(fl6.daddr));

		dst = ip6_route_output(&init_net, NULL, &fl6);
		if (dst->error) {
			nss_ipsecmgr_info("dst could not be retrived\n");
			dst_release(dst);
			dev_kfree_skb_any(skb);
			return true;
		}

		skb_dst_drop(skb);
		skb_dst_set(skb, dst);
		ip6_local_out(skb);
		return true;
	}

	default:
		BUG_ON(true);
	}
}

/*
 * nss_ipsecmgr_tunnel_get_dev()
 *	Get the net_device associated with the packet.
 */
static struct net_device *nss_ipsecmgr_tunnel_get_dev(struct sk_buff *skb)
{
	struct nss_ipsecmgr_sa_entry *sa_entry;
	struct nss_ipsec_tuple tuple;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;
	struct net_device *dev;
	struct dst_entry *dst;
	struct ip_esp_hdr *esph;
	size_t hdr_sz = 0;

	skb_reset_mac_header(skb);
	skb_reset_network_header(skb);

	switch (ip_hdr(skb)->version) {
	case IPVERSION:
	{
		struct iphdr *iph = ip_hdr(skb);
		struct rtable *rt;
		bool is_encap;

		hdr_sz = sizeof(struct iphdr);
		skb->protocol = cpu_to_be16(ETH_P_IP);

		/*
		 * Set the transport header skb pointer
		 */
		skb_set_transport_header(skb, hdr_sz);

		is_encap = (iph->protocol == IPPROTO_ESP);
		if (!is_encap && (iph->protocol == IPPROTO_UDP)) {
			if (udp_hdr(skb)->dest == NSS_IPSECMGR_NATT_PORT_DATA) {
				hdr_sz += sizeof(struct udphdr);
				is_encap = true;
			}
		}

		if (!is_encap) {
			rt = ip_route_output(&init_net, iph->saddr, 0, 0, 0);
			if (IS_ERR(rt)) {
				return NULL;
			}

			dst = (struct dst_entry *)rt;
			dev = dst->dev;
			dst_release(dst);
			goto done;
		}

		nss_ipsecmgr_v4_hdr2tuple(iph, &tuple);
		break;
	}

	case 6:
	{
		struct ipv6hdr *ip6h = ipv6_hdr(skb);
		struct flowi6 fl6;

		hdr_sz = sizeof(struct ipv6hdr);
		skb->protocol = cpu_to_be16(ETH_P_IPV6);

		nss_ipsecmgr_v6_hdr2tuple(ip6h, &tuple);

		if (tuple.proto_next_hdr != IPPROTO_ESP) {
			memset(&fl6, 0, sizeof(fl6));
			memcpy(&fl6.daddr, &ip6h->saddr, sizeof(fl6.daddr));

			dst = ip6_route_output(&init_net, NULL, &fl6);
			if (dst->error) {
				dst_release(dst);
				return NULL;
			}

			dev = dst->dev;
			dst_release(dst);
			goto done;
		}

		if (ip6h->nexthdr == NEXTHDR_FRAGMENT) {
			hdr_sz += sizeof(struct frag_hdr);
		}
		break;
	}
	default:
		nss_ipsecmgr_warn("%p:could not get dev for the flow\n", skb);
		return NULL;
	}

	skb_set_transport_header(skb, hdr_sz);
	esph = ip_esp_hdr(skb);
	tuple.esp_spi = ntohl(esph->spi);

	nss_ipsecmgr_sa_tuple2key(&tuple, &key);

	ref = nss_ipsecmgr_sa_lookup(&key);
	if (!ref) {
		nss_ipsecmgr_trace("unable to find SA (%p)\n", skb);
		return NULL;
	}

	sa_entry = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);
	dev = sa_entry->priv->dev;
done:

	skb->pkt_type = PACKET_HOST;
	skb->skb_iif = dev->ifindex;
	skb->dev = dev;
	return dev;
}

/*
 * nss_ipsecmgr_tunnel_rx()
 *	receive NSS exception packets
 */
static void nss_ipsecmgr_tunnel_rx(struct net_device *dummy, struct sk_buff *skb, __attribute((unused)) struct napi_struct *napi)
{
	struct nss_ipsecmgr_tunnel_entry *entry;
	nss_ipsecmgr_data_cb_t cb_fn;
	struct net_device *dev;

	BUG_ON(dummy == NULL);
	BUG_ON(skb == NULL);

	dev = nss_ipsecmgr_tunnel_get_dev(skb);
	if (unlikely(!dev)) {
		nss_ipsecmgr_trace("cannot find a dev(%p)\n", skb);
		dev_kfree_skb_any(skb);
		return;
	}

	dev_hold(dev);

	/*
	 * search the device in the registered callback table;
	 * if there is match then load the callback for indication
	 */
	entry = nss_ipsecmgr_tunnel_dev_index2entry(dev->ifindex);
	if (!entry || !entry->data) {

		/*
		 * ESP packets exceptioned here are egress packets and should be sent to local_out
		 * Clear text/decrypted packets are ingress packets and injected to stack.
		 */
		if (!nss_ipsecmgr_tunnel_rx_local(skb))
			netif_receive_skb(skb);

		goto done;
	}

	cb_fn = entry->data;
	BUG_ON(!cb_fn);

	cb_fn(entry->app_data, skb);
done:
	dev_put(dev);
}

/*
 * nss_ipsecmgr_tunnel_update_stats()
 *	Update tunnel stats
 */
static void nss_ipsecmgr_tunnel_update_stats(struct nss_ipsecmgr_priv *priv, struct nss_ipsec_msg *nim)
{
	struct rtnl_link_stats64 *tun_stats;
	struct nss_ipsec_sa_stats *pkts;

	tun_stats = &priv->stats;
	pkts = &nim->msg.stats.sa;

	/*
	 * update tunnel specific stats
	 */
	if (nim->type == NSS_IPSEC_TYPE_ENCAP) {
		tun_stats->tx_bytes += pkts->bytes;
		tun_stats->tx_packets += pkts->count;

		tun_stats->tx_dropped += pkts->no_headroom;
		tun_stats->tx_dropped += pkts->no_tailroom;
		tun_stats->tx_dropped += pkts->no_resource;
		tun_stats->tx_dropped += pkts->fail_queue;
		tun_stats->tx_dropped += pkts->fail_hash;
		tun_stats->tx_dropped += pkts->fail_replay;
		return;
	}

	tun_stats->rx_bytes += pkts->bytes;
	tun_stats->rx_packets += pkts->count;

	tun_stats->rx_dropped += pkts->no_headroom;
	tun_stats->rx_dropped += pkts->no_tailroom;
	tun_stats->rx_dropped += pkts->no_resource;
	tun_stats->rx_dropped += pkts->fail_queue;
	tun_stats->rx_dropped += pkts->fail_hash;
	tun_stats->rx_dropped += pkts->fail_replay;
}

/*
 * nss_ipsecmgr_tunnel_notify()
 * 	asynchronous event reception
 */
static void nss_ipsecmgr_tunnel_notify(__attribute((unused))void *app_data, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_node_stats *drv_stats;
	struct nss_ipsec_node_stats *node_stats;
	struct nss_ipsecmgr_sa_stats *sa_stats;
	struct nss_ipsecmgr_event stats_event;
	struct nss_ipsecmgr_sa_entry *sa;
	struct nss_ipsec_sa_stats *pkts;
	struct nss_ipsecmgr_priv *priv;
	nss_ipsecmgr_event_cb_t cb_fn;
	struct nss_ipsec_tuple *tuple;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;
	bool reset_fail_hash;
	struct net_device *dev;

	BUG_ON(nim == NULL);

	/*
	 * this holds the ref_cnt for the device
	 */
	dev = dev_get_by_index(&init_net, nim->tunnel_id);
	if (!dev) {
		nss_ipsecmgr_info("event received on deallocated I/F (%d)\n", nim->tunnel_id);
		return;
	}

	priv = netdev_priv(dev);

	switch (nim->cm.type) {
	case NSS_IPSEC_MSG_TYPE_SYNC_SA_STATS:

		/*
		 * prepare and lookup sa based on selector sent from nss
		 */
		tuple = &nim->tuple;
		if (tuple->ip_ver == 6) {
			nss_ipsecmgr_v6addr_swap(tuple->dst_addr, tuple->dst_addr);
			nss_ipsecmgr_v6addr_swap(tuple->src_addr, tuple->src_addr);
		}

		nss_ipsecmgr_sa_tuple2key(tuple, &key);

		write_lock(&ipsecmgr_ctx->lock);

		ref = nss_ipsecmgr_sa_lookup(&key);
		if (!ref) {
			write_unlock(&ipsecmgr_ctx->lock);
			nss_ipsecmgr_trace("event received on deallocated SA tunnel:(%d)\n", nim->tunnel_id);
			goto done;
		}

		sa = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);

		/*
		 * update sa stats in the local database
		 */
		nss_ipsecmgr_sa_stats_update(nim, sa);

		sa_stats = &stats_event.data.stats;

		/*
		 * update tunnel stats
		 */
		sa_stats->fail_hash_alarm = false;
		nss_ipsecmgr_tunnel_update_stats(priv, nim);

		if ((nim->type == NSS_IPSEC_TYPE_DECAP) &&
				sa->fail_hash_thresh) {
			pkts = &nim->msg.stats.sa;

			/*
			 * If the fail_hash_count is zero and packet count is
			 * non-zero. It indicates that the continuous hash
			 * failure was a transient state hence reset the count
			 */
			reset_fail_hash = (!pkts->fail_hash_cont &&
					pkts->count);
			sa->pkts.fail_hash_cont = reset_fail_hash ? 0
				: (sa->pkts.fail_hash_cont +
						pkts->fail_hash_cont);

			/*
			 * Check the fail_hash_cont hash crossed the threshold,
			 * if yes set the alarm.
			 */
			if (sa->pkts.fail_hash_cont >= sa->fail_hash_thresh) {
				sa_stats->fail_hash_alarm = true;
				sa->pkts.fail_hash_cont -= sa->fail_hash_thresh;
			}
		}

		memcpy(&sa_stats->sa, &sa->sa_info,
				sizeof(struct nss_ipsecmgr_sa));
		sa_stats->crypto_index = sa->nim.msg.rule.data.crypto_index;
		write_unlock(&ipsecmgr_ctx->lock);

		/*
		 * if event callback is available then post the statistics using the callback function
		 */
		cb_fn = priv->tun_entry.event;
		if (cb_fn) {
			stats_event.type = NSS_IPSECMGR_EVENT_SA_STATS;

			/*
			 * copy stats and SA information
			 */
			sa_stats->seq_num = nim->msg.stats.sa.seq_num;

			sa_stats->esn_enabled = nim->msg.stats.sa.esn_enabled;
			sa_stats->window_max = nim->msg.stats.sa.window_max;
			sa_stats->window_size = nim->msg.stats.sa.window_size;

			sa_stats->pkts.count = nim->msg.stats.sa.count;
			sa_stats->pkts.bytes = nim->msg.stats.sa.bytes;

			cb_fn(priv->tun_entry.app_data, &stats_event);
		}

		break;

	case NSS_IPSEC_MSG_TYPE_SYNC_NODE_STATS:

		write_lock(&ipsecmgr_ctx->lock);

		drv_stats = &ipsecmgr_ctx->enc_stats;
		if (unlikely(nim->type == NSS_IPSEC_TYPE_DECAP)) {
			drv_stats = &ipsecmgr_ctx->dec_stats;
		}

		node_stats = &nim->msg.stats.node;
		drv_stats->enqueued += node_stats->enqueued;
		drv_stats->completed += node_stats->completed;
		drv_stats->linearized += node_stats->linearized;
		drv_stats->exceptioned += node_stats->exceptioned;
		drv_stats->fail_enqueue += node_stats->fail_enqueue;

		write_unlock(&ipsecmgr_ctx->lock);

		break;

	default:
		break;
	}
done:
	dev_put(dev);
}

/*
 * nss_ipsecmgr_node_stats_read()
 * 	read node statistics
 */
static ssize_t nss_ipsecmgr_node_stats_read(struct file *fp, char __user *ubuf, size_t sz, loff_t *ppos)
{
	struct nss_ipsecmgr_node_stats *enc_stats = &ipsecmgr_ctx->enc_stats;
	struct nss_ipsecmgr_node_stats *dec_stats = &ipsecmgr_ctx->dec_stats;
	ssize_t ret = 0;
	char *local;
	int len;

	local = vmalloc(NSS_IPSECMGR_MAX_BUF_SZ);

	len = 0;
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tencap_enqueued: %lld\n", enc_stats->enqueued);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tencap_completed: %lld\n", enc_stats->completed);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tencap_exceptioned: %lld\n", enc_stats->exceptioned);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tencap_enqueue_failed: %lld\n", enc_stats->fail_enqueue);

	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_enqueued: %lld\n", dec_stats->enqueued);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_completed: %lld\n", dec_stats->completed);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_exceptioned: %lld\n", dec_stats->exceptioned);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_enqueue_failed: %lld\n", dec_stats->fail_enqueue);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_fail_sa: %lld\n", dec_stats->fail_sa);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "\tdecap_fail_loop: %lld\n", dec_stats->fail_loop);

	ret = simple_read_from_buffer(ubuf, sz, ppos, local, len + 1);

	vfree(local);

	return ret;
}

/*
 * file operation structure instance
 */
static const struct file_operations node_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_node_stats_read,
};

/*
 * nss_ipsecmgr_tunnel_add()
 * 	add a IPsec pseudo tunnel device
 */
struct net_device *nss_ipsecmgr_tunnel_add(struct nss_ipsecmgr_callback *cb)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct nss_ipsecmgr_priv *priv;
	struct net_device *dev;
	int status;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	dev = alloc_netdev(sizeof(struct nss_ipsecmgr_priv), NSS_IPSECMGR_TUN_NAME, nss_ipsecmgr_tunnel_setup);
#else
	dev = alloc_netdev(sizeof(struct nss_ipsecmgr_priv), NSS_IPSECMGR_TUN_NAME, NET_NAME_ENUM, nss_ipsecmgr_tunnel_setup);
#endif
	if (!dev) {
		nss_ipsecmgr_error("unable to allocate a tunnel device\n");
		return NULL;
	}

	priv = netdev_priv(dev);
	priv->dev = dev;

	INIT_LIST_HEAD(&priv->tun_entry.node);
	priv->tun_entry.dev_index = dev->ifindex;
	priv->tun_entry.app_data = cb->ctx;
	priv->tun_entry.data = cb->data_fn;
	priv->tun_entry.event = cb->event_fn;


	status = rtnl_is_locked() ? register_netdevice(dev) : register_netdev(dev);
	if (status < 0) {
		nss_ipsecmgr_error("register net dev failed :%s\n", dev->name);
		goto fail;
	}

	write_lock_bh(&ipsecmgr_ctx->lock);
	list_add(&priv->tun_entry.node, &tun_db->entries);
	atomic_inc(&tun_db->num_entries);
	write_unlock_bh(&ipsecmgr_ctx->lock);
	return dev;
fail:
	free_netdev(dev);
	return NULL;
}
EXPORT_SYMBOL(nss_ipsecmgr_tunnel_add);


/*
 * nss_ipsecmgr_del_tunnel()
 * 	delete an existing IPsec tunnel
 */
bool nss_ipsecmgr_tunnel_del(struct net_device *dev)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct nss_ipsecmgr_priv *priv = netdev_priv(dev);

	write_lock_bh(&ipsecmgr_ctx->lock);
	atomic_dec(&tun_db->num_entries);
	list_del(&priv->tun_entry.node);
	write_unlock_bh(&ipsecmgr_ctx->lock);

	nss_ipsecmgr_sa_flush_all(priv);

	/*
	 * The unregister should start here but the expectation is that the free would
	 * happen when the reference count goes down to '0'
	 */
	rtnl_is_locked() ? unregister_netdevice(dev) : unregister_netdev(dev);

	/*
	 * Reset the index of outer interface.
	 */
	atomic_set(&priv->outer_ifindex, -1);
	atomic_set(&priv->is_vlan, false);

	return true;
}
EXPORT_SYMBOL(nss_ipsecmgr_tunnel_del);

/*
 * nss_ipsecmgr_tunnel_update_callback()
 * 	update the tunnel databse with the new device index
 *
 * Note: the tun_db is database that also holds callback functions w.r.t a
 * device index. The tunnel_add would typically load this with the device
 * index of the tunnel. Overtime the caller can decide to update this with
 * different device index. In cases where the IPsec stack typically creates
 * a tunnel device of its own (KLIPS), the callback would then get mapped to
 * these devices instead of the IPsec manager created tunnel netdevice
 */
void nss_ipsecmgr_tunnel_update_callback(struct net_device *tun, struct net_device *cur)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct nss_ipsecmgr_tunnel_entry *tun_entry;
	int tun_dev_index = tun->ifindex;

	if (!atomic_read(&tun_db->num_entries))
		return;

	/*
	 * search the old device index in callback table
	 * and replace it with the new index
	 */
	write_lock_bh(&ipsecmgr_ctx->lock);
	list_for_each_entry(tun_entry, &tun_db->entries, node) {
		if (tun_entry->dev_index == tun_dev_index) {
			tun_entry->dev_index = cur->ifindex;
			break;
		}
	}

	write_unlock_bh(&ipsecmgr_ctx->lock);
}
EXPORT_SYMBOL(nss_ipsecmgr_tunnel_update_callback);

static const struct net_device_ops nss_ipsecmgr_ipsec_ndev_ops;

/*
 * nss_ipsecmgr_dummy_netdevice_setup()
 *	Setup function for dummy netdevice.
 */
static void nss_ipsecmgr_dummy_netdevice_setup(struct net_device *dev)
{
}

/*
 * flow file operation structure instance
 */
static const struct file_operations flow_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_flow_stats_read,
};

/*
 * per flow file operation structure instance
 */
static const struct file_operations per_flow_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_per_flow_stats_read,
	.write = nss_ipsecmgr_per_flow_stats_write,
};

/*
 * SA file operation structure instance
 */
static const struct file_operations sa_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_sa_stats_read,
};

/*
 * subnet file operation structure instance
 */
static const struct file_operations subnet_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_netmask_stats_read,
};

/*
 * nss_ipsecmgr_init_stats_debugfs()
 *	Initialize the debugfs tree.
 */
static int nss_ipsecmgr_init_stats_debugfs(struct dentry *stats_root)
{
	if (!debugfs_create_file("subnet", S_IRUGO, stats_root, (uint32_t *)NULL, &subnet_stats_op)) {
		nss_ipsecmgr_error("Debugfs file creation failed for subnet\n");
		return -1;
	}

	if (!debugfs_create_file("sa", S_IRUGO, stats_root, NULL, &sa_stats_op)) {
		nss_ipsecmgr_error("Debugfs file creation failed for SA\n");
		return -1;
	}

	if (!debugfs_create_file("flow", S_IRUGO, stats_root, NULL, &flow_stats_op)) {
		nss_ipsecmgr_error("Debugfs file creation failed for flow\n");
		return -1;
	}

	if (!debugfs_create_file("per_flow", S_IRWXUGO, stats_root, NULL, &per_flow_stats_op)) {
		nss_ipsecmgr_error("Debugfs file creation failed for per flow\n");
		return -1;
	}

	if (!debugfs_create_file("node", S_IRUGO, stats_root, NULL, &node_stats_op)) {
		nss_ipsecmgr_error("Debugfs file creation failed for per node\n");
		return -1;
	}

	return 0;
}

#if defined NSS_IPSECMGR_PMTU_SUPPORT

/*
 * nss_ipsecmgr_update_pmtu_v4()
 * 	PMTU notification handler for IPv4
 */
static int nss_ipsecmgr_update_pmtu_v4(struct notifier_block *this, unsigned long code, void *ptr)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct sk_buff *skb = (struct sk_buff *)ptr;
	struct nss_ipsecmgr_tunnel_entry *entry;
	struct nss_ipsecmgr_priv *priv;
	struct dst_entry *dst;
	struct icmphdr *icmph;
	struct iphdr *iph;
	uint16_t mtu;

	iph = (struct iphdr *)skb->data;
	icmph = icmp_hdr(skb);

	BUG_ON(!in_atomic());

	/*
	 * We are interested only in PMTU message
	 */
	if (code != ICMP_FRAG_NEEDED)
		return 0;

	mtu = ntohs(icmph->un.frag.mtu);

	/*
	 * XFRM will neglect the PMTU ICMP message, since the
	 * SA is not known. So update the MTU in dst.
	 */
	if (iph->protocol == IPPROTO_ESP)
		ipv4_update_pmtu(skb, dev_net(skb->dev), mtu, 0, 0, IPPROTO_ESP, 0);

	/*
	 * lookup dst for the destination address
	 */
	dst = (struct dst_entry *)ip_route_output(&init_net, iph->daddr, 0, 0, 0);
	if (IS_ERR(dst))
		goto exit;

	/*
	 * retrieve the tunnel priv corresponding to the destination address
	 */
	read_lock(&ipsecmgr_ctx->lock);
	list_for_each_entry(entry, &tun_db->entries, node) {
		if (entry->dest_addr[0] == iph->daddr) {
			/*
			 * set MTU from the dst to tunnel private structure
			 */
			priv = container_of(entry, struct nss_ipsecmgr_priv, tun_entry);
			atomic_set(&priv->outer_dst_mtu, dst_mtu(dst));
		}
	}

exit:
	dst_release(dst);
	read_unlock(&ipsecmgr_ctx->lock);
	return 0;
}

/*
 * nss_ipsecmgr_update_pmtu_v6()
 * 	PMTU notification handler for IPv6
 */
static int nss_ipsecmgr_update_pmtu_v6 (struct notifier_block *this, unsigned long code, void *ptr)
{
	struct nss_ipsecmgr_tunnel_db *tun_db = &ipsecmgr_ctx->tun_db;
	struct sk_buff *skb = (struct sk_buff *)ptr;
	struct nss_ipsecmgr_tunnel_entry *entry;
	struct nss_ipsecmgr_priv *priv;
	struct icmp6hdr *icmph;
	struct dst_entry *dst;
	struct ipv6hdr *iph;
	struct flowi6 fl6;

	iph = (struct ipv6hdr *)skb->data;
	icmph = icmp6_hdr(skb);

	BUG_ON(!in_atomic());

	/*
	 * XFRM will neglect the PMTU ICMP message, since the
	 * SA is not known. So update the MTU in dst.
	 */
	if (iph->nexthdr == IPPROTO_ESP)
		ip6_update_pmtu(skb, dev_net(skb->dev), icmph->icmp6_mtu, 0, 0);

	/*
	 * lookup dst for the destination address
	 */
	memcpy(&fl6.daddr, &iph->daddr, sizeof(fl6.daddr));
	dst = ip6_route_output(&init_net, NULL, &fl6);
	if (dst->error) {
		dst_release(dst);
		return 0;
	}

	/*
	 * retrieve the tunnel corresponding to the destination address
	 */
	read_lock(&ipsecmgr_ctx->lock);
	list_for_each_entry(entry, &tun_db->entries, node) {
		if (!memcmp(entry->dest_addr, &iph->daddr, sizeof(iph->daddr))) {
			/*
			 * set MTU from the dst to tunnel private structure
			 */
			priv = container_of(entry, struct nss_ipsecmgr_priv, tun_entry);
			atomic_set(&priv->outer_dst_mtu, dst_mtu(dst));
		}
	}

	dst_release(dst);
	read_unlock(&ipsecmgr_ctx->lock);
	return 0;
}

/*
 * register notifier for PMTU callback
 */
static struct notifier_block nss_ipsecmgr_pmtu_notify_handler_v4 = {
	.notifier_call = nss_ipsecmgr_update_pmtu_v4,
};

static struct notifier_block nss_ipsecmgr_pmtu_notify_handler_v6 = {
	.notifier_call = nss_ipsecmgr_update_pmtu_v6,
};

/*
 * nss_ipsecmgr_tunnel_rx_esp4()
 *	IPv4 Receive handler for ESP protocol
 */
static int nss_ipsecmgr_tunnel_rx_esp4(struct sk_buff *skb)
{
	struct ip_esp_hdr *esph = ip_esp_hdr(skb);
	struct nss_ipsecmgr_sa_entry *sa;
	struct iphdr *iph = ip_hdr(skb);
	struct nss_ipsec_tuple tuple;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;

	/*
	 * push the data pointer to network header
	 */
	skb_push(skb, sizeof(struct iphdr));

	/*
	 * check whether SA for this SKB is present in SAD
	 * if there are no rules, then send the packet along
	 */
	nss_ipsecmgr_v4_hdr2tuple(iph, &tuple);
	tuple.esp_spi = ntohl(esph->spi);
	nss_ipsecmgr_sa_tuple2key(&tuple, &key);

	ref = nss_ipsecmgr_sa_lookup(&key);
	if (!ref) {
		nss_ipsecmgr_info("unable to find SA (%p)\n", skb);
		write_lock(&ipsecmgr_ctx->lock);
		ipsecmgr_ctx->dec_stats.fail_sa++;
		write_unlock(&ipsecmgr_ctx->lock);
		goto drop;
	}

	/*
	 * Clear DST references before giving packet to NSS for decapsulation.
	 * As NSS will strip of the outer IP header
	 */
	skb_dst_drop(skb);

	/*
	 * NSS can exception this packet, if the SKB doesnot match any of SAD entry.
	 * So, stamp the packet to discriminate when NSS exceptions without
	 * decapsulating.
	 */
	nss_ipsecmgr_rx_init_loop(skb);

	/*
	 * send packet to NSS for decapsulation
	 */
	if (nss_ipsec_tx_buf(skb, ipsecmgr_ctx->decap_ifnum) != NSS_TX_SUCCESS) {
		nss_ipsecmgr_info("unable to queue the buffer to NSS (%p)\n", skb);

		/*
		 * update the stats in SA
		 */
		sa = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);
		write_lock(&ipsecmgr_ctx->lock);
		sa->pkts.fail_queue_h2n++;
		write_unlock(&ipsecmgr_ctx->lock);

		nss_ipsecmgr_rx_reset_loop(skb);
		goto drop;
	}

	return 0;

drop:
	dev_kfree_skb_any(skb);
	return 0;
}

/*
 * nss_ipsecmgr_esp4_err()
 *	IPv4 Error handler for ESP protocol
 */
static void nss_ipsecmgr_tunnel_error_esp4(struct sk_buff *skb, uint32_t mtu)
{
	/*
	 * Any error related to ESP packets will land here.
	 * Since PMTU error is handled in generic implementation
	 * we simply return from here.
	 */
	return;
}

/*
 * protocol handler for IPv4 ESP
 */
static const struct net_protocol nss_ipsecmgr_proto_esp4 = {
	.handler     = nss_ipsecmgr_tunnel_rx_esp4,
	.err_handler = nss_ipsecmgr_tunnel_error_esp4,
	.netns_ok    = 1,
};

/*
 * nss_ipsecmgr_tunnel_x_resp6()
 *	IPV6 Receive handler for ESP protocol
 */
static int nss_ipsecmgr_tunnel_rx_esp6(struct sk_buff *skb)
{
	struct ipv6hdr *ip6h = ipv6_hdr(skb);
	struct nss_ipsecmgr_sa_entry *sa;
	struct nss_ipsec_tuple tuple;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;
	struct ip_esp_hdr *esph = ip_esp_hdr(skb);

	/*
	 * push the data pointer to network header
	 */
	skb_push(skb, sizeof(struct ipv6hdr));

	/*
	 * check whether SA for this SKB is present in SAD
	 * if there are no rules, then send the packet along
	 */
	nss_ipsecmgr_v6_hdr2tuple(ip6h, &tuple);
	tuple.esp_spi = ntohl(esph->spi);
	nss_ipsecmgr_sa_tuple2key(&tuple, &key);

	ref = nss_ipsecmgr_sa_lookup(&key);
	if (!ref) {
		nss_ipsecmgr_info("unable to find SA (%p)\n", skb);
		write_lock(&ipsecmgr_ctx->lock);
		ipsecmgr_ctx->dec_stats.fail_sa++;
		write_unlock(&ipsecmgr_ctx->lock);
		goto drop;
	}

	/*
	 * Clear DST references before giving packet to NSS for decapsulation.
	 * As NSS will strip of the outer IP header
	 */
	skb_dst_drop(skb);

	/*
	 * NSS can exception this packet, if the SKB doesnot match any of SAD entry.
	 * So, stamp the packet to discriminate when NSS exceptions without
	 * decapsulating.
	 */
	nss_ipsecmgr_rx_init_loop(skb);

	/*
	 * send packet to NSS for decapsulation
	 */
	if (nss_ipsec_tx_buf(skb, ipsecmgr_ctx->decap_ifnum) != NSS_TX_SUCCESS) {
		nss_ipsecmgr_info("unable to queue the buffer to NSS (%p)\n", skb);

		/*
		 * update the stats in SA
		 */
		sa = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);
		write_lock(&ipsecmgr_ctx->lock);
		sa->pkts.fail_queue_h2n++;
		write_unlock(&ipsecmgr_ctx->lock);

		nss_ipsecmgr_rx_reset_loop(skb);
		goto drop;
	}

	return 0;

drop:
	dev_kfree_skb_any(skb);
	return 0;
}

/*
 * nss_ipsecmgr_tunnel_error_esp6()
 *	IPv4 Error handler for ESP protocol
 */
static void nss_ipsecmgr_tunnel_error_esp6(struct sk_buff *skb, struct inet6_skb_parm *opt, uint8_t type,
					uint8_t code, int32_t offset, uint32_t mtu)
{
	/*
	 * Any error related to ESP packets will land here.
	 * Since PMTU error is handled in generic implementation
	 * we simple return from here.
	 */
	return;
}

/*
 * protocol handler for IPv6 ESP
 */
static struct inet6_protocol nss_ipsecmgr_proto_esp6 = {
	.handler        =       nss_ipsecmgr_tunnel_rx_esp6,
	.err_handler    =       nss_ipsecmgr_tunnel_error_esp6,
	.flags          =       INET6_PROTO_NOPOLICY,
};
#endif

/*
 * nss_ipsecmgr_init()
 *	module init
 */
static int __init nss_ipsecmgr_init(void)
{
	int status;

	if (!nss_cmn_get_nss_enabled()) {
		nss_ipsecmgr_info_always("NSS is not enabled in this platform\n");
		return 0;
	}

	ipsecmgr_ctx = vzalloc(sizeof(struct nss_ipsecmgr_drv));
	if (!ipsecmgr_ctx) {
		nss_ipsecmgr_info_always("Allocating ipsecmgr context failed\n");
		return 0;
	}

	ipsecmgr_ctx->nss_ctx = nss_ipsec_get_context();
	if (!ipsecmgr_ctx->nss_ctx) {
		nss_ipsecmgr_info_always("Getting NSS Context failed\n");
		goto free;
	}

	ipsecmgr_ctx->data_ifnum = nss_ipsec_get_data_interface();
	ipsecmgr_ctx->encap_ifnum = nss_ipsec_get_encap_interface();
	ipsecmgr_ctx->decap_ifnum = nss_ipsec_get_decap_interface();

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 17, 0)
	ipsecmgr_ctx->ndev = alloc_netdev(0, NSS_IPSECMGR_DEFAULT_TUN_NAME, nss_ipsecmgr_dummy_netdevice_setup);
#else
	ipsecmgr_ctx->ndev = alloc_netdev(0, NSS_IPSECMGR_DEFAULT_TUN_NAME, NET_NAME_UNKNOWN, nss_ipsecmgr_dummy_netdevice_setup);
#endif
	if (!ipsecmgr_ctx->ndev) {
		nss_ipsecmgr_info_always("Ipsec: Could not allocate ipsec net_device\n");
		goto free;
	}

	ipsecmgr_ctx->ndev->netdev_ops = &nss_ipsecmgr_ipsec_ndev_ops;

	status = rtnl_is_locked() ? register_netdevice(ipsecmgr_ctx->ndev) : register_netdev(ipsecmgr_ctx->ndev);
	if (status) {
		nss_ipsecmgr_info_always("IPsec: Could not register ipsec net_device\n");
		goto netdev_free;
	}

	rwlock_init(&ipsecmgr_ctx->lock);
	nss_ipsecmgr_init_sa_db(&ipsecmgr_ctx->sa_db);
	nss_ipsecmgr_init_netmask_db(&ipsecmgr_ctx->net_db);
	nss_ipsecmgr_init_flow_db(&ipsecmgr_ctx->flow_db);
	nss_ipsecmgr_init_tunnel_db(&ipsecmgr_ctx->tun_db);

	nss_ipsec_data_register(ipsecmgr_ctx->data_ifnum, nss_ipsecmgr_tunnel_rx, ipsecmgr_ctx->ndev, 0);
	nss_ipsec_notify_register(ipsecmgr_ctx->encap_ifnum, nss_ipsecmgr_tunnel_notify, NULL);
	nss_ipsec_notify_register(ipsecmgr_ctx->decap_ifnum, nss_ipsecmgr_tunnel_notify, NULL);

	/*
	 * initialize debugfs.
	 */
	ipsecmgr_ctx->dentry = debugfs_create_dir("qca-nss-ipsecmgr", NULL);
	if (!ipsecmgr_ctx->dentry) {
		nss_ipsecmgr_info_always("Creating debug directory failed\n");
		goto unregister_dev;

	}

	ipsecmgr_ctx->stats_dentry = debugfs_create_dir("stats", ipsecmgr_ctx->dentry);
	if (!ipsecmgr_ctx->stats_dentry) {
		nss_ipsecmgr_info("Creating debug directory failed\n");
		goto debugfs_free;

	}

	/*
	 * Create debugfs entries for SA, flow and subnet
	 */
	if (nss_ipsecmgr_init_stats_debugfs(ipsecmgr_ctx->stats_dentry)) {
		nss_ipsecmgr_info("Creating debug tree failed\n");
		goto debugfs_free;

	}

#if defined NSS_IPSECMGR_PMTU_SUPPORT
	if (icmp_register_notifier(&nss_ipsecmgr_pmtu_notify_handler_v4, ICMP_DEST_UNREACH)) {
		nss_ipsecmgr_info("Failed to register for IPv4 PMTU notifier\n");
		goto debugfs_free;
	}

	icmpv6_register_notifier(&nss_ipsecmgr_pmtu_notify_handler_v6, ICMPV6_PKT_TOOBIG);

	/*
	 * Over-ride the default IPv4 ESP protocol handler with IPsecmgr handler
	 */
	status = inet_add_protocol(&nss_ipsecmgr_proto_esp4, IPPROTO_ESP);
	if (status < 0) {
		nss_ipsecmgr_info("Failed to register protocol handler for ESP-4\n");
		icmpv6_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v6, ICMPV6_PKT_TOOBIG);
		icmp_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v4, ICMP_DEST_UNREACH);
		goto debugfs_free;
	}

	/*
	 * Over-ride the default IPv6 ESP protocol handler with IPsecmgr handler
	 */
	status = inet6_add_protocol(&nss_ipsecmgr_proto_esp6, IPPROTO_ESP);
	if (status < 0) {
		nss_ipsecmgr_info("Failed to register protocol handler for ESP-6\n");
		icmpv6_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v6, ICMPV6_PKT_TOOBIG);
		icmp_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v4, ICMP_DEST_UNREACH);
		inet_del_protocol(&nss_ipsecmgr_proto_esp4, IPPROTO_ESP);
		goto debugfs_free;
	}

#endif

	init_completion(&ipsecmgr_ctx->complete);
	sema_init(&ipsecmgr_ctx->sem, 1);
	atomic_set(&ipsecmgr_ctx->seq_num, 0);

	nss_ipsecmgr_info_always("NSS IPsec manager loaded: %s\n", NSS_CLIENT_BUILD_ID);
	return 0;

debugfs_free:
	debugfs_remove_recursive(ipsecmgr_ctx->dentry);

unregister_dev:
	rtnl_is_locked() ? unregister_netdevice(ipsecmgr_ctx->ndev) : unregister_netdev(ipsecmgr_ctx->ndev);

netdev_free:
	free_netdev(ipsecmgr_ctx->ndev);

free:
	vfree(ipsecmgr_ctx);
	ipsecmgr_ctx = NULL;

	return 0;
}

/*
 * nss_ipsecmgr_exit()
 * 	module exit
 */
static void __exit nss_ipsecmgr_exit(void)
{
	if (!ipsecmgr_ctx) {
		nss_ipsecmgr_info_always("Invalid ipsecmgr Context\n");
		return;
	}

	if (!ipsecmgr_ctx->nss_ctx) {
		nss_ipsecmgr_info_always("Invalid NSS Context\n");
		vfree(ipsecmgr_ctx);
		ipsecmgr_ctx = NULL;
		return;
	}

	/*
	 * Unregister the callbacks from the HLOS as we are no longer
	 * interested in exception data & async messages
	 */
	nss_ipsec_data_unregister(ipsecmgr_ctx->nss_ctx, ipsecmgr_ctx->data_ifnum);

	nss_ipsec_notify_unregister(ipsecmgr_ctx->nss_ctx, ipsecmgr_ctx->encap_ifnum);
	nss_ipsec_notify_unregister(ipsecmgr_ctx->nss_ctx, ipsecmgr_ctx->decap_ifnum);

#if defined NSS_IPSECMGR_PMTU_SUPPORT
	icmp_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v4, ICMP_DEST_UNREACH);
	icmpv6_unregister_notifier(&nss_ipsecmgr_pmtu_notify_handler_v6, ICMPV6_PKT_TOOBIG);

	inet_del_protocol(&nss_ipsecmgr_proto_esp4, IPPROTO_ESP);
	inet6_del_protocol(&nss_ipsecmgr_proto_esp6, IPPROTO_ESP);
#endif

	if (ipsecmgr_ctx->ndev) {
		rtnl_is_locked() ? unregister_netdevice(ipsecmgr_ctx->ndev) : unregister_netdev(ipsecmgr_ctx->ndev);
	}

	/*
	 * Remove debugfs directory and entries below that.
	 */
	if (ipsecmgr_ctx->dentry) {
		debugfs_remove_recursive(ipsecmgr_ctx->dentry);
	}

	/*
	 * Free the ipsecmgr ctx
	 */
	vfree(ipsecmgr_ctx);
	ipsecmgr_ctx = NULL;

	nss_ipsecmgr_info_always("NSS IPsec manager unloaded\n");

}

MODULE_LICENSE("Dual BSD/GPL");

module_init(nss_ipsecmgr_init);
module_exit(nss_ipsecmgr_exit);
