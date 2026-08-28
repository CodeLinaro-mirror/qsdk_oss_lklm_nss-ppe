/*
 * Copyright (c) 2017, 2020, The Linux Foundation. All rights reserved.
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * @file ppe_drv_qos.h
 *	PPE QoS specific definitions.
 */

#ifndef _PPE_DRV_QOS_H_
#define _PPE_DRV_QOS_H_

#include <linux/list.h>

/**
 * @addtogroup ppe_drv_qos_subsystem
 * @{
 */

#define PPE_DRV_QOS_PORT_MAX	8		/**< Maximum ports for QoS. */
#define PPE_DRV_QOS_DRR_WEIGHT_MAX	1024	/**< Maximum DRR weight for QoS schedulers. */
#define PPE_DRV_QOS_PRIORITY_MAX	8	/**< Maximum priority for QoS schedulers. */

#define PPE_DRV_QOS_TCONT_MAX 32	/**<Maximum T-conts supported. */
#define PPE_DRV_QOS_TCONT_L0_RES_MAX 128	/**<Maximum PQs and L0 DRRs reserved for T-conts. */

/**
 * Queues in PPE are assigned blocks of memory (not packets).
 * Each block is 256B in size.
 */
#define PPE_DRV_QOS_MEM_BLOCK_SIZE 256

#define PPE_DRV_QOS_DEF_CFG_CEILING_VAL	0x960
#define PPE_DRV_QOS_DDRQ_CFG_CEILING_VAL	0x50

/**
 * Queue drop state which needs to be configured during the DDRQ bypass
 * configuration and its reset state
 */
#define PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE	9
#define PPE_DRV_QOS_DDRQ_BYPASS_DROP_STATE_RESET	0

/**
 * ppe_drv_qos_res_type
 *	QoS resource type.
 */
enum ppe_drv_qos_res_type {
	PPE_DRV_QOS_RES_TYPE_UCAST_QUEUE,	/**< Unicast queue. */
	PPE_DRV_QOS_RES_TYPE_MCAST_QUEUE,	/**< Multicast queue. */
	PPE_DRV_QOS_RES_TYPE_L0_CDRR,		/**< Level 0 C-DRR.*/
	PPE_DRV_QOS_RES_TYPE_L0_EDRR,		/**< Level 0 E-DRR.*/
	PPE_DRV_QOS_RES_TYPE_L0_SP,		/**< Level 0 strict priority resource.*/
	PPE_DRV_QOS_RES_TYPE_L1_CDRR,		/**< Level 1 C-DRR.*/
	PPE_DRV_QOS_RES_TYPE_L1_EDRR,		/**< Level 1 E-DRR.*/
	PPE_DRV_QOS_RES_TYPE_L1_SP,		/**< Level 1 strict priority resource.*/
	PPE_DRV_QOS_RES_TYPE_MAX		/**< Maximum resource type. */
};
typedef enum ppe_drv_qos_res_type ppe_drv_qos_res_type_t;

/**
 * ppe_drv_qos_level
 *	Shaper/Scheduler levels.
 */
enum ppe_drv_qos_level {
	PPE_DRV_QOS_SUB_QUEUE_LEVEL,	/**< Resource is for sub queue level. */
	PPE_DRV_QOS_QUEUE_LEVEL,	/**< Resource is for queue level. */
	PPE_DRV_QOS_FLOW_LEVEL,		/**< Resource is for flow level. */
	PPE_DRV_QOS_PORT_LEVEL,		/**< Resource is for port level. */
	PPE_DRV_QOS_MAX_LEVEL		/**< Maximum resource level. */
};
typedef enum ppe_drv_qos_level ppe_drv_qos_level_t;

/**
 * ppe_drv_qos_queue_color
 *	Supported queue colors.
 */
enum ppe_drv_qos_queue_color {
	PPE_DRV_QOS_QUEUE_COLOR_GREEN,		/**< QoS queue color is green. */
	PPE_DRV_QOS_QUEUE_COLOR_YELLOW,		/**< QoS queue color is yellow. */
	PPE_DRV_QOS_QUEUE_COLOR_RED,		/**< Qos queue color is red. */
	PPE_DRV_QOS_QUEUE_COLOR_MAX		/**< Maximum QoS queue color. */
};
typedef enum ppe_drv_qos_queue_color ppe_drv_qos_queue_color_t;

