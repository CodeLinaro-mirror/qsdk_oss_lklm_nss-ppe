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
#include <linux/vmalloc.h>

#include <nss_api_if.h>
#include <nss_ipsec.h>
#include <nss_ipsecmgr.h>
#include <nss_crypto_if.h>

#include "nss_ipsecmgr_priv.h"

/*
 * SA operation info
 */
struct nss_ipsecmgr_sa_info {
	struct nss_ipsec_msg nim;
	struct nss_ipsecmgr_key sa_key;
	struct nss_ipsecmgr_key child_key;
	struct nss_ipsecmgr_sa *sa;

	struct nss_ipsecmgr_ref * (*child_alloc)(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key);
	struct nss_ipsecmgr_ref * (*child_lookup)(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key);
};

/*
 * nss_ipsecmgr_sa_name_lookup()
 * 	lookup the SA in the sa_db
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_sa_name_lookup(struct nss_ipsecmgr_priv *priv, const char *name)
{
	struct nss_ipsecmgr_sa_db *db = &priv->sa_db;
	struct nss_ipsecmgr_sa_entry *entry;
	struct list_head *head;
	char *sa_name;
	uint32_t hash;
	int idx;

	sa_name = strchr(name, '@');
	if (!sa_name || hex2bin((uint8_t *)&hash, ++sa_name, sizeof(uint32_t))) {
		nss_ipsecmgr_error("%p: Invalid sa_name(%s)\n", priv, sa_name);
		return NULL;
	}

	idx = hash & (NSS_CRYPTO_MAX_IDXS - 1);
	if (idx >= NSS_CRYPTO_MAX_IDXS) {
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
 * nss_ipsecmgr_sa_stats_read()
 * 	read sa statistics
 */
static ssize_t nss_ipsecmgr_sa_stats_read(struct file *fp, char __user *ubuf, size_t sz, loff_t *ppos)
{
	struct dentry *parent = dget_parent(fp->f_dentry);
	struct nss_ipsecmgr_sa_entry *sa;
	struct nss_ipsec_rule_data *data;
	struct nss_ipsec_rule_oip *oip;
	struct nss_ipsecmgr_priv *priv;
	struct nss_ipsecmgr_ref *ref;
	struct net_device *dev;
	char *local, *type;
	uint32_t addr[4];
	ssize_t ret = 0;
	int len;

	dev = dev_get_by_index(&init_net, (uint32_t)fp->private_data);
	if (!dev) {
		return 0;
	}
	priv = netdev_priv(dev);

	local = vzalloc(NSS_IPSECMGR_MAX_BUF_SZ);
	if (!local) {
		nss_ipsecmgr_error("unable to allocate local buffer for tunnel-id: %d\n", (uint32_t)fp->private_data);
		goto done;
	}

	read_lock_bh(&priv->lock);
	ref = nss_ipsecmgr_sa_name_lookup(priv, parent->d_name.name);
	if (!ref) {
		read_unlock_bh(&priv->lock);
		vfree(local);
		nss_ipsecmgr_error("sa not found tunnel-id: %d\n", (uint32_t)fp->private_data);
		goto done;
	}

	sa = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);
	oip = &sa->nim.msg.push.oip;
	data = &sa->nim.msg.push.data;

	switch (sa->nim.cm.interface) {
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
	switch (oip->ip_ver) {
	case NSS_IPSEC_IPVER_4:
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_ip: %pI4h\n", &oip->dst_addr[0]);
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "src_ip: %pI4h\n", &oip->src_addr[0]);
		break;

	case NSS_IPSEC_IPVER_6:
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_ip: %pI6c\n", nss_ipsecmgr_v6addr_hton(oip->dst_addr, addr));
		len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "src_ip: %pI6c\n", nss_ipsecmgr_v6addr_hton(oip->src_addr, addr));
		break;

	}
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "spi_idx: 0x%x\n", oip->esp_spi);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "ttl: %d\n", oip->ttl_hop_limit);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "crypto session: %d\n", data->crypto_index);

	/*
	 * packet stats
	 */
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "processed: %llu\n", sa->pkts.count);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "no_headroom: %d\n", sa->pkts.no_headroom);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "no_tailroom: %d\n", sa->pkts.no_tailroom);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "no_buf: %d\n", sa->pkts.no_buf);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "fail_queue: %d\n", sa->pkts.fail_queue);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "fail_hash: %d\n", sa->pkts.fail_hash);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "fail_replay: %d\n", sa->pkts.fail_replay);

	read_unlock_bh(&priv->lock);

	ret = simple_read_from_buffer(ubuf, sz, ppos, local, len + 1);

	vfree(local);

