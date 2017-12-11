/*
 **************************************************************************
 * Copyright (c) 2016-2017, The Linux Foundation. All rights reserved.
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
 * nss_bridge_mgr.c
 *	NSS to HLOS Bridge Interface manager
 */
#include <linux/etherdevice.h>
#include <linux/if_vlan.h>
#include <linux/of.h>
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
#include <ref/ref_vsi.h>
#include <nss_vlan_mgr.h>
#include <fal/fal_fdb.h>
#include <fal/fal_stp.h>
#endif
#include <nss_api_if.h>

#if (NSS_BRIDGE_MGR_DEBUG_LEVEL < 1)
#define nss_bridge_mgr_assert(fmt, args...)
#else
#define nss_bridge_mgr_assert(c) BUG_ON(!(c))
#endif /* NSS_BRIDGE_MGR_DEBUG_LEVEL */

/*
 * Compile messages for dynamic enable/disable
 */
#if defined(CONFIG_DYNAMIC_DEBUG)
#define nss_bridge_mgr_warn(s, ...) \
		pr_debug("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#define nss_bridge_mgr_info(s, ...) \
		pr_debug("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#define nss_bridge_mgr_trace(s, ...) \
		pr_debug("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#else /* CONFIG_DYNAMIC_DEBUG */
/*
 * Statically compile messages at different levels
 */
#if (NSS_BRIDGE_MGR_DEBUG_LEVEL < 2)
#define nss_bridge_mgr_warn(s, ...)
#else
#define nss_bridge_mgr_warn(s, ...) \
		pr_warn("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#endif

#if (NSS_BRIDGE_MGR_DEBUG_LEVEL < 3)
#define nss_bridge_mgr_info(s, ...)
#else
#define nss_bridge_mgr_info(s, ...) \
		pr_notice("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#endif

#if (NSS_BRIDGE_MGR_DEBUG_LEVEL < 4)
#define nss_bridge_mgr_trace(s, ...)
#else
#define nss_bridge_mgr_trace(s, ...) \
		pr_info("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#endif
#endif /* CONFIG_DYNAMIC_DEBUG */

/*
 * nss interface check
 */
#define NSS_BRIDGE_MGR_IF_IS_TYPE_PHYSICAL(if_num) \
	(((if_num) >= NSS_PHYSICAL_IF_START) && \
	((if_num) < (NSS_PHYSICAL_IF_START + NSS_MAX_PHYSICAL_INTERFACES)))

#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
#define NSS_BRIDGE_MGR_SWITCH_ID	0
#define NSS_BRIDGE_MGR_SPANNING_TREE_ID	0
#endif

/*
 * bridge manager context structure
 */
struct nss_bridge_mgr_context {
	struct list_head list;		/* List of bridge instance */
	spinlock_t lock;		/* Lock to protect bridge instance */
} br_mgr_ctx;

/*
 * bridge manager private structure
 */
struct nss_bridge_pvt {
	struct list_head list;			/* List of bridge instance */
	struct net_device *dev;			/* Bridge netdevice */
	uint32_t ifnum;				/* Dynamic interface for bridge */
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	uint32_t vsi;				/* VSI set for bridge */
	uint32_t port_vsi[NSS_MAX_PHYSICAL_INTERFACES];	/* port VSI set for physical interfaces	*/
	uint32_t lag_ports[NSS_MAX_PHYSICAL_INTERFACES]; /* List of slave ports in LAG */
	int bond_dev_ref_cnt;			/* Total number of bond devices added into
						   bridge device */
#endif
	uint32_t mtu;				/* MTU for bridge */
	uint8_t dev_addr[ETH_ALEN];		/* MAC address for bridge */
};

/*
 * nss_bridge_mgr_create_instance()
 *	Create a bridge instance.
 */
static struct nss_bridge_pvt *nss_bridge_mgr_create_instance(struct net_device *dev)
{
	struct nss_bridge_pvt *br;

	if (!netif_is_bridge_master(dev))
		return NULL;

	br = kzalloc(sizeof(*br), GFP_KERNEL);
	if (!br)
		return NULL;

	INIT_LIST_HEAD(&br->list);

	return br;
}

/*
 * nss_bridge_mgr_delete_instance()
 *	Delete a bridge instance from bridge list and free the bridge instance.
 */
static void nss_bridge_mgr_delete_instance(struct nss_bridge_pvt *br)
{
	spin_lock(&br_mgr_ctx.lock);
	br->dev = NULL;
	if (!list_empty(&br->list))
		list_del(&br->list);

	spin_unlock(&br_mgr_ctx.lock);

	kfree(br);
}

/*
 * nss_bridge_mgr_find_instance()
 *	Find a bridge instance from bridge list.
 */
static struct nss_bridge_pvt *nss_bridge_mgr_find_instance(
						struct net_device *dev)
{
	struct nss_bridge_pvt *br;

	if (!netif_is_bridge_master(dev))
		return NULL;

	/*
	 * Do we have it on record?
	 */
	spin_lock(&br_mgr_ctx.lock);
	list_for_each_entry(br, &br_mgr_ctx.list, list) {
		if (br->dev == dev) {
			spin_unlock(&br_mgr_ctx.lock);
			return br;
		}
	}

	spin_unlock(&br_mgr_ctx.lock);
	return NULL;
}

#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
/*
 * nss_bridge_mgr_update_bond_slave()
 */
static int nss_bridge_mgr_update_bond_slave(struct net_device *bond_master,
		struct net_device *slave, struct nss_bridge_pvt *b_pvt, int linking)
{
	uint32_t *port_vsi;
	int port_id;
	int32_t ifnum;
	int32_t lagid = 0;
	int32_t bondid = 0;

	/*
	 * Figure out the aggregation id of this slave
	 */
	bondid = bond_get_id(bond_master);
	if (bondid < 0) {
		nss_bridge_mgr_warn("%p: Invalid LAG group id 0x%x\n",
				b_pvt, bondid);
		return -1;
	}

	lagid = bondid + NSS_LAG0_INTERFACE_NUM;

	if (linking) {
		nss_bridge_mgr_trace("%p: Bond Slave %s is added bridge\n",
				b_pvt, slave->name);

		ifnum = nss_cmn_get_interface_number_by_dev(slave);

		/*
		 * Hardware supports only PHYSICAL Ports as trunk ports
		 */
		if (ifnum < 0) {
			nss_bridge_mgr_warn("%p: Interface %s is not known to NSS\n",
					b_pvt, slave->name);
			return -1;
		}
		if (ifnum >= NSS_MAX_PHYSICAL_INTERFACES) {
			nss_bridge_mgr_warn("%p: Interface %s is not Physical Interface\n",
					b_pvt, slave->name);
			return -1;
		}

		nss_bridge_mgr_trace("%p: Interface %s adding into bridge\n",
				b_pvt, slave->name);
		port_id = ifnum;

		/*
		 * Take bridge lock as we are updating vsi and port forwarding
		 * details in PPE Hardware
		 */
		spin_lock(&br_mgr_ctx.lock);
		port_vsi = &b_pvt->port_vsi[port_id - 1];

		if (ppe_port_vsi_get(NSS_BRIDGE_MGR_SWITCH_ID, port_id, port_vsi)) {
			spin_unlock(&br_mgr_ctx.lock);
			nss_bridge_mgr_warn("%p: Couldn't get VSI for port %d\n",
					b_pvt, port_id);
			return -1;
		}

		if (ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_id, b_pvt->vsi)) {
			spin_unlock(&br_mgr_ctx.lock);
			nss_bridge_mgr_warn("%p: Couldn't set bridge VSI for port %d\n",
					b_pvt, port_id);
			return -1;
		}
		spin_unlock(&br_mgr_ctx.lock);

		if (nss_bridge_tx_join_msg(b_pvt->ifnum,
					slave) != NSS_TX_SUCCESS) {
			ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_id, *port_vsi);
			nss_bridge_mgr_warn("%p: Couldn't add port %d in bridge",
					b_pvt, port_id);
			return -1;
		}
		spin_lock(&br_mgr_ctx.lock);
		b_pvt->lag_ports[port_id] = lagid;
		spin_unlock(&br_mgr_ctx.lock);

		return 0;
	}

	nss_bridge_mgr_trace("%p: Bond Slave %s leaving bridge\n",
			b_pvt, slave->name);

	ifnum = nss_cmn_get_interface_number_by_dev(slave);
	if (ifnum < 0) {
		nss_bridge_mgr_trace("%p: Interface %s is not known to NSS\n",
				b_pvt, slave->name);
		return -1;
	}

	if (ifnum >= NSS_MAX_PHYSICAL_INTERFACES) {
		nss_bridge_mgr_trace("%p: Interface %s is not Physical Interface\n",
				b_pvt, slave->name);
		return -1;
	}

	nss_bridge_mgr_trace("%p: Interface %s leaving from bridge\n",
			b_pvt, slave->name);

	port_id = (fal_port_t)ifnum;

	/*
	 * Take bridge lock as we are updating vsi and port forwarding
	 * details in PPE Hardware
	 */
	spin_lock(&br_mgr_ctx.lock);
	port_vsi = &b_pvt->port_vsi[port_id - 1];

	if (b_pvt->lag_ports[port_id] != lagid) {
		spin_unlock(&br_mgr_ctx.lock);
		return -1;
	}

	if (ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_id, *port_vsi)) {
		spin_unlock(&br_mgr_ctx.lock);
		nss_bridge_mgr_trace("%p: Failed to restore VSI for port %d\n",
				b_pvt, port_id);
		return -1;
	}
	spin_unlock(&br_mgr_ctx.lock);

	if (nss_bridge_tx_leave_msg(b_pvt->ifnum,
				slave) != NSS_TX_SUCCESS) {
		ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_id, b_pvt->vsi);
		nss_bridge_mgr_trace("%p: Failed to remove port %d from bridge\n",
				b_pvt, port_id);
		return -1;
	}
	spin_lock(&br_mgr_ctx.lock);
	b_pvt->lag_ports[port_id] = 0;
	spin_unlock(&br_mgr_ctx.lock);

	return 0;
}

/*
 * nss_bridge_mgr_configure_bond()
 */
static int nss_bridge_mgr_configure_bond(struct net_device *bond_master,
		struct netdev_notifier_changeupper_info *cu_info)
{
	struct net_device *slave;
	struct nss_bridge_pvt *b_pvt;
	fal_vsi_newaddr_lrn_t newaddr_lrn;

	b_pvt = nss_bridge_mgr_find_instance(cu_info->upper_dev);
	if (!b_pvt)
		return NOTIFY_DONE;

	nss_bridge_mgr_trace("%p: Interface %s is %s bridge %s\n",
			b_pvt, bond_master->name, cu_info->linking?"Joining":"Leaving",
			cu_info->upper_dev->name);

	rcu_read_lock();
	for_each_netdev_in_bond_rcu(bond_master, slave) {
		if (nss_bridge_mgr_update_bond_slave(bond_master, slave, b_pvt, cu_info->linking?1:0)) {
			rcu_read_unlock();
			nss_bridge_mgr_warn("%p: Failed to update slave (%s) state in Bridge\n", b_pvt, slave->name);
			goto cleanup;
		}
	}
	rcu_read_unlock();

	if (cu_info->linking) {

		spin_lock(&br_mgr_ctx.lock);
		if (!b_pvt->bond_dev_ref_cnt) {
			/* Disable FDB learning in PPE */
			newaddr_lrn.lrn_en = 0;
			newaddr_lrn.action = FAL_MAC_FRWRD;
			if (fal_vsi_newaddr_lrn_set(NSS_BRIDGE_MGR_SWITCH_ID, b_pvt->vsi, &newaddr_lrn)) {
				spin_unlock(&br_mgr_ctx.lock);
				nss_bridge_mgr_warn("%p: Failed to disable FDB learning for Bridge vsi\n", b_pvt);
				goto cleanup;
			}

			/* Flush FDB entries */
			if (fal_fdb_entry_flush(NSS_BRIDGE_MGR_SWITCH_ID, FAL_FDB_DEL_STATIC)) {
				spin_unlock(&br_mgr_ctx.lock);
				nss_bridge_mgr_warn("%p: Failed to flush FDB table in PPE\n", b_pvt);
				goto cleanup;
			}
		}
		b_pvt->bond_dev_ref_cnt++;
		spin_unlock(&br_mgr_ctx.lock);

		/*
		 * Enable L2 exceptions in PPE.
		 */
		if (b_pvt->bond_dev_ref_cnt == 1) {
			if (nss_ppe_tx_l2_exception_msg(NSS_PPE_INTERFACE, 1) != NSS_TX_SUCCESS)
				nss_bridge_mgr_warn("%p: Failed to enable L2 exceptions in PPE\n", b_pvt);
		}
	} else {
		spin_lock(&br_mgr_ctx.lock);

		if (b_pvt->bond_dev_ref_cnt)
			b_pvt->bond_dev_ref_cnt--;

		if (!b_pvt->bond_dev_ref_cnt) {
			/* Enable FDB learning in PPE */
			newaddr_lrn.lrn_en = 1;
			newaddr_lrn.action = FAL_MAC_FRWRD;
			if (fal_vsi_newaddr_lrn_set(NSS_BRIDGE_MGR_SWITCH_ID, b_pvt->vsi, &newaddr_lrn)) {
				nss_bridge_mgr_warn("%p: Failed to disable FDB learning for Bridge vsi\n", b_pvt);
			}
		}
		spin_unlock(&br_mgr_ctx.lock);

		/*
		 * Disable L2 exceptions in PPE.
		 */
		if (!b_pvt->bond_dev_ref_cnt) {
			if (nss_ppe_tx_l2_exception_msg(NSS_PPE_INTERFACE, 0) != NSS_TX_SUCCESS)
				nss_bridge_mgr_warn("%p: Failed to disable L2 exceptions in PPE\n", b_pvt);
		}
	}

	return NOTIFY_DONE;

cleanup:
	rcu_read_lock();
	for_each_netdev_in_bond_rcu(bond_master, slave) {
		if (nss_bridge_mgr_update_bond_slave(bond_master, slave, b_pvt, 0)) {
			rcu_read_unlock();
			nss_bridge_mgr_warn("%p: Failed to remove slave (%s) from Bridge\n", b_pvt, slave->name);
			return NOTIFY_DONE;
		}
	}
	rcu_read_unlock();

	return NOTIFY_DONE;
}
#endif

/*
 * nss_bridge_mgr_join_bridge()
 *	Netdevice join bridge and send netdevice joining bridge message to NSS FW.
 */
static int nss_bridge_mgr_join_bridge(struct net_device *dev, struct nss_bridge_pvt *br, int32_t ifnum)
{
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	fal_port_t port_num = (fal_port_t)ifnum;

	if (is_vlan_dev(dev)) {
		if (nss_vlan_mgr_join_bridge(dev, br->vsi)) {
			nss_bridge_mgr_warn("%p: vlan device failed to join bridge\n", br);
			return -1;
		}
	} else if (NSS_BRIDGE_MGR_IF_IS_TYPE_PHYSICAL(ifnum)) {
		if (ppe_port_vsi_get(NSS_BRIDGE_MGR_SWITCH_ID, port_num, &br->port_vsi[port_num - 1])) {
			nss_bridge_mgr_warn("%p: failed to save port VSI of physical interface\n", br);
			return -1;
		}

		if (ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_num, br->vsi)) {
			nss_bridge_mgr_warn("%p: failed to set bridge VSI for physical interface\n", br);
			return -1;
		}
	}
#endif

	if (nss_bridge_tx_join_msg(br->ifnum, dev) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: Interface %s join bridge failed\n", br, dev->name);
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
		if (is_vlan_dev(dev))
			nss_vlan_mgr_leave_bridge(dev, br->vsi);
		else if (NSS_BRIDGE_MGR_IF_IS_TYPE_PHYSICAL(ifnum))
			ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_num, br->port_vsi[port_num - 1]);
#endif
		return -1;
	}

	return 0;
}

