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
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/* nss_ipsecmgr_subnet.c
 *	NSS IPsec manager subnet rules
 */

#include <linux/list.h>
#include <linux/hashtable.h>
#include <linux/types.h>
#include <linux/ip.h>
#include <linux/tcp.h>
#include <linux/module.h>
#include <linux/skbuff.h>
#include <net/ipv6.h>
#include <linux/version.h>
#include <linux/debugfs.h>
#include <linux/vmalloc.h>

#include <nss_crypto_if.h>
#include <nss_ipsecmgr.h>

#include "nss_ipsecmgr_priv.h"

/*
 * nss_ipsecmgr_subnet_name_lookup()
 * 	lookup subnet in subnet_db
 */
static struct nss_ipsecmgr_ref *nss_ipsecmgr_subnet_name_lookup(struct nss_ipsecmgr_priv *priv, const char *name)
{
	struct nss_ipsecmgr_netmask_db *db = &priv->net_db;
	struct nss_ipsecmgr_netmask_entry *netmask;
	struct nss_ipsecmgr_subnet_entry *subnet;
	struct list_head *head;
	uint8_t mask_bits;
	uint32_t hash;
	char *tmp;
	uint8_t idx;

	tmp = strchr(name, '@');
	if (!tmp || hex2bin((uint8_t *)&mask_bits, ++tmp, sizeof(uint8_t))) {
		nss_ipsecmgr_error("%p: Invalid input\n", priv);
		return NULL;
	}

	tmp = strchr(tmp, '@');
	if (!tmp || hex2bin((uint8_t *)&hash, ++tmp, sizeof(uint32_t))) {
		nss_ipsecmgr_error("%p: Invalid input\n", priv);
		return NULL;
	}

	idx = NSS_IPSECMGR_MAX_NETMASK - mask_bits;
	if (idx  >= NSS_IPSECMGR_MAX_NETMASK) {
		return NULL;
	}

	netmask = db->entries[idx];
	if (!netmask || !netmask->count) {
		return NULL;
	}

	idx = hash & (NSS_IPSECMGR_MAX_SUBNET - 1);
	head = &netmask->subnets[idx];

	list_for_each_entry(subnet, head, node) {
		if (nss_ipsecmgr_key_get_hash(&subnet->key) == hash) {
			return &subnet->ref;
		}
	}

	return NULL;
}


static inline uint32_t nss_ipsecmgr_netmask2idx(struct nss_ipsecmgr_key *key)
{
	uint32_t mask, data;

	nss_ipsecmgr_key_read(key, &data, &mask, NSS_IPSECMGR_KEY_POS_IPV4_DST);
	return ffs(mask) - 1;
}

/*
 * nss_ipsecmgr_netmask_free()
 * 	deallocate a netmask entry
 */
static bool nss_ipsecmgr_netmask_free(struct nss_ipsecmgr_netmask_db *db, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_netmask_entry *entry;
	uint32_t idx;

	idx = nss_ipsecmgr_netmask2idx(key);
	if (idx >= NSS_IPSECMGR_MAX_NETMASK) {
		return false;
	}

	entry = db->entries[idx];
	BUG_ON(!entry);

	if (--entry->count) {
		return false;
	}

	clear_bit(idx, db->bitmap);
	db->entries[idx] = NULL;

	kfree(entry);
	return true;
}

/*
 * nss_ipsecmgr_subnet_stats_read()
 * 	read subnet statistics
 */
