/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */
#include "ppe_drv.h"
#include <fal/fal_fdb.h>

/*
 * ppe_drv_port_mgmt_entries_init()
 * 	Initialize the values for port_isol tables.
 */
void ppe_drv_port_mgmt_entries_init(struct ppe_drv_port_mgmt *port_mgmt)
{
#ifdef NSS_PPE_DRV_PORT_ISOL_SUPPORT
	/*
	 * Initialize:
	 *
	 * OMCI index map: default all ports to invalid index
	 * Isolation bitmaps: disable subgroup flooding for all ports
	 * OMCI index info: mark all entries as invalid
	 * VP profile info: mark all entries as invalid
	 *
	 */
	for (size_t id = 0; id < PPE_DRV_PORTS_MAX; id++) {
		port_mgmt->port_omci_index_map[id] = (uint8_t)PPE_DRV_OMCI_INDEX_MAX;
	}

	for (size_t i = 0; i < PPE_DRV_OMCI_INDEX_MAX; i++) {
		port_mgmt->port_phy_isol_bitmap[i] = 0x0;
		port_mgmt->port_vp_isol_bitmap[i]  = 0x0;
	}

	for (size_t i = 0; i < PPE_DRV_OMCI_INDEX_MAX; i++) {
		port_mgmt->omci_index_info[i].omci_index = (uint8_t)PPE_DRV_OMCI_INDEX_MAX;
		port_mgmt->omci_index_info[i].in_use = false;
	}

	for (size_t i = 0; i < PPE_DRV_VP_PROFILE_ID_MAX; i++) {
		port_mgmt->vp_profile_id_info[i].profile_id = (uint8_t)PPE_DRV_VP_PROFILE_ID_MAX;
		port_mgmt->vp_profile_id_info[i].in_use = false;
	}

	/*
	 * Clear action tables: default set flooding
	 */
	memset(port_mgmt->port_phy_isol_action, 0xAA, sizeof(port_mgmt->port_phy_isol_action));
	memset(port_mgmt->port_vp_isol_action, 0xAA, sizeof(port_mgmt->port_vp_isol_action));
#ifdef NSS_PPE_PON_SUPPORT
	if (gem_port_bitmap)
		port_mgmt->us_port_id =  __builtin_ffs(gem_port_bitmap) - 1;
#endif
#endif
}

/*
 * ppe_drv_port_mgmt_entries_alloc()
 * 	Allocate the memory for port mgmt.
 */
struct ppe_drv_port_mgmt *ppe_drv_port_mgmt_entries_alloc(void)
{
	struct ppe_drv_port_mgmt *port_mgmt;

	port_mgmt = kzalloc(sizeof(*port_mgmt), GFP_KERNEL);
	if (!port_mgmt) {
		pr_warn("Port Mgmt: Failed to allocate table entries\n");
		return NULL;
	}

	spin_lock_init(&port_mgmt->lock);
	INIT_LIST_HEAD(&port_mgmt->active_rules);
	ppe_drv_port_mgmt_entries_init(port_mgmt);
	return port_mgmt;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_entries_alloc);

/*
 * ppe_drv_port_mgmt_entries_free()
 * 	Free the memory for Port Mgmt
 */
void ppe_drv_port_mgmt_entries_free(struct ppe_drv_port_mgmt *port_mgmt)
{
	if (!port_mgmt)
		return;

	kfree(port_mgmt);
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_entries_free);

#ifdef NSS_PPE_DRV_PORT_ISOL_SUPPORT
/*
 * ppe_drv_port_mgmt_set_omci_index()
 * 	Set the omci index in omci field.
 */
void ppe_drv_port_mgmt_set_omci_index(struct ppe_drv_port_mgmt *port_mgmt, uint8_t omci_index, uint8_t dev_id)
{
	port_mgmt->port_omci_index_map[dev_id] = omci_index;
}

/*
 * ppe_drv_port_mgmt_get_exist_vp_profile_id()
 * 	Get the existing VP profile
 */
uint8_t ppe_drv_port_mgmt_get_exist_vp_profile_id(struct ppe_drv_port_mgmt *port_mgmt, uint32_t port_id)
{
	uint8_t profile_id;
	/*
	 * The profile_id is reseved for phy_ports(0-8)
	 */
	if (PPE_DRV_PHY_PORT_CHK(port_id)) {
		port_mgmt->vp_profile_id_info[profile_id].in_use = true;
		port_mgmt->vp_profile_id_info[profile_id].port_id = port_id;
		return port_id;
	}

	/*
	 * Check if we have any existing profile_id for the port_id
	 */
	for (profile_id = PPE_DRV_PHYSICAL_MAX; profile_id < PPE_DRV_VP_PROFILE_ID_MAX; profile_id++) {
		if (port_mgmt->vp_profile_id_info[profile_id].in_use &&
				(port_mgmt->vp_profile_id_info[profile_id].port_id == port_id)) {
			return profile_id;
		}
	}
	return PPE_DRV_VP_PROFILE_ID_MAX;
}

