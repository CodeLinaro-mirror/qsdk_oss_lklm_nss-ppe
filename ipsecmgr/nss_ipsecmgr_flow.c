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
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTUOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/of.h>
#include <linux/ipv6.h>
#include <linux/skbuff.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/rtnetlink.h>
#include <asm/atomic.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/vmalloc.h>

#include <nss_api_if.h>
#include <nss_ipsec.h>
#include <nss_ipsecmgr.h>
#include <nss_crypto_if.h>

#include "nss_ipsecmgr_priv.h"

#define NSS_IPSECMGR_SYNC_STATS_TIMEOUT 10

/*
 * nss_ipsecmgr_flow_lookup()
 * 	lookup flow in flow_db
 */
static struct nss_ipsecmgr_ref *nss_ipsecmgr_flow_name_lookup(struct nss_ipsecmgr_priv *priv, const char *name)
{
	struct nss_ipsecmgr_flow_db *db = &priv->flow_db;
	struct nss_ipsecmgr_flow_entry *entry;
	struct list_head *head;
	char *flow_name;
	uint32_t hash;
	int idx;

	flow_name = strchr(name, '@');
	if (!flow_name || hex2bin((uint8_t *)&hash, ++flow_name, sizeof(uint32_t))) {
		nss_ipsecmgr_error("i%p: Invalid input\n", priv);
		return NULL;
	}

	idx = hash & (NSS_IPSECMGR_MAX_FLOW - 1);
	if (idx >= NSS_IPSECMGR_MAX_FLOW) {
		return NULL;
	}

	head = &db->entries[idx];
	list_for_each_entry(entry, head, node) {
		if (nss_ipsecmgr_key_get_hash(&entry->key) == hash) {
			return &entry->ref;
		}
	}

	return NULL;
}

/*
 *
 * nss_ipsecmgr_flow_resp()
 * 	response for the flow message
 *
 * Note: we don't have anything to process for flow responses as of now
 */
static void nss_ipsecmgr_flow_resp(void *app_data, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_flow_entry *flow __attribute__((unused)) = app_data;

	return;
}

/*
 * nss_ipsecmgr_flow_update()
 * 	update the flow with its associated data and notify NSS
 */
static void nss_ipsecmgr_flow_update(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_flow_entry *flow;
	struct nss_ipsec_rule_sel local_sel;
	struct nss_ipsec_rule_sel *flow_sel;

	flow = container_of(ref, struct nss_ipsecmgr_flow_entry, ref);
	flow_sel = &flow->nim.msg.push.sel;

	/*
	 * Create a local copy of flow selector
	 */
	memcpy(&local_sel, flow_sel, sizeof(struct nss_ipsec_rule_sel));
	memcpy(&flow->nim, nim, sizeof(struct nss_ipsec_msg));

	/*
	 * If, this flow is only getting updated with a new SA contents. We
	 * need to make sure that the existing selector remains the same
	 */
	if (nss_ipsecmgr_ref_is_updated(ref)) {
		memcpy(flow_sel, &local_sel, sizeof(struct nss_ipsec_rule_sel));
	}

	if (nss_ipsec_tx_msg(priv->nss_ctx, &flow->nim) != NSS_TX_SUCCESS) {
		/*
		 * XXX: Stop the TX queue and add this "entry"
		 * to pending queue
		 */
		nss_ipsecmgr_error("%p:unable to send the flow_update message\n", ref);
		return;
	}
}

/*
 * nss_ipsecmgr_flow_free()
 * 	free the associated flow entry and notify NSS
 */
static void nss_ipsecmgr_flow_free(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref)
{
	struct nss_ipsecmgr_flow_entry *flow = container_of(ref, struct nss_ipsecmgr_flow_entry, ref);

	/*
	 * update the common message structure
	 */
	flow->nim.cm.type = NSS_IPSEC_MSG_TYPE_DEL_RULE;

	if (nss_ipsec_tx_msg(priv->nss_ctx, &flow->nim) != NSS_TX_SUCCESS) {
		/*
		 * XXX: add this "entry" to pending queue
		 */
		nss_ipsecmgr_error("%p:unable to flow_free message\n", ref);
		return;
	}

	/*
	 * free all associated resources
	 */
	debugfs_remove_recursive(nss_ipsecmgr_ref_get_dentry(ref));

	list_del_init(&flow->node);
	kfree(flow);
}