static ssize_t nss_ipsecmgr_subnet_stats_read(struct file *fp, char __user *ubuf, size_t sz, loff_t *ppos)
{
	struct dentry *parent = dget_parent(fp->f_dentry);
	struct nss_ipsecmgr_subnet_entry *subnet;
	struct nss_ipsecmgr_priv *priv;
	struct nss_ipsecmgr_ref *ref;
	struct net_device *dev;
	uint32_t dst, m_dst;
	ssize_t ret = 0;
	uint8_t proto;
	char *local;
	int len;

	dev = dev_get_by_index(&init_net, (uint32_t)fp->private_data);
	if (!dev) {
		return 0;
	}
	priv = netdev_priv(dev);

	read_lock(&priv->lock);

	ref = nss_ipsecmgr_subnet_name_lookup(priv, parent->d_name.name);
	if (!ref) {
		read_unlock(&priv->lock);
		nss_ipsecmgr_error("subnet not found tunnel-id: %d\n", (uint32_t)fp->private_data);
		goto done;
	}

	subnet = container_of(ref, struct nss_ipsecmgr_subnet_entry, ref);

	/*
	 * Extract info from key.
	 * NOTE: for IPv6 respective positions for dst_ip and netmask should be used.
	 */
	nss_ipsecmgr_key_read(&subnet->key, &dst, &m_dst, NSS_IPSECMGR_KEY_POS_IPV4_DST);
	proto = nss_ipsecmgr_key_read_8(&subnet->key, NSS_IPSECMGR_KEY_POS_IP_PROTO);

	read_unlock(&priv->lock);

	local = vzalloc(NSS_IPSECMGR_MAX_BUF_SZ);
	if (!local) {
		nss_ipsecmgr_error("unable to allocate local buffer for tunnel-id: %d\n", (uint32_t)fp->private_data);
		goto done;
	}

	/*
	 * IPv4 Generel info
	 */
	len = 0;
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_ip: %pI4h\n", &dst);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "dst_mask: %pI4h\n", &m_dst);
	len += snprintf(local + len, NSS_IPSECMGR_MAX_BUF_SZ - len, "proto: %d\n", proto);

	ret = simple_read_from_buffer(ubuf, sz, ppos, local, len + 1);

	vfree(local);
done:
	dev_put(dev);
	return ret;
}

/*
 * nss_ipsecmgr_subnet_update()
 * 	update the subnet with its associated data
 */
static void nss_ipsecmgr_subnet_update(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref, struct nss_ipsec_msg *nim)
{
	struct nss_ipsecmgr_subnet_entry *subnet;

	subnet = container_of(ref, struct nss_ipsecmgr_subnet_entry, ref);

	memcpy(&subnet->nim, nim, sizeof(struct nss_ipsec_msg));
}

/*
 * nss_ipsecmgr_subnet_free()
 * 	free the associated subnet entry and notify NSS
 */
static void nss_ipsecmgr_subnet_free(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_ref *ref)
{
	struct nss_ipsecmgr_subnet_entry *subnet;

	subnet = container_of(ref, struct nss_ipsecmgr_subnet_entry, ref);

	BUG_ON(nss_ipsecmgr_ref_is_empty(ref) == false);

	debugfs_remove_recursive(nss_ipsecmgr_ref_get_dentry(ref));

	/*
	 * detach it from the netmask entry database and
	 * check if the netmask entry is empty. The netmask
	 * entry will get freed if there are no further entries
	 * available
	 */
	list_del(&subnet->node);

	nss_ipsecmgr_netmask_free(&priv->net_db, &subnet->key);
	kfree(subnet);
}

/*
 * nss_ipsecmgr_netmask_lookup()
 * 	lookup a netmask entry
 */
static inline struct nss_ipsecmgr_netmask_entry *nss_ipsecmgr_netmask_lookup(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_netmask_db *db = &priv->net_db;
	struct nss_ipsecmgr_netmask_entry *entry;
	uint32_t idx;

	idx = nss_ipsecmgr_netmask2idx(key) % NSS_IPSECMGR_MAX_NETMASK;
	entry = db->entries[idx];
	if (entry) {
		return entry;
	}

	return NULL;
}

/*
 * nss_ipsecmgr_netmask_alloc()
 * 	allocate a netmask entry
 */