/**
 * ppe_drv_qos_drr_unit
 *	Quantum unit.
 */
enum ppe_drv_qos_drr_unit {
	PPE_DRV_QOS_DRR_UNIT_BYTE,	/**< QoS Scheduler DRR unit is in bytes. */
	PPE_DRV_QOS_DRR_UNIT_PACKET,	/**< QoS scheduler DRR unit is in packets. */
};
typedef enum ppe_drv_qos_drr_unit ppe_drv_qos_drr_unit_t;

/**
 * ppe_drv_qos_queue_ceiling_unit
 *	Queue qdisc ceiling unit.
 */
enum ppe_drv_qos_queue_ceiling_unit {
	PPE_DRV_QOS_QUEUE_CEILING_UNIT_BYTES,	/**< Queue ceiling is in bytes. */
	PPE_DRV_QOS_QUEUE_CEILING_UNIT_PACKETS,	/**< Queue ceiling is in packets. */
	PPE_DRV_QOS_QUEUE_CEILING_UNIT_MAX	/**< Maximum ceiling unit. */
};
typedef enum ppe_drv_qos_queue_ceiling_unit ppe_drv_qos_queue_ceiling_unit_t;

/**
 * ppe_drv_qos_q_stat
 *	Information for QoS queue statistics.
 */
struct ppe_drv_qos_q_stat {
	uint32_t tx_pkts;	/**< Number of Tx packets from QoS queue. */
	uint32_t drop_pkts;	/**< Number of packets dropped from QoS queue. */
	uint64_t tx_bytes;	/**< Number of Tx bytes from QoS queue. */
	uint64_t drop_bytes;	/**< Number of bytes dropped from QoS queue. */
};

/**
 * ppe_drv_qos_tcont_stat
 *	Information for QoS Tcont statistics.
 */
struct ppe_drv_qos_tcont_stat {
	uint64_t pend_bytes;	/**< Number of bytes pending per Tcont. */
	uint32_t credit;	/**< Number of credits available for the Tcont. */
};

/**
 * ppe_drv_qos_queue
 *	Information for QoS queue.
 */
struct ppe_drv_qos_queue {
	uint32_t ucast_qid;	/**< Unicast Queue ID. */
	uint32_t mcast_qid;	/**< Multicast Queue ID. */
	uint32_t qlimit;	/**< Queue limit. */
	uint32_t min_th[PPE_DRV_QOS_QUEUE_COLOR_MAX];
				/**< Minimum threshold. */
	uint32_t max_th[PPE_DRV_QOS_QUEUE_COLOR_MAX];
				/**< Maximum threshold. */
	uint32_t resume_off[PPE_DRV_QOS_QUEUE_COLOR_MAX];
				/**< Resume offset for each color. */
	bool color_en;		/**< Enable color mode. */
	bool red_en;		/**< Enable RED algorithm. */
	bool backpressure_en;	/**< Enable backpressure. */
	bool mcast_valid;	/**<Multicast queue is valid. */
	ppe_drv_qos_queue_ceiling_unit_t q_ceiling_unit;
				/**< Queue qdisc ceiling unit. */
};

/**
 * ppe_drv_qos_shaper
 *	Information for QoS shaper.
 */
struct ppe_drv_qos_shaper {
	uint32_t rate;		/**< Allowed bandwidth. */
	uint32_t burst;		/**< Allowed burst. */
	uint32_t crate;		/**< Ceil bandwidth. */
	uint32_t cburst;	/**< Ceil burst. */
	uint32_t overhead;	/**< Overhead in bytes to be added for each packet. */
};

/**
 * ppe_drv_qos_scheduler
 *	Information for QoS scheduler.
 */
