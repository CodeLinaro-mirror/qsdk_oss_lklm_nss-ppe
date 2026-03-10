/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <ppe_mcast.h>
#include "ppe_mcast.h"
#include <linux/jhash.h>

struct ppe_mcast gbl_ppe_mcast = {0};

/*
 * ppe_mcast_hash_key()
 *	Generate hash key for multicast entry
 */
static u32 ppe_mcast_hash_key(struct ppe_mcast_hash_key *key)
{
	u32 hash;

	if (key->is_v4) {
		hash = jhash_1word(key->gip.v4, 0);
	} else {
		hash = jhash2(key->gip.v6, 4, 0);
	}

	return jhash_1word(key->port_id, hash);
}

/*
 * ppe_mcast_hash_find()
 *	Find multicast entry in hash table
 */
static struct ppe_mcast_entry *ppe_mcast_hash_find(struct ppe_mcast_hash_key *key)
{
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;
	struct ppe_mcast_entry *entry;
	u32 hash = ppe_mcast_hash_key(key);

	hash_for_each_possible(g_mcast->mc_hash_table, entry, hash_node, hash) {
		if (entry->entry.port == key->port_id &&
			entry->entry.is_v4 == key->is_v4) {

			if (key->is_v4) {
				if (entry->entry.gip.v4 == key->gip.v4)
					return entry;
			} else {
				if (ppe_drv_mcast_v6_addr_equal(entry->entry.gip.v6, key->gip.v6))
					return entry;
			}
		}
	}

	return NULL;
}

/*
 * ppe_mcast_hash_add()
 *	Add multicast entry to hash table
 */
static void ppe_mcast_hash_add(struct ppe_mcast_entry *entry)
{
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;
	struct ppe_mcast_hash_key key;

	key.is_v4 = entry->entry.is_v4;
	if (key.is_v4) {
		key.gip.v4 = entry->entry.gip.v4;
	} else {
		memcpy(key.gip.v6, entry->entry.gip.v6, sizeof(key.gip.v6));
	}
	key.port_id = entry->entry.port;

	hash_add(g_mcast->mc_hash_table, &entry->hash_node, ppe_mcast_hash_key(&key));
}

/*
 * ppe_mcast_hash_del()
 *	Remove multicast entry from hash table
 */
static void ppe_mcast_hash_del(struct ppe_mcast_entry *entry)
{
	hash_del(&entry->hash_node);
}


/*
 * ppe_mcast_get_group()
 *	Get multicast group.
 */
static struct ppe_mcast_group* ppe_mcast_get_group(union ppe_mcast_ip *ip, bool is_v4)
{
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;
	struct ppe_mcast_group *mc_grp;

	if (!list_empty(&g_mcast->mc_grp_list)) {
		list_for_each_entry(mc_grp, &g_mcast->mc_grp_list, list) {
			if (is_v4) {
				if ((mc_grp->is_v4) && (ppe_drv_mcast_v4_addr_equal(mc_grp->gip.v4, ip->v4))) {
					return mc_grp;
				}
			} else if (!(mc_grp->is_v4) && (ppe_drv_mcast_v6_addr_equal(mc_grp->gip.v6, ip->v6))) {
				return mc_grp;
			}
		}
	}

	return NULL;
}

/*
 * ppe_mcast_delete_group()
 *	Delete multicast group.
 */
static void ppe_mcast_delete_group(struct ppe_mcast_group *mc_grp)
{
		/*
		 * If no supported entry, delete multicast group
		 */
		if (!mc_grp->supported_dst_cnt) {
			list_del(&mc_grp->list);
			kfree(mc_grp);
		}
}

/*
 * ppe_mcast_delete_entry()
 *	Remove a multicast entry
 */
