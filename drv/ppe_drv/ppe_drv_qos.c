/*
 * Copyright (c) 2017-2020, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/types.h>
#include <linux/rcupdate.h>

#include <fal/fal_pon.h>
#include <fal/fal_qm.h>
#include <fal/fal_qos.h>
#include <fal/fal_shaper.h>
#include <fal/fal_bm.h>

#include "ppe_drv.h"

/*
 * Token number is assigned the max value so as to
 * avoid the packet loss at the start of shaper process.
 */
#define PPE_DRV_QOS_TOKEN_MAX		0x3fffffff

/*
 * Frame mode.
 */
enum ppe_drv_qos_frame_mode {
	PPE_DRV_QOS_FRAME_MODE_IPG_PREAMBLE_FRAME_CRC,	/* IPG + Preamble + Frame + CRC */
	PPE_DRV_QOS_FRAME_MODE_FRAME_CRC,	/* Frame + CRC */
	PPE_DRV_QOS_FRAME_MODE_L3_EXCLUDE_CRC,	/* after Ethernet type exclude CRC*/
	PPE_DRV_QOS_FRAME_MODE_MAX		/* Maximum frame mode */
};
typedef enum ppe_drv_qos_frame_mode ppe_drv_qos_frame_mode_t;

/*
 * Dynamic list of per-port shaper shadow state entries.
 */
static LIST_HEAD(ppe_drv_qos_port_state_list);
static DEFINE_SPINLOCK(ppe_drv_qos_port_state_lock);

/*
 * ppe_drv_qos_shaper_cfg_from_fal()
 *	Convert FAL shaper configuration to shadow shaper configuration.
 */
static inline void ppe_drv_qos_shaper_cfg_from_fal(struct ppe_drv_qos_shaper_cfg *dst,
						    const fal_shaper_config_t *src)
{
	if (!dst || !src) {
		return;
	}

	dst->c_shaper_en = src->c_shaper_en;
	dst->e_shaper_en = src->e_shaper_en;
	dst->cir = src->cir;
	dst->cbs = src->cbs;
	dst->eir = src->eir;
	dst->ebs = src->ebs;
	dst->couple_en = src->couple_en;
	dst->meter_unit = src->meter_unit;
	dst->frame_mode = src->shaper_frame_mode;
}

/*
 * ppe_drv_qos_shaper_cfg_to_fal()
 *	Convert shadow shaper configuration to FAL shaper configuration.
 */
static inline void ppe_drv_qos_shaper_cfg_to_fal(const struct ppe_drv_qos_shaper_cfg *src,
						  fal_shaper_config_t *dst)
{
	if (!dst) {
		return;
	}

	memset(dst, 0, sizeof(*dst));
	if (!src) {
		return;
	}

	dst->c_shaper_en = src->c_shaper_en;
	dst->e_shaper_en = src->e_shaper_en;
	dst->cir = src->cir;
	dst->cbs = src->cbs;
	dst->eir = src->eir;
	dst->ebs = src->ebs;
	dst->couple_en = src->couple_en;
	dst->meter_unit = src->meter_unit;
	dst->shaper_frame_mode = (fal_shaper_frame_mode_t)src->frame_mode;
}

/*
 * ppe_drv_qos_port_state_find()
 *	Find the state entry for a given port_id.
 *
 * Caller must hold ppe_drv_qos_port_state_lock.
 */
static struct ppe_drv_qos_port_state *ppe_drv_qos_port_state_find(uint32_t port_id)
{
	struct ppe_drv_qos_port_state *state;

	list_for_each_entry(state, &ppe_drv_qos_port_state_list, list) {
		if (state->port_id == port_id) {
			return state;
		}
	}

	return NULL;
}

/*
 * ppe_drv_qos_state_find_by_l0sp()
 *	Find state entry that owns the given l0sp id.
 *
 * Caller must hold ppe_drv_qos_port_state_lock.
 */
static struct ppe_drv_qos_port_state *ppe_drv_qos_state_find_by_l0sp(uint32_t l0sp_id)
{
	struct ppe_drv_qos_port_state *state;
	uint32_t base;
	uint32_t max;

	list_for_each_entry(state, &ppe_drv_qos_port_state_list, list) {
		base = state->port.base[PPE_DRV_QOS_RES_TYPE_L0_SP];
		max = state->port.max[PPE_DRV_QOS_RES_TYPE_L0_SP];
		if ((l0sp_id >= base) && (l0sp_id < (base + max))) {
			return state;
		}
	}

	return NULL;
}

/*
 * ppe_drv_qos_state_find_by_qid()
 *	Find state entry that owns the given queue id.
 *
 * Caller must hold ppe_drv_qos_port_state_lock.
 */
static struct ppe_drv_qos_port_state *ppe_drv_qos_state_find_by_qid(uint32_t qid)
{
	struct ppe_drv_qos_port_state *state;
	uint32_t base;
	uint32_t max;

	list_for_each_entry(state, &ppe_drv_qos_port_state_list, list) {
		base = state->port.base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE];
		max = state->port.max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE];
		if ((qid >= base) && (qid < (base + max))) {
			return state;
		}

		base = state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];
		max = state->port.max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];
		if ((qid >= base) && (qid < (base + max))) {
			return state;
		}
	}

	return NULL;
}

/*
 * ppe_drv_qos_int_pri_callback_unregister()
 *	API to unregister INT-PRI fetch callback.
 */
void ppe_drv_qos_int_pri_callback_unregister()
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->int_pri_get_cb = NULL;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_int_pri_callback_unregister);

/*
 * ppe_drv_qos_queue_info_callback_unregister()
 *	API to unregister PPE-QUEUE fetch callback.
 */
void ppe_drv_qos_queue_info_callback_unregister()
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->queue_info_get_cb = NULL;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_queue_info_callback_unregister);

/*
 * ppe_drv_qos_queue_info_callback_register()
 *	API to register QUEUE INFO fetch callback.
 */
void ppe_drv_qos_queue_info_callback_register(ppe_drv_qos_queue_info_callback_t cb)
{
	struct ppe_drv *p = ppe_drv_gbl;

	/*
	 * TODO: Return error in case cb is NULL
	 */
	spin_lock_bh(&p->lock);
	p->queue_info_get_cb = cb;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_queue_info_callback_register);


/*
 * ppe_drv_qos_int_pri_callback_register()
 *	API to register INT-PRI fetch callback.
 */
void ppe_drv_qos_int_pri_callback_register(ppe_drv_qos_int_pri_callback_t cb)
{
	struct ppe_drv *p = ppe_drv_gbl;

	/*
	 * TODO: Return error in case cb is NULL
	 */
	spin_lock_bh(&p->lock);
	p->int_pri_get_cb = cb;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_int_pri_callback_register);
/*
 * ppe_drv_qos_ddrq_callback_unregister()
 *	API to unregister DDRQ callbacks.
 */
void ppe_drv_qos_ddrq_callback_unregister(void)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	rcu_assign_pointer(p->ddrq_ops.ddrq_disable, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_enable, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_limit_set, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_enqueue_ctrl, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_dequeue_drop_ctrl, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_stats_get, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_stats_reset, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_cfg_reset, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_accu_stats_start, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_accu_stats_stop, NULL);
	rcu_assign_pointer(p->ddrq_ops.ddrq_pon_drainout, NULL);
	spin_unlock_bh(&p->lock);

	/*
	 * Ensure all readers that may have observed previous callbacks
	 * have exited before callback provider teardown continues.
	 */
	synchronize_rcu();
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_callback_unregister);

/*
 * ppe_drv_qos_ddrq_callback_register()
 *	API to register DDRQ callbacks.
 */
void ppe_drv_qos_ddrq_callback_register(const struct ppe_drv_qos_ddrq_ops *ops)
{
	struct ppe_drv *p = ppe_drv_gbl;

	if (!ops) {
		ppe_drv_qos_ddrq_callback_unregister();
		return;
	}

	spin_lock_bh(&p->lock);
	rcu_assign_pointer(p->ddrq_ops.ddrq_disable, ops->ddrq_disable);
	rcu_assign_pointer(p->ddrq_ops.ddrq_enable, ops->ddrq_enable);
	rcu_assign_pointer(p->ddrq_ops.ddrq_limit_set, ops->ddrq_limit_set);
	rcu_assign_pointer(p->ddrq_ops.ddrq_enqueue_ctrl, ops->ddrq_enqueue_ctrl);
	rcu_assign_pointer(p->ddrq_ops.ddrq_dequeue_drop_ctrl, ops->ddrq_dequeue_drop_ctrl);
	rcu_assign_pointer(p->ddrq_ops.ddrq_stats_get, ops->ddrq_stats_get);
	rcu_assign_pointer(p->ddrq_ops.ddrq_stats_reset, ops->ddrq_stats_reset);
	rcu_assign_pointer(p->ddrq_ops.ddrq_cfg_reset, ops->ddrq_cfg_reset);
	rcu_assign_pointer(p->ddrq_ops.ddrq_accu_stats_start, ops->ddrq_accu_stats_start);
	rcu_assign_pointer(p->ddrq_ops.ddrq_accu_stats_stop, ops->ddrq_accu_stats_stop);
	rcu_assign_pointer(p->ddrq_ops.ddrq_pon_drainout, ops->ddrq_pon_drainout);
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_callback_register);

/*
 * ppe_drv_qos_ddrq_accu_stats_cb_invoke()
 *	Invoke registered DDRQ accumulate-stats callback.
 */
static inline bool ppe_drv_qos_ddrq_accu_stats_cb_invoke(struct ppe_drv *p,
						  ppe_drv_qos_ddrq_accu_stats_callback_t cb,
						  const char *op)
{
	const char *op_name = op ? op : "unknown";

	ppe_drv_trace("enter p:%px cb:%px op:%s\n", p, cb, op_name);
	if (!cb) {
		return true;
	}

	if (cb()) {
		ppe_drv_warn("%px:ddrq %s callback failed", p, op_name);
		return false;
	}

	return true;
}

/*
 * ppe_drv_qos_ddrq_pon_drainout_cb_invoke()
 *	Invoke registered PON T-CONT drainout callback.
 */
static inline bool ppe_drv_qos_ddrq_pon_drainout_cb_invoke(struct ppe_drv *p,
							    ppe_drv_qos_ddrq_pon_drainout_callback_t cb)
{
	if (!cb) {
		return true;
	}

	if (cb()) {
		ppe_drv_warn("%px:ddrq PON drainout callback failed", p);
		return false;
	}

	return true;
}

/*
 * ppe_drv_qos_ddrq_accu_stats_start()
 *	API to invoke DDRQ accumulate-stats start callback.
 */
void ppe_drv_qos_ddrq_accu_stats_start(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool ddrq_cb_ok;

	rcu_read_lock();
	ddrq_cb_ok = ppe_drv_qos_ddrq_accu_stats_cb_invoke(p,
							    rcu_dereference(p->ddrq_ops.ddrq_accu_stats_start),
							    "accu stats start");
	rcu_read_unlock();
	if (!ddrq_cb_ok) {
		ppe_drv_warn("%px:ddrq accu stats start callback failed", p);
	}
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_accu_stats_start);

/*
 * ppe_drv_qos_ddrq_accu_stats_stop()
 *	API to invoke DDRQ accumulate-stats stop callback.
 */
void ppe_drv_qos_ddrq_accu_stats_stop(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool ddrq_cb_ok;

	rcu_read_lock();
	ddrq_cb_ok = ppe_drv_qos_ddrq_accu_stats_cb_invoke(p,
							    rcu_dereference(p->ddrq_ops.ddrq_accu_stats_stop),
							    "accu stats stop");
	rcu_read_unlock();
	if (!ddrq_cb_ok) {
		ppe_drv_warn("%px:ddrq accu stats stop callback failed", p);
	}
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_accu_stats_stop);

/*
 * ppe_drv_qos_ddrq_cb_invoke()
 *	Invoke registered DDRQ callback for DDRQ queue IDs.
 */
static inline bool ppe_drv_qos_ddrq_cb_invoke(struct ppe_drv *p,
					      ppe_drv_qos_ddrq_callback_t cb,
					      uint32_t qid, const char *op)
{
	const char *op_name = op ? op : "unknown";

	ppe_drv_trace("enter p:%px cb:%px qid:%u op:%s\n",
				 p, cb, qid, op_name);
	if ((qid >= PPE_DRV_DDRQ_MAX_CNT) || !cb) {
		return true;
	}

	if (cb(qid)) {
		ppe_drv_warn("%px:ddrq %s callback failed for qid:%u", p, op_name, qid);
		return false;
	}
	return true;
}

