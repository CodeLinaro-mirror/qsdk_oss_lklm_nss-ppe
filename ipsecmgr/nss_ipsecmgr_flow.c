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

extern struct nss_ipsecmgr_drv *ipsecmgr_ctx;

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
	struct nss_ipsec_msg nss_nim;

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

	/*
	 * Convert the message to NSS format
	 */
	nss_ipsecmgr_copy_nim(&flow->nim, &nss_nim);

	if (nss_ipsec_tx_msg(ipsecmgr_ctx->nss_ctx, &nss_nim) != NSS_TX_SUCCESS) {
		/*
		 * XXX: Stop the TX queue and add this "entry"
		 * to pending queue
		 */
		nss_ipsecmgr_info("%p:unable to send the flow_update message\n", ref);
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
	struct nss_ipsec_msg nss_nim;

	/*
	 * update the common message structure
	 */
	flow->nim.cm.type = NSS_IPSEC_MSG_TYPE_DEL_RULE;

	/*
	 * Convert the message to NSS format
	 */
	nss_ipsecmgr_copy_nim(&flow->nim, &nss_nim);

	if (nss_ipsec_tx_msg(ipsecmgr_ctx->nss_ctx, &nss_nim) != NSS_TX_SUCCESS) {
		/*
		 * XXX: add this "entry" to pending queue
		 */
		nss_ipsecmgr_info("%p:unable to send flow_free message\n", ref);
	}

	list_del_init(&flow->node);
	kfree(flow);
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
	struct nss_ipsecmgr_flow_db *db = &ipsecmgr_ctx->flow_db;
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
 * nss_ipsecmgr_flow_alloc()
 * 	allocate a flow entry
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_flow_alloc(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_flow_entry *flow;
	struct nss_ipsecmgr_flow_db *db;
	struct nss_ipsecmgr_ref *ref;
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
		nss_ipsecmgr_info("failed to alloc flow_entry\n");
		return NULL;
	}

	flow->priv = priv;
	ref = &flow->ref;

	/*
	 * add flow to the database
	 */
	db = &ipsecmgr_ctx->flow_db;
	INIT_LIST_HEAD(&flow->node);

	/*
	 * update key
	 */
	idx = nss_ipsecmgr_key_data2idx(key, NSS_IPSECMGR_MAX_FLOW);

	memcpy(&flow->key, key, sizeof(struct nss_ipsecmgr_key));
	list_add(&flow->node, &db->entries[idx]);

	/*
	 * initiallize the reference object
	 */
	nss_ipsecmgr_ref_init(ref, nss_ipsecmgr_flow_update, nss_ipsecmgr_flow_free);

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
		read_lock_bh(&ipsecmgr_ctx->lock);
		flow_ref = nss_ipsecmgr_flow_lookup(priv, &flow_key);
		read_unlock_bh(&ipsecmgr_ctx->lock);

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
		write_lock_bh(&ipsecmgr_ctx->lock);

		subnet_ref = nss_ipsecmgr_v4_subnet_match(priv, &subnet_key);
		if (!subnet_ref) {
			write_unlock_bh(&ipsecmgr_ctx->lock);
			return false;
		}

		/*
		 * copy nim data from subnet entry
		 */
		nss_ipsecmgr_copy_subnet(&nim, subnet_ref);

		/*
		 * if, the same flow was added in between then flow alloc will return the
		 * same flow. The only side affect of this will be NSS getting duplicate
		 * add requests and thus rejecting one of them
		 */

		flow_ref = nss_ipsecmgr_flow_alloc(priv, &flow_key);
		if (!flow_ref) {
			write_unlock_bh(&ipsecmgr_ctx->lock);
			return false;
		}

		/*
		 * add reference to subnet and trigger an update
		 */
		nss_ipsecmgr_ref_add(flow_ref, subnet_ref);
		nss_ipsecmgr_ref_update(priv, flow_ref, &nim);

		write_unlock_bh(&ipsecmgr_ctx->lock);

		break;

	case htons(ETH_P_IPV6):
		sel = &nim.msg.push.sel;

		nss_ipsecmgr_v6_hdr2sel((struct ipv6hdr *)skb_network_header(skb), sel);
		nss_ipsecmgr_encap_sel2key(sel, &flow_key);

		/*
		 * flow lookup is done with read lock
		 */
		read_lock_bh(&ipsecmgr_ctx->lock);
		flow_ref = nss_ipsecmgr_flow_lookup(priv, &flow_key);
		read_unlock_bh(&ipsecmgr_ctx->lock);

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
		nss_ipsecmgr_v6_subnet_sel2key(sel, &subnet_key);

		/*
		 * write lock as it can update the flow database
		 */
		write_lock(&ipsecmgr_ctx->lock);

		subnet_ref = nss_ipsecmgr_v6_subnet_match(priv, &subnet_key);
		if (!subnet_ref) {
			write_unlock(&ipsecmgr_ctx->lock);
			return false;
		}

		/*
		 * copy nim data from subnet entry
		 */
		nss_ipsecmgr_copy_subnet(&nim, subnet_ref);

		/*
		 * if, the same flow was added in between then flow alloc will return the
		 * same flow. The only side affect of this will be NSS getting duplicate
		 * add requests and thus rejecting one of them
		 */
		flow_ref = nss_ipsecmgr_flow_alloc(priv, &flow_key);
		if (!flow_ref) {
			write_unlock(&ipsecmgr_ctx->lock);
			return false;
		}

		/*
		 * add reference to subnet and trigger an update
		 */
		nss_ipsecmgr_ref_add(flow_ref, subnet_ref);
		nss_ipsecmgr_ref_update(priv, flow_ref, &nim);

		write_unlock(&ipsecmgr_ctx->lock);
		break;

	default:
		nss_ipsecmgr_warn("%p:protocol(%d) offload not supported\n", priv->dev, ntohs(skb->protocol));
		return false;
	}

	return true;
}

