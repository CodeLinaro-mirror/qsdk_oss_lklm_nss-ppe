/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/types.h>

#include <fal/fal_qos.h>

#include "ppe_drv.h"

/*
 * ppe_drv_cos_map_cosmap_flags_check()
 *	check action flags.
 */
static inline bool ppe_drv_cos_map_cosmap_flags_check(struct ppe_drv_cos_map_cosmap *map, uint32_t flags)
{
	return (map->flags & flags);
}

/*
 * ppe_drv_cos_map_cosmap_config()
 *	Build FAL CoS config from a cosmap action.
 */
static void ppe_drv_cos_map_cosmap_config(struct ppe_drv_cos_map_cosmap *map, fal_qos_cosmap_t *cos_cfg)
{
	/*
	 * Set DSCP fields in CoS config
	 */
	if (ppe_drv_cos_map_cosmap_flags_check(map, PPE_DRV_COS_MAP_ACTION_SET_DSCP)) {
		cos_cfg->internal_dscp = map->dscp << PPE_DRV_DSCP_SHIFT;
		cos_cfg->dscp_mask = PPE_DRV_DSCP_MASK;
		cos_cfg->dscp_en = A_TRUE;
	}

	/*
	 * Set PCP fields in CoS config
	 */
	if (ppe_drv_cos_map_cosmap_flags_check(map, PPE_DRV_COS_MAP_ACTION_SET_PCP)) {
		cos_cfg->internal_pcp = map->pcp;
		cos_cfg->pcp_en = A_TRUE;
	}

	/*
	 * Set DEI fields in CoS config
	 */
	if (ppe_drv_cos_map_cosmap_flags_check(map, PPE_DRV_COS_MAP_ACTION_SET_DEI)) {
		cos_cfg->internal_dei = map->dei;
		cos_cfg->dei_en = A_TRUE;
	}

	/*
	 * Set priority profile in CoS config
	 */
	if (ppe_drv_cos_map_cosmap_flags_check(map, PPE_DRV_COS_MAP_ACTION_SET_INT_PRI)) {
		cos_cfg->internal_pri = map->pri;
		cos_cfg->pri_en = A_TRUE;
	}

	/*
	 * Set drop precedence profile in CoS config
	 */
	if (ppe_drv_cos_map_cosmap_flags_check(map, PPE_DRV_COS_MAP_ACTION_SET_DP)) {
		cos_cfg->internal_dp = map->dp;
		cos_cfg->dp_en = A_TRUE;
	}
}

/*
 * ppe_drv_cos_map_config_set()
 *	Issues the FAL cosmap set call for the given switch, based on cfg->type.
 */