/*
 * nss_bridge_mgr_leave_bridge()
 *	Netdevice leave bridge and send netdevice leaving bridge message to NSS FW.
 */
static int nss_bridge_mgr_leave_bridge(struct net_device *dev, struct nss_bridge_pvt *br, int32_t ifnum)
{
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	fal_port_t port_num = (fal_port_t)ifnum;

	if (is_vlan_dev(dev)) {
		if (nss_vlan_mgr_leave_bridge(dev, br->vsi)) {
			nss_bridge_mgr_warn("%p: vlan device failed to leave bridge\n", br);
			return -1;
		}
	} else if (NSS_BRIDGE_MGR_IF_IS_TYPE_PHYSICAL(ifnum)) {
		if (fal_stp_port_state_set(NSS_BRIDGE_MGR_SWITCH_ID, NSS_BRIDGE_MGR_SPANNING_TREE_ID, port_num, FAL_STP_FORWARDING)) {
			nss_bridge_mgr_warn("%p: faied to set the STP state to forwarding\n", br);
			return -1;
		}

		if (ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_num, br->port_vsi[port_num - 1])) {
			nss_bridge_mgr_warn("%p: failed to restore port VSI of physical interface\n", br);
			fal_stp_port_state_set(NSS_BRIDGE_MGR_SWITCH_ID, NSS_BRIDGE_MGR_SPANNING_TREE_ID, port_num, FAL_STP_DISABLED);
			return -1;
		}
	}