/*
 * ppe_drv_qos_ddrq_port_cb_invoke()
 *	Invoke registered DDRQ callback for port IDs.
 */
static inline bool ppe_drv_qos_ddrq_port_cb_invoke(struct ppe_drv *p,
						   ppe_drv_qos_ddrq_callback_t cb,
						   uint32_t port_id, const char *op)
{
	const char *op_name = op ? op : "unknown";

	ppe_drv_trace("enter p:%px cb:%px port_id:%u op:%s\n",
				 p, cb, port_id, op_name);
	if (!cb) {
		return true;
	}

	if (cb(port_id)) {
		ppe_drv_warn("%px:ddrq %s callback failed for port:%u", p, op_name, port_id);
		return false;
	}

	return true;
}

/*
 * ppe_drv_qos_ddrq_limit_set_cb_invoke()
 *	Invoke registered DDRQ limit-set callback with queue configuration.
 */
static inline bool ppe_drv_qos_ddrq_limit_set_cb_invoke(struct ppe_drv *p,
								ppe_drv_qos_ddrq_limit_set_callback_t cb,
								struct ppe_drv_qos_ddrq_limit_set_cfg *cfg)
{
	struct ppe_drv_qos_queue *q_cfg;

	if (!cb || !cfg || !cfg->qos_queue) {
		return true;
	}

	q_cfg = cfg->qos_queue;
	if (q_cfg->ucast_qid >= PPE_DRV_DDRQ_MAX_CNT) {
		return true;
	}

	if (cb(cfg)) {
		ppe_drv_warn("%px:ddrq limit set callback failed for ucast_qid:%u mcast_qid:%u qdisc_ceiling_unit:%u port_mtu:%u",
			     p, q_cfg->ucast_qid, q_cfg->mcast_qid, cfg->qdisc_ceiling_unit, cfg->port_mtu);
		return false;
	}
	return true;
}

/*
 * ppe_drv_qos_ddrq_stats_get_cb_invoke()
 *	Invoke registered DDRQ stats-get callback.
 */
static inline void ppe_drv_qos_ddrq_stats_get_cb_invoke(struct ppe_drv *p,
							ppe_drv_qos_ddrq_stats_get_callback_t cb,
							uint32_t qid, struct ppe_drv_qos_q_stat *stats)
{
	int ret;

	if (!cb || !stats || (qid >= PPE_DRV_DDRQ_MAX_CNT)) {
		return;
	}

	ret = cb(qid, stats);
	if (ret) {
		ppe_drv_trace("%px:ddrq stats get callback failed for qid:%u", p, qid);
	}

}

static inline bool ppe_drv_qos_ddrq_enq_deq_ctrl_cb_invoke(struct ppe_drv *p,
							   ppe_drv_qos_ddrq_enq_deq_ctrl_callback_t cb,
							   uint32_t qid, bool enable, const char *op)
{
	const char *op_name = op ? op : "unknown";

	ppe_drv_trace("enter p:%px cb:%px qid:%u enable:%u op:%s\n",
				 p, cb, qid, enable, op_name);
	if (!cb || (qid >= PPE_DRV_DDRQ_MAX_CNT)) {
		return true;
	}

	if (cb(qid, enable)) {
		ppe_drv_warn("%px:ddrq %s configuration  callback failed for qid:%u enable: %d",
			     p, op_name, qid, enable);
		return false;
	}
	return true;
}

/*
 * ppe_drv_qos_queue_cfg_fill()
 *	Convert unicast queue configuration to dynamic threshold configuration.
 */
static void ppe_drv_qos_queue_cfg_fill(struct ppe_drv_qos_queue *q_cfg,
				       fal_ac_dynamic_threshold_t *dynamic_cfg)
{
	dynamic_cfg->color_enable = q_cfg->color_en;
	dynamic_cfg->wred_enable = q_cfg->red_en;
	dynamic_cfg->ceiling = q_cfg->qlimit;
	dynamic_cfg->green_min_off = q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
	dynamic_cfg->yel_min_off = q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
	dynamic_cfg->yel_max_off = q_cfg->max_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
	dynamic_cfg->red_min_off = q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_RED];
	dynamic_cfg->red_max_off = q_cfg->max_th[PPE_DRV_QOS_QUEUE_COLOR_RED];

	/*
	 * Set resume offsets for all three colors.
	 * Resume offsets control when the queue resumes accepting packets after dropping,
	 * which helps prevent buffer oscillation.
	 */
	dynamic_cfg->green_resume_off = q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
	dynamic_cfg->yel_resume_off = q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
	dynamic_cfg->red_resume_off = q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_RED];
}

/*
 * ppe_drv_qos_queue_cfg_extract()
 *	Convert dynamic threshold configuration to unicast queue configuration.
 */
static void ppe_drv_qos_queue_cfg_extract(uint32_t qid,
					  fal_ac_dynamic_threshold_t *dynamic_cfg,
					  struct ppe_drv_qos_queue *q_cfg)
{
	q_cfg->ucast_qid = qid;
	q_cfg->qlimit = dynamic_cfg->ceiling;
	q_cfg->color_en = dynamic_cfg->color_enable;
	q_cfg->red_en = dynamic_cfg->wred_enable;
	q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_GREEN] = dynamic_cfg->green_min_off;
	q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW] = dynamic_cfg->yel_min_off;
	q_cfg->max_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW] = dynamic_cfg->yel_max_off;
	q_cfg->min_th[PPE_DRV_QOS_QUEUE_COLOR_RED] = dynamic_cfg->red_min_off;
	q_cfg->max_th[PPE_DRV_QOS_QUEUE_COLOR_RED] = dynamic_cfg->red_max_off;
	q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_GREEN] = dynamic_cfg->green_resume_off;
	q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_YELLOW] = dynamic_cfg->yel_resume_off;
	q_cfg->resume_off[PPE_DRV_QOS_QUEUE_COLOR_RED] = dynamic_cfg->red_resume_off;
}

/*
 * ppe_drv_qos_queue_cfg_get_nolock()
 *	Gets queue dynamic threshold configuration in PPE.
 *
 * NOTE: Caller must hold p->lock.
 */
static ppe_drv_ret_t ppe_drv_qos_queue_cfg_get_nolock(struct ppe_drv *p,
						      uint32_t qid,
						      struct ppe_drv_qos_queue *q_cfg)
{
	fal_ac_dynamic_threshold_t dynamic_cfg = {0};