done:
	dev_put(dev);
	return ret;
}

/*
 * nss_ipsecmgr_sa_free()
 * 	deallocate the SA if there are no references
 */
static void nss_ipsecmgr_sa_free(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref)
{
	struct nss_ipsecmgr_sa_entry *entry = container_of(ref, struct nss_ipsecmgr_sa_entry, ref);

	if (!nss_ipsecmgr_ref_is_empty(ref)) {
		return;
	}

	debugfs_remove_recursive(nss_ipsecmgr_ref_get_dentry(ref));

	/*
	 * there should be no references remove it from
	 * the sa_db and free the entry
	 */
	list_del_init(&entry->node);
	kfree(entry);
}

/*
 * nss_ipsecmgr_sa_del()
 * 	delete sa/child from the reference chain
 */
static bool nss_ipsecmgr_sa_del(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_sa_info *info)
{
	struct nss_ipsecmgr_ref *sa_ref, *child_ref;

	/*
	 * lock database
	 */
	write_lock_bh(&priv->lock);

	/*
	 * search the flow for deletion
	 */
	child_ref = info->child_lookup(priv, &info->child_key);
	if (!child_ref) {
		/*
		 * unlock device
		 */
		write_unlock_bh(&priv->lock);

		nss_ipsecmgr_warn("%p:failed to lookup child_entry\n", priv->dev);
		nss_ipsecmgr_trace("%p:child_lookup(%p)\n", priv, info->child_lookup);
		return false;
	}

	/*
	 * search the SA in sa_db
	 */
	sa_ref = nss_ipsecmgr_sa_lookup(priv, &info->sa_key);
	if (!sa_ref) {
		write_unlock_bh(&priv->lock);

		nss_ipsecmgr_warn("%p:failed to lookup sa_entry\n", priv->dev);
		return false;
	}

	/*
	 * Remove the reference if it is associated the SA
	 */
	if (nss_ipsecmgr_ref_is_child(child_ref, sa_ref)) {
		nss_ipsecmgr_ref_free(priv, child_ref);
	}

	/*
	 * This deallocates the SA if there are no further references
	 */
	nss_ipsecmgr_sa_free(priv, sa_ref);

	write_unlock_bh(&priv->lock);
	return true;
}

/*
 * nss_ipsecmgr_sa_add()
 * 	add sa/child from the reference chain
 */
static bool nss_ipsecmgr_sa_add(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_sa_info *info)
{
	struct nss_ipsecmgr_ref *sa_ref, *child_ref;
	struct nss_ipsecmgr_sa_entry *sa;

	BUG_ON(!info->child_alloc);
	BUG_ON(!info->child_lookup);

	/*
	 * lock database
	 */
	write_lock_bh(&priv->lock);

	/*
	 * allocate a flow, this returns either a new flow or an existing
	 * one incase it is found
	 */
	child_ref = info->child_alloc(priv, &info->child_key);
	if (!child_ref) {
		/*
		 * unlock device
		 */
		write_unlock_bh(&priv->lock);

		nss_ipsecmgr_warn("%p:failed to alloc child_entry\n", priv->dev);
		nss_ipsecmgr_trace("%p:child_alloc(%p)\n", priv, info->child_alloc);
		return false;
	}

	/*
	 * allocate a SA, when flow alloc is successful. This returns either
	 * new SA or an existing one incase it is found
	 */
	sa_ref = nss_ipsecmgr_sa_alloc(priv, &info->sa_key);
	if (!sa_ref) {
		/*
		 * release the flow and unlock device
		 */
		nss_ipsecmgr_ref_free(priv, child_ref);
		write_unlock_bh(&priv->lock);

		nss_ipsecmgr_warn("%p:failed to alloc sa_entry\n", priv->dev);
		return false;
	}

	/*
	 * we are only interested the storing the SA portion of the message
	 */
	sa = container_of(sa_ref, struct nss_ipsecmgr_sa_entry, ref);
	sa->ifnum = info->nim.cm.interface;

	memcpy(&sa->nim, &info->nim, sizeof(struct nss_ipsec_msg));
	memset(&sa->nim.msg.push.sel, 0, sizeof(struct nss_ipsec_rule_sel));

	/*
	 * Store the SA information to update user for stats reporting
	 */
	memcpy(&sa->sa_info, info->sa, sizeof(struct nss_ipsecmgr_sa));

	/*
	 * add child to parent
	 */
	nss_ipsecmgr_ref_add(child_ref, sa_ref);

	/*
	 * Trigger the notification chain for the child
	 * Note: if there is change in any data then the trigger
	 * will update the NSS for the change
	 */
	nss_ipsecmgr_ref_update(priv, child_ref, &info->nim);

	write_unlock_bh(&priv->lock);
	return true;
}