/*
 * ppe_drv_port_mgmt_get_vp_profile_id()
 * 	Get the existing or new profile ID.
 */
uint8_t ppe_drv_port_mgmt_get_vp_profile_id(struct ppe_drv_port_mgmt *port_mgmt, uint32_t port_id)
{
	uint8_t profile_id;

	/*
	 * Check if we have any existing profile_id for the port_id
	 * Else, assign a new profile ID.
	 */
	profile_id = ppe_drv_port_mgmt_get_exist_vp_profile_id(port_mgmt, port_id);
	if (profile_id != PPE_DRV_VP_PROFILE_ID_MAX) {
		return profile_id;
	}

	for (profile_id = PPE_DRV_PHYSICAL_MAX; profile_id < PPE_DRV_VP_PROFILE_ID_MAX; profile_id++) {
		if (!port_mgmt->vp_profile_id_info[profile_id].in_use) {
			port_mgmt->vp_profile_id_info[profile_id].in_use = true;
			port_mgmt->vp_profile_id_info[profile_id].port_id = port_id;
			return profile_id;
		}
	}
	return PPE_DRV_VP_PROFILE_ID_MAX;
}

/*
 * ppe_drv_port_mgmt_clear_vp_profile_id()
 * 	Reset the vp profile if not in use.
 */
void ppe_drv_port_mgmt_clear_vp_profile_id(struct ppe_drv_port_mgmt *port_mgmt, uint32_t port_id)
{
	uint8_t profile_id;
	profile_id = ppe_drv_port_mgmt_get_exist_vp_profile_id(port_mgmt, port_id);
	if (profile_id != PPE_DRV_VP_PROFILE_ID_MAX) {
		port_mgmt->vp_profile_id_info[profile_id].in_use = false;
		port_mgmt->vp_profile_id_info[profile_id].port_id = PPE_DRV_PORTS_MAX;
		port_mgmt->vp_profile_id_info[profile_id].profile_id = PPE_DRV_VP_PROFILE_ID_MAX;
	}
}

/*
 * ppe_drv_get_omci_index()
 * 	Get the Omci index corresponding to OMCI ID.
 */
uint16_t ppe_drv_get_omci_index(struct ppe_drv_port_mgmt *port_mgmt, uint32_t omci_id)
{
	uint16_t omci_index;

	/*
	 * Check if we have any existing omci_index for the omci_id
	 * Else, Assign a new OMCI index.
	 */
	for (omci_index = 0; omci_index < PPE_DRV_OMCI_INDEX_MAX; omci_index++) {
		if (port_mgmt->omci_index_info[omci_index].in_use &&
				(port_mgmt->omci_index_info[omci_index].omci_id == omci_id)) {
			port_mgmt->omci_index_info[omci_index].port_cnt += 1;
			return omci_index;
		}
	}

	for (omci_index = 0; omci_index < PPE_DRV_OMCI_INDEX_MAX; omci_index++) {
		if (!port_mgmt->omci_index_info[omci_index].in_use) {
			port_mgmt->omci_index_info[omci_index].in_use = true;
			port_mgmt->omci_index_info[omci_index].omci_id = omci_id;
			port_mgmt->omci_index_info[omci_index].port_cnt = 1;
			return omci_index;
		}
	}
	return PPE_DRV_OMCI_INDEX_MAX;
}

/*
 * ppe_drv_clear_omci_index()
 * 	Reset the flag if OMCI index.
 */
void ppe_drv_clear_omci_index(struct ppe_drv_port_mgmt *port_mgmt, uint16_t omci_index)
{
	if (port_mgmt->omci_index_info[omci_index].in_use) {
		port_mgmt->omci_index_info[omci_index].in_use = false;
		port_mgmt->omci_index_info[omci_index].omci_id = PPE_DRV_OMCI_INDEX_MAX;
		port_mgmt->omci_index_info[omci_index].port_cnt = 0;
	}
}

/*
 * ppe_drv_deref_omci_index()
 * 	Deref the OMCI index if not in use.
 */
void ppe_drv_deref_omci_index(struct ppe_drv_port_mgmt *port_mgmt, uint16_t omci_index)
{
	if (omci_index != PPE_DRV_OMCI_INDEX_MAX) {
		port_mgmt->omci_index_info[omci_index].port_cnt--;
		if(!port_mgmt->omci_index_info[omci_index].port_cnt)
			ppe_drv_clear_omci_index(port_mgmt, omci_index);
	}
}