	if (fal_ac_dynamic_threshold_get(PPE_DRV_SWITCH_ID, qid, &dynamic_cfg) != SW_OK) {
		ppe_drv_warn("%px:queue dynamic threshold get failed for ucast_qid:%u", p, qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	ppe_drv_qos_queue_cfg_extract(qid, &dynamic_cfg, q_cfg);
	ppe_drv_trace("%px:queue ac dynamic threshold get for ucast_qid:%u, color_en:%u, wred_enable:%u, ceiling:%u, green_min_off:%u, yel_min_off:%u, yel_max_off:%u, red_min_off:%u, red_max_off:%u, green_resume_off:%u, yel_resume_off:%u, red_resume_off:%u",
			p, qid, dynamic_cfg.color_enable, dynamic_cfg.wred_enable, dynamic_cfg.ceiling,
			dynamic_cfg.green_min_off, dynamic_cfg.yel_min_off, dynamic_cfg.yel_max_off,
			dynamic_cfg.red_min_off, dynamic_cfg.red_max_off,
			dynamic_cfg.green_resume_off, dynamic_cfg.yel_resume_off, dynamic_cfg.red_resume_off);

	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_qos_queue_cfg_set_nolock()
 *	Sets queue dynamic threshold configuration in PPE.
 *
 * NOTE: Caller must hold p->lock.
 */
static ppe_drv_ret_t ppe_drv_qos_queue_cfg_set_nolock(struct ppe_drv *p,
						      uint32_t qid,
						      struct ppe_drv_qos_queue *q_cfg)
{
	fal_ac_dynamic_threshold_t dynamic_cfg = {0};

	if (fal_ac_dynamic_threshold_get(PPE_DRV_SWITCH_ID, qid, &dynamic_cfg) != SW_OK) {
		ppe_drv_warn("%px:queue dynamic threshold get failed for ucast_qid:%u", p, qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	ppe_drv_qos_queue_cfg_fill(q_cfg, &dynamic_cfg);

	ppe_drv_trace("%px:queue ac dynamic threshold set for ucast_qid:%u, color_en:%u, wred_enable:%u, ceiling:%u, green_min_off:%u, yel_min_off:%u, yel_max_off:%u, red_min_off:%u, red_max_off:%u, green_resume_off:%u, yel_resume_off:%u, red_resume_off:%u",
			p, qid, dynamic_cfg.color_enable, dynamic_cfg.wred_enable, dynamic_cfg.ceiling,
			dynamic_cfg.green_min_off, dynamic_cfg.yel_min_off, dynamic_cfg.yel_max_off,
			dynamic_cfg.red_min_off, dynamic_cfg.red_max_off,
			dynamic_cfg.green_resume_off, dynamic_cfg.yel_resume_off, dynamic_cfg.red_resume_off);
	if (fal_ac_dynamic_threshold_set(PPE_DRV_SWITCH_ID, qid, &dynamic_cfg) != SW_OK) {
		ppe_drv_warn("%px:queue dynamic threshold set failed for ucast_qid:%u", p, qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_qos_queue_cfg_get()
 *	Gets queue dynamic threshold configuration in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_cfg_get(uint32_t qid, struct ppe_drv_qos_queue *q_cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_ret_t ret;

	if (!q_cfg) {
		ppe_drv_warn("%px:invalid queue configuration for ucast_qid:%u", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	spin_lock_bh(&p->lock);
	ret = ppe_drv_qos_queue_cfg_get_nolock(p, qid, q_cfg);
	spin_unlock_bh(&p->lock);

	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%px:queue configuration get failed for qid:%u", p, qid);
	}

	return ret;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_cfg_get);

/*
 * ppe_drv_qos_queue_cfg_set()
 *	Sets queue dynamic threshold configuration in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_cfg_set(uint32_t qid, struct ppe_drv_qos_queue *q_cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_ret_t ret;

	if (!q_cfg) {
		ppe_drv_warn("%px:invalid queue configuration for ucast_qid:%u", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	spin_lock_bh(&p->lock);
	ret = ppe_drv_qos_queue_cfg_set_nolock(p, qid, q_cfg);
	spin_unlock_bh(&p->lock);

	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%px:queue configuration set failed for qid:%u", p, qid);
	}

	return ret;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_cfg_set);

static int ppe_drv_qos_get_ddrq_state(uint32_t qid)
{
	fal_ucast_queue_ddrq_en_t ddrq_en_cfg = {0};
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	/*
	 * Check whether the mapped DDRQ is enabled or not
	 */
	if (fal_ucast_queue_ddrq_en_get(PPE_DRV_SWITCH_ID, qid, &ddrq_en_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px: Error in getting the DDRQ state of qid:%u\n", p, qid);
		return -EINVAL;
	}
	spin_unlock_bh(&p->lock);

	return ((ddrq_en_cfg.ddrq_en) ? 1 : 0);
}

/*
 * ppe_drv_qos_ddrq_state_get()
 *	Fetches DDRQ enable state for a queue.
 *	For non-DDRQ queue ids, return success with disabled DDRQ state.
 */
static bool ppe_drv_qos_ddrq_state_get(uint32_t qid, bool *is_ddrq_en)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int status;

	if (!is_ddrq_en) {
		ppe_drv_trace("%px: Invalid parameter received in DDRQ:%d state get configuration\n", p, qid);
		return false;
	}

	*is_ddrq_en = false;
	if (qid >= PPE_DRV_DDRQ_MAX_CNT) {
		ppe_drv_trace("%px: DDRQ:%d is more than maximum DDRQ count\n", p, qid);
		return true;
	}

	status = ppe_drv_qos_get_ddrq_state(qid);
	if (status < 0) {
		ppe_drv_warn("%px: Error in getting the DDRQ state of qid:%u\n", p, qid);
		return false;
	}

	*is_ddrq_en = !!status;
	ppe_drv_trace("%px: DDRQ:%d current configured state is %d\n", p, qid, *is_ddrq_en);
	return true;
}


/*
 * ppe_drv_qos_ac_ctrl_set()
 *	Internal helper to configure AC control on the given queue object.
 */
static bool ppe_drv_qos_ac_ctrl_set(uint32_t qid, fal_ac_obj_t *obj, fal_ac_ctrl_t *ctrl_cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;

	if (!ctrl_cfg || !obj) {
		ppe_drv_warn("Invalid AC control configuration received for qid:%d\n", qid);
		return false;
	}

	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:queue flow control set for ucast_qid:%u, enable:%u", p, qid, ctrl_cfg->ac_fc_en);
	if (fal_ac_ctrl_set(PPE_DRV_SWITCH_ID, obj, ctrl_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue flow control set failed for ucast_qid:%u", p, qid);
		return false;
	}
	spin_unlock_bh(&p->lock);

	return true;
}

/*
 * ppe_drv_qos_queue_drop_cfg_set()
 *	API to configure drop configuration for the given queue id.
 */
static bool ppe_drv_qos_queue_drop_cfg_set(uint32_t qid, fal_ac_obj_t *obj, fal_ac_drop_state_t *drop_cfg)
{
	struct ppe_drv *p = ppe_drv_gbl;

	if (!obj || !drop_cfg) {
		ppe_drv_warn("Invalid drop configuration received for qid:%d\n", qid);
		return false;
	}

	spin_lock_bh(&p->lock);
	if (fal_qm_ac_drop_state_set(PPE_DRV_SWITCH_ID, obj, drop_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue drop configuration set failed for qid:%u", p, qid);
		return false;
	}
	spin_unlock_bh(&p->lock);

	return true;
}

/*
 * ppe_drv_qos_queue_info_get()
 *	Updates the the INT-PRI and unicast PPE queue ID for the given qdisc handle ID or leaf class ID.
 */
bool ppe_drv_qos_queue_info_get(struct net_device *dev, uint32_t handle_id, struct ppe_drv_queue_info* pq_info)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_qos_queue_info_callback_t cb;
	int8_t ret = 0;

	/*
	 * Read the callback pointer under p->lock for visibility, then release
	 * before invoking. This avoids an ABBA deadlock with the stats workqueue
	 * path (ppe_qdisc_stats_get_node) which holds cursor->lock and then
	 * acquires p->lock inside ppe_drv_qos_queue_stats_get. If we held p->lock
	 * here while the callback acquires cursor->lock, the two lock orders
	 * would be inverted causing a deadlock.
	 */
	spin_lock_bh(&p->lock);
	cb = p->queue_info_get_cb;
	spin_unlock_bh(&p->lock);

	if (cb) {
		ret = cb(dev, handle_id, pq_info);
	}

	ppe_drv_info("%px:get int_pri:%d successful for dev:%px handle_id:%u", p, pq_info->int_pri, dev, handle_id);
	return ret;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_info_get);

/*
 * ppe_drv_qos_int_pri_get()
 *	Returns the INT-PRI value for a qdisc handle ID or leaf class ID.
  */
int ppe_drv_qos_int_pri_get(struct net_device *dev, uint32_t handle_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_qos_int_pri_callback_t cb;
	uint8_t int_pri = 0;

	/*
	 * Same lock ordering fix as ppe_drv_qos_queue_info_get: read the
	 * callback pointer under p->lock, then release before calling to
	 * avoid ABBA deadlock with the stats workqueue path.
	 */
	spin_lock_bh(&p->lock);
	cb = p->int_pri_get_cb;
	spin_unlock_bh(&p->lock);

	if (cb) {
		int_pri = cb(dev, handle_id);
	}

	ppe_drv_info("%px:get int_pri:%d successful for dev:%px handle_id:%u", p, int_pri, dev, handle_id);
	return int_pri;
}
EXPORT_SYMBOL(ppe_drv_qos_int_pri_get);

/*
 * ppe_drv_qos_queue_stats_get()
 *	API to fetch queue statistics from PPE HW.
 */
void ppe_drv_qos_queue_stats_get(uint32_t qid, bool is_red, struct ppe_drv_qos_q_stat *stats)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_queue_stats_t info = {0};
	bool is_ddrq_en;

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return;
	}

	spin_lock_bh(&p->lock);
	fal_queue_counter_get(PPE_DRV_SWITCH_ID, qid, &info);
	spin_unlock_bh(&p->lock);

	stats->tx_pkts = info.tx_packets;
	stats->tx_bytes = info.tx_bytes;

	stats->drop_pkts = info.drop_packets[PPE_DRV_QOS_QUEUE_COLOR_GREEN + 3];
	stats->drop_bytes = info.drop_bytes[PPE_DRV_QOS_QUEUE_COLOR_GREEN + 3];

	if (is_red) {
		stats->drop_pkts += info.drop_packets[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
		stats->drop_bytes += info.drop_bytes[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
	}

	if (is_ddrq_en) {
		rcu_read_lock();
		ppe_drv_qos_ddrq_stats_get_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_stats_get), qid,
						     stats);
		rcu_read_unlock();
	}

	ppe_drv_info("%px:get queue stats successful for qid:%u", p, qid);
}
EXPORT_SYMBOL(ppe_drv_qos_queue_stats_get);

/*
 * ppe_drv_qos_queue_stats_reset()
 *	API to reset queue statistics in PPE HW.
 */
void ppe_drv_qos_queue_stats_reset(uint32_t qid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool is_ddrq_en;
	bool ddrq_cb_ok;

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return;
	}

	spin_lock_bh(&p->lock);
	fal_queue_counter_cleanup(PPE_DRV_SWITCH_ID, qid);
	spin_unlock_bh(&p->lock);

	if (is_ddrq_en) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_stats_reset),
							qid, "stats reset");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			ppe_drv_warn("%px:ddrq stats reset callback failed for qid:%u", p, qid);
		}
	}

	ppe_drv_info("%px:reset queue stats successful for qid:%u", p, qid);
}
EXPORT_SYMBOL(ppe_drv_qos_queue_stats_reset);

/*
 * ppe_drv_qos_queue_enqueue_ctrl()
 *	Controls enqueue operation for a queue in PPE HW.
 */
ppe_drv_ret_t ppe_drv_qos_queue_enqueue_ctrl(uint32_t qid, bool enable)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool is_ddrq_en;
	bool ddrq_cb_ok;

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (is_ddrq_en) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_enq_deq_ctrl_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_enqueue_ctrl),
								      qid, enable, "enqueue control");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}
	}

	spin_lock_bh(&p->lock);
	if (fal_qm_enqueue_ctrl_set(PPE_DRV_SWITCH_ID, qid, enable ? A_TRUE : A_FALSE) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue enqueue control failed for qid:%u enable:%d", p, qid, enable);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:queue enqueue control successful for qid:%u enable:%d", p, qid, enable);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_enqueue_ctrl);

/*
 * ppe_drv_qos_queue_dequeue_ctrl()
 *	Controls dequeue operation for a queue in PPE HW.
 */
ppe_drv_ret_t ppe_drv_qos_queue_dequeue_ctrl(uint32_t qid, bool enable)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	if (fal_scheduler_dequeue_ctrl_set(PPE_DRV_SWITCH_ID, qid, enable ? A_TRUE : A_FALSE) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue dequeue control failed for qid:%u enable:%d", p, qid, enable);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:queue dequeue control successful for qid:%u enable:%d", p, qid, enable);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_dequeue_ctrl);

/*
 * ppe_drv_qos_queue_dequeue_drop_ctrl()
 *	Controls dequeue drop operation for a queue in PPE HW.
 */
ppe_drv_ret_t ppe_drv_qos_queue_dequeue_drop_ctrl(uint32_t qid, bool enable)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool is_ddrq_en;
	bool ddrq_cb_ok;

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (is_ddrq_en) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_enq_deq_ctrl_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_dequeue_drop_ctrl),
								      qid, enable, "dequeue drop control");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}
	}

	spin_lock_bh(&p->lock);

	if (fal_qm_dequeue_drop_set(PPE_DRV_SWITCH_ID, qid, enable ? A_TRUE : A_FALSE) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue dequeue drop control failed for qid:%u enable:%d", p, qid, enable);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:queue dequeue drop control successful for qid:%u enable:%d", p, qid, enable);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_dequeue_drop_ctrl);

/*
 * ppe_drv_qos_res_queue_disable()
 *	Disables a queue in PPE HW.
 */
ppe_drv_ret_t ppe_drv_qos_queue_disable(uint32_t port_id, uint32_t qid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool is_ddrq_en = false;
	bool ddrq_cb_ok;
	bool ddrq_unmapped = false;
	bool enqueue_disabled = false;
	bool dequeue_disabled = false;
	bool ddrq_disable_done = false;
	a_bool_t is_enq_en = A_FALSE;
	fal_ucast_queue_ddrq_en_t ddrq_en = {0};

	spin_lock_bh(&p->lock);

	if (fal_qm_enqueue_ctrl_get(PPE_DRV_SWITCH_ID, qid, &is_enq_en) != SW_OK) {
		ppe_drv_warn("%px:queue enqueue get failed for port:%u qid:%u", p, port_id, qid);
		goto queue_disable_fail_locked;
	}

	/*
	 * Return if the queue is already in the disabled state
	 */
	if (!is_enq_en) {
		spin_unlock_bh(&p->lock);
		return PPE_DRV_RET_SUCCESS;
	}

	spin_unlock_bh(&p->lock);

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (is_ddrq_en) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_disable), qid, "disable");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			ppe_drv_warn("%px:ddrq disable callback failed for qid:%u", p, qid);
			goto queue_disable_fail;
		}
		ddrq_disable_done = true;
	}

	spin_lock_bh(&p->lock);

	/*
	 * It is recommended to disable the DDRQ_EN flag for the SRAM
	 * queue, before calling that queue's flush operation. Otherwise we were
	 * always observing FLUSH_BUSY=1 in the flush configuration register,
	 * leading to the failure in the respective queue's flush operation
	 */
	if (is_ddrq_en) {
		ddrq_en.ddrq_en = A_FALSE;
		if (fal_ucast_queue_ddrq_en_set(PPE_DRV_SWITCH_ID, qid, &ddrq_en) != SW_OK) {
			ppe_drv_warn("%px:ddrq_en unmap failed for qid:%u", p, qid);
			goto queue_disable_fail_locked;
		}
		ddrq_unmapped = true;
	}

	/*
	 * Disable queue enqueue, dequeue and flush the queue.
	 */
	if (fal_qm_enqueue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_FALSE) != SW_OK) {
		ppe_drv_warn("%px:queue enqueue failed for port:%u qid:%u", p, port_id, qid);
		goto queue_disable_fail_locked;
	}
	enqueue_disabled = true;

	if (fal_scheduler_dequeue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_FALSE) != SW_OK) {
		ppe_drv_warn("%px:queue dequeue failed for port:%u qid:%u", p, port_id, qid);
		goto queue_disable_fail_locked;
	}
	dequeue_disabled = true;

	if (fal_queue_flush(PPE_DRV_SWITCH_ID, port_id, qid) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue flush failed for port:%u qid:%u", p, port_id, qid);
		goto queue_disable_fail;
	}

	if (is_ddrq_en) {
		ddrq_en.ddrq_en = A_TRUE;
		if (fal_ucast_queue_ddrq_en_set(PPE_DRV_SWITCH_ID, qid, &ddrq_en) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:ddrq_en remap failed for qid:%u", p, qid);
			goto queue_disable_fail;
		}
		ddrq_unmapped = false;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:disable level0 queue scheduler successful for port:%u qid:%u", p, port_id, qid);
	return PPE_DRV_RET_SUCCESS;

queue_disable_fail:
	spin_lock_bh(&p->lock);
queue_disable_fail_locked:
	if (dequeue_disabled) {
		if (fal_scheduler_dequeue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_TRUE) != SW_OK) {
			ppe_drv_warn("%px:queue dequeue rollback failed for port:%u qid:%u", p, port_id, qid);
		}
	}

	if (enqueue_disabled) {
		if (fal_qm_enqueue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_TRUE) != SW_OK) {
			ppe_drv_warn("%px:queue enqueue rollback failed for port:%u qid:%u", p, port_id, qid);
		}
	}

	if (is_ddrq_en && ddrq_unmapped) {
		ddrq_en.ddrq_en = A_TRUE;
		if (fal_ucast_queue_ddrq_en_set(PPE_DRV_SWITCH_ID, qid, &ddrq_en) != SW_OK) {
			ppe_drv_warn("%px:ddrq_en rollback remap failed for qid:%u", p, qid);
		}
	}

	spin_unlock_bh(&p->lock);

	if (is_ddrq_en && ddrq_disable_done) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_enable), qid, "enable");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			ppe_drv_warn("%px:ddrq enable callback rollback failed for qid:%u", p, qid);
		}
	}

	return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_disable);