#endif

	if (nss_bridge_tx_leave_msg(br->ifnum, dev) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: Interface %s leave bridge failed\n", br, dev->name);
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
		if (is_vlan_dev(dev))
			nss_vlan_mgr_join_bridge(dev, br->vsi);
		else if (NSS_BRIDGE_MGR_IF_IS_TYPE_PHYSICAL(ifnum)) {
			fal_stp_port_state_set(NSS_BRIDGE_MGR_SWITCH_ID, NSS_BRIDGE_MGR_SPANNING_TREE_ID, port_num, FAL_STP_DISABLED);
			ppe_port_vsi_set(NSS_BRIDGE_MGR_SWITCH_ID, port_num, br->vsi);
		}
#endif
		return -1;
	}

	return 0;
}

/*
 * nss_bridge_mgr_changemtu_event()
 *	Change bridge MTU and send change bridge MTU message to NSS FW.
 */
static int nss_bridge_mgr_changemtu_event(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);
	struct nss_bridge_pvt *b_pvt = nss_bridge_mgr_find_instance(dev);

	if (!b_pvt)
		return NOTIFY_DONE;

	spin_lock(&br_mgr_ctx.lock);
	if (b_pvt->mtu == dev->mtu) {
		spin_unlock(&br_mgr_ctx.lock);
		return NOTIFY_DONE;
	}
	spin_unlock(&br_mgr_ctx.lock);

	nss_bridge_mgr_trace("%p: MTU changed to %d, send message to NSS\n", b_pvt, dev->mtu);

	if (nss_bridge_tx_set_mtu_msg(b_pvt->ifnum, dev->mtu) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: Failed to send change MTU message to NSS\n", b_pvt);
		return NOTIFY_BAD;
	}

	spin_lock(&br_mgr_ctx.lock);
	b_pvt->mtu = dev->mtu;
	spin_unlock(&br_mgr_ctx.lock);

	return NOTIFY_DONE;
}