static ppe_drv_ret_t ppe_drv_cos_map_config_set(uint32_t switch_id, struct ppe_drv_cos_map_cfg *cfg, fal_qos_cosmap_t *cos_cfg)
{
	sw_error_t err;

	switch (cfg->type) {
		case PPE_DRV_COS_MAP_TYPE_TOS:
			err = fal_qos_cosmap_dscp_set(switch_id, cfg->group_id, cfg->val, cos_cfg);
			break;

		case PPE_DRV_COS_MAP_TYPE_TCI:
			err = fal_qos_cosmap_pcp_set(switch_id, cfg->group_id, cfg->val, cos_cfg);
			break;

		default:
			return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	if (err != SW_OK) {
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_cos_map_cosmap_set()
 *	Sets internal PCP, internal priority, internal DP,
 * and internal DSCP-based on packet DSCP/PCP.
 */
ppe_drv_ret_t ppe_drv_cos_map_cosmap_set(struct ppe_drv_cos_map_cfg *cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_qos_cosmap_t cos_cfg = {0};
	ppe_drv_ret_t ret;

	if (!cfg) {
		ppe_drv_warn("%p cos map configuration failed", cfg);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	ppe_drv_cos_map_cosmap_config(&cfg->map, &cos_cfg);

	ppe_drv_trace("%p cosmap set configuration type:%d val:%d actions:"
		"dscp_en:%d internal_dscp:%d pcp_en:%d internal_pcp:%d dei_en:%d internal_dei:%d"
		"pri_en:%d internal_pri:%d dp_en:%d internal_dp:%d", cfg, cfg->type, cfg->val, cos_cfg.dscp_en,
		cos_cfg.internal_dscp, cos_cfg.pcp_en, cos_cfg.internal_pcp, cos_cfg.dei_en, cos_cfg.internal_dei,
		cos_cfg.pri_en, cos_cfg.internal_pri, cos_cfg.dp_en, cos_cfg.internal_dp);

	spin_lock_bh(&p->lock);
	ret = ppe_drv_cos_map_config_set(PPE_DRV_SWITCH_ID, cfg, &cos_cfg);
	spin_unlock_bh(&p->lock);

	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p cos map configuration failed for cos_type:%d val:%d flags:%d", cfg, cfg->type, cfg->val, cfg->map.flags);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	ppe_drv_trace("%p cos map configuration done for cos_type:%d val:%d flags:%d", cfg, cfg->type, cfg->val, cfg->map.flags);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_cos_map_cosmap_set);

/*
 * ppe_drv_cos_map_set_htt()
 *	Sets internal PCP/DSCP-based CoS map on the HTT switch.
 *
 * The caller must check ppe_drv_cos_map_htt_switch_is_enabled() first.
 *
 * Lock-less: unlike switch 0 (memory-mapped), the HTT switch is accessed
 * over MDIO, whose FAL/SSDK path can sleep (mutex_lock(), usleep_range()).
 * Callers MUST NOT hold any spinlock (or other atomic-context lock) across
 * this call.
 */
ppe_drv_ret_t ppe_drv_cos_map_set_htt(struct ppe_drv_cos_map_cfg *cfg)
{
	fal_qos_cosmap_t cos_cfg = {0};

	if (!cfg) {
		ppe_drv_warn("%p cos map configuration failed on HTT switch", cfg);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	ppe_drv_cos_map_cosmap_config(&cfg->map, &cos_cfg);

	if (ppe_drv_cos_map_config_set(PPE_DRV_HTT_SWITCH_ID, cfg, &cos_cfg) != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p cos map configuration failed on HTT switch for cos_type:%d val:%d flags:%d", cfg, cfg->type, cfg->val, cfg->map.flags);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	ppe_drv_trace("%p cos map configuration done on HTT switch for cos_type:%d val:%d flags:%d", cfg, cfg->type, cfg->val, cfg->map.flags);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_cos_map_set_htt);

/*
 * ppe_drv_cos_map_htt_switch_is_enabled()
 *	Checks whether HTT-switch CoS-map mirroring is enabled, which needs
 *	an HTT switch to be present and the htt_switch_en sysctl to be set.
 */
bool ppe_drv_cos_map_htt_switch_is_enabled(void)
{
#ifdef NSS_PPE_DSA_QUEUE_SUPPORT
	return ppe_drv_gbl->htt_switch_valid && htt_switch_en;
#else
	return false;
#endif
}
EXPORT_SYMBOL(ppe_drv_cos_map_htt_switch_is_enabled);

/*
 * ppe_drv_cos_map_port_group_set()
 *	 Sets port group ID for PCP, DSCP, and flow-based QOS.
 */
ppe_drv_ret_t ppe_drv_cos_map_port_group_set(uint32_t port_id, struct ppe_drv_cos_map_port_group_cfg *cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_qos_group_t grp_cfg = {0};
	sw_error_t err;

	if (!cfg) {
		ppe_drv_warn("%p cos port group configuration failed", cfg);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	/*
	 * Set DSCP, PCP, flow group
	 */
	grp_cfg.pcp_group = (uint8_t)cfg->tci_grp_id;
	grp_cfg.dscp_group = (uint8_t)cfg->tos_grp_id;
	grp_cfg.flow_group = (uint8_t)cfg->flow_grp_id;

	spin_lock_bh(&p->lock);
	err = fal_qos_port_group_set(PPE_DRV_SWITCH_ID, port_id, &grp_cfg);
	if (err != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p cos port group configuration failed for port:%d", cfg, port_id);
		return PPE_DRV_RET_COS_MAP_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);
	ppe_drv_trace("%p cos port group configuration done for port:%d", cfg, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_cos_map_port_group_set);