/*
 * nss_ipsecmgr_flow_stats_resp()
 * 	response for the flow message
 *
 * Note: we don't have anything to process for flow responses as of now
 */
static void nss_ipsecmgr_flow_stats_resp(void *app_data, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_priv *priv = app_data;
	struct nss_ipsecmgr_flow_entry *flow;
	struct nss_ipsec_rule_sel *sel;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;
	struct net_device *dev;
	uint32_t interface;

	if (nim->cm.response != NSS_CMN_RESPONSE_ACK) {
		return;
	}

	dev = dev_get_by_index(&init_net, nim->tunnel_id);
	if (!dev || (netdev_priv(dev) != priv)) {
		return;
	}

	interface = nim->cm.interface;
	sel = &nim->msg.flow_stats.sel;

	/*
	 * prepare key from selector
	 */
	switch (interface) {
	case NSS_IPSEC_ENCAP_IF_NUMBER:
		nss_ipsecmgr_encap_sel2key(sel, &key);
		break;

	case NSS_IPSEC_DECAP_IF_NUMBER:
		nss_ipsecmgr_decap_sel2key(sel, &key);
		break;

	default:
		goto done;
	}

	/*
	 * lookup and copy incoming stats to flow
	 */
	write_lock(&priv->lock);

	ref = nss_ipsecmgr_flow_lookup(priv, &key);
	if (!ref) {
		write_unlock(&priv->lock);
		nss_ipsecmgr_error("Flow deleted during stat update \n");
		goto done;
	}

	flow = container_of(ref, struct nss_ipsecmgr_flow_entry, ref);
	flow->pkts_processed = nim->msg.flow_stats.processed;

	write_unlock(&priv->lock);

	complete(&priv->complete);
done:
	dev_put(dev);
	return;
}

/*
 * nss_ipsecmgr_flow_stats_read()
 * 	read flow statistics
 */
