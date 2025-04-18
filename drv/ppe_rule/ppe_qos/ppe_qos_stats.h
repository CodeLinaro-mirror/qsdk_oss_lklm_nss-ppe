/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef __PPE_QOS_STATS_H
#define __PPE_QOS_STATS_H

#define PPE_QOS_STATS_NODE_NAME		"PPE_QOS"

/*
 * ppe_qos_stats
 *	Message structure for ppe general stats
 */
struct ppe_qos_stats {
	atomic64_t qos_create_interface_queues_fail;		/* Qos create interface queues fail */
	atomic64_t qos_create_interface_queues_success;		/* Qos create interface queues success */
	atomic64_t qos_flush_interface_queues_fail;	/* Qos flush interface queues fail */
	atomic64_t qos_flush_interface_queues_success;		/* Qos flush interface queues success */
	atomic64_t qos_set_interface_shaper_fail;		/* Qos det interface shaper fail */
	atomic64_t qos_set_interface_shaper_success;		/* Qos set interface shaper success */
	atomic64_t qos_pq_to_tcont_mapping_fail;	/* Qos priority queue maooing to Tcont fail */
	atomic64_t qos_pq_to_tcont_mapping_success;	/* Qos priority queue maooing to Tcont fail */
	atomic64_t qos_set_queue_tm_fail;	/* Qos set queue traffic management fail */
	atomic64_t qos_set_queue_tm_success;		/* Qos set queue traffic management success */
	atomic64_t qos_set_queue_limit_fail;			/* QoS set queue limit fail */
	atomic64_t qos_set_queue_limit_success;			/* QoS set queue limit success */
	atomic64_t qos_create_shaper_fail;		/* Qos create shaper fail */
	atomic64_t qos_create_shaper_success;		/* Qos create shaper fail */
	atomic64_t qos_delete_shaper_fail;		/* Qos delete shaper fail */
	atomic64_t qos_delete_shaper_success;		/* Qos delete shaper success */
};

/*
 * ppe_qos_stats_dec()
 *	Decrement stats counter.
 */
static inline void ppe_qos_stats_dec(atomic64_t *stat)
{
	atomic64_dec(stat);
}

/*
 * ppe_qos_stats_inc()
 *	Increment stats counter.
 */
static inline void ppe_qos_stats_inc(atomic64_t *stat)
{
	atomic64_inc(stat);
}

int ppe_qos_stats_debugfs_init(struct dentry *dentry);
void ppe_qos_stats_debugfs_exit(void);

#endif
