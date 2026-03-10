/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/vmalloc.h>
#include <linux/netdevice.h>
#include <ppe_drv_public.h>
#include <ppe_drv.h>
#include <ppe_drv_port_mgmt.h>
#include "ppe_port_mgmt.h"

/*
 * Global PORT_MGMT context
 */
struct ppe_port_mgmt_base ppe_port_mgmt_gbl = {0};

#ifdef NSS_PPE_PORT_ISOL_SUPPORT
/*
 * ppe_port_mgmt_isol_config()
 *	Set port isolation.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_isol_config(struct ppe_port_mgmt_isol *isol_info)
{
	int ppe_ret = PPE_PORT_MGMT_RET_SUCCESS;
	struct net_device *dev;
	struct ppe_drv_iface *iface;
	int32_t port_id;
	struct ppe_drv_port_mgmt_isol drv_isol_info = {0};

	dev = dev_get_by_name(&init_net, isol_info->port_name);
	if (!dev) {
		ppe_port_mgmt_warn("Port name %s not found\n", isol_info->port_name);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	iface = ppe_drv_iface_get_by_dev(dev);
	if (!iface) {
		ppe_port_mgmt_warn("Failed to find PPE interface for dev: %s\n", isol_info->port_name);
		dev_put(dev);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	port_id = ppe_drv_iface_port_idx_get(iface);
	if (port_id < 0) {
		ppe_port_mgmt_warn("Failed to find PPE port for iface: %s\n", isol_info->port_name);
		dev_put(dev);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	dev_put(dev);
	isol_info->port_id = port_id;

	drv_isol_info.port_id = isol_info->port_id;
	drv_isol_info.omci_id = isol_info->omci_id;

	ppe_port_mgmt_info("Enable isolation for port %d, OMCI ID: %d\n", isol_info->port_id, isol_info->omci_id);
	ppe_ret = ppe_drv_port_mgmt_isol_configure(&drv_isol_info);
	if (ppe_ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to set port isolation for port %d, err: %d\n", isol_info->port_id, ppe_ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_isol_config);

/*
 * ppe_port_mgmt_act_ctrl_set()
 *	Set action control configs.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_act_ctrl_set(struct ppe_port_mgmt_isol *isol_info)
{
	uint8_t ppe_ret;
	struct ppe_drv_port_mgmt_isol drv_isol_info = {0};

	drv_isol_info.mc_isol_en = isol_info->mc_isol_en;
	drv_isol_info.bc_isol_en = isol_info->bc_isol_en;

	ppe_ret = ppe_drv_port_mgmt_act_ctrl_set(&drv_isol_info);
	if (ppe_ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to set act ctrl, err: %d\n", ppe_ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_act_ctrl_set);

/*
 * ppe_port_mgmt_default_isol_set()
 *      Set default port isolation configs.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_default_isol_set(void)
{
	uint8_t ppe_ret;

	ppe_ret = ppe_drv_port_mgmt_default_isol_set();
	if (ppe_ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to set act ctrl, err: %d\n", ppe_ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_default_isol_set);
#else
ppe_port_mgmt_ret_t ppe_port_mgmt_isol_config(struct ppe_port_mgmt_isol *isol_info)
{
	ppe_port_mgmt_warn("Feature not supported, failed to set port isolation for port.\n");
	return PPE_PORT_MGMT_RET_FAILURE;
}
EXPORT_SYMBOL(ppe_port_mgmt_isol_config);

ppe_port_mgmt_ret_t ppe_port_mgmt_act_ctrl_set(struct ppe_port_mgmt_isol *isol_info)
{
	ppe_port_mgmt_warn("Feature not supported, failed to set act ctrl\n");
	return PPE_PORT_MGMT_RET_FAILURE;
}
EXPORT_SYMBOL(ppe_port_mgmt_act_ctrl_set);

ppe_port_mgmt_ret_t ppe_port_mgmt_default_isol_set(void)
{
	ppe_port_mgmt_warn("Feature not supported, failed to set act ctrl\n");
	return PPE_PORT_MGMT_RET_FAILURE;
}
EXPORT_SYMBOL(ppe_port_mgmt_default_isol_set);
#endif

/*
 * ppe_port_mgmt_mac_lrn_limit_set()
 *	Set mac learn limit for the port.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_mac_lrn_limit_set(struct ppe_port_mac_lrn_limit *mac_lrn_limit)
{
	uint8_t ppe_ret;
	struct net_device *dev;
	struct ppe_drv_iface *iface;
	int32_t port_id;
	struct ppe_drv_port_mac_lrn_limit drv_mac_lrn_limit = {0};

	dev = dev_get_by_name(&init_net, mac_lrn_limit->port_name);
	if (!dev) {
		ppe_port_mgmt_warn("Port name %s not found\n", mac_lrn_limit->port_name);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	iface = ppe_drv_iface_get_by_dev(dev);
	if (!iface) {
		ppe_port_mgmt_warn("Failed to find PPE interface for dev: %s\n", mac_lrn_limit->port_name);
		dev_put(dev);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	port_id = ppe_drv_iface_port_idx_get(iface);
	if (port_id < 0) {
		ppe_port_mgmt_warn("Failed to find PPE port for iface: %s\n", mac_lrn_limit->port_name);
		dev_put(dev);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	dev_put(dev);
	mac_lrn_limit->port_id = port_id;

	drv_mac_lrn_limit.port_id = mac_lrn_limit->port_id;
	drv_mac_lrn_limit.port_learn_limit_en = mac_lrn_limit->port_learn_limit_en;
	drv_mac_lrn_limit.port_learn_limit = mac_lrn_limit->port_learn_limit;
	drv_mac_lrn_limit.lrn_exceed_action_en = mac_lrn_limit->lrn_exceed_action_en;
	drv_mac_lrn_limit.lrn_exceed_action = (ppe_drv_port_mgmt_fwd_cmd_t)mac_lrn_limit->lrn_exceed_action;

	ppe_port_mgmt_info("Mac learn limit for port %d, mac learn limit: %d\n", mac_lrn_limit->port_id, mac_lrn_limit->port_learn_limit);
	ppe_ret = ppe_drv_port_mgmt_mac_lrn_limit_set(&drv_mac_lrn_limit);
	if (ppe_ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to set mac learn limit, err: %d\n", ppe_ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}
	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_mac_lrn_limit_set);

/*
 * ppe_port_mgmt_mac_filter_clear()
 *      Allow MAC address via PPE port MAC filter.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_mac_filter_clear(struct ppe_port_mac_filter *mac_filter)
{
	uint8_t ret;
	struct ppe_drv_port_mac_filter drv_mac_filter = {0};
	struct net_device *dev = NULL;

	if (!mac_filter) {
		ppe_port_mgmt_warn("mac_filter is NULL\n");
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	if (mac_filter->fid_valid) {
		if (!mac_filter->fid_name[0]) {
			ppe_port_mgmt_warn("FID name not provided while fid_valid is set\n");
			return PPE_PORT_MGMT_RET_FAILURE;
		}

		dev = dev_get_by_name(&init_net, mac_filter->fid_name);
		if (!dev) {
			ppe_port_mgmt_warn("Interface '%s' not found\n", mac_filter->fid_name);
			return PPE_PORT_MGMT_RET_FAILURE;
		}
		dev_put(dev);
	}

	ret = ppe_drv_port_mgmt_fid_get(dev, &mac_filter->fid_index);
	if (ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to clear MAC filter (err=%d)\n", ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	memcpy(&drv_mac_filter.mac, mac_filter->mac, ETH_ALEN);
	drv_mac_filter.fid_valid = mac_filter->fid_valid;
	drv_mac_filter.fid_index = mac_filter->fid_index;

	ret = ppe_drv_port_mgmt_mac_filter_clear(&drv_mac_filter);
	if (ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to clear MAC filter (err=%d)\n", ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_mac_filter_clear);

/*
 * ppe_port_mgmt_mac_filter_set()
 *      Block a MAC address via PPE port MAC filter.
 */