/*
 * nss_bridge_mgr_changeaddr_event()
 *	Change bridge MAC address and send change bridge address message to NSS FW.
 */
static int nss_bridge_mgr_changeaddr_event(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);
	struct nss_bridge_pvt *b_pvt = nss_bridge_mgr_find_instance(dev);

	if (!b_pvt)
		return NOTIFY_DONE;

	spin_lock(&br_mgr_ctx.lock);
	if (!memcmp(b_pvt->dev_addr, dev->dev_addr, ETH_ALEN)) {
		spin_unlock(&br_mgr_ctx.lock);
		nss_bridge_mgr_trace("%p: MAC are the same..skip processing it\n", b_pvt);
		return NOTIFY_DONE;
	}
	spin_unlock(&br_mgr_ctx.lock);

	nss_bridge_mgr_trace("%p: MAC changed to %pM, update NSS\n", b_pvt, dev->dev_addr);

	if (nss_bridge_tx_set_mac_addr_msg(b_pvt->ifnum, dev->dev_addr) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: Failed to send change MAC address message to NSS\n", b_pvt);
		return NOTIFY_BAD;
	}

	spin_lock(&br_mgr_ctx.lock);
	ether_addr_copy(b_pvt->dev_addr, dev->dev_addr);
	spin_unlock(&br_mgr_ctx.lock);

	return NOTIFY_DONE;
}