struct ppe_drv_qos_scheduler {
	uint32_t l0c_drrid;	/**< Level 0 C_DRR ID. */
	uint32_t l0e_drrid;	/**< Level 0 E_DRR ID. */
	uint32_t l1c_drrid;	/**< Level 1 C_DRR ID. */
	uint32_t l1e_drrid;	/**< Level 1 E_DRR ID. */
	uint32_t drr_weight;	/**< DRR weight. */
	ppe_drv_qos_drr_unit_t drr_unit;
				/**< DRR quantum unit. */
	uint32_t priority;	/**< Priority value. */
	bool l1_valid;		/**< Level 1 scheduler resources valid? */
	bool l0_valid;		/**< Level 0 scheduler resources valid? */
};


/**
 * ppe_drv_qos_info
 * 	Information about PPE queue
 */
 struct ppe_drv_queue_info {
	 uint8_t int_pri;	/**< INT_PRI of PPE queue. */
	 uint8_t port_id;	/**< PPE port ID. */
	 uint32_t ucast_qid;	/**< Ucast ID of PPE queue. */
	 uint32_t handle_id;	/**< Handle/Class ID of PPE queue. */
	 bool valid;		/**< Valid flag. */
};


/**
 * ppe_drv_qos_res
 *	Information for QoS resources.
 */
struct ppe_drv_qos_res {
	struct ppe_drv_qos_queue q;		/**< PPE HW queue related parameters. */
	struct ppe_drv_qos_shaper shaper;	/**< PPE HW shaper parameters. */
	struct ppe_drv_qos_scheduler scheduler;	/**< PPE HW scheduler parameters. */
	uint32_t l0spid;			/**< Level 0 SP Id. */
	uint32_t l1spid;			/**< Level 1 SP Id. */
	uint32_t port_mtu;			/**< MTU configured on the associated port netdev. */
};

/*
 * ppe_drv_qos_port_drr_map
 *	Level 1 and Level 0 DRR map for the port.
 */
struct ppe_drv_qos_port_drr_map {
	ppe_drv_qos_level_t	level;	/**< QoS scheduler level. */
	uint32_t sp_id;		/**< Level 1 SP ID or Level 0 SP ID. */
	uint32_t priority;	/**< Priority value. */
	uint32_t drr_id;		/**< Mapped DRR ID. */
	uint32_t ref_cnt;	/**< Reference count */
	struct ppe_drv_qos_port_drr_map *next; /**< Pointer to next map entry. */
};

/**
 * ppe_drv_qos_port
 *	Information for QoS port resources.
 */
struct ppe_drv_qos_port {
	uint32_t base[PPE_DRV_QOS_RES_TYPE_MAX];	/**< Base Id for QoS resource. */
	uint32_t max[PPE_DRV_QOS_RES_TYPE_MAX];		/**< Maximum QoS resouurce. */
	uint32_t max_pri;		/**< Maximum priority supported. */
};

/**
 * ppe_drv_qos_shaper_cfg
 *	Complete shaper configuration for shadow state.
 */
struct ppe_drv_qos_shaper_cfg {
	bool c_shaper_en;	/**< Committed shaper enable. */
	bool e_shaper_en;	/**< Excess shaper enable. */
	uint32_t cir;		/**< Committed information rate (kbits/sec). */
	uint32_t cbs;		/**< Committed burst size (bytes). */
	uint32_t eir;		/**< Excess information rate (kbits/sec). */
	uint32_t ebs;		/**< Excess burst size (bytes). */
	bool couple_en;		/**< Couple enable. */
	uint8_t meter_unit;	/**< Meter unit. */
	uint8_t frame_mode;	/**< Frame mode. */
};

/**
 * ppe_drv_qos_flow_shaper_state
 *	Live shadow of the flow shaper for one flow entry.
 */
struct ppe_drv_qos_flow_shaper_state {
	struct ppe_drv_qos_shaper_cfg cfg;	/**< Shaper configuration. */
	bool valid;				/**< True when a shaper is configured. */
};

/**
 * ppe_drv_qos_queue_shaper_state
 *	Live shadow of the queue shaper for one queue entry.
 */
struct ppe_drv_qos_queue_shaper_state {
	struct ppe_drv_qos_shaper_cfg cfg;	/**< Shaper configuration. */
	bool valid;				/**< True when a shaper is configured. */
};

/**
 * ppe_drv_qos_port_state
 *	Live software shadow of all shaper configurations for a PPE port.
 */