/*
 * ppe_drv_qos_queue_enable()
 *	Enables a queue in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_enable(uint32_t qid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	bool is_ddrq_en;
	bool ddrq_cb_ok;

	if (!ppe_drv_qos_ddrq_state_get(qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (is_ddrq_en) {
		rcu_read_lock();
		ddrq_cb_ok = ppe_drv_qos_ddrq_cb_invoke(p, rcu_dereference(p->ddrq_ops.ddrq_enable), qid, "enable");
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}
	}

	/*
	 * Enable queue dequeue and enqueue.
	 */
	spin_lock_bh(&p->lock);
	if (fal_scheduler_dequeue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_TRUE) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue dequeue failed for qid:%u", p, qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	if (fal_qm_enqueue_ctrl_set(PPE_DRV_SWITCH_ID, qid, A_TRUE) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue enqueue failed for qid:%u", p, qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:enable SSDK level0 queue scheduler successful for qid:%u", p, qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_enable);

/*
 * ppe_drv_qos_tcont_stats_get()
 *	API to fetch tcont statistics from PPE HW.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_stats_get(uint32_t tcont_id, struct ppe_drv_qos_tcont_stat *stats)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_queue_tcont_stat_t info;

	spin_lock_bh(&p->lock);
	if (fal_qm_tcont_stat_get(PPE_DRV_SWITCH_ID, tcont_id, &info) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:tcont stats get failed for tcont:%u", p, tcont_id);
		return PPE_DRV_RET_QOS_TCONT_STATS_FAIL;
	}

	spin_unlock_bh(&p->lock);
	stats->pend_bytes = info.bytes;
	stats->credit = info.credit;

	ppe_drv_info("%px:get tcont stats get successful for tcont_id:%u", p, tcont_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_tcont_stats_get);

/*
 * ppe_drv_qos_tcont_credit_reset()
 *	Resets T-cont credit to 0.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_credit_reset(uint32_t tcont_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_queue_tcont_stat_t stats = {0};

	spin_lock_bh(&p->lock);
	if (fal_qm_tcont_stat_set(PPE_DRV_SWITCH_ID, tcont_id, &stats) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:tcont stats resetting failed for tcont:%u", p, tcont_id);
		return PPE_DRV_RET_QOS_TCONT_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:tcont stats resetting successful for tcont:%u", p, tcont_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_tcont_credit_reset);

/*
 * ppe_drv_qos_tcont_set()
 *	Sets T-cont configuration for a queue in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_set(struct ppe_drv_qos_res *res, uint32_t tcont_id, bool valid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_queue_tcont_cfg_t cfg = {0};

	spin_lock_bh(&p->lock);
	cfg.tcont_id = tcont_id;
	cfg.valid = valid;

	ppe_drv_trace("%px:tcont configuration: queue_id:%u, tcont_id:%u, tcont_valid=%u",
			p, res->q.ucast_qid, tcont_id, valid);
	if (fal_qm_tcont_set(PPE_DRV_SWITCH_ID, res->q.ucast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:tcont configuration failed for tcont:%u", p, tcont_id);
		return PPE_DRV_RET_QOS_TCONT_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:tcont configuration successful for tcont:%u", p, tcont_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_tcont_set);

#ifdef NSS_PPE_PON_SUPPORT
/*
 * ppe_drv_qos_pon_port_get_internal()
 *	Gets the PON port ID with the driver lock already held.
 */
uint32_t ppe_drv_qos_pon_port_get_internal(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_iface *pon_iface = p->pon_iface;
	struct ppe_drv_port *pon_port;

	if (!pon_iface) {
		ppe_drv_trace("%px:PON interface not present", p);
		return 0;
	}

	pon_port = ppe_drv_iface_port_get(pon_iface);
	if (!pon_port) {
		ppe_drv_trace("%px:PON port not present", p);
		return 0;
	}

	return pon_port->port;
}

/*
 * ppe_drv_qos_pon_port_alloc_callback_unregister()
 *	API to unregister PON port QoS resource allocation callback.
 */
void ppe_drv_qos_pon_port_alloc_callback_unregister(void)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->pon_port_alloc_cb = NULL;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_pon_port_alloc_callback_unregister);

/*
 * ppe_drv_qos_pon_port_alloc_callback_register()
 *	API to register PON port QoS resource allocation callback.
 */
void ppe_drv_qos_pon_port_alloc_callback_register(ppe_drv_qos_pon_port_alloc_callback_t cb)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->pon_port_alloc_cb = cb;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_qos_pon_port_alloc_callback_register);

/*
 * ppe_drv_qos_pon_port_get()
 *	Gets the PON port ID.
 */
uint32_t ppe_drv_qos_pon_port_get(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	uint32_t port_num;

	spin_lock_bh(&p->lock);
	port_num = ppe_drv_qos_pon_port_get_internal();
	spin_unlock_bh(&p->lock);

	return port_num;
}
EXPORT_SYMBOL(ppe_drv_qos_pon_port_get);
#endif

/*
 * ppe_drv_qos_l1_scheduler_set()
 *	Configures Level 1 scheduler in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_l1_scheduler_set(struct ppe_drv_qos_res *res, uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_qos_scheduler_cfg_t l1cfg = {0};

	if (res->scheduler.drr_weight >= PPE_DRV_QOS_DRR_WEIGHT_MAX) {
		ppe_drv_warn("%px:drr weight:%u should be less than 1024",
				p, res->scheduler.drr_weight);
		return PPE_DRV_RET_QOS_SCHEDULER_CFG_FAIL;
	}

	spin_lock_bh(&p->lock);
	l1cfg.sp_id = res->l1spid;

	l1cfg.c_drr_wt = res->scheduler.drr_weight ? res->scheduler.drr_weight : 1;
	l1cfg.c_drr_unit = res->scheduler.drr_unit;
	l1cfg.e_drr_wt = res->scheduler.drr_weight ? res->scheduler.drr_weight : 1;
	l1cfg.e_drr_unit = res->scheduler.drr_unit;
	l1cfg.c_pri = res->scheduler.priority;
	l1cfg.e_pri = res->scheduler.priority;
	l1cfg.c_drr_id = res->scheduler.l1c_drrid;
	l1cfg.e_drr_id = res->scheduler.l1e_drrid;
	l1cfg.drr_frame_mode = (fal_qos_drr_frame_mode_t)PPE_DRV_QOS_FRAME_MODE_FRAME_CRC;

	ppe_drv_trace("%px:level1 configuration: port:%u, l0spid:%u, l1spid:%u, c_drrid:%u, c_pri:%u, c_drr_wt:%u, e_drrid:%u, e_pri:%u, e_drr_wt:%u, l1spid:%u",
			p, port_id, res->l0spid, res->l1spid, l1cfg.c_drr_id, l1cfg.c_pri, l1cfg.c_drr_wt, l1cfg.e_drr_id, l1cfg.e_pri, l1cfg.e_drr_wt, l1cfg.sp_id);
	if (fal_queue_scheduler_set(PPE_DRV_SWITCH_ID, res->l0spid, PPE_DRV_QOS_FLOW_LEVEL - 1, port_id, &l1cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:level1 queue scheduler configuration failed for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_SCHEDULER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:level1 queue scheduler configuration successful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_l1_scheduler_set);

/*
 * ppe_drv_qos_l0_scheduler_set()
 *	Configures a level 0 queue scheduler in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_l0_scheduler_set(struct ppe_drv_qos_res *res, uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_qos_scheduler_cfg_t l0cfg = {0};

	if (res->scheduler.drr_weight >= PPE_DRV_QOS_DRR_WEIGHT_MAX) {
		ppe_drv_warn("%px:drr weight:%u should be less than 1024", p, res->scheduler.drr_weight);
		return PPE_DRV_RET_QOS_SCHEDULER_CFG_FAIL;
	}

	spin_lock_bh(&p->lock);
	l0cfg.sp_id = res->l0spid;
	l0cfg.c_drr_wt = res->scheduler.drr_weight ? res->scheduler.drr_weight : 1;
	l0cfg.c_drr_unit = res->scheduler.drr_unit;
	l0cfg.e_drr_wt = res->scheduler.drr_weight ? res->scheduler.drr_weight : 1;
	l0cfg.e_drr_unit = res->scheduler.drr_unit;
	l0cfg.c_pri = res->scheduler.priority;
	l0cfg.e_pri = res->scheduler.priority;
	l0cfg.c_drr_id = res->scheduler.l0c_drrid;
	l0cfg.e_drr_id = res->scheduler.l0e_drrid;
	l0cfg.drr_frame_mode = (fal_qos_drr_frame_mode_t)PPE_DRV_QOS_FRAME_MODE_FRAME_CRC;

	ppe_drv_trace("%px:level0 configuration: port:%u, ucast_qid:%u, c_drrid:%u, c_pri:%u, c_drr_wt:%u, e_drrid:%u, e_pri:%u, e_drr_wt:%u, l0spid:%u",
			p, port_id, res->q.ucast_qid, l0cfg.c_drr_id, l0cfg.c_pri, l0cfg.c_drr_wt, l0cfg.e_drr_id, l0cfg.e_pri, l0cfg.e_drr_wt, l0cfg.sp_id);
	if (fal_queue_scheduler_set(PPE_DRV_SWITCH_ID, res->q.ucast_qid, PPE_DRV_QOS_QUEUE_LEVEL - 1, port_id, &l0cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:level0 queue scheduler configuration failed for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_SCHEDULER_CFG_FAIL;
	}

	/*
	 * This function is called at the time of queue qdisc allocation at all
	 * levels and class allocation at PPE_DRV_QOS_QUEUE_LEVEL.
	 * Also invoked at time of change class at PPE_DRV_QOS_QUEUE_LEVEL and
	 * PPE_DRV_QOS_SUB_QUEUE_LEVEL levels. But, mcast will be valid only in
	 * case queue is attached at PPE_DRV_QOS_SUB_QUEUE_LEVEL and change class
	 * is invoked at PPE_DRV_QOS_QUEUE_LEVEL level.
	 * Other scenarios catering to mcast queue are handled in separate mcast APIs.
	 */
	if (res->q.mcast_qid) {
		ppe_drv_trace("%px:level0 configuration: port:%u, mcast_qid:%u, c_drrid:%u, c_pri:%u, c_drr_wt:%u, e_drrid:%u, e_pri:%u, e_drr_wt:%u, l0spid:%u",
				p, port_id, res->q.mcast_qid, l0cfg.c_drr_id, l0cfg.c_pri, l0cfg.c_drr_wt, l0cfg.e_drr_id, l0cfg.e_pri, l0cfg.e_drr_wt, l0cfg.sp_id);
		if (fal_queue_scheduler_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, PPE_DRV_QOS_QUEUE_LEVEL - 1, port_id, &l0cfg) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:level0 multicast queue scheduler configuration failed for port:%u", p, port_id);
			return PPE_DRV_RET_QOS_SCHEDULER_CFG_FAIL;
		}
	}

	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:level0 queue scheduler configuration successful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_l0_scheduler_set);

/*
 * ppe_drv_qos_port_state_alloc()
 *	Allocate shaper shadow state arrays for a port.
 */