/*
 * nss_bridge_mgr_changeupper_event()
 *	Bridge manager handles netdevice joining or leaving bridge notification.
 */
static int nss_bridge_mgr_changeupper_event(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);
	struct netdev_notifier_changeupper_info *cu_info;
	struct nss_bridge_pvt *b_pvt;
	int32_t slave_ifnum;

	cu_info = (struct netdev_notifier_changeupper_info *)info;

	/*
	 * Check if the master pointer is valid
	 */
	if (!cu_info->master)
		return NOTIFY_DONE;

	b_pvt = nss_bridge_mgr_find_instance(cu_info->upper_dev);
	if (!b_pvt) {
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
		struct net_device *bridge_master;

		if (!netif_is_bond_slave(dev))
			return NOTIFY_DONE;

		bridge_master = netdev_master_upper_dev_get(cu_info->upper_dev);
		if (!bridge_master)
			return NOTIFY_DONE;

		nss_bridge_mgr_info("%s: is a bond slave, upper_dev=%s\n",
				dev->name, cu_info->upper_dev->name);

		b_pvt = nss_bridge_mgr_find_instance(bridge_master);
		if (!b_pvt)
			return NOTIFY_DONE;

		if (nss_bridge_mgr_update_bond_slave(cu_info->upper_dev, dev, b_pvt, cu_info->linking?1:0)) {
			nss_bridge_mgr_warn("%p: Failed to update %s slave state in Bridge %s\n",
					b_pvt, cu_info->upper_dev->name, bridge_master->name);
		}
#endif
		return NOTIFY_DONE;
	}

	/*
	 * Only care about interfaces known by NSS
	 */
	slave_ifnum = nss_cmn_get_interface_number_by_dev(dev);
	if (slave_ifnum < 0) {
		nss_bridge_mgr_warn("%s: failed to find interface number\n", dev->name);
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
		/* slave device is bond master */
		if (netif_is_bond_master(dev))
			return nss_bridge_mgr_configure_bond(dev, cu_info);
#endif
		return NOTIFY_DONE;
	}

	if (cu_info->linking) {
		nss_bridge_mgr_trace("%p: Interface %s joining bridge %s\n", b_pvt, dev->name, cu_info->upper_dev->name);
		if (nss_bridge_mgr_join_bridge(dev, b_pvt, slave_ifnum))
			nss_bridge_mgr_warn("%p: Interface %s failed to join bridge %s\n", b_pvt, dev->name, cu_info->upper_dev->name);

		return NOTIFY_DONE;
	}

	nss_bridge_mgr_trace("%p: Interface %s leaving bridge %s\n", b_pvt, dev->name, cu_info->upper_dev->name);
	if (nss_bridge_mgr_leave_bridge(dev, b_pvt, slave_ifnum))
		nss_bridge_mgr_warn("%p: Interface %s failed to leave bridge %s\n", b_pvt, dev->name, cu_info->upper_dev->name);

	return NOTIFY_DONE;
}