ppe_mcast_ret_t ppe_mcast_delete_entry(struct ppe_mcast_entry_info *info)
{
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;
	struct ppe_mcast_group *mc_grp = NULL;
	struct ppe_mcast_entry *mc_entry = NULL;
	struct net_device *dev = NULL;
	int port_id;

	/*
	 * Check if multicast group does not exist exist
	 */
	spin_lock_bh(&g_mcast->lock);
	mc_grp = ppe_mcast_get_group(&info->gip, info->is_v4);
	if (!mc_grp) {
		ppe_mcast_stats_inc(&g_mcast->stats.mcast_delete_entry_fail);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast group deletion failed for dev %s", info->dev);
		return PPE_MCAST_MC_ENTRY_DELETE_FAIL;
	}

	/*
	 * Check if destinaton info L2 physical port
	 */
	dev = dev_get_by_name(&init_net, info->dev);
	if (!dev) {
		ppe_mcast_stats_inc(&g_mcast->stats.mcast_delete_entry_fail);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast group deletion failed for dev %s, invalid device", info->dev);
		return PPE_MCAST_MC_ENTRY_DELETE_FAIL;
	}

	port_id = ppe_drv_iface_idx_get_by_dev(dev);
	if ((port_id < 0) || (port_id >= PPE_DRV_PHYSICAL_MAX)) {
		goto update_mc_group;
	}

	/*
	 * Find entry using hash table
	 */
	struct ppe_mcast_hash_key key;
	key.is_v4 = info->is_v4;
	if (key.is_v4) {
		key.gip.v4 = info->gip.v4;
	} else {
		memcpy(key.gip.v6, info->gip.v6, sizeof(key.gip.v6));
	}
	key.port_id = port_id;

	mc_entry = ppe_mcast_hash_find(&key);
	if (!mc_entry) {
		dev_put(dev);
		ppe_mcast_stats_inc(&g_mcast->stats.mcast_delete_entry_fail);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast group deletion failed for dev %s, mcast entry does not exist", info->dev);
		return PPE_MCAST_MC_ENTRY_DELETE_FAIL;
	}

	/*
	 * Delete entry from PPE
	 */
	if (ppe_drv_mcast_entry_delete(&mc_entry->entry) != PPE_DRV_RET_SUCCESS) {
		dev_put(dev);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast entry deletion failed in ppe for dev %s", info->dev);
		return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
	}

	 mc_grp->supported_dst_cnt--;
	 ppe_mcast_hash_del(mc_entry);
	 kfree(mc_entry);

	ppe_mcast_delete_group(mc_grp);

	dev_put(dev);
	ppe_mcast_stats_inc(&g_mcast->stats.mcast_delete_entry_success);
	spin_unlock_bh(&g_mcast->lock);
	return PPE_MCAST_SUCCESS;

update_mc_group:
	/*
	 * If destination entry is an unsupported port (virtual: wlan or DSA interface),
	 * check if multicast group already exists, if unsupported interface count is 0,
	 * then add all other entries in that group to forward
	 */
	mc_grp->unsupported_dst_cnt--;
	if (!mc_grp->unsupported_dst_cnt) {
		/*
		 * Re-add all entries for this group from hash table
		 */
		struct ppe_mcast_entry *entry;
		int i;

		hash_for_each(g_mcast->mc_hash_table, i, entry, hash_node) {
			if (entry->entry.is_v4 == mc_grp->is_v4) {
				if ((entry->entry.is_v4 && entry->entry.gip.v4 == mc_grp->gip.v4) ||
					(!entry->entry.is_v4 && ppe_drv_mcast_v6_addr_equal(entry->entry.gip.v6, mc_grp->gip.v6))) {
					ppe_drv_mcast_entry_add(&entry->entry);
				}
			}
		}
	}

	dev_put(dev);
	spin_unlock_bh(&g_mcast->lock);
	return PPE_MCAST_SUCCESS;
}
EXPORT_SYMBOL(ppe_mcast_delete_entry);

/*
 * ppe_mcast_create_entry()
 *	Create a multicast entry
 */