struct ppe_drv_qos_port_state {
	struct list_head list;				/**< Linkage in port state list. */
	uint32_t refcnt;				/**< Number of active owners. */
	uint32_t port_id;				/**< PPE port ID. */
	struct ppe_drv_qos_port port;			/**< Resource base/max info. */
	struct ppe_drv_qos_shaper_cfg port_shaper;	/**< Port-level shaper shadow. */
	bool port_shaper_valid;			/**< Port shaper shadow valid flag. */
	struct ppe_drv_qos_flow_shaper_state *flows;	/**< Flow shaper shadow array. */
	struct ppe_drv_qos_queue_shaper_state *ucast_queues;
						/**< Unicast queue shaper shadow array. */
	struct ppe_drv_qos_queue_shaper_state *mcast_queues;
						/**< Multicast queue shaper shadow array. */
};

/**
 * Callback function for getting INT-PRI value.
 *
 * @datatypes
 * net_device
 *
 * @param[in] dev         Pointer to the associated net device.
 * @param[in] tag         Qos tag.
 */
typedef uint8_t (*ppe_drv_qos_int_pri_callback_t)(struct net_device *dev, uint32_t tag);

/**
 * Callback function for getting PPE queue information
 *
 * @datatypes
 * net_device
 *
 * @param[in] dev               Pointer to the associated net device.
 * @param[in] handle_id         Qdisc handle ID or leaf class ID of queue for which information needs to be fetched.
 * @param[out] pq_info          PPE queue information.
 */
typedef bool (*ppe_drv_qos_queue_info_callback_t)(struct net_device *dev, uint32_t handle_id, struct ppe_drv_queue_info *pq_info);

/**
 * Callback function type for DDRQ queue operation.
 *
 * @param[in] ddrq_id	DDRQ queue identifier.
 *
 * @return
 * 0 on success, else non-zero.
 */
typedef int (*ppe_drv_qos_ddrq_callback_t)(uint32_t ddrq_id);

/**
 * ppe_drv_qos_ddrq_limit_set_cfg
 *	Context passed to the DDRQ queue limit-set callback.
 */
struct ppe_drv_qos_ddrq_limit_set_cfg {
	struct ppe_drv_qos_queue *qos_queue;		/**< Pointer to PPE queue configuration information. */
	ppe_drv_qos_queue_ceiling_unit_t qdisc_ceiling_unit;
						/**< Queue qdisc ceiling unit. */
	uint32_t port_mtu;				/**< Port netdev MTU used when queue limit is in packets. */
};

/**
 * Callback function type for DDRQ queue limit-set operation.
 *
 * @param[in] cfg	Pointer to DDRQ queue limit-set callback context.
 *
 * @return
 * 0 on success, else non-zero.
 */
typedef int (*ppe_drv_qos_ddrq_limit_set_callback_t)(struct ppe_drv_qos_ddrq_limit_set_cfg *cfg);

typedef int (*ppe_drv_qos_ddrq_enq_deq_ctrl_callback_t)(uint32_t ddrq_id, bool enable);
typedef int (*ppe_drv_qos_ddrq_stats_get_callback_t)(uint32_t ddrq_id, struct ppe_drv_qos_q_stat *stats);
typedef int (*ppe_drv_qos_ddrq_accu_stats_callback_t)(void);
typedef int (*ppe_drv_qos_ddrq_pon_drainout_callback_t)(void);

/**
 * ppe_drv_qos_ddrq_ops
 *	DDRQ callbacks registered by NSS-DP.
 */