/*
 * file operation structure instance
 */
static const struct file_operations sa_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_sa_stats_read,
};

/*
 * nss_ipsecmgr_sa_alloc()
 * 	allocate the SA if there is none in the DB
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_sa_alloc(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	char hash_str[NSS_IPSECMGR_MAX_KEY_NAME] = {0};
	struct nss_ipsecmgr_sa_entry *sa;
	struct nss_ipsecmgr_sa_db *db;
	struct nss_ipsecmgr_ref *ref;
	struct dentry *dentry;
	int idx;

	/*
	 * Search the object in the database first
	 */
	ref = nss_ipsecmgr_sa_lookup(priv, key);
	if (ref) {
		return ref;
	}

	/*
	 * Object doesn't exist, allocate it
	 */
	sa = kzalloc(sizeof(struct nss_ipsecmgr_sa_entry), GFP_ATOMIC);
	if (!sa) {
		nss_ipsecmgr_error("failed to alloc sa_entry\n");
		return NULL;
	}

	/*
	 * store tunnel private reference
	 */
	sa->priv = priv;

	/*
	 * initialize sa list node
	 */
	ref = &sa->ref;
	db = &priv->sa_db;
	INIT_LIST_HEAD(&sa->node);

	/*
	 * update key and generate/store hash
	 */
	idx = nss_ipsecmgr_key_data2idx(key, NSS_CRYPTO_MAX_IDXS);
	nss_ipsecmgr_key_gen_hash(key, NSS_CRYPTO_MAX_IDXS);

	memcpy(&sa->key, key, sizeof(struct nss_ipsecmgr_key));
	list_add(&sa->node, &db->entries[idx]);

	/*
	 * create a string from hash
	 */
	nss_ipsecmgr_key_hash2str(key, hash_str);

	/*
	 * initiallize the reference object
	 */
	nss_ipsecmgr_ref_init(&sa->ref, NULL, nss_ipsecmgr_sa_free);

	/*
	 * setup the debugfs entries
	 */
	nss_ipsecmgr_ref_update_name(ref, "sa@");
	nss_ipsecmgr_ref_update_name(ref, hash_str);

	/*
	 * we don't know the parent of this node now hence attach it to the root node
	 */
	dentry = debugfs_create_dir(nss_ipsecmgr_ref_get_name(ref), priv->dentry);
	debugfs_create_file("stats", S_IRUGO, dentry, (uint32_t *)priv->dev->ifindex, &sa_stats_op);

	nss_ipsecmgr_ref_set_dentry(ref, dentry);
	return ref;
}

/*
 * nss_ipsecmgr_sa_copy()
 * 	update the SA entry with the SA data
 */
void nss_ipsecmgr_copy_v4_sa(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_v4 *sa)
{
	struct nss_ipsec_rule_oip *oip = &nim->msg.push.oip;

	oip->dst_addr[0] = sa->dst_ip;
	oip->src_addr[0] = sa->src_ip;
	oip->ttl_hop_limit = sa->ttl;
	oip->esp_spi = sa->spi_index;
	oip->ip_ver = NSS_IPSEC_IPVER_4;
}

/*
 * nss_ipsecmgr_copy_v6_sa()
 * 	update the SA entry with the SA data
 */