ppe_mcast_ret_t ppe_mcast_create_entry(struct ppe_mcast_entry_info *info)
{
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;
	struct ppe_mcast_group *mc_grp = NULL;
	struct ppe_mcast_entry *mc_entry = NULL;
	struct net_device *dev = NULL;
	int port_id;

	/*
	 * Check if multicast group already exists, add entry to that group
	 * else create a multicast group and add entry
	 */
	spin_lock_bh(&g_mcast->lock);
	mc_grp = ppe_mcast_get_group(&info->gip, info->is_v4);
	if (!mc_grp) {
		mc_grp = kzalloc(sizeof(struct ppe_mcast_group), GFP_ATOMIC);
		if (!mc_grp) {
			ppe_mcast_stats_inc(&g_mcast->stats.mcast_create_entry_fail);
			spin_unlock_bh(&g_mcast->lock);
			ppe_mcast_warn("multicast group creation failed for dev %s", info->dev);
			return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
		}

		mc_grp->is_v4 = info->is_v4;
		mc_grp->gip.v4 = info->gip.v4;
		memcpy(mc_grp->gip.v6, info->gip.v6, sizeof(mc_grp->gip.v6));

		list_add(&mc_grp->list, &g_mcast->mc_grp_list);
	}

	/*
	 * Check if destinaton info L2 physical port
	 */
	dev = dev_get_by_name(&init_net, info->dev);
	if (!dev) {
		goto update_mc_group;
	}

	port_id = ppe_drv_iface_idx_get_by_dev(dev);
	if ((port_id < 0) || (port_id >= PPE_DRV_PHYSICAL_MAX)) {
		goto update_mc_group;
	}

	/*
	 * Check if entry already exists using hash table
	 */
	struct ppe_mcast_hash_key key;
	key.is_v4 = info->is_v4;
	if (key.is_v4) {
		key.gip.v4 = info->gip.v4;
	} else {
		memcpy(key.gip.v6, info->gip.v6, sizeof(key.gip.v6));
	}
	key.port_id = port_id;

	mc_entry = ppe_mcast_hash_find(&key);
	if (mc_entry) {
		/*
		 * Entry exists - delete from hardware first
		 */
		ppe_mcast_info("Replacing existing multicast entry for dev %s port %d, group %pI4h",
			info->dev, port_id, &info->gip.v4);

		/*
		 * Delete entry from PPE
		 */
		if (ppe_drv_mcast_entry_delete(&mc_entry->entry) != PPE_DRV_RET_SUCCESS) {
			dev_put(dev);
			spin_unlock_bh(&g_mcast->lock);
			ppe_mcast_warn("Failed to delete existing entry for replacement");
			return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
		}

		/*
		 * Remove from hash table
		 */
		mc_grp->supported_dst_cnt--;
		ppe_mcast_hash_del(mc_entry);
		kfree(mc_entry);
	}

	mc_entry = kzalloc(sizeof(struct ppe_mcast_entry), GFP_ATOMIC);
	if (!mc_entry) {
		dev_put(dev);
		ppe_mcast_delete_group(mc_grp);
		ppe_mcast_stats_inc(&g_mcast->stats.mcast_create_entry_fail);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast entry creation failed for dev %s", info->dev);
		return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
	}

	mc_entry->entry.is_v4 = info->is_v4;
	mc_entry->entry.sip_valid = info->sip_enabled;
	if (info->is_v4) {
		mc_entry->entry.sip.v4 = info->sip.v4;
		mc_entry->entry.gip.v4 = info->gip.v4;
	} else {
		memcpy(mc_entry->entry.sip.v6, info->sip.v6, sizeof(mc_entry->entry.sip.v6));
		memcpy(mc_entry->entry.gip.v6, info->gip.v6, sizeof(mc_entry->entry.gip.v6));
	}

	mc_entry->entry.vlan_valid = info->vlan_enabled;
	mc_entry->entry.vid = info->vlan_id;
	mc_entry->entry.port = port_id;
	mc_entry->entry.fwd_cmd = PPE_DRV_MCAST_FWD_CMD_FWD;

	/*
	 * Configure multicast entry in PPE
	 */
	if (ppe_drv_mcast_entry_add(&mc_entry->entry) != PPE_DRV_RET_SUCCESS) {
		dev_put(dev);
		ppe_mcast_delete_group(mc_grp);
		ppe_mcast_stats_inc(&g_mcast->stats.mcast_create_entry_fail);
		spin_unlock_bh(&g_mcast->lock);
		ppe_mcast_warn("multicast entry creation failed in ppe for dev %s", info->dev);
		return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
	}

	mc_grp->supported_dst_cnt++;

	/*
	 * Add to hash table
	 */
	ppe_mcast_hash_add(mc_entry);

	dev_put(dev);
	ppe_mcast_stats_inc(&g_mcast->stats.mcast_create_entry_success);
	spin_unlock_bh(&g_mcast->lock);

	return PPE_MCAST_SUCCESS;

update_mc_group:
	/*
	 * TODO: If destination entry is an unsupported port (virtual: wlan or DSA interface),
	 * check if multicast group has supported ports, then set fwd_cmd for all supported entries
	 * in that group to redirect to CPU
	 */
	mc_grp->unsupported_dst_cnt++;
	dev_put(dev);

	/*
	 * Delete all MC entries from PPE using hash table
	 */
	struct ppe_mcast_entry *entry;
	int i;

	hash_for_each(g_mcast->mc_hash_table, i, entry, hash_node) {
		if (entry->entry.is_v4 == mc_grp->is_v4) {
			if ((entry->entry.is_v4 && entry->entry.gip.v4 == mc_grp->gip.v4) ||
				(!entry->entry.is_v4 && ppe_drv_mcast_v6_addr_equal(entry->entry.gip.v6, mc_grp->gip.v6))) {
				ppe_drv_mcast_entry_delete(&entry->entry);
			}
		}
	}

	ppe_mcast_delete_group(mc_grp);
	ppe_mcast_stats_inc(&g_mcast->stats.mcast_create_entry_fail);
	spin_unlock_bh(&g_mcast->lock);
	ppe_mcast_warn("multicast entry creation failed for dev %s, unsupported interface", info->dev);
	return PPE_MCAST_MC_ENTRY_CREATE_FAIL;
}
EXPORT_SYMBOL(ppe_mcast_create_entry);

/*
 * ppe_mcast_deinit()
 *	Multicast deinit API
 */
void ppe_mcast_deinit(void)
{
	ppe_mcast_stats_debugfs_exit();
	ppe_mcast_dump_exit();
}

/*
 * ppe_mcast_init()
 *	Multicast init API
 */
void ppe_mcast_init(struct dentry *d_rule)
{
	/* Multicast global config*/
	struct ppe_mcast *g_mcast = &gbl_ppe_mcast;

	spin_lock_init(&g_mcast->lock);
	INIT_LIST_HEAD(&g_mcast->mc_grp_list);

	/* Initialize hash table */
	hash_init(g_mcast->mc_hash_table);

	ppe_mcast_stats_debugfs_init(d_rule);
	ppe_mcast_dump_init(d_rule);
}