static ssize_t nss_ipsecmgr_flow_stats_read(struct file *fp, char __user *ubuf, size_t sz, loff_t *ppos)
{
	struct dentry *parent = dget_parent(fp->f_dentry);
	uint32_t tunnel_id = (uint32_t)fp->private_data;
	struct nss_ipsecmgr_flow_entry *flow;
	struct nss_ipsec_rule_sel *flow_sel;
	struct nss_ipsecmgr_priv *priv;
	struct nss_ipsecmgr_ref *ref;
	struct nss_ipsecmgr_key key;
	struct nss_ipsec_msg nim;
	struct net_device *dev;
	uint16_t interface;
	uint32_t addr[4];
	ssize_t ret = 0;
	char *local;
	char *type;
	int len;

	dev = dev_get_by_index(&init_net, tunnel_id);
	if (!dev) {
		return 0;
	}

	priv = netdev_priv(dev);

	read_lock_bh(&priv->lock);

	ref = nss_ipsecmgr_flow_name_lookup(priv, parent->d_name.name);
	if (!ref) {
		read_unlock_bh(&priv->lock);
		nss_ipsecmgr_error("flow not found tunnel-id: %d\n", tunnel_id);
		goto done;
	}

	flow = container_of(ref, struct nss_ipsecmgr_flow_entry, ref);

	/*
	 * prepare IPsec message
	 */
	interface = flow->nim.cm.interface;
	memset(&nim, 0, sizeof(struct nss_ipsec_msg));
	nss_ipsec_msg_init(&nim, /* message */
			   interface, /* interface no */
			   NSS_IPSEC_MSG_TYPE_SYNC_FLOW_STATS, /* message type */
			   NSS_IPSEC_MSG_LEN, /* message length */
			   nss_ipsecmgr_flow_stats_resp, /* response callback */
			   priv); /* app_data */

	nim.tunnel_id = flow->nim.tunnel_id;

	/*
	 * copy selector and key
	 */
	memcpy(&nim.msg.flow_stats.sel, &flow->nim.msg.push.sel, sizeof(struct nss_ipsec_rule_sel));
	memcpy(&key, &flow->key, sizeof(struct nss_ipsecmgr_key));

	read_unlock_bh(&priv->lock);

	/*
	 * send stats message to nss
	 */
	if (nss_ipsec_tx_msg(priv->nss_ctx, &nim) != NSS_TX_SUCCESS) {
		nss_ipsecmgr_error("nss tx msg error\n");
		goto done;
	}

	/*
	 * Blocking call, wait till we get ACK for this msg.
	 */
	ret = wait_for_completion_timeout(&priv->complete, msecs_to_jiffies(NSS_IPSECMGR_SYNC_STATS_TIMEOUT));
	if (!ret) {
		nss_ipsecmgr_error("nss stats message timed out \n");
		goto done;
	}

	local = vzalloc(NSS_IPSECMGR_MAX_BUF_SZ);
	if (!local) {
		nss_ipsecmgr_error("unable to allocate local buffer for tunnel-id: %d\n", tunnel_id);
		goto done;
	}

	/*
	 * After wait_for_completion, confirm if flow still exist
	 */
	read_lock_bh(&priv->lock);
	ref = nss_ipsecmgr_flow_lookup(priv, &key);
	if (!ref) {
		read_unlock_bh(&priv->lock);
		vfree(local);
		nss_ipsecmgr_error("flow not found tunnel-id: %d\n", tunnel_id);
		goto done;
	}

	flow = container_of(ref, struct nss_ipsecmgr_flow_entry, ref);
	flow_sel = &flow->nim.msg.push.sel;

	/*
	 * IPv4 Generel info
	 */
	switch (interface) {
	case NSS_IPSEC_ENCAP_IF_NUMBER:
		type = "encap";
		break;

	case NSS_IPSEC_DECAP_IF_NUMBER:
		type = "decap";
		break;
	default:
		type = "none";
		break;
	}

	len = 0;
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "type:%s\n", type);
	switch (flow_sel->ip_ver) {
	case NSS_IPSEC_IPVER_4:
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_ip: %pI4h\n", &flow_sel->dst_addr[0]);
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "src_ip: %pI4h\n", &flow_sel->src_addr[0]);
		break;

	case NSS_IPSEC_IPVER_6:
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_ip: %pI6c\n", nss_ipsecmgr_v6addr_ntohl(flow_sel->dst_addr, addr));
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "src_ip: %pI6c\n", nss_ipsecmgr_v6addr_ntohl(flow_sel->src_addr, addr));
		break;

	}

	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "proto: %d\n", flow_sel->proto_next_hdr);

	/*
	 * packet stats
	 */
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "processed: %d\n", flow->pkts_processed);

	read_unlock_bh(&priv->lock);

	ret = simple_read_from_buffer(ubuf, sz, ppos, local, len + 1);
	vfree(local);
done:
	dev_put(dev);
	return ret;
}

/*
 * nss_ipsecmgr_encap_flow_init()
 * 	initiallize the encap flow with a particular type
 */
void nss_ipsecmgr_encap_flow_init(struct nss_ipsec_msg *nim, enum nss_ipsec_msg_type type, struct nss_ipsecmgr_priv *priv)
{
	memset(nim, 0, sizeof(struct nss_ipsec_msg));
	nss_ipsec_msg_init(nim, NSS_IPSEC_ENCAP_IF_NUMBER, type, NSS_IPSEC_MSG_LEN, nss_ipsecmgr_flow_resp, priv->dev);
	nim->tunnel_id = priv->dev->ifindex;
}

/*
 * nss_ipsecmgr_decap_flow_init()
 * 	initiallize the decap flow with a particular type
 */