/*
 * nss_bridge_mgr_register_event()
 *	Bridge manager handles bridge registration notification.
 */
static int nss_bridge_mgr_register_event(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);
	struct nss_bridge_pvt *b_pvt;
	int ifnum;
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	uint32_t vsi_id = 0;
#endif

	b_pvt = nss_bridge_mgr_create_instance(dev);
	if (!b_pvt)
		return NOTIFY_DONE;

	b_pvt->dev = dev;

	ifnum = nss_dynamic_interface_alloc_node(NSS_DYNAMIC_INTERFACE_TYPE_BRIDGE);
	if (ifnum < 0) {
		nss_bridge_mgr_warn("%p: failed to alloc bridge di\n", b_pvt);
		nss_bridge_mgr_delete_instance(b_pvt);
		return NOTIFY_DONE;
	}

	if (!nss_bridge_register(ifnum, dev, NULL, NULL, 0, b_pvt)) {
		nss_bridge_mgr_warn("%p: failed to register bridge di to NSS", b_pvt);
		goto fail;
	}

#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	if (ppe_vsi_alloc(NSS_BRIDGE_MGR_SWITCH_ID, &vsi_id)) {
		nss_bridge_mgr_warn("%p: failed to alloc bridge vsi\n", b_pvt);
		goto fail_1;
	}

	b_pvt->vsi = vsi_id;

	if (nss_bridge_tx_vsi_assign_msg(ifnum, vsi_id) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: failed to assign vsi msg\n", b_pvt);
		goto fail_2;
	}