void nss_ipsecmgr_copy_v6_sa(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_v6 *sa)
{
	struct nss_ipsec_rule_oip *oip = &nim->msg.push.oip;

	/*
	 * copy outer header
	 */
	memcpy(oip->dst_addr, sa->dst_ip, sizeof(uint32_t) * 4);
	memcpy(oip->src_addr, sa->src_ip, sizeof(uint32_t) * 4);

	oip->esp_spi = sa->spi_index;
	oip->ttl_hop_limit = sa->hop_limit;
	oip->ip_ver = NSS_IPSEC_IPVER_6;
}

/*
 * nss_ipsecmgr_sa_copy()
 * 	update the SA entry with the SA data
 */
void nss_ipsecmgr_copy_sa_data(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_data *sa_data)
{
	struct nss_ipsec_rule_data *data = &nim->msg.push.data;

	data->crypto_index = (uint16_t)sa_data->crypto_index;
	data->window_size = sa_data->esp.replay_win;
	data->nat_t_req = sa_data->esp.nat_t_req;

	data->cipher_algo = nss_crypto_get_cipher(data->crypto_index);
	data->auth_algo = nss_crypto_get_auth(data->crypto_index);

	data->esp_icv_len = sa_data->esp.icv_len;
	data->esp_seq_skip = sa_data->esp.seq_skip;
	data->use_pattern = sa_data->use_pattern;
	data->dscp = sa_data->esp.dscp;

	if (sa_data->esp.dscp_copy) {
		data->sa_dscp_mask = NSS_IPSECMGR_DSCP_MASK_OFF;
		data->flow_dscp_mask = NSS_IPSECMGR_DSCP_MASK_ON;
	} else {
		data->sa_dscp_mask = NSS_IPSECMGR_DSCP_MASK_ON;
		data->flow_dscp_mask = NSS_IPSECMGR_DSCP_MASK_OFF;
	}
}

/*
 * nss_ipsecmgr_v4_sa2key()
 * 	convert a SA into a key
 */