struct ppe_drv_qos_ddrq_ops {
	ppe_drv_qos_ddrq_callback_t ddrq_disable;		/**< Disable callback. */
	ppe_drv_qos_ddrq_callback_t ddrq_enable;		/**< Enable callback. */
	ppe_drv_qos_ddrq_limit_set_callback_t ddrq_limit_set;	/**< Limit set callback. */
	ppe_drv_qos_ddrq_enq_deq_ctrl_callback_t ddrq_enqueue_ctrl;		/**< Enqueue control callback. */
	ppe_drv_qos_ddrq_enq_deq_ctrl_callback_t ddrq_dequeue_drop_ctrl;	/**< Dequeue drop control callback. */
	ppe_drv_qos_ddrq_stats_get_callback_t ddrq_stats_get;	/**< DDRQ statistics callback. */
	ppe_drv_qos_ddrq_callback_t ddrq_stats_reset;	/**< DDRQ statistics reset callback. */
	ppe_drv_qos_ddrq_callback_t ddrq_cfg_reset;	/**< DDRQ default configuration reset callback. */
	ppe_drv_qos_ddrq_accu_stats_callback_t ddrq_accu_stats_start;	/**< DDRQ accumulate statistics start callback. */
	ppe_drv_qos_ddrq_accu_stats_callback_t ddrq_accu_stats_stop;	/**< DDRQ accumulate statistics stop callback. */
	ppe_drv_qos_ddrq_pon_drainout_callback_t ddrq_pon_drainout;	/**< PON T-CONT drainout callback. */
};

/**
 * ppe_drv_qos_ddrq_callback_unregister
 *	API to unregister DDRQ callbacks.
 *
 * @return
 * None
 */
void ppe_drv_qos_ddrq_callback_unregister(void);

/**
 * ppe_drv_qos_ddrq_callback_register
 *	API to register DDRQ callbacks.
 *
 * @param[in] ops	Pointer to DDRQ callback operations.
 *
 * @return
 * None
 */
void ppe_drv_qos_ddrq_callback_register(const struct ppe_drv_qos_ddrq_ops *ops);

/**
 * ppe_drv_qos_ddrq_accu_stats_start
 *	API to trigger registered DDRQ accumulate statistics start callback.
 *
 * @return
 * None
 */
void ppe_drv_qos_ddrq_accu_stats_start(void);

/**
 * ppe_drv_qos_ddrq_accu_stats_stop
 *	API to trigger registered DDRQ accumulate statistics stop callback.
 *
 * @return
 * None
 */
void ppe_drv_qos_ddrq_accu_stats_stop(void);

/**
 * ppe_drv_qos_int_pri_callback_unregister
 *	API to unregister INT-PRI fetch callback.
 *
 * @return
 * None
 */
void ppe_drv_qos_int_pri_callback_unregister(void);

/**
 * ppe_drv_qos_queue_info_callback_unregister
 *	API to unregister PPE queue info fetch callback.
 *
 * @return
 * None
 */
void ppe_drv_qos_queue_info_callback_unregister(void);

/**
 * ppe_drv_qos_int_pri_callback_register
 *	API to register INT-PRI fetch callback.
 *
 * @param[in] cb     Pointer to the callback function.
 *
 * @return
 * None
 */
void ppe_drv_qos_int_pri_callback_register(ppe_drv_qos_int_pri_callback_t cb);

/**
 * ppe_drv_qos_queue_info_callback_register
 *	API to register PPE queue info fetch callback.
 *
 * @param[in] cb     Pointer to the callback function.
 *
 * @return
 * None
 */
void ppe_drv_qos_queue_info_callback_register(ppe_drv_qos_queue_info_callback_t cb);

/**
 * ppe_drv_qos_int_pri_get
 *	Returns the INT-PRI value for a class ID.
 *
 * @datatypes
 * struct net_device
 *
 * @param[in] dev       Pointer to the network device.
 * @param[in] handle_id Qdisc Handle ID/Class ID of the Qdisc/class.
 *
 * @return
 * INT-PRI value
 */
int ppe_drv_qos_int_pri_get(struct net_device *dev, uint32_t handle_id);

/**
 * ppe_drv_qos_port_id_get
 *	Returns the PORT-ID value for a class ID.
 *
 * @datatypes
 * struct net_device
 *
 * @param[in] dev       Pointer to the associated net dev.
 * @param[in] handle_id Qdisc Handle ID/Class ID corresponding to the PPE queue.
 * @param[out] pq_info  PPE queue information.
 *
 * @return
 * PPE PORT-ID value
 */

bool ppe_drv_qos_queue_info_get(struct net_device *dev, uint32_t handle_id, struct ppe_drv_queue_info* pq_info);