ppe_drv_ret_t ppe_drv_qos_port_state_alloc(uint32_t port_id, struct ppe_drv_qos_port *port)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	struct ppe_drv_qos_port_state *state_existing = NULL;
	uint32_t flow_max;
	uint32_t ucast_max;
	uint32_t mcast_max;

	if (!port) {
		ppe_drv_warn("%px:port state alloc failed for port:%u due to NULL port cfg", p, port_id);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	flow_max = port->max[PPE_DRV_QOS_RES_TYPE_L0_SP];
	ucast_max = port->max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE];
	mcast_max = port->max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state_existing = ppe_drv_qos_port_state_find(port_id);
	if (state_existing) {
		state_existing->refcnt++;
		spin_unlock_bh(&ppe_drv_qos_port_state_lock);
		return PPE_DRV_RET_SUCCESS;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	state = kzalloc(sizeof(*state), GFP_ATOMIC);
	if (!state) {
		ppe_drv_warn("%px:port state alloc failed, kzalloc failed for port:%u", p, port_id);
		return PPE_DRV_RET_FAILURE_NO_RESOURCE;
	}

	state->port_id = port_id;
	state->refcnt = 1;
	state->port = *port;

	if (flow_max) {
		state->flows = kcalloc(flow_max, sizeof(*state->flows), GFP_ATOMIC);
		if (!state->flows) {
			ppe_drv_warn("%px:port state alloc failed for port:%u flow state alloc failed max:%u",
					p, port_id, flow_max);
			goto fail;
		}
	}

	if (ucast_max) {
		state->ucast_queues = kcalloc(ucast_max, sizeof(*state->ucast_queues), GFP_ATOMIC);
		if (!state->ucast_queues) {
			ppe_drv_warn("%px:port state alloc failed for port:%u ucast state alloc failed max:%u",
					p, port_id, ucast_max);
			goto fail;
		}
	}

	if (mcast_max) {
		state->mcast_queues = kcalloc(mcast_max, sizeof(*state->mcast_queues), GFP_ATOMIC);
		if (!state->mcast_queues) {
			ppe_drv_warn("%px:port state alloc failed for port:%u mcast state alloc failed max:%u",
					p, port_id, mcast_max);
			goto fail;
		}
	}

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state_existing = ppe_drv_qos_port_state_find(port_id);
	if (state_existing) {
		state_existing->refcnt++;
		spin_unlock_bh(&ppe_drv_qos_port_state_lock);
		goto duplicate;
	}

	list_add_tail(&state->list, &ppe_drv_qos_port_state_list);
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	return PPE_DRV_RET_SUCCESS;

duplicate:
	kfree(state->flows);
	kfree(state->ucast_queues);
	kfree(state->mcast_queues);
	kfree(state);
	return PPE_DRV_RET_SUCCESS;

fail:
	kfree(state->flows);
	kfree(state->ucast_queues);
	kfree(state->mcast_queues);
	kfree(state);
	return PPE_DRV_RET_FAILURE_NO_RESOURCE;
}
EXPORT_SYMBOL(ppe_drv_qos_port_state_alloc);

/*
 * ppe_drv_qos_port_state_free()
 *	Free shaper shadow state arrays for a port.
 */
void ppe_drv_qos_port_state_free(uint32_t port_id)
{
	struct ppe_drv_qos_port_state *state;
	struct ppe_drv_qos_port_state *tmp;
	struct ppe_drv_qos_port_state *to_free = NULL;

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	list_for_each_entry_safe(state, tmp, &ppe_drv_qos_port_state_list, list) {
		if (state->port_id != port_id) {
			continue;
		}

		if (state->refcnt > 1) {
			state->refcnt--;
			spin_unlock_bh(&ppe_drv_qos_port_state_lock);
			return;
		}

		list_del(&state->list);
		to_free = state;
		break;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	if (!to_free) {
		return;
	}

	kfree(to_free->flows);
	kfree(to_free->ucast_queues);
	kfree(to_free->mcast_queues);
	kfree(to_free);
}
EXPORT_SYMBOL(ppe_drv_qos_port_state_free);

/*
 * ppe_drv_qos_port_shapers_unlimited()
 *	Set all shapers (port, flow, and queues) to unlimited rate.
 */
ppe_drv_ret_t ppe_drv_qos_port_shapers_unlimited(uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	fal_shaper_config_t cfg = {
		.c_shaper_en = A_TRUE,
		.e_shaper_en = A_TRUE,
		/*
		 * TODO: Current max rate configured is that for 10G link.
		 * Add the logic to configure max rate (10G/25G) depending
		 * on the link speed.
		 */
		.cir = FAL_BYTE_SHAPER_MAX_RATE,
		.eir = FAL_BYTE_SHAPER_MAX_RATE,
	};
	ppe_drv_ret_t ret = PPE_DRV_RET_SUCCESS;
	const char *fail_stage = "none";
	uint32_t i;
	uint32_t id = port_id;
	uint32_t flow_set = 0;
	uint32_t ucast_set = 0;
	uint32_t mcast_set = 0;

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_port_state_find(port_id);
	if (!state) {
		spin_unlock_bh(&ppe_drv_qos_port_state_lock);
		ppe_drv_info("%px:no QoS hierarchy found for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_HIERARCHY_NOT_FOUND;
	}

	spin_lock_bh(&p->lock);

	fail_stage = "port_shaper_disable";
	if (fal_port_shaper_set(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
		goto fail;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_L0_SP]; i++) {
		id = state->port.base[PPE_DRV_QOS_RES_TYPE_L0_SP] + i;
		fail_stage = "flow_shaper_disable";
		if (fal_flow_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		flow_set++;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE]; i++) {
		id = state->port.base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] + i;
		fail_stage = "ucast_queue_shaper_disable";
		if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		ucast_set++;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE]; i++) {
		id = state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] + i;
		fail_stage = "mcast_queue_shaper_disable";
		if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		mcast_set++;
	}

	spin_unlock_bh(&p->lock);
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	return PPE_DRV_RET_SUCCESS;

fail:
	spin_unlock_bh(&p->lock);
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);
	ppe_drv_warn("%px:port:%u shapers unlimited failed stage:%s resource_id:%u flow_done:%u ucast_done:%u mcast_done:%u",
			p, port_id, fail_stage, id, flow_set, ucast_set, mcast_set);
	return ret;
}
EXPORT_SYMBOL(ppe_drv_qos_port_shapers_unlimited);

/*
 * ppe_drv_qos_port_shapers_restore()
 *	Restore all shapers (port, flow, and queues) from shadow state.
 */
ppe_drv_ret_t ppe_drv_qos_port_shapers_restore(uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	fal_shaper_token_number_t token = {
		.c_token_number = PPE_DRV_QOS_TOKEN_MAX,
		.e_token_number = PPE_DRV_QOS_TOKEN_MAX,
	};
	fal_shaper_config_t cfg = {0};
	ppe_drv_ret_t ret = PPE_DRV_RET_SUCCESS;
	const char *fail_stage = "none";
	uint32_t i;
	uint32_t id = port_id;
	uint32_t flow_restore = 0;
	uint32_t ucast_restore = 0;
	uint32_t mcast_restore = 0;
	bool port_restore = false;

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_port_state_find(port_id);
	if (!state) {
		spin_unlock_bh(&ppe_drv_qos_port_state_lock);
		ppe_drv_info("%px:no QoS hierarchy found for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_HIERARCHY_NOT_FOUND;
	}

	spin_lock_bh(&p->lock);

	if (state->port_shaper_valid) {
		ppe_drv_qos_shaper_cfg_to_fal(&state->port_shaper, &cfg);
		fail_stage = "port_shaper_restore";
		id = port_id;
		if ((fal_port_shaper_token_number_set(PPE_DRV_SWITCH_ID, port_id, &token) != SW_OK) ||
		    (fal_port_shaper_set(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK)) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		port_restore = true;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_L0_SP]; i++) {
		if (!state->flows[i].valid) {
			continue;
		}

		id = state->port.base[PPE_DRV_QOS_RES_TYPE_L0_SP] + i;
		ppe_drv_qos_shaper_cfg_to_fal(&state->flows[i].cfg, &cfg);
		fail_stage = "flow_shaper_restore";
		if ((fal_flow_shaper_token_number_set(PPE_DRV_SWITCH_ID, id, &token) != SW_OK) ||
		    (fal_flow_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK)) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		flow_restore++;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE]; i++) {
		if (!state->ucast_queues[i].valid) {
			continue;
		}

		id = state->port.base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] + i;
		ppe_drv_qos_shaper_cfg_to_fal(&state->ucast_queues[i].cfg, &cfg);
		fail_stage = "ucast_queue_shaper_restore";
		if ((fal_queue_shaper_token_number_set(PPE_DRV_SWITCH_ID, id, &token) != SW_OK) ||
		    (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK)) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		ucast_restore++;
	}

	for (i = 0; i < state->port.max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE]; i++) {
		if (!state->mcast_queues[i].valid) {
			continue;
		}

		id = state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] + i;
		ppe_drv_qos_shaper_cfg_to_fal(&state->mcast_queues[i].cfg, &cfg);
		fail_stage = "mcast_queue_shaper_restore";
		if ((fal_queue_shaper_token_number_set(PPE_DRV_SWITCH_ID, id, &token) != SW_OK) ||
		    (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, id, &cfg) != SW_OK)) {
			ret = PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
			goto fail;
		}
		mcast_restore++;
	}

	spin_unlock_bh(&p->lock);
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	return PPE_DRV_RET_SUCCESS;

fail:
	spin_unlock_bh(&p->lock);
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);
	ppe_drv_warn("%px:port:%u shapers restore failed stage:%s resource_id:%u port_restored:%u flow_done:%u ucast_done:%u mcast_done:%u",
			p, port_id, fail_stage, id, port_restore, flow_restore, ucast_restore, mcast_restore);
	return ret;
}
EXPORT_SYMBOL(ppe_drv_qos_port_shapers_restore);

/*
 * ppe_drv_qos_pon_drainout()
 *	Drain all PON T-CONTs through the registered DDRQ callback.
 */
ppe_drv_ret_t ppe_drv_qos_pon_drainout(void)
{
#if defined(NSS_PPE_PON_SUPPORT) && defined(CONFIG_IPQ_PON)
	struct ppe_drv *p = ppe_drv_gbl;
	bool ddrq_cb_ok;

	rcu_read_lock();
	ddrq_cb_ok = ppe_drv_qos_ddrq_pon_drainout_cb_invoke(p,
							      rcu_dereference(p->ddrq_ops.ddrq_pon_drainout));
	rcu_read_unlock();
	if (!ddrq_cb_ok)
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
#endif

	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_pon_drainout);

/*
 * ppe_drv_qos_port_is_pon()
 *	Check whether a port is the configured PON port.
 */
bool ppe_drv_qos_port_is_pon(uint32_t port_id)
{
#if defined(NSS_PPE_PON_SUPPORT) && defined(CONFIG_IPQ_PON)
	uint32_t pon_port_id = ppe_drv_qos_pon_port_get();

	return pon_port_id && (port_id == pon_port_id);
#else
	return false;
#endif
}
EXPORT_SYMBOL(ppe_drv_qos_port_is_pon);