void nss_ipsecmgr_decap_flow_init(struct nss_ipsec_msg *nim, enum nss_ipsec_msg_type type, struct nss_ipsecmgr_priv *priv)
{
	memset(nim, 0, sizeof(struct nss_ipsec_msg));
	nss_ipsec_msg_init(nim, NSS_IPSEC_DECAP_IF_NUMBER, type, NSS_IPSEC_MSG_LEN, nss_ipsecmgr_flow_resp, priv->dev);
	nim->tunnel_id = priv->dev->ifindex;
}

/*
 * nss_ipsecmgr_copy_encap_v4_flow()
 * 	copy flow data into the selector
 */
void nss_ipsecmgr_copy_encap_v4_flow(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_encap_v4_tuple *flow)
{
	struct nss_ipsec_rule_sel *sel = &nim->msg.push.sel;

	sel->dst_addr[0] = flow->dst_ip;
	sel->src_addr[0] = flow->src_ip;
	sel->proto_next_hdr = flow->protocol;
	sel->ip_ver = NSS_IPSEC_IPVER_4;

	sel->esp_spi = 0;
	sel->dst_port = 0;
	sel->src_port = 0;
}

/*
 * nss_ipsecmgr_copy_decap_v4_flow()
 * 	copy decap flow
 */
void nss_ipsecmgr_copy_decap_v4_flow(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_v4 *flow)
{
	struct nss_ipsec_rule_sel *sel = &nim->msg.push.sel;

	sel->dst_addr[0] = flow->dst_ip;
	sel->src_addr[0] = flow->src_ip;
	sel->proto_next_hdr = IPPROTO_ESP;
	sel->esp_spi = flow->spi_index;
	sel->ip_ver = NSS_IPSEC_IPVER_4;

	sel->dst_port = 0;
	sel->src_port = 0;
}

/*
 * nss_ipsecmgr_encap_v4_flow2key()
 * 	convert an encap v4_flow into a key
 */