static struct nss_ipsecmgr_netmask_entry *nss_ipsecmgr_netmask_alloc(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_netmask_db *db = &priv->net_db;
	struct nss_ipsecmgr_netmask_entry *entry;
	int idx;

	entry = nss_ipsecmgr_netmask_lookup(priv, key);
	if (entry) {
		return entry;
	}

	entry = kzalloc(sizeof(struct nss_ipsecmgr_netmask_entry), GFP_ATOMIC);
	if (!entry) {
		return NULL;
	}

	nss_ipsecmgr_init_subnet_db(entry);
	entry->count = 1;

	idx = nss_ipsecmgr_netmask2idx(key);
	if (idx >= NSS_IPSECMGR_MAX_NETMASK) {
		return NULL;
	}

	entry->mask_bits = NSS_IPSECMGR_MAX_NETMASK - idx;
	set_bit(idx, db->bitmap);
	db->entries[idx] = entry;

	return entry;
}


/*
 * nss_ipsecmgr_copy_v4_subnet()
 * 	copy v4_subnet nim
 */
void nss_ipsecmgr_copy_v4_subnet(struct nss_ipsec_msg *nim, struct nss_ipsecmgr_ref *ref)
{
	struct nss_ipsecmgr_subnet_entry *entry;
	struct nss_ipsec_rule_data *data;
	struct nss_ipsec_rule_oip *oip;

	entry = container_of(ref, struct nss_ipsecmgr_subnet_entry, ref);

	oip = &entry->nim.msg.push.oip;
	data = &entry->nim.msg.push.data;

	memcpy(&nim->msg.push.oip, oip, sizeof(struct nss_ipsec_rule_oip));
	memcpy(&nim->msg.push.data, data, sizeof(struct nss_ipsec_rule_data));
}

/*
 * nss_ipsecmgr_v4_subnet_sel2key()
 * 	convert subnet selector to key
 */
void nss_ipsecmgr_v4_subnet_sel2key(struct nss_ipsec_rule_sel *sel, struct nss_ipsecmgr_key *key)
{
	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 4 /* ipv4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, sel->proto_next_hdr, NSS_IPSECMGR_KEY_POS_IP_PROTO);
	nss_ipsecmgr_key_write_32(key, nss_ipsecmgr_get_v4addr(sel->dst_addr), NSS_IPSECMGR_KEY_POS_IPV4_DST);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV4_SUBNET;
}

/*
 * nss_ipsecmgr_v4_subnet2key()
 *      convert an v4 subnet into a key
 */
void nss_ipsecmgr_v4_subnet2key(struct nss_ipsecmgr_encap_v4_subnet *net, struct nss_ipsecmgr_key *key)
{
	nss_ipsecmgr_key_reset(key);

	nss_ipsecmgr_key_write_8(key, 4 /* ipv4 */, NSS_IPSECMGR_KEY_POS_IP_VER);
	nss_ipsecmgr_key_write_8(key, (uint8_t)net->protocol, NSS_IPSECMGR_KEY_POS_IP_PROTO);
	nss_ipsecmgr_key_write(key, net->dst_subnet, net->dst_mask, NSS_IPSECMGR_KEY_POS_IPV4_DST);

	key->len = NSS_IPSECMGR_KEY_LEN_IPV4_SUBNET;
}

/*
 * nss_ipsecmgr_v4_subnet_match()
 * 	peform a v4 subnet based match in netmask database
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_v4_subnet_match(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_netmask_db *db = &priv->net_db;
	struct nss_ipsecmgr_key tmp_key;
	struct nss_ipsecmgr_ref *ref;
	int i;

	memcpy(&tmp_key, key, sizeof(struct nss_ipsecmgr_key));

	/*
	 * cycle through the bitmap for each subnet
	 */
	for_each_set_bit(i, db->bitmap, NSS_IPSECMGR_MAX_NETMASK) {

		BUG_ON(db->entries[i] == NULL);
		BUG_ON(db->entries[i]->count == 0);

		/*
		 * set the key with the right mask for hash index computation;
		 * each subnet index has its associated mask value
		 */
		nss_ipsecmgr_key_lshift_mask(&tmp_key, i, NSS_IPSECMGR_KEY_POS_IPV4_DST);

		ref = nss_ipsecmgr_subnet_lookup(priv, &tmp_key);
		if (ref) {
			return ref;
		}
	}

	return NULL;
}