#endif

	if (nss_bridge_tx_set_mac_addr_msg(ifnum, dev->dev_addr) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: failed to set mac_addr msg\n", b_pvt);
		goto fail_3;
	}

	if (nss_bridge_tx_set_mtu_msg(ifnum, dev->mtu) != NSS_TX_SUCCESS) {
		nss_bridge_mgr_warn("%p: failed to set mtu msg\n", b_pvt);
		goto fail_3;
	}

	/*
	 * All done, take a snapshot of the current mtu and mac addrees
	 */
	b_pvt->ifnum = ifnum;
	b_pvt->mtu = dev->mtu;
	ether_addr_copy(b_pvt->dev_addr, dev->dev_addr);
	spin_lock(&br_mgr_ctx.lock);
	list_add(&b_pvt->list, &br_mgr_ctx.list);
	spin_unlock(&br_mgr_ctx.lock);

	return NOTIFY_DONE;

fail_3:
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	if (nss_bridge_tx_vsi_unassign_msg(ifnum, vsi_id) != NSS_TX_SUCCESS)
		nss_bridge_mgr_warn("%p: failed to unassign vsi\n", b_pvt);

fail_2:
	ppe_vsi_free(NSS_BRIDGE_MGR_SWITCH_ID, vsi_id);

fail_1:
#endif
	nss_bridge_unregister(ifnum);

fail:
	if (nss_dynamic_interface_dealloc_node(ifnum, NSS_DYNAMIC_INTERFACE_TYPE_BRIDGE) != NSS_TX_SUCCESS)
		nss_bridge_mgr_warn("%p: failed to dealloc bridge di\n", b_pvt);

	nss_bridge_mgr_delete_instance(b_pvt);

	return NOTIFY_DONE;
}

/*
 * nss_bridge_mgr_unregister_event()
 *	Bridge manager handles bridge unregistration notification.
 */