/**
 * ppe_drv_qos_ac_ctrl_cfg
 *	API to configure AC control configuration on the given queue.
 *
 * @param[in] qid       Queue ID.
 * @param[in] ac_en     AC enable.
 * @param[in] ac_fc_en  AC flow control enable.
 *
 * @return
 * true if configuration is successful; otherwise false.
 */
bool ppe_drv_qos_ac_ctrl_cfg(uint32_t qid, bool ac_en, bool ac_fc_en);

/**
 * ppe_drv_qos_queue_stats_get
 *	API to fetch queue statistics from PPE HW.
 *
 * @param[in]  qid         Queue ID.
 * @param[in]  is_red      Is queue RED or FIFO?
 * @param[out] stats       Statistics
 *
 * @return
 * None.
 */
void ppe_drv_qos_queue_stats_get(uint32_t qid, bool is_red, struct ppe_drv_qos_q_stat *stats);

/*
 * ppe_drv_qos_queue_stats_reset
 *	API to reset queue statistics in PPE HW.
 *
 * @param[in] qid      Queue ID.
 *
 * @return
 * None.
 */
void ppe_drv_qos_queue_stats_reset(uint32_t qid);

/**
 * ppe_drv_qos_queue_enqueue_ctrl
 *	API to control enqueue operation for a queue in PPE HW.
 *
 * @param[in] qid      Queue ID.
 * @param[in] enable   Enable or disable enqueue.
 *
 * @return
 * Status of the QoS queue enqueue control operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_enqueue_ctrl(uint32_t qid, bool enable);

/**
 * ppe_drv_qos_queue_dequeue_ctrl
 *	API to control dequeue operation for a queue in PPE HW.
 *
 * @param[in] qid      Queue ID.
 * @param[in] enable   Enable or disable dequeue.
 *
 * @return
 * Status of the QoS queue dequeue control operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_dequeue_ctrl(uint32_t qid, bool enable);

/**
 * ppe_drv_qos_queue_dequeue_drop_ctrl
 *	API to control dequeue drop operation for a queue in PPE HW.
 *
 * @param[in] qid      Queue ID.
 * @param[in] enable   Enable or disable dequeue drop.
 *
 * @return
 * Status of the QoS queue dequeue drop control operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_dequeue_drop_ctrl(uint32_t qid, bool enable);

/**
 * ppe_drv_qos_queue_disable
 *	API to disable a queue in PPE HW.
 *
 * @param[in] port_id  Port ID of the port.
 * @param[in] qid      Queue ID.
 *
 * @return
 * Status of the QoS queue disable operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_disable(uint32_t port_id, uint32_t qid);

/**
 * ppe_drv_qos_pon_drainout
 *	Drain all PON T-CONTs through the registered DDRQ callback.
 *
 * @return
 * Status of the PON drainout operation.
 */
ppe_drv_ret_t ppe_drv_qos_pon_drainout(void);

/**
 * ppe_drv_qos_port_is_pon
 *	Checks whether a port is the configured PON port.
 *
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * True when the port is the configured PON port.
 */
bool ppe_drv_qos_port_is_pon(uint32_t port_id);