/*
 * ppe_drv_port_isol_ctrl_set()
 * 	configure control actions for the corresponding profile ID.
 */
int ppe_drv_port_isol_ctrl_set(struct ppe_drv_port_mgmt *port_mgmt,
		uint32_t src_port_id,
		uint32_t dst_port_id,
		bool enable)
{
	sw_error_t err;
	fal_port_isol_ctrl_t isol_ctrl = {0};

	uint8_t src_isol_type = PPE_DRV_PHY_PORT_CHK(src_port_id) ? PPE_DRV_PORT_PHYSICAL : PPE_DRV_PORT_VIRTUAL;
	uint8_t dst_isol_type = PPE_DRV_PHY_PORT_CHK(dst_port_id) ? PPE_DRV_PORT_PHYSICAL : PPE_DRV_PORT_VIRTUAL;
	uint8_t src_profile_id = ppe_drv_port_mgmt_get_vp_profile_id(port_mgmt, src_port_id);
	uint8_t dst_profile_id = ppe_drv_port_mgmt_get_vp_profile_id(port_mgmt, dst_port_id);

	if (src_profile_id == PPE_DRV_VP_PROFILE_ID_MAX ||
			dst_profile_id == PPE_DRV_VP_PROFILE_ID_MAX)
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;

	/*
	 * Set isol control in PRE Routing table
	 */
	isol_ctrl.dir = FAL_DIR_INGRESS;
	isol_ctrl.enable = enable;
	isol_ctrl.isol_group_id = src_profile_id;
	err = fal_port_isol_ctrl_set(PPE_DRV_SWITCH_ID, src_isol_type, &isol_ctrl);
	if (err != SW_OK) {
		ppe_drv_warn("Failed to set port isolation control, err %d\n", err);
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	/*
	 * Set isol control in Post Routing table
	 */
	isol_ctrl.dir = FAL_DIR_EGRESS;
	isol_ctrl.enable = enable;
	isol_ctrl.isol_group_id = dst_profile_id;
	err = fal_port_isol_ctrl_set(PPE_DRV_SWITCH_ID, dst_isol_type, &isol_ctrl);
	if (err != SW_OK) {
		ppe_drv_warn("Failed to set port isolation control, err %d\n", err);
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	return err;
}

/*
 * ppe_drv_port_isol_action_set()
 *    Set port isolation action bitmap.
 */
ppe_drv_ret_t ppe_drv_port_isol_action_set(struct ppe_drv_port_mgmt *port_mgmt, int dev_id,
		uint32_t *action_tab,
		enum ppe_drv_port_type isol_type)
{
	sw_error_t err;
	fal_port_isol_act_idx_t isol_id = {0};
	fal_port_isol_act_t isol_act = {0};
	uint8_t vp_profile_id;

	switch (isol_type) {
		case PPE_DRV_PORT_VIRTUAL:
			vp_profile_id = ppe_drv_port_mgmt_get_vp_profile_id(port_mgmt, dev_id);
			if (vp_profile_id == PPE_DRV_VP_PROFILE_ID_MAX)
				return PPE_DRV_RET_FAILURE_INVALID_PARAM;

			isol_id.isol_group_id = vp_profile_id;
			isol_id.isol_type = FAL_ISOL_ACT_GROUP;
			memcpy(isol_act.act_bitmap, action_tab, sizeof(isol_act.act_bitmap));

			err = fal_port_isol_action_set(PPE_DRV_SWITCH_ID, &isol_id, &isol_act);
			if (err != SW_OK) {
				ppe_drv_warn("Failed to set port isolation action for port_id %d, err %d\n", dev_id, err);
				return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
			}
			break;

		case PPE_DRV_PORT_PHYSICAL:
			isol_id.isol_type = FAL_ISOL_ACT_PPORT;
			isol_id.pport_id = dev_id;
			memcpy(isol_act.act_bitmap, action_tab, sizeof(isol_act.act_bitmap));

			err = fal_port_isol_action_set(PPE_DRV_SWITCH_ID, &isol_id, &isol_act);
			if (err != SW_OK) {
				ppe_drv_warn("Failed to set port isolation action for port_id %d, err %d\n", dev_id, err);
				return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
			}
			break;

		default:
			ppe_drv_warn("%s: invalid isol_type %d\n", __func__, isol_type);
			return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	ppe_drv_info("Set port isolation: isol_act[3]:%d isol_act[2]:%d isol_act[1]:%d isol_act[0]:%d\n", isol_act.act_bitmap[3], isol_act.act_bitmap[2], isol_act.act_bitmap[1], isol_act.act_bitmap[0]);
	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_port_isol_bitmap_set()
 *    Set port isolation membership bitmap.
 */
ppe_drv_ret_t ppe_drv_port_isol_bitmap_set(int dev_id,
		uint64_t *bitmap_tab)
{
	sw_error_t err;
	err = fal_port_isol_member_set(PPE_DRV_SWITCH_ID, dev_id, *bitmap_tab);
	if (err != SW_OK) {
		ppe_drv_warn("Issue while pushing isolation bitmap\n");
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	ppe_drv_info("Bitmap set, dev_id: %d isol_bitmap: %lld\n", dev_id, *bitmap_tab);
	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_port_mgmt_isol_fal_set()
 * 	Apply isolation bitmap and action for port.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_isol_fal_set(struct ppe_drv_port_mgmt *port_mgmt, int dev_id,
		uint64_t *bitmap_tab,
		uint32_t *action_tab,
		enum ppe_drv_port_type isol_type)
{
	ppe_drv_ret_t ret;
	ret = ppe_drv_port_isol_bitmap_set(dev_id, bitmap_tab);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Failed to set isolation bitmap for %s port %d, err: %d\n",
				(isol_type == PPE_DRV_PORT_VIRTUAL) ? "VP" : "PHY", dev_id, ret);
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	ret = ppe_drv_port_isol_action_set(port_mgmt, dev_id, action_tab, isol_type);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Failed to set isolation action for %s port %d, err: %d\n",
				(isol_type == PPE_DRV_PORT_VIRTUAL) ? "VP" : "PHY", dev_id, ret);
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	return PPE_DRV_RET_SUCCESS;
}

/**
 * ppe_drv_port_mgmt_isol_bit_set()
 * 	Update isolation bitmap and action for a given OMCI ID.
 */
static inline void ppe_drv_port_mgmt_isol_bit_set(struct ppe_drv_port_mgmt *port_mgmt, bool is_phy,
		int omci_index, uint16_t idx, bool fwd)
{
	uint8_t bit_pos = idx;
	size_t byte_idx = bit_pos / 16;
	uint8_t shift_b  = (bit_pos % 16) * 2;

	if (byte_idx >= PPE_DRV_PORT_MGMT_ACT_ARR_SIZE)
		return;

	if (fwd) {
		if (is_phy) {
			/*
			 * Set membership within the OMCI group:
			 * Clear flood bit, and
			 * Clear 2-bit field to allow subgroup forwarding
			 */
			port_mgmt->port_phy_isol_bitmap[omci_index] |= (0x1ULL << bit_pos);
			port_mgmt->port_phy_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
		} else {
			/*
			 * Set membership within the OMCI group:
			 * Clear flood bit to restrict forwarding to isol
			 * Set the VP profile ID in isol_action.
			 */
			port_mgmt->port_vp_isol_bitmap[omci_index] |= port_mgmt->port_phy_isol_bitmap[omci_index];
			port_mgmt->port_vp_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
		}
	} else {
		if (is_phy) {
			/*
			 * Clear membership within the OMCI group:
			 * Set flood bit, and
			 * Set 2-bit field to restrict forwarding
			 */
			port_mgmt->port_phy_isol_bitmap[omci_index] &= ~(0x1ULL << bit_pos);
			port_mgmt->port_phy_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
			port_mgmt->port_phy_isol_action[omci_index][byte_idx] |=  (0x2ULL << shift_b);
		} else {
			/*
			 * Clear membership within the OMCI group:
			 * Set flood bit, and
			 * Set 2-bit field to restrict forwarding.
			 */
			port_mgmt->port_vp_isol_bitmap[omci_index] |= port_mgmt->port_phy_isol_bitmap[omci_index];
			port_mgmt->port_vp_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
			port_mgmt->port_vp_isol_action[omci_index][byte_idx] |=  (0x2ULL << shift_b);
		}
	}

	/*
	 * Always allow forwarding to CPU port 0
	 */
	port_mgmt->port_phy_isol_action[omci_index][0] &= ~(0x3ULL);
	port_mgmt->port_vp_isol_action[omci_index][0] &= ~(0x3ULL);

#ifdef NSS_PPE_PON_SUPPORT
	/*
	 * Always enable flooding and Forwarding to upstream port
	 */
	if (gem_port_bitmap && (port_mgmt->us_port_id < PPE_DRV_PHYSICAL_MAX)) {
		bit_pos = port_mgmt->us_port_id;
		byte_idx = bit_pos / 16;
		shift_b = (bit_pos % 16) * 2;
		port_mgmt->port_phy_isol_bitmap[omci_index] &= ~(0x1ULL << bit_pos);
		port_mgmt->port_vp_isol_bitmap[omci_index] &= ~(0x1ULL << bit_pos);
		port_mgmt->port_phy_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
		port_mgmt->port_vp_isol_action[omci_index][byte_idx] &= ~(0x3ULL << shift_b);
	}
#endif
}

/**
 * ppe_drv_port_mgmt_isol_apply_all()
 * 	Apply isolation bitmap and action for all ports with same omci index.
 */
static inline ppe_drv_ret_t ppe_drv_port_mgmt_isol_apply_all(struct ppe_drv_port_mgmt *port_mgmt,
		uint8_t omci_idx)
{
	ppe_drv_ret_t ret;
	int phy_port, vp_port;
#ifdef NSS_PPE_PON_SUPPORT
	uint64_t us_port_bitmap = (uint64_t)gem_port_bitmap;
#endif
	/*
	 * PHY table
	 *
	 * CASE 1: PHY<=>PHY mapping
	 * 	Flooding allowed to PHY ports and US ports.
	 */
	for (phy_port = PPE_DRV_PHYSICAL_START; phy_port < PPE_DRV_PHYSICAL_MAX; phy_port++) {
		/*
		 * Modify the bitmap and action fields for OMCI index.
		 */
		if (omci_idx == port_mgmt->port_omci_index_map[phy_port]) {
			ret = ppe_drv_port_mgmt_isol_fal_set(port_mgmt, phy_port,
					&port_mgmt->port_phy_isol_bitmap[omci_idx],
					port_mgmt->port_phy_isol_action[omci_idx],
					PPE_DRV_PORT_PHYSICAL);
			if (ret != PPE_DRV_RET_SUCCESS)
				return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
		}
	}

	/*
	 * VP table
	 *
	 * CASE 2: VP<=>US(PHY) mapping
	 * 	Flooding allowed to US port only.
	 *
	 * CASE 3: PHY<=>VP mapping
	 * 	Flooding allowed to phy ports and US port.
	 */
#ifdef NSS_PPE_PON_SUPPORT
	if (gem_port_bitmap) {
		for (vp_port = PPE_DRV_VIRTUAL_START; vp_port < PPE_DRV_PORTS_MAX; vp_port++) {
			if (omci_idx == port_mgmt->port_omci_index_map[vp_port]) {
				ret = ppe_drv_port_mgmt_isol_fal_set(port_mgmt, vp_port,
						&us_port_bitmap, port_mgmt->port_vp_isol_action[omci_idx],
						PPE_DRV_PORT_VIRTUAL);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

				ret = ppe_drv_port_isol_ctrl_set(port_mgmt, vp_port,
						port_mgmt->us_port_id, A_TRUE);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

				ret = ppe_drv_port_isol_ctrl_set(port_mgmt, port_mgmt->us_port_id,
						vp_port, A_TRUE);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
			}
		}
	}
#endif
	for (vp_port = PPE_DRV_VIRTUAL_START; vp_port < PPE_DRV_PORTS_MAX; vp_port++) {
		for (phy_port = PPE_DRV_PHYSICAL_START; phy_port < PPE_DRV_PHYSICAL_MAX; phy_port++) {
			if (omci_idx == port_mgmt->port_omci_index_map[phy_port]) {
				ret = ppe_drv_port_mgmt_isol_fal_set(port_mgmt, vp_port,
						&port_mgmt->port_vp_isol_bitmap[omci_idx],
						port_mgmt->port_vp_isol_action[omci_idx],
						PPE_DRV_PORT_VIRTUAL);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

				ret = ppe_drv_port_isol_ctrl_set(port_mgmt, phy_port,
						vp_port, A_TRUE);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

				ret = ppe_drv_port_isol_ctrl_set(port_mgmt, vp_port,
						phy_port, A_TRUE);
				if (ret != PPE_DRV_RET_SUCCESS)
					return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
			}
		}
	}
	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_port_mgmt_isol_configure()
 * 	Configure isolation for a given port and update context.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_isol_configure(struct ppe_drv_port_mgmt_isol *isol_info)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port_mgmt *port_mgmt = p->port_mgmt;
	uint8_t cur_omci_idx;
	uint8_t new_omci_idx;
	uint8_t vp_profile_id;
	uint8_t idx;
	uint16_t dev_id;
	uint32_t omci_id;
	bool is_phy, is_vp;
	ppe_drv_ret_t ret;

	if (!isol_info)
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

	dev_id = isol_info->port_id;
	omci_id = isol_info->omci_id;
	is_phy = PPE_DRV_PHY_PORT_CHK(dev_id);
	is_vp  = PPE_DRV_VIRTUAL_PORT_CHK(dev_id);

	if (!is_phy && !is_vp)
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

	cur_omci_idx = port_mgmt->port_omci_index_map[dev_id];
	new_omci_idx = ppe_drv_get_omci_index(port_mgmt, omci_id);
	if (new_omci_idx == PPE_DRV_OMCI_INDEX_MAX)
		return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;

	ppe_drv_port_mgmt_set_omci_index(port_mgmt, new_omci_idx, dev_id);

	if (is_vp) {
		vp_profile_id = ppe_drv_port_mgmt_get_vp_profile_id(port_mgmt, dev_id);
		if (vp_profile_id == PPE_DRV_VP_PROFILE_ID_MAX)
			return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
	}

	idx = is_phy ? dev_id : vp_profile_id;

	/*
	 * Clear bit for old isol map
	 */
	if (cur_omci_idx != PPE_DRV_OMCI_INDEX_MAX)
		ppe_drv_port_mgmt_isol_bit_set(port_mgmt, is_phy, cur_omci_idx, idx, false);
	/*
	 * Set bit for flooding
	 */
	ppe_drv_port_mgmt_isol_bit_set(port_mgmt, is_phy, new_omci_idx, idx, true);

	/*
	 * Update OMCI Index map
	 */
	if (cur_omci_idx != PPE_DRV_OMCI_INDEX_MAX)
		ret = ppe_drv_port_mgmt_isol_apply_all(port_mgmt, cur_omci_idx);
	ret = ppe_drv_port_mgmt_isol_apply_all(port_mgmt, new_omci_idx);
	ppe_drv_deref_omci_index(port_mgmt, cur_omci_idx);

	return ret;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_isol_configure);

/*
 * ppe_drv_port_mgmt_act_ctrl_fill()
 *     Set multicast/broadcast isolation action control.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_act_ctrl_fill(struct ppe_drv_port_mgmt *port_mgmt)
{
	sw_error_t err;
	fal_port_isol_act_ctrl_t fal_act_ctrl = {0};

	fal_act_ctrl.bc_isol_en = port_mgmt->bc_isol_en;
	fal_act_ctrl.mc_isol_en = port_mgmt->mc_isol_en;

	err = fal_port_isol_action_ctrl_set(PPE_DRV_SWITCH_ID, &fal_act_ctrl);
	if (err != SW_OK) {
		ppe_drv_warn("Failed to set port isolation action control, err %d\n", err);
		return PPE_DRV_RET_PORT_MGMT_ACT_CONFIG_FAIL;
	}

	ppe_drv_info("Set port isolation action control: mc_isol_en=%d, bc_isol_en=%d\n", fal_act_ctrl.mc_isol_en, fal_act_ctrl.bc_isol_en);
	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_port_mgmt_act_ctrl_set()
 * 	Sets the action control fields.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_act_ctrl_set(struct ppe_drv_port_mgmt_isol *isol_info)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port_mgmt *port_mgmt = p->port_mgmt;

	port_mgmt->mc_isol_en = isol_info->mc_isol_en;
	port_mgmt->bc_isol_en = isol_info->bc_isol_en;
	ppe_drv_port_mgmt_act_ctrl_fill(port_mgmt);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_act_ctrl_set);

/*
 * ppe_drv_port_mgmt_default_isol_set()
 * 	Set the default isolation configuration.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_default_isol_set(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port_mgmt *port_mgmt = p->port_mgmt;
	uint32_t action[PPE_DRV_PORT_MGMT_ACT_ARR_SIZE];
	uint64_t isol;
	int phy_port, vp_port;
	ppe_drv_ret_t ret;

	for (phy_port = PPE_DRV_PHYSICAL_START; phy_port < PPE_DRV_PHYSICAL_MAX; phy_port++) {
		if (port_mgmt->port_omci_index_map[phy_port] != PPE_DRV_OMCI_INDEX_MAX) {
			memset(action, 0x0, sizeof(action));
			memset(&isol, 0xFF, sizeof(isol));
			ret = ppe_drv_port_mgmt_isol_fal_set(port_mgmt, phy_port,
					&isol, action, PPE_DRV_PORT_PHYSICAL);
			if (ret != PPE_DRV_RET_SUCCESS)
				return PPE_DRV_RET_PORT_MGMT_ISOL_DEFAULT_FAIL;
		}
	}

	for (vp_port = PPE_DRV_VIRTUAL_START; vp_port < PPE_DRV_PORTS_MAX; vp_port++) {
		for (phy_port = PPE_DRV_PHYSICAL_START; phy_port < PPE_DRV_PHYSICAL_MAX; phy_port++) {
			ret = ppe_drv_port_isol_ctrl_set(port_mgmt, phy_port,
					vp_port, A_FALSE);
			if (ret != PPE_DRV_RET_SUCCESS)
				return PPE_DRV_RET_PORT_MGMT_ISOL_DEFAULT_FAIL;

			ret = ppe_drv_port_isol_ctrl_set(port_mgmt, vp_port,
					phy_port, A_FALSE);
			if (ret != PPE_DRV_RET_SUCCESS)
				return PPE_DRV_RET_PORT_MGMT_ISOL_DEFAULT_FAIL;
		}
	}

	ppe_drv_port_mgmt_entries_init(port_mgmt);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_default_isol_set);
#else
ppe_drv_ret_t ppe_drv_port_mgmt_isol_configure(struct ppe_drv_port_mgmt_isol *isol_info)
{
	return PPE_DRV_RET_PORT_MGMT_ISOL_CONFIG_FAIL;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_isol_configure);

ppe_drv_ret_t ppe_drv_port_mgmt_act_ctrl_set(struct ppe_drv_port_mgmt_isol *isol_info)
{
	return PPE_DRV_RET_PORT_MGMT_ACT_CONFIG_FAIL;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_act_ctrl_set);

ppe_drv_ret_t ppe_drv_port_mgmt_default_isol_set(void)
{
	return PPE_DRV_RET_PORT_MGMT_ISOL_DEFAULT_FAIL;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_default_isol_set);
#endif

/*
 * ppe_drv_port_mgmt_mac_lrn_limit_set()
 *	Set the MAC learning limit configuration.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_mac_lrn_limit_set(struct ppe_drv_port_mac_lrn_limit *mac_lrn_limit)
{
	sw_error_t err;
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	err = fal_port_fdb_learn_limit_set(PPE_DRV_SWITCH_ID, mac_lrn_limit->port_id,
			mac_lrn_limit->port_learn_limit_en, mac_lrn_limit->port_learn_limit);
	if (err != SW_OK) {
		ppe_drv_warn("Failed to set port mac leran limit, err %d\n", err);
		spin_unlock_bh(&p->lock);
		return PPE_DRV_RET_PORT_MGMT_MAC_LRN_LMT_CONFIG_FAIL;
	}

	if (mac_lrn_limit->lrn_exceed_action_en) {
		fal_port_fdb_learn_exceed_cmd_set(PPE_DRV_SWITCH_ID, mac_lrn_limit->port_id,
				(fal_fwd_cmd_t)mac_lrn_limit->lrn_exceed_action);
		if (err != SW_OK) {
			ppe_drv_warn("Failed to set action command when mac learn limit exceeds, err %d\n", err);
			spin_unlock_bh(&p->lock);
			return PPE_DRV_RET_PORT_MGMT_MAC_LRN_LMT_CONFIG_FAIL;
		}
	}

	ppe_drv_info("port mac learn limit is enabled:%d for port_id: %d and limit: %d\n", mac_lrn_limit->port_learn_limit_en, mac_lrn_limit->port_id, mac_lrn_limit->port_learn_limit);
	spin_unlock_bh(&p->lock);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_mac_lrn_limit_set);


/*
 * ppe_drv_port_mgmt_fid_get()
 *	Gets the FID associated with the MAC filter.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_fid_get(struct net_device *dev, uint8_t* fid_index)
{
	struct ppe_drv_iface *iface;
	struct ppe_drv_vsi *vsi;

	/* Resolve iface by FID name when available, else via upstream GEM bitmap */
	if (dev) {
		iface = ppe_drv_iface_get_by_dev(dev);
		if (!iface) {
			ppe_drv_warn("Failed to resolve PPE iface for dev '%s'\n", dev->name);
			return PPE_DRV_RET_PORT_MGMT_FID_GET_FAIL;
		}

	} else {
#ifdef NSS_PPE_PON_SUPPORT
		/*
		 * If no VSI, then derive from gem port.
		 */
		if (!gem_port_bitmap) {
			ppe_drv_warn("No upstream port (gem_port_bitmap=0) and no FID name\n");
			return PPE_DRV_RET_PORT_MGMT_FID_GET_FAIL;
		}

		int port_idx = __builtin_ffs(gem_port_bitmap) - 1;
		iface = ppe_drv_iface_get_by_idx(port_idx);
		if (!iface) {
			ppe_drv_warn("Failed to resolve PPE iface for upstream port idx=%d\n", port_idx);
			return PPE_DRV_RET_PORT_MGMT_FID_GET_FAIL;
		}
#else
		ppe_drv_warn("No upstream port (gem_port_bitmap=0) and no FID name\n");
		return PPE_DRV_RET_PORT_MGMT_FID_GET_FAIL;
#endif
	}

	/* Get VSI info */
	vsi = ppe_drv_iface_vsi_get(iface);
	if (!vsi) {
		ppe_drv_warn("Invalid VSI for given iface\n");
		return PPE_DRV_RET_PORT_MGMT_FID_GET_FAIL;
	}

	*fid_index = vsi->index;
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_fid_get);

/*
 * ppe_drv_port_mgmt_mac_filter_clear()
 *	Clears the MAC filter configuration.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_mac_filter_clear(struct ppe_drv_port_mac_filter *mac_filter)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_fdb_entry_t entry;
	sw_error_t err;
	ppe_drv_ret_t ret = PPE_DRV_RET_SUCCESS;

	if (!mac_filter) {
		ppe_drv_warn("Invalid arguments: mac_filter=%p\n", mac_filter);
		return PPE_DRV_RET_PORT_MGMT_MAC_FILTER_CLR_FAIL;
	}

	memset(&entry, 0, sizeof(entry));
	memcpy(&entry.addr, mac_filter->mac, ETH_ALEN);
	entry.fid = mac_filter->fid_index;

	spin_lock_bh(&p->lock);

	/* Look up existing entry; clear only if present and currently DROPPING */
	err = fal_fdb_entry_search(PPE_DRV_SWITCH_ID, &entry);
	if (err != SW_OK) {
		ppe_drv_warn("FDB entry not found for MAC %pM (fid=%u), err=%d\n",
				mac_filter->mac, entry.fid, err);
		ret = PPE_DRV_RET_PORT_MGMT_MAC_FILTER_CLR_FAIL;
		goto out_unlock;
	}

	if (entry.dacmd == FAL_MAC_DROP && entry.sacmd == FAL_MAC_DROP) {
		entry.dacmd = FAL_MAC_RDT_TO_CPU;
		entry.sacmd = FAL_MAC_RDT_TO_CPU;

		err = fal_fdb_entry_add(PPE_DRV_SWITCH_ID, &entry);
		if (err != SW_OK) {
			ppe_drv_warn("Failed to program RDT-to-CPU for MAC %pM (fid=%u), err=%d\n",
					mac_filter->mac, entry.fid, err);
			ret = PPE_DRV_RET_PORT_MGMT_MAC_FILTER_CLR_FAIL;
			goto out_unlock;
		}

		ppe_drv_info("RDT-to-CPU applied for MAC %pM (fid=%u)\n",
				mac_filter->mac, entry.fid);
	} else {
		/* Not a DROP entry; treat as no-op */
		ppe_drv_info("No-op: FDB for MAC %pM (fid=%u) not in DROP state (dacmd=%u sacmd=%u)\n",
				mac_filter->mac, entry.fid, entry.dacmd, entry.sacmd);
	}

out_unlock:
	spin_unlock_bh(&p->lock);
	return ret;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_mac_filter_clear);

/*
 * ppe_drv_port_mgmt_mac_filter_set()
 *	Sets the MAC filter configuration.
 */
ppe_drv_ret_t ppe_drv_port_mgmt_mac_filter_set(struct ppe_drv_port_mac_filter *mac_filter)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_fdb_entry_t entry;
	sw_error_t err;

	if (!mac_filter) {
		ppe_drv_warn("Invalid arguments: p=%p mac_filter=%p\n", p, mac_filter);
		return PPE_DRV_RET_PORT_MGMT_MAC_FILTER_SET_FAIL;
	}

	memset(&entry, 0, sizeof(entry));
	memcpy(&entry.addr, mac_filter->mac, ETH_ALEN);
	entry.fid = mac_filter->fid_index;

	spin_lock_bh(&p->lock);

	/* Find the existing FDB entry. */
	fal_fdb_entry_search(PPE_DRV_SWITCH_ID, &entry);

	/* Update or Add in FDB */
	entry.dacmd = FAL_MAC_DROP;
	entry.sacmd = FAL_MAC_DROP;
	err = fal_fdb_entry_add(PPE_DRV_SWITCH_ID, &entry);

	spin_unlock_bh(&p->lock);

	if (err != SW_OK) {
		ppe_drv_warn("Failed to add/update drop FDB for MAC %pM (fid=%u), err=%d\n",
				mac_filter->mac, entry.fid, err);
		return PPE_DRV_RET_PORT_MGMT_MAC_FILTER_SET_FAIL;
	}

	ppe_drv_info("Drop FDB entry applied for MAC %pM (fid=%u)\n",
			mac_filter->mac, entry.fid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_port_mgmt_mac_filter_set);