/*
 * ppe_drv_qos_port_shaper_reset()
 *	Resets a port shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_port_shaper_reset(struct ppe_drv_qos_res *res, uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	fal_shaper_config_t cfg = {0};

	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:port shaper reset for port:%u", p, port_id);
	if (fal_port_shaper_set(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port shaper configuration failed for port:%u",
				p, port_id);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_port_state_find(port_id);
	if (state) {
		memset(&state->port_shaper, 0, sizeof(state->port_shaper));
		state->port_shaper_valid = false;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px: port shaper configuration successful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_port_shaper_reset);

/*
 * ppe_drv_qos_port_shaper_set()
 *	Configures a port shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_port_shaper_set(struct ppe_drv_qos_res *res, uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	fal_shaper_token_number_t token = {0};
	fal_shaper_config_t cfg = {0};

	/*
	 * Set port shaper token number
	 */
	spin_lock_bh(&p->lock);
	token.c_token_number = PPE_DRV_QOS_TOKEN_MAX;
	token.e_token_number = PPE_DRV_QOS_TOKEN_MAX;

	ppe_drv_trace("%px:port token set for port:%u, c_token_number:%x, e_token_number:%x",
		p, port_id, token.c_token_number, token.e_token_number);
	if (fal_port_shaper_token_number_set(PPE_DRV_SWITCH_ID, port_id, &token) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port shaper token configuration failed for port:%u",
				p, port_id);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	/*
	 * Set port shaper configuration
	 * Note: SSDK API requires burst in bytes/sec
	 * while rate in kbits/sec.
	 */
	cfg.c_shaper_en = 1;
	cfg.cbs = res->shaper.cburst;
	cfg.cir = (res->shaper.crate / 1000) * 8;
	cfg.shaper_frame_mode = (fal_shaper_frame_mode_t)PPE_DRV_QOS_FRAME_MODE_FRAME_CRC;

	ppe_drv_trace("%px:port shaper configuration for port:%u, couple_en:%u, meter_unit:%u, c_shaper_en:%u, cbs:%u, cir:%u, ebs:%u, eir:%u, shaper_frame_mode:%u",
			p, port_id, cfg.couple_en, cfg.meter_unit, cfg.c_shaper_en, cfg.cbs, cfg.cir, cfg.ebs, cfg.eir, cfg.shaper_frame_mode);
	if (fal_port_shaper_set(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port shaper configuration failed for port:%u",
				p, port_id);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_port_state_find(port_id);
	if (state) {
		ppe_drv_qos_shaper_cfg_from_fal(&state->port_shaper, &cfg);
		state->port_shaper_valid = true;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:port shaper configuration successful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_port_shaper_set);

/*
 * ppe_drv_qos_flow_shaper_reset()
 *	Resets a flow shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_flow_shaper_reset(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_config_t cfg = {0};

	/*
	 * Reset flow shaper configuration
	 */
	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:flow shaper reset for l0spid:%u", p, res->l0spid);
	if (fal_flow_shaper_set(PPE_DRV_SWITCH_ID, res->l0spid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:flow shaper configuration failed for l0spid:%u",
			p, res->l0spid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_state_find_by_l0sp(res->l0spid);
	if (state) {
		offset = res->l0spid - state->port.base[PPE_DRV_QOS_RES_TYPE_L0_SP];
		memset(&state->flows[offset].cfg, 0, sizeof(state->flows[offset].cfg));
		state->flows[offset].valid = false;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:flow shaper configuration successful for l0spid:%u", p, res->l0spid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_flow_shaper_reset);

/*
 * ppe_drv_qos_flow_shaper_set()
 *	Configures a flow shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_flow_shaper_set(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_token_number_t token = {0};
	fal_shaper_config_t cfg = {0};

	/*
	 * Set flow shaper token number
	 */
	spin_lock_bh(&p->lock);
	token.c_token_number = PPE_DRV_QOS_TOKEN_MAX;
	token.e_token_number = PPE_DRV_QOS_TOKEN_MAX;

	ppe_drv_trace("%px:flow token set for l0spid:%u, c_token_number:%x, e_token_number:%x",
		p, res->l0spid, token.c_token_number, token.e_token_number);
	if (fal_flow_shaper_token_number_set(PPE_DRV_SWITCH_ID, res->l0spid, &token) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:flow shaper token configuration failed for l0spid:%u",
			p, res->l0spid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	/*
	 * Set flow shaper token configuration
	 * Note: SSDK API requires burst in bytes/sec
	 * while rate in kbits/sec.
	 */
	cfg.c_shaper_en = 1;
	cfg.cbs = res->shaper.burst;
	cfg.cir = (res->shaper.rate / 1000) * 8;
	cfg.e_shaper_en = 1;
	cfg.ebs = res->shaper.cburst;
	cfg.eir = ((res->shaper.crate / 1000) * 8) - cfg.cir;
	cfg.shaper_frame_mode = (fal_shaper_frame_mode_t)PPE_DRV_QOS_FRAME_MODE_FRAME_CRC;

	ppe_drv_trace("%px:flow shaper configuration: l0spid:%u, couple_en:%u, meter_unit:%u, c_shaper_en:%u, cbs:%u, cir:%u, ebs:%u, eir:%u, shaper_frame_mode:%u",
		p, res->l0spid, cfg.couple_en, cfg.meter_unit, cfg.c_shaper_en, cfg.cbs, cfg.cir, cfg.ebs, cfg.eir, cfg.shaper_frame_mode);
	if (fal_flow_shaper_set(PPE_DRV_SWITCH_ID, res->l0spid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:flow shaper configuration failed for l0spid:%u",
			p, res->l0spid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_state_find_by_l0sp(res->l0spid);
	if (state) {
		offset = res->l0spid - state->port.base[PPE_DRV_QOS_RES_TYPE_L0_SP];
		ppe_drv_qos_shaper_cfg_from_fal(&state->flows[offset].cfg, &cfg);
		state->flows[offset].valid = true;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:flow shaper configuration successful for l0spid:%u", p, res->l0spid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_flow_shaper_set);

/*
 * ppe_drv_qos_mcast_queue_shaper_reset()
 *	Resets a multicast queue shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_shaper_reset(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_config_t cfg = {0};

	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:multicast queue shaper reset for mcast_qid:%u", p, res->q.mcast_qid);
	if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:multicast queue shaper configuration failed for mcast_qid:%u",
			p, res->q.mcast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_state_find_by_qid(res->q.mcast_qid);
	if (state) {
		offset = res->q.mcast_qid - state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];
		memset(&state->mcast_queues[offset].cfg, 0, sizeof(state->mcast_queues[offset].cfg));
		state->mcast_queues[offset].valid = false;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:multicast queue reset successful for mcast_qid:%u", p, res->q.mcast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_mcast_queue_shaper_reset);

/*
 * ppe_drv_qos_mcast_queue_shaper_set()
 *	Configures a multicast queue shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_shaper_set(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_token_number_t token = {0};
	fal_shaper_config_t cfg = {0};

	spin_lock_bh(&p->lock);
	token.c_token_number = PPE_DRV_QOS_TOKEN_MAX;
	token.e_token_number = PPE_DRV_QOS_TOKEN_MAX;

	ppe_drv_trace("%px:multicast queue token set for mcast_qid:%u, c_token_number:%x, e_token_number:%x",
		p, res->q.mcast_qid, token.c_token_number, token.e_token_number);
	if (fal_queue_shaper_token_number_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, &token) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:multicast queue shaper token configuration failed for mcast_qid:%u", p, res->q.mcast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	if (fal_queue_shaper_get(PPE_DRV_SWITCH_ID, res->q.ucast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue shaper configuration fetch failed for ucast_qid:%u", p, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	ppe_drv_trace("%px:multicast queue shaper configuration for mcast_qid:%u, couple_en:%u, meter_unit:%u, c_shaper_en:%u, cbs:%u, cir:%u, ebs:%u, eir:%u, shaper_frame_mode:%u",
		p, res->q.mcast_qid, cfg.couple_en, cfg.meter_unit, cfg.c_shaper_en, cfg.cbs, cfg.cir, cfg.ebs, cfg.eir, cfg.shaper_frame_mode);
	if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:multicast queue shaper configuration failed for mcast_qid:%u", p, res->q.mcast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_state_find_by_qid(res->q.mcast_qid);
	if (state) {
		offset = res->q.mcast_qid - state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];
		ppe_drv_qos_shaper_cfg_from_fal(&state->mcast_queues[offset].cfg, &cfg);
		state->mcast_queues[offset].valid = true;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:queue shaper configuration successful for mcast_qid:%u", p, res->q.mcast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_mcast_queue_shaper_set);

/*
 * ppe_drv_qos_queue_shaper_reset()
 *	Resets a queue shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_shaper_reset(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_config_t cfg = {0};

	/*
	 * Reset queue shaper configuration
	 */
	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:queue shaper reset for ucast_qid:%u",
			p, res->q.ucast_qid);
	if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, res->q.ucast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue shaper configuration failed for ucast_qid:%u",
			p, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);
	state = ppe_drv_qos_state_find_by_qid(res->q.ucast_qid);
	if (state) {
		offset = res->q.ucast_qid - state->port.base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE];
		memset(&state->ucast_queues[offset].cfg, 0, sizeof(state->ucast_queues[offset].cfg));
		state->ucast_queues[offset].valid = false;
	}
	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:queue shaper configuration successful for ucast_qid:%u", p, res->q.ucast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_shaper_reset);

/*
 * ppe_drv_qos_queue_shaper_set()
 *	Configures a queue shaper in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_shaper_set(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_qos_port_state *state = NULL;
	uint32_t offset;
	fal_shaper_token_number_t token = {0};
	fal_shaper_config_t cfg = {0};

	/*
	 * Set queue shaper token number
	 */
	spin_lock_bh(&p->lock);
	token.c_token_number = PPE_DRV_QOS_TOKEN_MAX;
	token.e_token_number = PPE_DRV_QOS_TOKEN_MAX;

	ppe_drv_trace("%px:queue token set for ucast_qid:%u, c_token_number:%x, e_token_number:%x",
		p, res->q.ucast_qid, token.c_token_number, token.e_token_number);
	if (fal_queue_shaper_token_number_set(PPE_DRV_SWITCH_ID, res->q.ucast_qid, &token) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue shaper token configuration failed for ucast_qid:%u", p, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	/*
	 * Set queue shaper confguration
	 * Note: SSDK API requires burst in bytes/sec
	 * while rate in kbits/sec.
	 */
	cfg.c_shaper_en = 1;
	cfg.cbs = res->shaper.burst;
	cfg.cir = (res->shaper.rate / 1000) * 8;
	cfg.e_shaper_en = 1;
	cfg.ebs = res->shaper.cburst;
	cfg.eir = ((res->shaper.crate / 1000) * 8) - cfg.cir;
	cfg.shaper_frame_mode = (fal_shaper_frame_mode_t)PPE_DRV_QOS_FRAME_MODE_FRAME_CRC;

	ppe_drv_trace("%px:queue shaper configuration for ucast_qid:%u, couple_en:%u, meter_unit:%u, c_shaper_en:%u, cbs:%u, cir:%u, ebs:%u, eir:%u, shaper_frame_mode:%u",
		p, res->q.ucast_qid, cfg.couple_en, cfg.meter_unit, cfg.c_shaper_en, cfg.cbs, cfg.cir, cfg.ebs, cfg.eir, cfg.shaper_frame_mode);
	if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, res->q.ucast_qid, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue shaper configuration failed for ucast_qid:%u", p, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
	}

	/*
	 * This function is called at the time of class allocation and change at
	 * PPE_DRV_QOS_QUEUE_LEVEL. But, mcast will be valid only in case queue
	 * is attached at PPE_DRV_QOS_SUB_QUEUE_LEVEL and change class is invoked
	 * at PPE_DRV_QOS_QUEUE_LEVEL level.
	 * Other scenarios catering to mcast queue are handled in separate mcast APIs.
	 */
	if (res->q.mcast_qid) {
		ppe_drv_trace("%px:multicast queue shaper configuration for mcast_qid:%u, couple_en:%u, meter_unit:%u, c_shaper_en:%u, cbs:%u, cir:%u, ebs:%u, eir:%u, shaper_frame_mode:%u",
			p, res->q.mcast_qid, cfg.couple_en, cfg.meter_unit, cfg.c_shaper_en, cfg.cbs, cfg.cir, cfg.ebs, cfg.eir, cfg.shaper_frame_mode);
		if (fal_queue_shaper_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, &cfg) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:multicast queue shaper configuration failed for mcast_qid:%u", p, res->q.mcast_qid);
			return PPE_DRV_RET_QOS_SHAPER_CFG_FAIL;
		}
	}

	spin_unlock_bh(&p->lock);

	spin_lock_bh(&ppe_drv_qos_port_state_lock);

	state = ppe_drv_qos_state_find_by_qid(res->q.ucast_qid);
	if (state) {
		offset = res->q.ucast_qid - state->port.base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE];
		ppe_drv_qos_shaper_cfg_from_fal(&state->ucast_queues[offset].cfg, &cfg);
		state->ucast_queues[offset].valid = true;
	}

	if (res->q.mcast_qid) {
		state = ppe_drv_qos_state_find_by_qid(res->q.mcast_qid);
		if (state) {
			offset = res->q.mcast_qid - state->port.base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE];
			ppe_drv_qos_shaper_cfg_from_fal(&state->mcast_queues[offset].cfg, &cfg);
			state->mcast_queues[offset].valid = true;
		}
	}

	spin_unlock_bh(&ppe_drv_qos_port_state_lock);

	ppe_drv_info("%px:queue shaper configuration successful for mcast_qid:%u", p, res->q.mcast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_shaper_set);

/*
 * ppe_drv_qos_mcast_queue_set()
 *	Configures a multicast queue in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_set(struct ppe_drv_qos_res *res, uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_qos_scheduler_cfg_t l0cfg;

	spin_lock_bh(&p->lock);
	ppe_drv_trace("%px:level0 configuration get for port:%u, ucast_qid:%u", p, port_id, res->q.ucast_qid);
	if (fal_queue_scheduler_get(PPE_DRV_SWITCH_ID, res->q.ucast_qid, PPE_DRV_QOS_QUEUE_LEVEL - 1, &port_id, &l0cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:level0 unicast queue scheduler configuration fetch failed for port:%u, ucast_qid:%u", p, port_id, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	ppe_drv_trace("%px:level0 configuration: port:%u, mcast_qid:%u, c_drrid:%u, c_pri:%u, c_drr_wt:%u, e_drrid:%u, e_pri:%u, e_drr_wt:%u, l0spid:%u",
			p, port_id, res->q.mcast_qid, l0cfg.c_drr_id, l0cfg.c_pri, l0cfg.c_drr_wt, l0cfg.e_drr_id, l0cfg.e_pri, l0cfg.e_drr_wt, l0cfg.sp_id);
	if (fal_queue_scheduler_set(PPE_DRV_SWITCH_ID, res->q.mcast_qid, PPE_DRV_QOS_QUEUE_LEVEL - 1, port_id, &l0cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:level0 multicast queue scheduler configuration failed for port:%u, ucast_qid:%u", p, port_id, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	spin_unlock_bh(&p->lock);
	ppe_drv_info("%px:multicast queue configuration successful for port:%u, ucast_qid:%u", p, port_id, res->q.ucast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_mcast_queue_set);

/*
 * ppe_drv_qos_queue_limit_set()
 *	Sets queue size in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_queue_limit_set(struct ppe_drv_qos_res *res)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_ac_obj_t obj = {0};
	fal_ac_static_threshold_t static_cfg;
	fal_ac_ctrl_t ctrl_cfg = {0};
	struct ppe_drv_qos_queue q_cfg = {0};
	ppe_drv_ret_t ret;
	bool ddrq_cb_ok;
	bool is_ddrq_en;
	ppe_drv_qos_ddrq_limit_set_callback_t ddrq_limit_set_cb = NULL;
	struct ppe_drv_qos_ddrq_limit_set_cfg ddrq_limit_set_cfg;

	if (!ppe_drv_qos_ddrq_state_get(res->q.ucast_qid, &is_ddrq_en)) {
		ppe_drv_warn("%px: Error in fetching the mapped DDRQ:%u state\n", p, res->q.ucast_qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	/*
	 * Unicast queue configuration.
	 */
	spin_lock_bh(&p->lock);
	obj.obj_id = res->q.ucast_qid;

	/*
	 * Enable force drop for PPE qdisc.
	 * When ac_en/ac_fc_en are set to 1, the flow control will be overriden
	 * for that queue and packets drop gets enabled.
	 */
	ctrl_cfg.ac_en = A_TRUE;
	ctrl_cfg.ac_fc_en = A_TRUE;

	if (res->q.mcast_valid) {
		goto config_mcast;
	}

	ppe_drv_trace("%px:queue flow control set for ucast_qid:%u, enable:%u", p, res->q.ucast_qid, ctrl_cfg.ac_fc_en);
	if (fal_ac_ctrl_set(PPE_DRV_SWITCH_ID, &obj, &ctrl_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue flow control set failed for ucast_qid:%u", p, res->q.ucast_qid);
		return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
	}

	if (!is_ddrq_en) {
		ret = ppe_drv_qos_queue_cfg_set_nolock(p, res->q.ucast_qid, &res->q);
		if (ret != PPE_DRV_RET_SUCCESS) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:queue configuration set failed for ucast_qid:%u", p, res->q.ucast_qid);
			return ret;
		}
	} else {
		spin_unlock_bh(&p->lock);

		ddrq_limit_set_cfg.qos_queue = &res->q;
		ddrq_limit_set_cfg.qdisc_ceiling_unit = res->q.q_ceiling_unit;
		ddrq_limit_set_cfg.port_mtu = res->port_mtu;

		rcu_read_lock();
		ddrq_limit_set_cb = rcu_dereference(p->ddrq_ops.ddrq_limit_set);
		ddrq_cb_ok = ppe_drv_qos_ddrq_limit_set_cb_invoke(p, ddrq_limit_set_cb,
								  &ddrq_limit_set_cfg);
		rcu_read_unlock();
		if (!ddrq_cb_ok) {
			ppe_drv_warn("%px:ddrq queue limit callback failed for ucast_qid:%u", p, res->q.ucast_qid);
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}
		spin_lock_bh(&p->lock);

		/*
		 * For mapped DDRQ queues, fetch current dynamic threshold config
		 * and only override ceiling with DDRQ-specific value.
		 */
		ret = ppe_drv_qos_queue_cfg_get_nolock(p, res->q.ucast_qid, &q_cfg);
		if (ret != PPE_DRV_RET_SUCCESS) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:queue configuration get failed for ucast_qid:%u", p, res->q.ucast_qid);
			return ret;
		}

		q_cfg.qlimit = PPE_DRV_QOS_DDRQ_CFG_CEILING_VAL;
		ret = ppe_drv_qos_queue_cfg_set_nolock(p, res->q.ucast_qid, &q_cfg);
		if (ret != PPE_DRV_RET_SUCCESS) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:ddrq ceiling set failed for ucast_qid:%u", p, res->q.ucast_qid);
			return ret;
		}
	}