void nss_ipsecmgr_encap_v4_flow2key(struct nss_ipsecmgr_encap_v4_tuple *flow, struct nss_ipsecmgr_key *key)
{
	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 4 /* v4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, flow->protocol, NSS_IPSECMGR_KEY_POS_IP_PROTO);
	nss_ipsecmgr_key_write_32(key, flow->dst_ip, NSS_IPSECMGR_KEY_POS_IPV4_DST);
	nss_ipsecmgr_key_write_32(key, flow->src_ip, NSS_IPSECMGR_KEY_POS_IPV4_SRC);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV4_ENCAP_FLOW;
}

/*
 * nss_ipsecmgr_decap_v4_flow2key()
 * 	convert a decap flow into a key
 */
void nss_ipsecmgr_decap_v4_flow2key(struct nss_ipsecmgr_sa_v4 *flow, struct nss_ipsecmgr_key *key)
{
	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 4 /* v4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);
	nss_ipsecmgr_key_write_32(key, flow->dst_ip, NSS_IPSECMGR_KEY_POS_IPV4_DST);
	nss_ipsecmgr_key_write_32(key, flow->src_ip, NSS_IPSECMGR_KEY_POS_IPV4_SRC);
	nss_ipsecmgr_key_write_32(key, flow->spi_index, NSS_IPSECMGR_KEY_POS_IPV4_ESP_SPI);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV4_DECAP_FLOW;
}

/*
 * nss_ipsecmgr_encap_v4_sel2key()
 * 	convert a selector to key
 */
void nss_ipsecmgr_encap_sel2key(struct nss_ipsec_rule_sel *sel, struct nss_ipsecmgr_key *key)
{
	uint32_t i;

	nss_ipsecmgr_key_reset(key);
	switch (sel->ip_ver) {
	case NSS_IPSEC_IPVER_4:
		nss_ipsecmgr_key_write_8(key, 4 /* v4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
		nss_ipsecmgr_key_write_8(key, sel->proto_next_hdr, NSS_IPSECMGR_KEY_POS_IP_PROTO);
		nss_ipsecmgr_key_write_32(key, nss_ipsecmgr_get_v4addr(sel->dst_addr), NSS_IPSECMGR_KEY_POS_IPV4_DST);
		nss_ipsecmgr_key_write_32(key, nss_ipsecmgr_get_v4addr(sel->src_addr), NSS_IPSECMGR_KEY_POS_IPV4_SRC);

		key->len = NSS_IPSECMGR_KEY_LEN_IPV4_ENCAP_FLOW;
		break;

	case NSS_IPSEC_IPVER_6:
		nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
		nss_ipsecmgr_key_write_8(key, sel->proto_next_hdr, NSS_IPSECMGR_KEY_POS_IP_PROTO);

		for (i  = 0; i < 4; i++) {
			nss_ipsecmgr_key_write_32(key, sel->dst_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
			nss_ipsecmgr_key_write_32(key, sel->src_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
		}

		key->len = NSS_IPSECMGR_KEY_LEN_IPV6_ENCAP_FLOW;
		break;

	default:
		nss_ipsecmgr_warn("%p:Invalid selector\n", sel);
		return;
	}
}

/*
 * nss_ipsecmgr_decap_sel2key()
 * 	convert a selector to key
 */
void nss_ipsecmgr_decap_sel2key(struct nss_ipsec_rule_sel *sel, struct nss_ipsecmgr_key *key)
{
	uint32_t i;

	nss_ipsecmgr_key_reset(key);

	switch (sel->ip_ver) {
	case NSS_IPSEC_IPVER_4:
		nss_ipsecmgr_key_write_8(key, 4 /* v4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
		nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);
		nss_ipsecmgr_key_write_32(key, nss_ipsecmgr_get_v4addr(sel->dst_addr), NSS_IPSECMGR_KEY_POS_IPV4_DST);
		nss_ipsecmgr_key_write_32(key, nss_ipsecmgr_get_v4addr(sel->src_addr), NSS_IPSECMGR_KEY_POS_IPV4_SRC);
		nss_ipsecmgr_key_write_32(key, sel->esp_spi, NSS_IPSECMGR_KEY_POS_IPV4_ESP_SPI);

		key->len = NSS_IPSECMGR_KEY_LEN_IPV4_DECAP_FLOW;
		break;

	case NSS_IPSEC_IPVER_6:
		nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
		nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);

		for (i  = 0; i < 4; i++) {
			nss_ipsecmgr_key_write_32(key, sel->dst_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
			nss_ipsecmgr_key_write_32(key, sel->src_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
		}

		nss_ipsecmgr_key_write_32(key, sel->esp_spi, NSS_IPSECMGR_KEY_POS_IPV6_ESP_SPI);

		key->len = NSS_IPSECMGR_KEY_LEN_IPV6_DECAP_FLOW;
		break;

	default:
		nss_ipsecmgr_warn("%p:Invalid selector\n", sel);
		return;
	}
}

/*
 * nss_ipsecmgr_copy_encap_v6_flow()
 * 	copy flow data into the selector
 */
void nss_ipsecmgr_copy_encap_v6_flow(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_encap_v6_tuple *flow)
{
	struct nss_ipsec_rule_sel *sel = &nim->msg.push.sel;

	memcpy(sel->src_addr, flow->src_ip, sizeof(uint32_t) * 4);
	memcpy(sel->dst_addr, flow->dst_ip, sizeof(uint32_t) * 4);

	sel->proto_next_hdr = flow->next_hdr;
	sel->ip_ver = NSS_IPSEC_IPVER_6;

	sel->esp_spi = 0;
	sel->dst_port = 0;
	sel->src_port = 0;
}

/*
 * nss_ipsecmgr_copy_decap_v6_flow()
 * 	copy decap flow
 */
void nss_ipsecmgr_copy_decap_v6_flow(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_v6 *flow)
{
	struct nss_ipsec_rule_sel *sel = &nim->msg.push.sel;

	memcpy(sel->src_addr, flow->src_ip, sizeof(uint32_t) * 4);
	memcpy(sel->dst_addr, flow->dst_ip, sizeof(uint32_t) * 4);

	sel->esp_spi = flow->spi_index;
	sel->ip_ver = NSS_IPSEC_IPVER_6;

	sel->proto_next_hdr = IPPROTO_ESP;

	sel->dst_port = 0;
	sel->src_port = 0;
}

/*
 * nss_ipsecmgr_encap_v6_flow2key()
 * 	convert an encap v6_flow into a key
 */
void nss_ipsecmgr_encap_v6_flow2key(struct nss_ipsecmgr_encap_v6_tuple *flow, struct nss_ipsecmgr_key *key)
{
	uint32_t i;

	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, flow->next_hdr, NSS_IPSECMGR_KEY_POS_IP_PROTO);

	for (i  = 0; i < 4; i++) {
		nss_ipsecmgr_key_write_32(key, flow->dst_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
		nss_ipsecmgr_key_write_32(key, flow->src_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
	}

	key->len = NSS_IPSECMGR_KEY_LEN_IPV6_ENCAP_FLOW;
}

/*
 * nss_ipsecmgr_decap_v6_flow2key()
 * 	convert a decap flow into a key
 */
void nss_ipsecmgr_decap_v6_flow2key(struct nss_ipsecmgr_sa_v6 *flow, struct nss_ipsecmgr_key *key)
{
	uint32_t i;

	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);

	for (i  = 0; i < 4; i++) {
		nss_ipsecmgr_key_write_32(key, flow->dst_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
		nss_ipsecmgr_key_write_32(key, flow->src_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
	}

	nss_ipsecmgr_key_write_32(key, flow->spi_index, NSS_IPSECMGR_KEY_POS_IPV6_ESP_SPI);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV6_DECAP_FLOW;
}

/*
 * nss_ipsecmgr_flow_lookup()
 * 	lookup flow in flow_db
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_flow_lookup(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_flow_db *db = &priv->flow_db;
	struct nss_ipsecmgr_flow_entry *entry;
	struct list_head *head;
	int idx;

	idx = nss_ipsecmgr_key_data2idx(key, NSS_IPSECMGR_MAX_FLOW);
	head = &db->entries[idx];

	list_for_each_entry(entry, head, node) {
		if (nss_ipsecmgr_key_cmp(&entry->key, key)) {
			return &entry->ref;
		}
	}

	return NULL;
}

/*
 * file operation structure instance
 */
static const struct file_operations flow_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_flow_stats_read,
};

/*
 * nss_ipsecmgr_flow_alloc()
 * 	allocate a flow entry
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_flow_alloc(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	char hash_str[NSS_IPSECMGR_MAX_KEY_NAME] = {0};
	struct nss_ipsecmgr_flow_entry *flow;
	struct nss_ipsecmgr_flow_db *db;
	struct nss_ipsecmgr_ref *ref;
	struct dentry *dentry;
	int idx;

	/*
	 * flow lookup before allocating a new one
	 */
	ref = nss_ipsecmgr_flow_lookup(priv, key);
	if (ref) {
		return ref;
	}

	flow = kzalloc(sizeof(struct nss_ipsecmgr_flow_entry), GFP_ATOMIC);
	if (!flow) {
		nss_ipsecmgr_error("failed to alloc flow_entry\n");
		return NULL;
	}

	flow->priv = priv;
	ref = &flow->ref;

	/*
	 * add flow to the database
	 */
	db = &priv->flow_db;
	INIT_LIST_HEAD(&flow->node);

	idx = nss_ipsecmgr_key_data2idx(key, NSS_IPSECMGR_MAX_FLOW);
	memcpy(&flow->key, key, sizeof(struct nss_ipsecmgr_key));
	list_add(&flow->node, &db->entries[idx]);

	/*
	 * create a string from hash
	 */
	nss_ipsecmgr_key_hash2str(key, hash_str);

	/*
	 * initiallize the reference object
	 */
	nss_ipsecmgr_ref_init(ref, nss_ipsecmgr_flow_update, nss_ipsecmgr_flow_free);

	/*
	 * setup the debugfs entries
	 */
	nss_ipsecmgr_ref_update_name(ref, "flow@");
	nss_ipsecmgr_ref_update_name(ref, hash_str);

	/*
	 * we don't know the parent of this node now hence attach it to the root node
	 */
	dentry = debugfs_create_dir(nss_ipsecmgr_ref_get_name(ref), priv->dentry);
	debugfs_create_file("stats", S_IRUGO, dentry, (uint32_t *)priv->dev->ifindex, &flow_stats_op);

	nss_ipsecmgr_ref_set_dentry(ref, dentry);

	return ref;
}

/*
 * nss_ipsecmgr_flow_offload()
 * 	check if the flow can be offloaded to NSS for encapsulation
 */
bool nss_ipsecmgr_flow_offload(struct nss_ipsecmgr_priv *priv, struct sk_buff *skb)
{
	struct nss_ipsecmgr_ref *subnet_ref, *flow_ref;
	struct nss_ipsecmgr_key subnet_key, flow_key;
	struct nss_ipsec_rule_sel *sel;
	struct nss_ipsec_msg nim;

	nss_ipsecmgr_encap_flow_init(&nim, NSS_IPSEC_MSG_TYPE_ADD_RULE, priv);

	switch (skb->protocol) {
	case htons(ETH_P_IP):
		sel = &nim.msg.push.sel;

		nss_ipsecmgr_v4_hdr2sel(ip_hdr(skb), sel);
		nss_ipsecmgr_encap_sel2key(sel, &flow_key);

		/*
		 * flow lookup is done with read lock
		 */
		read_lock(&priv->lock);
		flow_ref = nss_ipsecmgr_flow_lookup(priv, &flow_key);
		read_unlock(&priv->lock);

		/*
		 * if flow is found then proceed with the TX
		 */
		if (flow_ref) {
			return true;
		}
		/*
		 * flow table miss results in lookup in the subnet table. If,
		 * a match is found then a rule is inserted in NSS for encapsulating
		 * this flow.
		 */
		nss_ipsecmgr_v4_subnet_sel2key(sel, &subnet_key);

		/*
		 * write lock as it can update the flow database
		 */
		write_lock(&priv->lock);

		subnet_ref = nss_ipsecmgr_v4_subnet_match(priv, &subnet_key);
		if (!subnet_ref) {
			write_unlock(&priv->lock);
			return false;
		}

		/*
		 * copy nim data from subnet entry
		 */
		nss_ipsecmgr_copy_v4_subnet(&nim, subnet_ref);

		/*
		 * if, the same flow was added in between then flow alloc will return the
		 * same flow. The only side affect of this will be NSS getting duplicate
		 * add requests and thus rejecting one of them
		 */

		flow_ref = nss_ipsecmgr_flow_alloc(priv, &flow_key);
		if (!flow_ref) {
			write_unlock(&priv->lock);
			return false;
		}

		/*
		 * add reference to subnet and trigger an update
		 */
		nss_ipsecmgr_ref_add(flow_ref, subnet_ref);
		nss_ipsecmgr_ref_update(priv, flow_ref, &nim);

		write_unlock(&priv->lock);

		break;

	case htons(ETH_P_IPV6):
		sel = &nim.msg.push.sel;

		nss_ipsecmgr_v6_hdr2sel((struct ipv6hdr *)skb_network_header(skb), sel);
		nss_ipsecmgr_encap_sel2key(sel, &flow_key);

		/*
		 * flow lookup is done with read lock
		 */
		read_lock(&priv->lock);
		flow_ref = nss_ipsecmgr_flow_lookup(priv, &flow_key);
		read_unlock(&priv->lock);

		/*
		 * if flow is found then proceed with the TX
		 */
		if (!flow_ref) {
			return false;
		}

		break;

	default:
		nss_ipsecmgr_warn("%p:protocol(%d) offload not supported\n", priv->dev, ntohs(skb->protocol));
		return false;
	}

	return true;
}

