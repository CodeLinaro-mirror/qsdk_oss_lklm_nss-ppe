/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "ppe_qos.h"

/*
 * ppe_qos_set_ucast_prio_map()
 *	API to set unicast priority map for a given device.
 */
ppe_qos_ret_t ppe_qos_set_ucast_prio_map(struct ppe_qos_ucast_prio_map_info *info)
{
	struct net_device *dev;
	struct ppe_drv_iface *iface;
	uint32_t port_id;
	uint32_t profile_id;
	int i;

	if (!info) {
		ppe_qos_warn("Unicast priority map info is NULL");
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_SET_UCAST_PRIO_MAP_FAIL;
	}

	/*
	 * Only physical interfaces are supported for this operation
	 */
	if (info->if_data.type != PPE_QOS_INTERFACE_TYPE_PHYSICAL) {
		ppe_qos_warn("Only physical interfaces are supported for unicast priority map setting");
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_SET_UCAST_PRIO_MAP_FAIL;
	}

	/*
	 * Validate priority map values (0-7 for unicast queue classes)
	 */
	for (i = 0; i < PPE_DRV_MAX_PRIORITY; i++) {
		if (info->prio_map[i] > 7) {
			ppe_qos_warn("Invalid unicast queue class %u at index %d (max: 7)",
				info->prio_map[i], i);
			ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
			return PPE_QOS_SET_UCAST_PRIO_MAP_FAIL;
		}
	}

	/*
	 * Get the net device
	 */
	dev = dev_get_by_name(&init_net, info->if_data.interface.dev);
	if (!dev) {
		ppe_qos_warn("Device %s not found", info->if_data.interface.dev);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	spin_lock_bh(&gbl_ppe_qos.lock);

	/*
	 * Get the PPE interface for this device
	 */
	iface = ppe_drv_iface_get_by_dev(dev);
	if (!iface) {
		spin_unlock_bh(&gbl_ppe_qos.lock);
		dev_put(dev);
		ppe_qos_warn("PPE interface not found for device %s", dev->name);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	/*
	 * Get the port ID from the interface
	 */
	port_id = ppe_drv_iface_port_idx_get(iface);
	if (port_id >= PPE_DRV_PHYSICAL_MAX) {
		spin_unlock_bh(&gbl_ppe_qos.lock);
		dev_put(dev);
		ppe_qos_warn("Invalid port ID %u for device %s", port_id, dev->name);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	/*
	 * Get the profile ID for this port
	 */
	profile_id = ppe_drv_port_ucast_queue_profile_get(port_id);

	/*
	 * Configure the unicast priority map
	 */
	if (!ppe_drv_confgiure_ucast_prio_map_tbl(profile_id, info->prio_map)) {
		spin_unlock_bh(&gbl_ppe_qos.lock);
		dev_put(dev);
		ppe_qos_warn("Failed to configure unicast priority map for device %s", dev->name);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_fail);
		return PPE_QOS_SET_UCAST_PRIO_MAP_FAIL;
	}

	/*
	 * Cache the unicast priority map for dump purposes
	 */
	memcpy(gbl_ppe_qos.port_res[port_id].ucast_prio_map, info->prio_map, sizeof(info->prio_map));
	gbl_ppe_qos.port_res[port_id].ucast_prio_map_valid = true;

	spin_unlock_bh(&gbl_ppe_qos.lock);
	dev_put(dev);

	ppe_qos_info("Successfully configured unicast priority map for device %s", dev->name);
	ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_ucast_prio_map_success);
	return PPE_QOS_SUCCESS;
}
EXPORT_SYMBOL(ppe_qos_set_ucast_prio_map);


/*
 * ppe_qos_set_mcast_prio_map()
 *	API to set multicast priority map for a given port.
 */
ppe_qos_ret_t ppe_qos_set_mcast_prio_map(struct ppe_qos_mcast_prio_map_info *info)
{
	struct net_device *dev;
	struct ppe_drv_iface *iface;
	uint32_t port_id;
	int i;

	if (!info) {
		ppe_qos_warn("Multicast priority map info is NULL");
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
		return PPE_QOS_FAIL;
	}

	/*
	 * Only physical interfaces are supported for this operation
	 */
	if (info->if_data.type != PPE_QOS_INTERFACE_TYPE_PHYSICAL) {
		ppe_qos_warn("Only physical interfaces are supported for multicast priority map setting");
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
		return PPE_QOS_FAIL;
	}

	/*
	 * Validate priority map values (0-3 for multicast queue classes)
	 */
	for (i = 0; i < PPE_DRV_MAX_PRIORITY; i++) {
		if (info->prio_map[i] > PPE_DRV_MCAST_QUEUE_CLASS_MAX) {
			ppe_qos_warn("Invalid multicast queue class %u at index %d (max: %d)",
			            info->prio_map[i], i, PPE_DRV_MCAST_QUEUE_CLASS_MAX);
			ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
			return PPE_QOS_FAIL;
		}
	}

	/*
	 * Get the net device
	 */
	dev = dev_get_by_name(&init_net, info->if_data.interface.dev);
	if (!dev) {
		ppe_qos_warn("Device %s not found", info->if_data.interface.dev);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	spin_lock_bh(&gbl_ppe_qos.lock);

	/*
	 * Get the PPE interface for this device
	 */
	iface = ppe_drv_iface_get_by_dev(dev);
	if (!iface) {
		spin_unlock_bh(&gbl_ppe_qos.lock);
		dev_put(dev);
		ppe_qos_warn("PPE interface not found for device %s", dev->name);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	/*
	 * Get the port ID from the interface
	 */
	port_id = ppe_drv_iface_port_idx_get(iface);
	if (port_id >= PPE_DRV_PHYSICAL_MAX) {
		spin_unlock_bh(&gbl_ppe_qos.lock);
		dev_put(dev);
		ppe_qos_warn("Invalid port ID %u for device %s", port_id, dev->name);
		ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
		return PPE_QOS_INVALID_DEV;
	}

	/*
	 * Call the PPE driver function to configure multicast priority map for each priority
	 */
	for (i = 0; i < PPE_DRV_MAX_PRIORITY; i++) {
		if (!ppe_drv_port_mcast_priority_class_set(port_id, i, info->prio_map[i])) {
			spin_unlock_bh(&gbl_ppe_qos.lock);
			dev_put(dev);
			ppe_qos_warn("Failed to set multicast priority map for port %u, priority %d", port_id, i);
			ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_fail);
			return PPE_QOS_FAIL;
		}
	}

	/*
	 * Cache the multicast priority map for dump purposes
	 */
	memcpy(gbl_ppe_qos.port_res[port_id].mcast_prio_map, info->prio_map, sizeof(info->prio_map));
	gbl_ppe_qos.port_res[port_id].mcast_prio_map_valid = true;

	spin_unlock_bh(&gbl_ppe_qos.lock);
	dev_put(dev);

	ppe_qos_info("Successfully set multicast priority map for port %u", port_id);
	ppe_qos_stats_inc(&gbl_ppe_qos.stats.qos_set_mcast_prio_map_success);
	return PPE_QOS_SUCCESS;
}
EXPORT_SYMBOL(ppe_qos_set_mcast_prio_map);