config_mcast:
	/*
	 * Multicast queue configuration.
	 */
	if (res->q.mcast_qid) {
		if (fal_ac_static_threshold_get(PPE_DRV_SWITCH_ID, &obj, &static_cfg) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:queue static threshold get failed for mcast_qid:%u", p, res->q.mcast_qid);
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		} else {
			obj.obj_id = res->q.mcast_qid;
			static_cfg.color_enable = res->q.color_en;
			static_cfg.wred_enable = res->q.red_en;
			static_cfg.green_min_off = res->q.min_th[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
			static_cfg.yel_min_off = res->q.min_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
			static_cfg.yel_max_off = res->q.max_th[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
			static_cfg.red_min_off = res->q.min_th[PPE_DRV_QOS_QUEUE_COLOR_RED];
			static_cfg.red_max_off = res->q.max_th[PPE_DRV_QOS_QUEUE_COLOR_RED];

			/*
			 * Set resume offsets for all three colors.
			 * Resume offsets control when the queue resumes accepting packets after dropping,
			 * which helps prevent buffer oscillation.
			 */
			static_cfg.green_resume_off = res->q.resume_off[PPE_DRV_QOS_QUEUE_COLOR_GREEN];
			static_cfg.yel_resume_off = res->q.resume_off[PPE_DRV_QOS_QUEUE_COLOR_YELLOW];
			static_cfg.red_resume_off = res->q.resume_off[PPE_DRV_QOS_QUEUE_COLOR_RED];
		}

		ppe_drv_trace("%px:multicast queue buffer set for mcast_qid:%u, qlimit:%u", p, res->q.mcast_qid, res->q.qlimit);
		if (fal_ac_prealloc_buffer_set(PPE_DRV_SWITCH_ID, &obj, res->q.qlimit) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:multicast queue buffer allocation failed for mcast_qid:%u", p, res->q.mcast_qid);
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}

		ppe_drv_trace("%px:multicast queue ac threshold set for mcast_qid:%u, wred_enable:%u, green_max:%u, green_min_off:%u",
			p, res->q.mcast_qid, static_cfg.wred_enable, static_cfg.green_max, static_cfg.green_min_off);
		if (fal_ac_static_threshold_set(PPE_DRV_SWITCH_ID, &obj, &static_cfg) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:multicast queue static threshold failed for mcast_qid:%u", p, res->q.mcast_qid);
			return PPE_DRV_RET_QOS_QUEUE_CFG_FAIL;
		}
	}

	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px:queue configuration successful for ucast_qid:%u", p, res->q.ucast_qid);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_queue_limit_set);

/*
 *ppe_drv_qos_port_bm_control_enable()
 *	Enable or disable port buffer management for flow control
 */
void ppe_drv_qos_port_bm_control_enable(uint32_t port_id, bool set)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	fal_port_bm_ctrl_set(PPE_DRV_SWITCH_ID, port_id, set);
	spin_unlock_bh(&p->lock);

	ppe_drv_info("%px: port buffer flow control is successfully set to %d for port %u", p, set, port_id);

}
EXPORT_SYMBOL(ppe_drv_qos_port_bm_control_enable);

/*
 * ppe_drv_qos_default_conf_set()
 *	Sets default queue scheduler in PPE.
 */
ppe_drv_ret_t ppe_drv_qos_default_conf_set(uint32_t port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_portscheduler_resource_t cfg = {0};
	fal_ucast_queue_ddrq_en_t ddrq_en_cfg = {0};
	struct ppe_drv_qos_queue q_cfg = {0};
	uint32_t qid;
	ppe_drv_ret_t ret;
	bool ddrq_cb_ok;

	/*
	 * Invoke SSDK API to reset the default configuration for a given port.
	 */
	spin_lock_bh(&p->lock);
	if (fal_port_scheduler_cfg_reset(PPE_DRV_SWITCH_ID, port_id) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:reset default queue configuration failed for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_PORT_CFG_FAIL;
	}

	if (fal_port_scheduler_resource_get(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port QoS resources info failed for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_PORT_CFG_FAIL;
	}

	for (qid = cfg.ucastq_start; qid < (cfg.ucastq_start + cfg.ucastq_num); qid++) {
		if (fal_ucast_queue_ddrq_en_get(PPE_DRV_SWITCH_ID, qid, &ddrq_en_cfg) != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:failed to fetch DDRQ state for ucast_qid:%u", p, qid);
			return PPE_DRV_RET_QOS_PORT_CFG_FAIL;
		}

		if (!ddrq_en_cfg.ddrq_en) {
			continue;
		}

		/*
		 * Fetch current dynamic threshold config and only restore ceiling.
		 */
		ret = ppe_drv_qos_queue_cfg_get_nolock(p, qid, &q_cfg);
		if (ret != PPE_DRV_RET_SUCCESS) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:queue configuration get failed for qid:%u", p, qid);
			return ret;
		}

		q_cfg.qlimit = PPE_DRV_QOS_DEF_CFG_CEILING_VAL;
		ret = ppe_drv_qos_queue_cfg_set_nolock(p, qid, &q_cfg);
		if (ret != PPE_DRV_RET_SUCCESS) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%px:default ceiling reset failed for qid:%u", p, qid);
			return ret;
		}
	}

	spin_unlock_bh(&p->lock);

	rcu_read_lock();
	ddrq_cb_ok = ppe_drv_qos_ddrq_port_cb_invoke(p,
				rcu_dereference(p->ddrq_ops.ddrq_cfg_reset),
				port_id, "default config reset");
	rcu_read_unlock();
	if (!ddrq_cb_ok) {
		ppe_drv_warn("%px:reset default DDRQ configuration failed for port:%u", p, port_id);
		return PPE_DRV_RET_QOS_PORT_CFG_FAIL;
	}

	ppe_drv_qos_port_bm_control_enable(port_id, true);
	ppe_drv_info("%px:queue configuration successful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_default_conf_set);

/*
 * ppe_drv_qos_port_res_get()
 *	Gets boot time QoS resource allocation information for a given port.
 */
ppe_drv_ret_t ppe_drv_qos_port_res_get(uint32_t port_id, struct ppe_drv_qos_port *port)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_portscheduler_resource_t cfg = {0};

	/*
	 * Get boot time QoS resource information for a given port
	 */
	spin_lock_bh(&p->lock);
	if (fal_port_scheduler_resource_get(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port QoS resources info failed for port:%u", p, port_id);
		return PPE_DRV_RET_FAILURE_NO_RESOURCE;
	}

	spin_unlock_bh(&p->lock);
	port->max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] = cfg.ucastq_num;
	port->base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] = cfg.ucastq_start;

	port->max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] = cfg.mcastq_num;
	port->base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] = cfg.mcastq_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L0_CDRR] = cfg.l0cdrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_CDRR] = cfg.l0cdrr_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L0_EDRR] = cfg.l0edrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_EDRR] = cfg.l0edrr_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L0_SP] = cfg.l0sp_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_SP] = cfg.l0sp_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L1_CDRR] = cfg.l1cdrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_CDRR] = cfg.l1cdrr_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L1_EDRR] = cfg.l1edrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_EDRR] = cfg.l1edrr_start;


	port->max[PPE_DRV_QOS_RES_TYPE_L1_SP] = cfg.l1sp_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_SP] = cfg.l1sp_start;

	ppe_drv_info("%px:port QoS resources infosuccessful for port:%u", p, port_id);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_port_res_get);