/*
 * nss_ipsecmgr_subnet_lookup()
 * 	lookup a subnet entry
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_subnet_lookup(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	struct nss_ipsecmgr_netmask_entry *netmask;
	struct nss_ipsecmgr_subnet_entry *entry;
	struct list_head *head;
	int idx;

	netmask = nss_ipsecmgr_netmask_lookup(priv, key);
	if (!netmask) {
		return NULL;
	}

	BUG_ON(netmask->count == 0);

	idx = nss_ipsecmgr_key_data2idx(key, NSS_IPSECMGR_MAX_SUBNET);
	head = &netmask->subnets[idx];

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
static const struct file_operations subnet_stats_op = {
	.open = simple_open,
	.llseek = default_llseek,
	.read = nss_ipsecmgr_subnet_stats_read,
};

/*
 * nss_ipsecmgr_subnet_alloc()
 *      allocate a subnet entry
 */
struct nss_ipsecmgr_ref *nss_ipsecmgr_subnet_alloc(struct nss_ipsecmgr_priv *priv, struct nss_ipsecmgr_key *key)
{
	char hash_str[NSS_IPSECMGR_MAX_KEY_NAME] = {0};
	struct nss_ipsecmgr_netmask_entry *netmask;
	struct nss_ipsecmgr_subnet_entry *subnet;
	struct nss_ipsecmgr_ref *ref;
	struct dentry *dentry;
	uint32_t idx;

	/*
	 * subne lookup before allocating a new one
	 */
	ref = nss_ipsecmgr_subnet_lookup(priv, key);
	if (ref) {
		return ref;
	}

	/*
	 * allocate the netmask
	 */
	netmask = nss_ipsecmgr_netmask_alloc(priv, key);
	if (!netmask) {
		return NULL;
	}

	/*
	 * allocate the subnet entry
	 */
	subnet = kzalloc(sizeof(struct nss_ipsecmgr_subnet_entry), GFP_ATOMIC);
	if (!subnet) {
		return NULL;
	}

	subnet->priv = priv;
	ref = &subnet->ref;

	/*
	 * add flow to the database
	 */
	INIT_LIST_HEAD(&subnet->node);

	idx = nss_ipsecmgr_key_data2idx(key, NSS_IPSECMGR_MAX_SUBNET);
	memcpy(&subnet->key, key, sizeof(struct nss_ipsecmgr_key));
	list_add(&subnet->node, &netmask->subnets[idx]);
	netmask->count++;

	/*
	 * initiallize the reference object
	 */
	nss_ipsecmgr_ref_init(&subnet->ref, nss_ipsecmgr_subnet_update, nss_ipsecmgr_subnet_free);

	/*
	 * create a string from hash
	 */
	nss_ipsecmgr_key_hash2str(key, hash_str);
	/* nss_ipsecmgr_key_netmask2str(key, mask_str, NSS_IPSECMGR_KEY_POS_IPV4_DST); */

	/*
	 * setup the debugfs entries
	 */
	nss_ipsecmgr_ref_update_name(ref, "subnet@");
	nss_ipsecmgr_ref_update_name_u8(ref, (uint8_t)netmask->mask_bits);
	nss_ipsecmgr_ref_update_name(ref, "@");
	nss_ipsecmgr_ref_update_name(ref, hash_str);

	/*
	 * we don't know the parent of this node now hence attach it to the root node
	 */
	dentry = debugfs_create_dir(nss_ipsecmgr_ref_get_name(ref), priv->dentry);
	debugfs_create_file("stats", S_IRUGO, dentry, (uint32_t *)priv->dev->ifindex, &subnet_stats_op);

	nss_ipsecmgr_ref_set_dentry(ref, dentry);
	return ref;
}