ppe_port_mgmt_ret_t ppe_port_mgmt_mac_filter_set(struct ppe_port_mac_filter *mac_filter)
{
	uint8_t ret;
	struct ppe_drv_port_mac_filter drv_mac_filter = {0};
	struct net_device *dev = NULL;

	if (!mac_filter) {
		ppe_port_mgmt_warn("mac_filter is NULL\n");
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	if (mac_filter->fid_valid) {
		if (!mac_filter->fid_name[0]) {
			ppe_port_mgmt_warn("FID name not provided while fid_valid is set\n");
			return PPE_PORT_MGMT_RET_FAILURE;
		}

		dev = dev_get_by_name(&init_net, mac_filter->fid_name);
		if (!dev) {
			ppe_port_mgmt_warn("Interface '%s' not found\n", mac_filter->fid_name);
			return PPE_PORT_MGMT_RET_FAILURE;
		}
		dev_put(dev);
	}

	ret = ppe_drv_port_mgmt_fid_get(dev, &mac_filter->fid_index);
	if (ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to clear MAC filter (err=%d)\n", ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	memcpy(&drv_mac_filter.mac, mac_filter->mac, ETH_ALEN);
	drv_mac_filter.fid_valid = mac_filter->fid_valid;
	drv_mac_filter.fid_index = mac_filter->fid_index;

	ret = ppe_drv_port_mgmt_mac_filter_set(&drv_mac_filter);
	if (ret != PPE_PORT_MGMT_RET_SUCCESS) {
		ppe_port_mgmt_warn("Failed to set MAC filter (err=%d)\n", ret);
		return PPE_PORT_MGMT_RET_FAILURE;
	}

	return PPE_PORT_MGMT_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_port_mgmt_mac_filter_set);

/*
 * ppe_port_mgmt_init()
 *      PORT_MGMT init API.
 */
void ppe_port_mgmt_init(struct dentry *d_rule)
{
	struct ppe_port_mgmt_base *port_mgmt_g = &ppe_port_mgmt_gbl;

	spin_lock_init(&port_mgmt_g->lock);

	INIT_LIST_HEAD(&port_mgmt_g->active_rules);

	ppe_port_mgmt_stats_debugfs_init(d_rule);
	ppe_port_mgmt_gen_dump_init(d_rule);
}
EXPORT_SYMBOL(ppe_port_mgmt_init);

/*
 * ppe_port_mgmt_deinit()
 *      PORT_MGMT deinit API.
 */
void ppe_port_mgmt_deinit(void)
{
	ppe_port_mgmt_stats_debugfs_exit();
	ppe_port_mgmt_gen_dump_exit();
}
EXPORT_SYMBOL(ppe_port_mgmt_deinit);