#ifdef NSS_PPE_DSA_QUEUE_SUPPORT
/*
 * ppe_drv_qos_port_htt_res_get()
 *	Gets boot-time QoS resource allocation information for a given switch slave port.
 */
ppe_drv_ret_t ppe_drv_qos_port_htt_res_get(uint32_t port_id, uint8_t index, struct ppe_drv_qos_port *port)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_portscheduler_resource_t cfg = {0};
	int num_groups;

	/*
	 * Get boot time QoS resource information for a given port.
	 */
	spin_lock_bh(&p->lock);
	if (fal_port_scheduler_resource_get(PPE_DRV_SWITCH_ID, port_id, &cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:port QoS resources info failed for port:%u", p, port_id);
		return PPE_DRV_RET_FAILURE_NO_RESOURCE;
	}

	spin_unlock_bh(&p->lock);

	num_groups = cfg.ucastq_num / cfg.max_pri;
	if (!num_groups) {
		ppe_drv_warn("%px:invalid HTT queue groups:0 for port:%u ucastq_num:%u max_pri:%u",
				p, port_id, cfg.ucastq_num, cfg.max_pri);
		return PPE_DRV_RET_FAILURE_NO_RESOURCE;
	}

	port->max[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] = cfg.ucastq_num / num_groups;
	port->base[PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE] = cfg.ucastq_start + index * cfg.max_pri;

	port->max[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] = cfg.mcastq_num / num_groups;
	port->base[PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE] = cfg.mcastq_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L0_CDRR] = cfg.l0cdrr_num / num_groups;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_CDRR] = cfg.l0cdrr_start + index * (cfg.l0cdrr_num / num_groups);

	port->max[PPE_DRV_QOS_RES_TYPE_L0_EDRR] = cfg.l0edrr_num / num_groups;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_EDRR] = cfg.l0edrr_start + index * (cfg.l0edrr_num / num_groups);

	port->max[PPE_DRV_QOS_RES_TYPE_L0_SP] = cfg.l0sp_num / num_groups;;
	port->base[PPE_DRV_QOS_RES_TYPE_L0_SP] = cfg.l0sp_start + index * (cfg.l0sp_num / num_groups);;

	port->max[PPE_DRV_QOS_RES_TYPE_L1_CDRR] = cfg.l1cdrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_CDRR] = cfg.l1cdrr_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L1_EDRR] = cfg.l1edrr_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_EDRR] = cfg.l1edrr_start;

	port->max[PPE_DRV_QOS_RES_TYPE_L1_SP] = cfg.l1sp_num;
	port->base[PPE_DRV_QOS_RES_TYPE_L1_SP] = cfg.l1sp_start;

	port->max_pri = cfg.max_pri;

	ppe_drv_info("%px:port QoS resources infosuccessful for port:%u index:%u", p, port_id, index);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_port_htt_res_get);
#endif

/*
 * ppe_drv_qos_ac_ctrl_cfg()
 *	API to configure AC control configuration on the given queue
 */
bool ppe_drv_qos_ac_ctrl_cfg(uint32_t qid, bool ac_en, bool ac_fc_en)
{
	fal_ac_obj_t obj = {0};
	fal_ac_ctrl_t ctrl_cfg = {0};

	obj.obj_id = qid;
	obj.type = FAL_AC_QUEUE;
	ctrl_cfg.ac_en = ac_en ? A_TRUE : A_FALSE;
	ctrl_cfg.ac_fc_en = ac_fc_en ? A_TRUE : A_FALSE;

	return ppe_drv_qos_ac_ctrl_set(qid, &obj, &ctrl_cfg);
}
EXPORT_SYMBOL(ppe_drv_qos_ac_ctrl_cfg);

/*
 * ppe_drv_qos_ddrq_bypass_set()
 *	API to configure DDRQ bypass on the given queue id
 */
ppe_drv_ret_t ppe_drv_qos_ddrq_bypass_set(uint32_t qid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_ac_ctrl_t ac_ctrl_cfg = {0};
	fal_ac_ctrl_t ac_ctrl_cfg_backup = {0};
	fal_ac_obj_t obj = {0};
	fal_ac_drop_state_t drop_cfg = {0};
	ppe_drv_ret_t ret;
	fal_ucast_queue_ddrq_en_t ddrq_en_cfg = {0};

	spin_lock_bh(&p->lock);
	/*
	 * Check whether the mapped DDRQ is enabled or not
	 */
	if (fal_ucast_queue_ddrq_en_get(PPE_DRV_SWITCH_ID, qid, &ddrq_en_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Error in getting the DDRQ state of qid:%d\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (!ddrq_en_cfg.ddrq_en) {
		spin_unlock_bh(&p->lock);
		ppe_drv_info("%p: The mapped DDRQ is not enabled for the input qid:%d\n", p, qid);
		return PPE_DRV_RET_SUCCESS;
	}

	obj.obj_id = qid;
	obj.type = FAL_AC_QUEUE;

	if (fal_ac_ctrl_get(PPE_DRV_SWITCH_ID, &obj, &ac_ctrl_cfg_backup) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue flow control get failed for qid:%u", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}
	spin_unlock_bh(&p->lock);

	/*
	 * Set AC enable and AC flow control enable configurations for the given queue id
	 */
	ac_ctrl_cfg.ac_en = A_TRUE;
	ac_ctrl_cfg.ac_fc_en = A_TRUE;
	if (!ppe_drv_qos_ac_ctrl_set(qid, &obj, &ac_ctrl_cfg)) {
		ppe_drv_warn("Error in configuring AC control configuration for qid:%d\n", qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	/*
	 * Disable the enqueue operation for the given queue id
	 */
	ret = ppe_drv_qos_queue_enqueue_ctrl(qid, false);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Error (%d) in disabling the qid:%d enqueue operation\n", ret, qid);
		if (!ppe_drv_qos_ac_ctrl_set(qid, &obj, &ac_ctrl_cfg_backup)) {
			ppe_drv_warn("Error in restoring AC control configuration for qid:%d\n", qid);
			return PPE_DRV_RET_FAILURE_INVALID_PARAM;
		}

		return ret;
	}

	/*
	 * Add the delay for letting the PPE consume all the pending packets in the queue
	 *
	 * TODO : Handle the scenarios in which the 30 msec time is not enough
	 * for the PPE to drain the SRAM queue, like the following:
	 * 1. SRAM queue has some lower rate shaper configuration applied over it
	 * 2. The configured credit is not enough to drain the packets from the
	 * PON interface.
	 */
	mdelay(30);

	/*
	 * Set all the drop states to 9 for the given queue id
	 */
	spin_lock_bh(&p->lock);
	if (fal_qm_ac_drop_state_get(PPE_DRV_SWITCH_ID, &obj, &drop_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue drop configuration get failed for qid:%u", p, qid);
		ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
		if (ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_warn("Error (%d) in restoring the qid:%d enqueue operation after drop get failure\n", ret, qid);
		}

		if (!ppe_drv_qos_ac_ctrl_set(qid, &obj, &ac_ctrl_cfg_backup)) {
			ppe_drv_warn("Error in restoring AC control configuration for qid:%d\n", qid);
		}

		return PPE_DRV_RET_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	drop_cfg.green_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE;
	drop_cfg.yel_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE;
	drop_cfg.red_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE;

	/*
	 * TODO: For the scenario, when the SRAM queue has the shaper configuration
	 * applied to it. We need to ideally create the backup of the applied
	 * shaper configuration, configure the PPE to drain the packet with max
	 * rate and then re-store the applied shaper configuration.
	 */
	if (!ppe_drv_qos_queue_drop_cfg_set(qid, &obj, &drop_cfg)) {
		ppe_drv_warn("%px: Error in setting the queue drop configuration for qid:%d\n", p, qid);
		ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
		if (ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_warn("Error (%d) in restoring the qid:%d enqueue operation after drop set failure\n", ret, qid);
		}

		if (!ppe_drv_qos_ac_ctrl_set(qid, &obj, &ac_ctrl_cfg_backup)) {
			ppe_drv_warn("Error in restoring AC control configuration for qid:%d\n", qid);
		}

		return PPE_DRV_RET_QUEUE_CFG_FAIL;
	}

	/*
	 * Enable the enqueue operation for the given queue id
	 */
	ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Error (%d) in enabling the qid:%d enqueue operation\n", ret, qid);
		return ret;
	}

	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_bypass_set);

/*
 * ppe_drv_qos_ddrq_bypass_reset()
 *	API to reset DDRQ bypass configuration on the given queue id
 */
ppe_drv_ret_t ppe_drv_qos_ddrq_bypass_reset(uint32_t qid)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_ac_ctrl_t ac_ctrl_cfg = {0};
	fal_ac_obj_t obj = {0};
	fal_ac_drop_state_t drop_cfg;
	ppe_drv_ret_t ret;
	fal_ucast_queue_ddrq_en_t ddrq_en_cfg = {0};

	spin_lock_bh(&p->lock);
	/*
	 * Check whether the mapped DDRQ is enabled or not
	 */
	if (fal_ucast_queue_ddrq_en_get(PPE_DRV_SWITCH_ID, qid, &ddrq_en_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Error in getting the DDRQ state of qid:%d\n", p, qid);
		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	if (!ddrq_en_cfg.ddrq_en) {
		spin_unlock_bh(&p->lock);
		ppe_drv_info("%p: The mapped DDRQ is not enabled for the input qid:%d\n", p, qid);
		return PPE_DRV_RET_SUCCESS;
	}
	spin_unlock_bh(&p->lock);

	/*
	 * Disable the enqueue operation for the given queue id
	 */
	ret = ppe_drv_qos_queue_enqueue_ctrl(qid, false);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Error (%d) in disabling the qid:%d enqueue operation\n", ret, qid);
		return ret;
	}

	/*
	 * Reset all the drop states to its default value for the given queue id
	 */
	spin_lock_bh(&p->lock);
	obj.obj_id = qid;
	obj.type = FAL_AC_QUEUE;
	if (fal_qm_ac_drop_state_get(PPE_DRV_SWITCH_ID, &obj, &drop_cfg) != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%px:queue drop configuration get failed for qid:%u", p, qid);
		ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
		if (ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_warn("Error (%d) in restoring the qid:%d enqueue operation after drop get failure\n", ret, qid);
		}

		return PPE_DRV_RET_QUEUE_CFG_FAIL;
	}
	spin_unlock_bh(&p->lock);

	drop_cfg.green_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE_RESET;
	drop_cfg.yel_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE_RESET;
	drop_cfg.red_drop = PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE_RESET;

	if (!ppe_drv_qos_queue_drop_cfg_set(qid, &obj, &drop_cfg)) {
		ppe_drv_warn("%px: Error in setting the queue drop configuration for qid:%d\n", p, qid);
		ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
		if (ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_warn("Error (%d) in restoring the qid:%d enqueue operation after drop set failure\n", ret, qid);
		}

		return PPE_DRV_RET_QUEUE_CFG_FAIL;
	}

	/*
	 * Add the delay for letting the PPE consume all the pending packets in the mapped DDRQ
	 */
	mdelay(30);

	/*
	 * Reset AC control configurations to the default values for the given queue id
	 */
	ac_ctrl_cfg.ac_en = A_TRUE;
	ac_ctrl_cfg.ac_fc_en = A_FALSE;
	if (!ppe_drv_qos_ac_ctrl_set(qid, &obj, &ac_ctrl_cfg)) {
		ppe_drv_warn("Error in configuring AC control configuration for qid:%d\n", qid);
		ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
		if (ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_warn("Error (%d) in restoring the qid:%d enqueue operation after AC config failure\n", ret, qid);
			return ret;
		}

		return PPE_DRV_RET_FAILURE_INVALID_PARAM;
	}

	/*
	 * Enable the enqueue operation for the given queue id
	 */
	ret = ppe_drv_qos_queue_enqueue_ctrl(qid, true);
	if (ret != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("Error (%d) in enabling the qid:%d enqueue operation\n", ret, qid);
		return ret;
	}

	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_qos_ddrq_bypass_reset);