/**
 * ppe_drv_qos_queue_enable
 *	API to enable a queue in PPE HW.
 *
 * @param[in] qid      Queue ID.
 *
 * @return
 * Status of the QoS queue enable operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_enable(uint32_t qid);

/**
 * ppe_drv_qos_tcont_stats_get
 *	API to fetch tcont statistics from PPE HW.
 *
 * @param[in]  tcont_id         Queue ID.
 * @param[out] stats       Statistics
 *
 * @return
 * Status of the QoS T-cont statistics get operation.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_stats_get(uint32_t tcont_id, struct ppe_drv_qos_tcont_stat *stats);

/**
 * ppe_drv_qos_tcont_credit_reset
 *	Resets T-cont credit to 0.
 *
 * @param[in] tcont_id  T-cont ID of the port.
 *
 * @return
 * Status of the QoS T-cont configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_credit_reset(uint32_t tcont_id);

/**
 * ppe_drv_qos_tcont_set
 *	Sets T-cont configuration for a queue in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res       Pointer to the QoS resource.
 * @param[in] tcont_id  T-cont ID of the port.
 * @param[in] valid     Is T-cont valid?
 *
 * @return
 * Status of the QoS T-cont configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_tcont_set(struct ppe_drv_qos_res *res, uint32_t tcont_id, bool valid);

/**
 * ppe_drv_qos_l1_scheduler_set
 *	Sets a level 1 queue scheduler in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res      Pointer to the QoS resource.
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the QoS L1 scheduler configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_l1_scheduler_set(struct ppe_drv_qos_res *res, uint32_t port_id);

/**
 * ppe_drv_qos_l0_scheduler_set
 *	Sets a level 0 queue scheduler in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res      Pointer to the QoS resource.
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the QoS L0 scheduler configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_l0_scheduler_set(struct ppe_drv_qos_res *res, uint32_t port_id);

/**
 * ppe_drv_qos_port_shaper_reset
 *	Resets a port shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res      Pointer to the QoS resource.
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the QoS port shaper reset operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_shaper_reset(struct ppe_drv_qos_res *res, uint32_t port_id);

/**
 * ppe_drv_qos_port_shaper_set
 *	Configures a port shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res      Pointer to the QoS resource.
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the QoS port shaper configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_shaper_set(struct ppe_drv_qos_res *res, uint32_t port_id);

/**
 * ppe_drv_qos_flow_shaper_reset
 *	Resets a flow shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS flow shaper reset operation.
 */