static int nss_bridge_mgr_unregister_event(struct netdev_notifier_info *info)
{
	struct net_device *dev = netdev_notifier_info_to_dev(info);
	struct nss_bridge_pvt *b_pvt;

	/*
	 * Do we have it on record?
	 */
	b_pvt = nss_bridge_mgr_find_instance(dev);
	if (!b_pvt)
		return NOTIFY_DONE;

	/*
	 * sequence of free:
	 * 1. issue VSI unassign to NSS
	 * 2. free VSI
	 * 3. unregister bridge netdevice from data plane
	 * 4. deallocate dynamic interface associated with bridge netdevice
	 * 5. free bridge netdevice
	 */
#if defined(NSS_BRIDGE_MGR_PPE_SUPPORT)
	/*
	 * VSI unassign function in NSS firmware only returns
	 * CNODE_SEND_NACK in the beginning of the function when it
	 * detects that bridge VSI is not assigned for the bridge.
	 * Please refer to the function bridge_configure_vsi_unassign
	 * in NSS firmware for detailed operation.
	 */
	if (nss_bridge_tx_vsi_unassign_msg(b_pvt->ifnum, b_pvt->vsi) != NSS_TX_SUCCESS)
		nss_bridge_mgr_warn("%p: failed to unassign vsi\n", b_pvt);

	ppe_vsi_free(NSS_BRIDGE_MGR_SWITCH_ID, b_pvt->vsi);
#endif

	nss_bridge_mgr_trace("%p: Bridge %s unregsitered. Freeing bridge di %d\n", b_pvt, dev->name, b_pvt->ifnum);

	nss_bridge_unregister(b_pvt->ifnum);

	if (nss_dynamic_interface_dealloc_node(b_pvt->ifnum, NSS_DYNAMIC_INTERFACE_TYPE_BRIDGE) != NSS_TX_SUCCESS)
		nss_bridge_mgr_warn("%p: dealloc bridge di failed\n", b_pvt);

	nss_bridge_mgr_delete_instance(b_pvt);

	return NOTIFY_DONE;
}

/*
 * nss_bridge_mgr_netdevice_event()
 *	Bridge manager handles bridge operation notifications.
 */
static int nss_bridge_mgr_netdevice_event(struct notifier_block *unused,
				unsigned long event, void *ptr)
{
	struct netdev_notifier_info *info = (struct netdev_notifier_info *)ptr;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		return nss_bridge_mgr_changeupper_event(info);
	case NETDEV_CHANGEADDR:
		return nss_bridge_mgr_changeaddr_event(info);
	case NETDEV_CHANGEMTU:
		return nss_bridge_mgr_changemtu_event(info);
	case NETDEV_REGISTER:
		return nss_bridge_mgr_register_event(info);
	case NETDEV_UNREGISTER:
		return nss_bridge_mgr_unregister_event(info);
	}

	/*
	 * Notify done for all the events we don't care
	 */
	return NOTIFY_DONE;
}


static struct notifier_block nss_bridge_mgr_netdevice_nb __read_mostly = {
	.notifier_call = nss_bridge_mgr_netdevice_event,
};

/*
 * nss_bridge_mgr_init_module()
 *	bridge_mgr module init function
 */
int __init nss_bridge_mgr_init_module(void)
{
	/*
	 * Monitor bridge activity only on supported platform
	 */
	if (!of_machine_is_compatible("qcom,ipq807x"))
		return 0;

	INIT_LIST_HEAD(&br_mgr_ctx.list);
	spin_lock_init(&br_mgr_ctx.lock);
	register_netdevice_notifier(&nss_bridge_mgr_netdevice_nb);
	nss_bridge_mgr_info("Module (Build %s) loaded\n", NSS_CLIENT_BUILD_ID);

	return 0;
}

/*
 * nss_bridge_mgr_exit_module()
 *	bridge_mgr module exit function
 */
void __exit nss_bridge_mgr_exit_module(void)
{
	unregister_netdevice_notifier(&nss_bridge_mgr_netdevice_nb);
	nss_bridge_mgr_info("Module unloaded\n");
}

module_init(nss_bridge_mgr_init_module);
module_exit(nss_bridge_mgr_exit_module);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("NSS bridge manager");