void nss_ipsecmgr_v4_sa2key(struct nss_ipsecmgr_sa_v4 *sa, struct nss_ipsecmgr_key *key)
{
	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 4 /* v4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);
	nss_ipsecmgr_key_write_32(key, sa->dst_ip, NSS_IPSECMGR_KEY_POS_IPV4_DST);
	nss_ipsecmgr_key_write_32(key, sa->src_ip, NSS_IPSECMGR_KEY_POS_IPV4_SRC);
	nss_ipsecmgr_key_write_32(key, sa->spi_index, NSS_IPSECMGR_KEY_POS_IPV4_ESP_SPI);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV4_SA;
}

/*
 * nss_ipsecmgr_sa_sel2key()
 * 	convert a SA into a key
 */
void nss_ipsecmgr_sa_sel2key(struct nss_ipsec_rule_sel *sel, struct nss_ipsecmgr_key *key)
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

		key->len = NSS_IPSECMGR_KEY_LEN_IPV4_SA;
		break;

	case NSS_IPSEC_IPVER_6:
		nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
		nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);

		nss_ipsecmgr_v6addr_swap(sel->dst_addr, sel->dst_addr);
		nss_ipsecmgr_v6addr_swap(sel->src_addr, sel->src_addr);

		for (i  = 0; i < 4; i++) {
			nss_ipsecmgr_key_write_32(key, sel->dst_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
			nss_ipsecmgr_key_write_32(key, sel->src_addr[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
		}

		nss_ipsecmgr_key_write_32(key, sel->esp_spi, NSS_IPSECMGR_KEY_POS_IPV6_ESP_SPI);
		key->len = NSS_IPSECMGR_KEY_LEN_IPV6_SA;
		break;
	}
}

/*
 * nss_ipsecmgr_v6_sa2key()
 * 	convert a SA into a key
 */
void nss_ipsecmgr_v6_sa2key(struct nss_ipsecmgr_sa_v6 *sa, struct nss_ipsecmgr_key *key)
{
	uint32_t i;

	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 6 /* v6 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, IPPROTO_ESP, NSS_IPSECMGR_KEY_POS_IP_PROTO);

	for (i  = 0; i < 4; i++) {
		nss_ipsecmgr_key_write_32(key, sa->dst_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_DST + (i * 32));
		nss_ipsecmgr_key_write_32(key, sa->src_ip[i], NSS_IPSECMGR_KEY_POS_IPV6_SRC + (i * 32));
	}

	nss_ipsecmgr_key_write_32(key, sa->spi_index, NSS_IPSECMGR_KEY_POS_IPV6_ESP_SPI);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV6_SA;
}

/*
 * nss_ipsecmgr_sa_stats_update()
 * 	Update sa stats locally
 */
void nss_ipsecmgr_sa_stats_update(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_sa_entry *sa)
{
	struct nss_ipsecmgr_sa_pkt_stats *stats;
	struct nss_ipsec_pkt_sa_stats *pkts;

	pkts = &nim->msg.sa_stats.pkts;
	stats = &sa->pkts;

	stats->count += pkts->count;
	stats->bytes += pkts->bytes;

	stats->no_headroom = pkts->no_headroom;
	stats->no_tailroom = pkts->no_tailroom;
	stats->no_buf = pkts->no_buf;

	stats->fail_queue = pkts->fail_queue;
	stats->fail_hash = pkts->fail_hash;
	stats->fail_replay = pkts->fail_replay;
}

/*
 * nss_ipsecmgr_sa_lookup()
 * 	lookup the SA in the sa_db
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_sa_lookup(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_sa_db *db = &priv->sa_db;
	struct nss_ipsecmgr_sa_entry *entry;
	struct list_head *head;
	int idx;

	idx = nss_ipsecmgr_key_data2idx(key, NSS_CRYPTO_MAX_IDXS);
	head = &db->entries[idx];

	list_for_each_entry(entry, head, node) {
		if (nss_ipsecmgr_key_cmp(&entry->key, key)) {
			return &entry->ref;
		}
	}

	return NULL;
}

/*
 * nss_ipsecmgr_sa_stats_all()
 * 	retrieve the SA statistics for all SA(s)
 */
struct rtnl_link_stats64 *nss_ipsecmgr_sa_stats_all(struct nss_ipsecmgr_priv *priv, struct rtnl_link_stats64 *stats)
{
	struct nss_ipsecmgr_sa_db *sa_db = &priv->sa_db;
	struct nss_ipsecmgr_sa_entry *sa;
	struct list_head *head;
	int i;

	memset(stats, 0, sizeof(struct net_device_stats));

	/*
	 * trigger a stats update chain
	 */
	read_lock_bh(&priv->lock);

	/*
	 * walk the SA database for each entry and get stats for attached SA
	 */
	for (i = 0, head = sa_db->entries; i < NSS_IPSECMGR_MAX_SA; i++, head++) {
		list_for_each_entry(sa, head, node) {
			/*
			 * Check the SA type (ENCAP or DECAP)
			 */
			switch (sa->ifnum) {
			case NSS_IPSEC_ENCAP_IF_NUMBER:
				stats->tx_bytes += sa->pkts.bytes;
				stats->tx_packets += sa->pkts.count;
				stats->tx_dropped += sa->pkts.no_headroom;
				stats->tx_dropped += sa->pkts.no_tailroom;
				stats->tx_dropped += sa->pkts.no_buf;
				stats->tx_dropped += sa->pkts.fail_queue;
				stats->tx_dropped += sa->pkts.fail_hash;
				stats->tx_dropped += sa->pkts.fail_replay;
				break;

			case NSS_IPSEC_DECAP_IF_NUMBER:
				stats->rx_bytes += sa->pkts.bytes;
				stats->rx_packets += sa->pkts.count;
				stats->rx_dropped += sa->pkts.no_headroom;
				stats->rx_dropped += sa->pkts.no_tailroom;
				stats->rx_dropped += sa->pkts.no_buf;
				stats->rx_dropped += sa->pkts.fail_queue;
				stats->rx_dropped += sa->pkts.fail_hash;
				stats->rx_dropped += sa->pkts.fail_replay;
				break;

			default:
				break;
			}
		}
	}

	read_unlock_bh(&priv->lock);

	return stats;
}

/*
 * nss_ipsecmgr_sa_flush_all()
 * 	remove all SA and its corresponding references
 */
void nss_ipsecmgr_sa_flush_all(struct nss_ipsecmgr_priv *priv)
{
	struct nss_ipsecmgr_sa_db *sa_db = &priv->sa_db;
	struct nss_ipsecmgr_sa_entry *entry;
	struct list_head *head;
	int i;

	/*
	 * lock database
	 */
	write_lock_bh(&priv->lock);

	/*
	 * walk the SA database for each entry and delete the attached SA
	 */
	for (i = 0, head = sa_db->entries; i < NSS_IPSECMGR_MAX_SA; i++, head++) {
		while (!list_empty(head)) {
			entry = list_first_entry(head, struct nss_ipsecmgr_sa_entry, node);
			nss_ipsecmgr_ref_free(priv, &entry->ref);
		}
	}

	/*
	 * unlock database
	 */
	write_unlock_bh(&priv->lock);
}

/*
 * nss_ipsecmgr_encap_add()
 * 	add encap flow/subnet to an existing or new SA
 */
bool nss_ipsecmgr_encap_add(struct net_device *tun, struct nss_ipsecmgr_encap_flow *flow,
				struct nss_ipsecmgr_sa *sa, struct nss_ipsecmgr_sa_data *data)
{
	struct nss_ipsecmgr_priv *priv = netdev_priv(tun);
	struct nss_ipsecmgr_sa_info info;


	nss_ipsecmgr_info("%p:encap_add initiated\n", tun);

	memset(&info, 0, sizeof(struct nss_ipsecmgr_sa_info));
	nss_ipsecmgr_encap_flow_init(&info.nim, NSS_IPSEC_MSG_TYPE_ADD_RULE, priv);

	switch (flow->type) {
	case NSS_IPSECMGR_FLOW_TYPE_V4_TUPLE:

		nss_ipsecmgr_copy_encap_v4_flow(&info.nim, &flow->data.v4_tuple);

		nss_ipsecmgr_encap_v4_flow2key(&flow->data.v4_tuple, &info.child_key);

		info.child_alloc = nss_ipsecmgr_flow_alloc;
		info.child_lookup = nss_ipsecmgr_flow_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V4_SUBNET:

		if (nss_ipsecmgr_verify_v4_subnet(&flow->data.v4_subnet)) {
			nss_ipsecmgr_warn("%p:invalid subnet and mask\n", tun);
			return false;
		}

		nss_ipsecmgr_v4_subnet2key(&flow->data.v4_subnet, &info.child_key);

		info.child_alloc = nss_ipsecmgr_subnet_alloc;
		info.child_lookup = nss_ipsecmgr_subnet_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V6_TUPLE:

		nss_ipsecmgr_copy_encap_v6_flow(&info.nim, &flow->data.v6_tuple);

		nss_ipsecmgr_encap_v6_flow2key(&flow->data.v6_tuple, &info.child_key);

		info.child_alloc = nss_ipsecmgr_flow_alloc;
		info.child_lookup = nss_ipsecmgr_flow_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V6_SUBNET:

		if (nss_ipsecmgr_verify_v6_subnet(&flow->data.v6_subnet)) {
			nss_ipsecmgr_warn("%p:invalid subnet and mask\n", tun);
			return false;
		}

		nss_ipsecmgr_v6_subnet2key(&flow->data.v6_subnet, &info.child_key);

		info.child_alloc = nss_ipsecmgr_subnet_alloc;
		info.child_lookup = nss_ipsecmgr_subnet_lookup;
		break;

	default:
		nss_ipsecmgr_warn("%p:unknown flow type(%d)\n", tun, flow->type);
		return false;
	}

	switch (sa->type) {
	case NSS_IPSECMGR_SA_TYPE_V4:

		nss_ipsecmgr_copy_v4_sa(&info.nim, &sa->data.v4);
		nss_ipsecmgr_copy_sa_data(&info.nim, data);
		nss_ipsecmgr_v4_sa2key(&sa->data.v4, &info.sa_key);
		break;

	case NSS_IPSECMGR_SA_TYPE_V6:

		nss_ipsecmgr_copy_v6_sa(&info.nim, &sa->data.v6);
		nss_ipsecmgr_copy_sa_data(&info.nim, data);
		nss_ipsecmgr_v6_sa2key(&sa->data.v6, &info.sa_key);
		break;

	default:
		nss_ipsecmgr_warn("%p:unknown sa type(%d)\n", tun, sa->type);
		return false;


	}

	info.sa = sa;
	return nss_ipsecmgr_sa_add(priv, &info);
}
EXPORT_SYMBOL(nss_ipsecmgr_encap_add);

/*
 * nss_ipsecmgr_encap_del()
 * 	del encap flow/subnet to an existing SA
 *
 * Note: if this is the only/last flow or subnet in the SA then
 * the SA will be also be deallocated
 */
bool nss_ipsecmgr_encap_del(struct net_device *tun, struct nss_ipsecmgr_encap_flow *flow, struct nss_ipsecmgr_sa *sa)
{
	struct nss_ipsecmgr_priv *priv = netdev_priv(tun);
	struct nss_ipsecmgr_sa_info info;

	nss_ipsecmgr_info("%p:encap_del initiated\n", tun);

	memset(&info, 0, sizeof(struct nss_ipsecmgr_sa_info));
	nss_ipsecmgr_encap_flow_init(&info.nim, NSS_IPSEC_MSG_TYPE_DEL_RULE, priv);

	switch (flow->type) {
	case NSS_IPSECMGR_FLOW_TYPE_V4_TUPLE:

		nss_ipsecmgr_copy_encap_v4_flow(&info.nim, &flow->data.v4_tuple);
		nss_ipsecmgr_copy_v4_sa(&info.nim, &sa->data.v4);

		nss_ipsecmgr_encap_v4_flow2key(&flow->data.v4_tuple, &info.child_key);
		nss_ipsecmgr_v4_sa2key(&sa->data.v4, &info.sa_key);

		info.child_alloc = nss_ipsecmgr_flow_alloc;
		info.child_lookup = nss_ipsecmgr_flow_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V4_SUBNET:

		if (nss_ipsecmgr_verify_v4_subnet(&flow->data.v4_subnet)) {
			nss_ipsecmgr_warn("%p:invalid subnet and mask\n", tun);
			return false;
		}

		nss_ipsecmgr_copy_v4_sa(&info.nim, &sa->data.v4);

		nss_ipsecmgr_v4_subnet2key(&flow->data.v4_subnet, &info.child_key);
		nss_ipsecmgr_v4_sa2key(&sa->data.v4, &info.sa_key);

		info.child_alloc = nss_ipsecmgr_subnet_alloc;
		info.child_lookup = nss_ipsecmgr_subnet_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V6_TUPLE:

		nss_ipsecmgr_copy_encap_v6_flow(&info.nim, &flow->data.v6_tuple);
		nss_ipsecmgr_copy_v6_sa(&info.nim, &sa->data.v6);

		nss_ipsecmgr_encap_v6_flow2key(&flow->data.v6_tuple, &info.child_key);
		nss_ipsecmgr_v6_sa2key(&sa->data.v6, &info.sa_key);

		info.child_alloc = nss_ipsecmgr_flow_alloc;
		info.child_lookup = nss_ipsecmgr_flow_lookup;
		break;

	case NSS_IPSECMGR_FLOW_TYPE_V6_SUBNET:

		if (nss_ipsecmgr_verify_v6_subnet(&flow->data.v6_subnet)) {
			nss_ipsecmgr_warn("%p:invalid subnet and mask\n", tun);
			return false;
		}

		nss_ipsecmgr_copy_v6_sa(&info.nim, &sa->data.v6);

		nss_ipsecmgr_v6_subnet2key(&flow->data.v6_subnet, &info.child_key);
		nss_ipsecmgr_v6_sa2key(&sa->data.v6, &info.sa_key);

		info.child_alloc = nss_ipsecmgr_subnet_alloc;
		info.child_lookup = nss_ipsecmgr_subnet_lookup;
		break;

	default:
		nss_ipsecmgr_warn("%p:unknown flow type(%d)\n", tun, flow->type);
		return false;
	}


	return nss_ipsecmgr_sa_del(priv, &info);
}
EXPORT_SYMBOL(nss_ipsecmgr_encap_del);

/*
 * nss_ipsecmgr_decap_add()
 * 	add decap flow/subnet to an existing or new SA
 *
 * Note: In case of decap rule, sa become flow for lookup into flow table
 */
bool nss_ipsecmgr_decap_add(struct net_device *tun, struct nss_ipsecmgr_sa *sa, struct nss_ipsecmgr_sa_data *data)
{
	struct nss_ipsecmgr_priv *priv = netdev_priv(tun);
	struct nss_ipsec_rule *ipsec_rule;
	struct nss_ipsecmgr_sa_info info;

	nss_ipsecmgr_info("%p:decap_add initiated\n", tun);

	memset(&info, 0, sizeof(struct nss_ipsecmgr_sa_info));
	nss_ipsecmgr_decap_flow_init(&info.nim, NSS_IPSEC_MSG_TYPE_ADD_RULE, priv);

	switch (sa->type) {
	case NSS_IPSECMGR_SA_TYPE_V4:

		nss_ipsecmgr_copy_decap_v4_flow(&info.nim, &sa->data.v4);
		nss_ipsecmgr_copy_v4_sa(&info.nim, &sa->data.v4);
		nss_ipsecmgr_copy_sa_data(&info.nim, data);

		/*
		 * if NATT is set override the protocol and port numbers
		 */
		ipsec_rule = &info.nim.msg.push;
		if (ipsec_rule->data.nat_t_req) {
			ipsec_rule->sel.proto_next_hdr = IPPROTO_UDP;
			ipsec_rule->sel.dst_port = NSS_IPSECMGR_NATT_PORT_DATA;
			ipsec_rule->sel.src_port = NSS_IPSECMGR_NATT_PORT_DATA;
		}

		nss_ipsecmgr_decap_v4_flow2key(&sa->data.v4, &info.child_key);
		nss_ipsecmgr_v4_sa2key(&sa->data.v4, &info.sa_key);
		break;

	case NSS_IPSECMGR_SA_TYPE_V6:

		nss_ipsecmgr_copy_decap_v6_flow(&info.nim, &sa->data.v6);
		nss_ipsecmgr_copy_v6_sa(&info.nim, &sa->data.v6);
		nss_ipsecmgr_copy_sa_data(&info.nim, data);

		nss_ipsecmgr_decap_v6_flow2key(&sa->data.v6, &info.child_key);
		nss_ipsecmgr_v6_sa2key(&sa->data.v6, &info.sa_key);
		break;

	default:
		nss_ipsecmgr_warn("%p:unknown flow type(%d)\n", tun, sa->type);
		return false;
	}

	info.child_alloc = nss_ipsecmgr_flow_alloc;
	info.child_lookup = nss_ipsecmgr_flow_lookup;
	info.sa = sa;

	return nss_ipsecmgr_sa_add(priv, &info);
}
EXPORT_SYMBOL(nss_ipsecmgr_decap_add);

/*
 * nss_ipsecmgr_sa_flush()
 * 	flush sa and all associated references.
 */
bool nss_ipsecmgr_sa_flush(struct net_device *tun, struct nss_ipsecmgr_sa *sa)
{
	struct nss_ipsecmgr_priv *priv = netdev_priv(tun);
	struct nss_ipsecmgr_key sa_key;
	struct nss_ipsecmgr_ref *sa_ref;

	switch (sa->type) {
	case NSS_IPSECMGR_SA_TYPE_V4:
		nss_ipsecmgr_v4_sa2key(&sa->data.v4, &sa_key);
		break;

	case NSS_IPSECMGR_SA_TYPE_V6:
		nss_ipsecmgr_v6_sa2key(&sa->data.v6, &sa_key);
		break;

	default:
		nss_ipsecmgr_warn("%p:Unsupported sa type (type - %d)\n", tun, sa->type);
		return false;
	}

	/*
	 * lock database
	 */
	write_lock_bh(&priv->lock);

	/*
	 * search the SA in sa_db
	 */
	sa_ref = nss_ipsecmgr_sa_lookup(priv, &sa_key);
	if (!sa_ref) {
		write_unlock_bh(&priv->lock);
		nss_ipsecmgr_warn("%p:failed to lookup SA\n", priv);
		return false;
	}

	/*
	 * remove the reference from its associated SA
	 */
	nss_ipsecmgr_ref_free(priv, sa_ref);

	/*
	 * unlock database
	 */
	write_unlock_bh(&priv->lock);

	return true;
}
EXPORT_SYMBOL(nss_ipsecmgr_sa_flush);