ppe_drv_ret_t ppe_drv_qos_flow_shaper_reset(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_flow_shaper_set
 *	Configures a flow shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS flow shaper configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_flow_shaper_set(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_mcast_queue_shaper_reset
 *	Resets a multicast queue shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS queue shaper reset operation.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_shaper_reset(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_mcast_queue_shaper_set
 *	Configures a multicast queue shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS queue shaper configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_shaper_set(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_queue_shaper_reset
 *	Resets a queue shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS queue shaper reset operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_shaper_reset(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_queue_shaper_set
 *	Configures a queue shaper in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS queue shaper configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_shaper_set(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_mcast_queue_set
 *	Configures a multicast queue in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res      Pointer to the QoS resource.
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the QoS port shaper reset operation.
 */
ppe_drv_ret_t ppe_drv_qos_mcast_queue_set(struct ppe_drv_qos_res *res, uint32_t port_id);

/**
 * ppe_drv_qos_queue_cfg_get
 *	Gets queue dynamic threshold configuration in PPE.
 *
 * @datatypes
 * ppe_drv_qos_queue
 *
 * @param[in]  qid    Queue ID.
 * @param[out] q_cfg  Pointer to the queue configuration.
 *
 * @return
 * Status of the queue configuration get operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_cfg_get(uint32_t qid, struct ppe_drv_qos_queue *q_cfg);

/**
 * ppe_drv_qos_queue_cfg_set
 *	Sets queue dynamic threshold configuration in PPE.
 *
 * @datatypes
 * ppe_drv_qos_queue
 *
 * @param[in] qid    Queue ID.
 * @param[in] q_cfg  Pointer to the queue configuration.
 *
 * @return
 * Status of the queue configuration set operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_cfg_set(uint32_t qid, struct ppe_drv_qos_queue *q_cfg);

/**
 * ppe_drv_qos_queue_limit_set
 *	Sets queue size in PPE.
 *
 * @datatypes
 * ppe_drv_qos_res
 *
 * @param[in] res  Pointer to the QoS resource.
 *
 * @return
 * Status of the QoS queue limit set operation.
 */
ppe_drv_ret_t ppe_drv_qos_queue_limit_set(struct ppe_drv_qos_res *res);

/**
 * ppe_drv_qos_port_state_alloc
 *	Allocate shaper shadow state arrays for a port.
 *
 * @param[in] port_id  Port ID of the port.
 * @param[in] port     Pointer to QoS resource information.
 *
 * @return
 * Status of the allocation operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_state_alloc(uint32_t port_id, struct ppe_drv_qos_port *port);

/**
 * ppe_drv_qos_port_state_free
 *	Free shaper shadow state arrays for a port.
 *
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * None.
 */
void ppe_drv_qos_port_state_free(uint32_t port_id);

/**
 * ppe_drv_qos_port_shapers_unlimited
 *	Set all shapers for a port to unlimited rate.
 *
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_shapers_unlimited(uint32_t port_id);

/**
 * ppe_drv_qos_port_shapers_restore
 *	Restore all shapers for a port from shadow state.
 *
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_shapers_restore(uint32_t port_id);

/**
 * ppe_drv_qos_default_conf_set
 *	Sets default queue scheduler in PPE.
 *
 * @param[in] port_id  Port ID of the port.
 *
 * @return
 * Status of the default configuration set operation.
 */
ppe_drv_ret_t ppe_drv_qos_default_conf_set(uint32_t port_id);

/**
 *
 * ppe_drv_qos_port_bm_control_enable
 * 	Enable or disable port buffer management for flow control
 *
 * @param[in] port_id  Port ID of the port.
 * @param[in] set      Enable/Disable flow control.
 *
 * @return
 * none
 */
void ppe_drv_qos_port_bm_control_enable(uint32_t port_id, bool set);

/**
 * ppe_drv_qos_port_res_get
 *	Gets boot time QoS resource allocation information for a given port.
 *
 * @datatypes
 * ppe_drv_qos_port
 *
 * @param[in] port_id  Port ID of the port.
 * @param[in] port     Pointer to the port.
 *
 * @return
 * Status of the resource get operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_res_get(uint32_t port_id, struct ppe_drv_qos_port *port);

#ifdef NSS_PPE_PON_SUPPORT
/**
 * ppe_drv_qos_pon_port_get
 *	Gets the PON port ID.
 *
 * @datatypes
 * None
 *
 * @return
 * PON port ID.
 */
uint32_t ppe_drv_qos_pon_port_get(void);

/**
 * Callback function for allocating QoS resources for the PON port.
 * Invoked from ppe_drv_dp_gem_enable() after the PON port is configured in HW.
 *
 * @param[in] pon_port_id  PON port ID.
 */
typedef void (*ppe_drv_qos_pon_port_alloc_callback_t)(uint32_t pon_port_id);

/**
 * ppe_drv_qos_pon_port_alloc_callback_register
 *	Register callback for PON port QoS resource allocation.
 *
 * @param[in] cb  Pointer to the callback function.
 *
 * @return
 * None.
 */
void ppe_drv_qos_pon_port_alloc_callback_register(ppe_drv_qos_pon_port_alloc_callback_t cb);

/**
 * ppe_drv_qos_pon_port_alloc_callback_unregister
 *	Unregister callback for PON port QoS resource allocation.
 *
 * @return
 * None.
 */
void ppe_drv_qos_pon_port_alloc_callback_unregister(void);
#endif

/**
 * ppe_drv_qos_port_htt_res_get
 *	Gets boot-time QoS resource allocation information for a given switch slave port.
 *
 * @datatypes
 * ppe_drv_qos_port
 *
 * @param[in] port_id  Port ID of the port.
 * @param[in] index    Index ID of the slave port.
 * @param[in] port     Pointer to the port.
 *
 * @return
 * Status of the resource get operation.
 */
ppe_drv_ret_t ppe_drv_qos_port_htt_res_get(uint32_t port_id, uint8_t index, struct ppe_drv_qos_port *port);

/** @} */ /* end_addtogroup ppe_drv_qos_subsystem */

/**
 * ppe_drv_qos_ddrq_bypass_set
 *	API to configure DDRQ bypass configuration on the given queue id.
 *
 * @datatypes
 * ppe_drv_ret_t
 *
 * @return
 * Status of the DDRQ bypass configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_ddrq_bypass_set(uint32_t qid);

/**
 * ppe_drv_qos_ddrq_bypass_reset
 *	API to reset DDRQ bypass configuration on the given queue id.
 *
 * @datatypes
 * ppe_drv_ret_t
 *
 * @return
 * Status of the DDRQ bypass reset configuration operation.
 */
ppe_drv_ret_t ppe_drv_qos_ddrq_bypass_reset(uint32_t qid);
#endif /* _PPE_DRV_V4_H_ */
