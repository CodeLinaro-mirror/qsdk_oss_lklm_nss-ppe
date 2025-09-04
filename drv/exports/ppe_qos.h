/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * @file ppe_qos.h
 *	NSS PPE RFS definitions.
 */

#ifndef _PPE_QOS_H_
#define _PPE_QOS_H_

#include <linux/if.h>

#define PPE_QOS_MAX_NAME_LENGTH 64

/*
 * ppe_qos_ret
 *	PPE QoS return status types.
 */
typedef enum ppe_qos_ret {
	PPE_QOS_SUCCESS = 0,	/**< Success. */
	PPE_QOS_CLASS_NON_LEAF,	/**< Class is not a leaf node. */
	PPE_QOS_INVALID_HANDLE_ID,	/**< Class ID is not present in Qos heirarchy. */
	PPE_QOS_INVALID_DEV,	/**< Interface not valid. */
	PPE_QOS_FAIL,		/**< Failure. */
	PPE_QOS_CREATE_SHAPER_FAIL,	/**< Shaper creation failure.. */
	PPE_QOS_DELETE_SHAPER_FAIL,	/**< shaper delete failure. */
	PPE_QOS_CREATE_INTERFACE_QUEUES_FAIL,	/**< Interface's queue creation faliure. */
	PPE_QOS_FLUSH_INTERFACE_QUEUES_FAIL,	/**< Interface's queue deletion faliure. */
	PPE_QOS_SET_INTERFACE_SHAPER_FAIL,	/**< Interface's shaper configuration faliure. */
	PPE_QOS_MAP_PQ_TO_TCONT_FAIL,	/**< Priority queue to Tcont mapping faliure. */
	PPE_QOS_TCONT_STATS_GET_FAIL,	/**< Tcont stats fetch failure. */
	PPE_QOS_RESET_TCONT_CREDIT_FAIL,	/**< Tcont credit reset failure. */
	PPE_QOS_SET_QUEUE_TM_FAIL,	/**< Queue's traffic management configuration faliure. */
	PPE_QOS_SET_QUEUE_LIMIT_FAIL,	/**< Queue's thresholds configuration faliure. */
} ppe_qos_ret_t;

/*
 * ppe_qos_interface_type
 *	PPE QoS interface types.
 * TODO: Extend the support for virtual ports
 */
typedef enum ppe_qos_interface_type {
	PPE_QOS_INTERFACE_TYPE_PHYSICAL = 0,	/**< Physical interface. */
	PPE_QOS_INTERFACE_TYPE_TCONT,	/**< Tcont interface. */
} ppe_qos_interface_type_t;

/*
 * ppe_qos_interface
 *	PPE QoS interface information.
 * TODO: Extend the support for virtual ports
 */
 struct ppe_qos_interface {
	union {
		char dev[IFNAMSIZ];			/** Device name */
		uint32_t tcont_id;			/** Tcont ID */
	} interface;					/** interface info. */
	ppe_qos_interface_type_t type;		/** interface type */
};

/*
 * ppe_qos_req
 *	PPE QoS request information.
 */
struct ppe_qos_req {
	uint8_t int_pri;	/**< INT PRI value of PPE Queue. */
	uint8_t port_id;	/**< PPE port ID. */
	uint32_t port_type;		/** Port type, 0: Tcont, 1: UNI .*/
	uint16_t ucast_qid;	/**< Unicast queue number of PPE. */
	uint32_t handle_id;	/**< Qdisc handle ID/Class ID corresponding to the PPE Queue. */
	char dev[IFNAMSIZ];	/**< Netdevice. */
};

/*
 * ppe_qos_shaper_info
 *	PPE QoS shaper profile information.
 */
struct ppe_qos_shaper_info {
	char name[PPE_QOS_MAX_NAME_LENGTH];	/** Shaper name. */
	uint32_t cir;	/** Committed Information Rate. */
	uint32_t eir;	/** Exceed Information Rate. */
	uint32_t cbs;	/** Committed burst size. */
	uint32_t ebs;	/** Exceed Burst size. */
};

/*
 * ppe_qos_queue_limit_info
 *	PPE QoS queue limit configuration.
 */
struct ppe_qos_queue_limit_info {
	struct ppe_qos_interface if_data;		/** interface data */
	uint32_t queue_id;	/** Queue number. */
	uint32_t ceiling;	/** Queue ceiling */
	bool color_en;		/** Color enable */
	bool wred_en;		/** WRED enable */
	uint32_t green_min_off;		/** Green minimum offset */
	uint32_t yellow_max_off;	/** Yellow maximum offset */
	uint32_t yellow_min_off;	/** Yellow minimum offset */
	uint32_t red_max_off;		/** Red maximum offset */
	uint32_t red_min_off;		/** Red minimum offset */
	uint32_t green_resume_off;	/** Green resume offset */
	uint32_t yellow_resume_off;	/** Yellow resume offset */
	uint32_t red_resume_off;	/** Red resume offset */
};

/*
 * ppe_qos_queue_tm_info
 *	PPE QoS queue traffic management information.
 */
struct ppe_qos_queue_tm_info {
	struct ppe_qos_interface if_data;		/** interface data */
	uint32_t queue_id;	/** Queue number. */
	uint32_t priority;	/** Priority of the queue. */
	uint32_t weight;	/** Weight assigned to the queue. */
};

#ifdef NSS_PPE_PON_SUPPORT
/*
 * ppe_qos_tcont_stats_info
 *	PPE QoS Tcont statistics information.
 */
struct ppe_qos_tcont_stats_info {
	uint32_t tcont_id;	/** T-cont ID. */
	uint32_t credit;	/** T-cont credit. */
	uint64_t bytes;	/** T-cont pending bytes. */
};

/*
 * ppe_qos_pq_to_tcont_info
 *	PPE QoS priority queue to tcont mapping information.
 */
struct ppe_qos_pq_to_tcont_info {
	uint32_t queue_id;	/** Queue ID. */
	uint32_t tcont_id;	/** T-cont ID. */
};
#endif

/*
 * ppe_qos_interface_queues_info
 *	PPE QoS T-cont/UNI queues information.
 */
struct ppe_qos_interface_queues_info {
	struct ppe_qos_interface if_data;		/** interface data */
	uint32_t num_queues;	/** Number of queues. */
};

/*
 * ppe_qos_interface_shaper_info
 *	PPE QoS T-cont/UNI shaper information.
 */
struct ppe_qos_interface_shaper_info {
	struct ppe_qos_interface if_data;		/** interface data */
	char shaper_name[PPE_QOS_MAX_NAME_LENGTH];	/** Shaper name. */
};

/**
 * ppe_qos_get_int_pri_func
 *	Function to fetch PPE QoS internal priority for a Qdisc/leaf class.
 *
 *
 * @param[in] req         PPE QoS request's information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_get_int_pri_func(struct ppe_qos_req *req);

/**
 * ppe_qos_delete_shaper
 *	Function to delete shaper profile.
 *
 *
 * @param[in] info         PPE QoS shaper profile information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_delete_shaper(struct ppe_qos_shaper_info *info);

/**
 * ppe_qos_create_shaper
 *	Function to create shaper profile.
 *
 *
 * @param[in] info         PPE QoS shaper profile information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_create_shaper(struct ppe_qos_shaper_info *info);

/**
 * ppe_qos_set_queue_limit
 *	Function to set queue limit and thresholds for a given queue.
 *
 *
 * @param[in] info         PPE QoS queue's traffic management information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_set_queue_limit(struct ppe_qos_queue_limit_info *info);

/**
 * ppe_qos_set_queue_tm
 *	Function to set QoS traffic management for a given interface's queue.
 *
 *
 * @param[in] info         PPE QoS queue's traffic management information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_set_queue_tm(struct ppe_qos_queue_tm_info *info);

#ifdef NSS_PPE_PON_SUPPORT
/**
 * ppe_qos_get_tcont_stats
 *	Function to get statistics for a given Tcont ID.
 *
 *
 * @param[in] info         PPE QoS T-cont statistics information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_get_tcont_stats(struct ppe_qos_tcont_stats_info *info);

/**
 * ppe_qos_reset_tcont_credit
 *	Function to reset credit of a given Tcont ID.
 *
 *
 * @param[in] info         PPE QoS T-cont information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_reset_tcont_credit(struct ppe_qos_tcont_stats_info *info);

/**
 * ppe_qos_map_pq_to_tcont
 *	Function to map priority queue to a given Tcont ID.
 *
 *
 * @param[in] info         PPE QoS priority queue mapping information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_map_pq_to_tcont(struct ppe_qos_pq_to_tcont_info *info);
#endif

/**
 * ppe_qos_set_interface_shaper
 *	Function to set shaper at interface.
 *
 *
 * @param[in] info         PPE QoS interface's shaper information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_set_interface_shaper(struct ppe_qos_interface_shaper_info *info);

/**
 * ppe_qos_flush_interface_queues
 *	Function to flush and delete number of queues for a given t-cont/UNI.
 *
 *
 * @param[in] info         PPE QoS interface information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_flush_interface_queues(struct ppe_qos_interface_queues_info *info);

/**
 * ppe_qos_create_interface_queues
 *	Function to create number of queues for a given t-cont/UNI.
 *
 *
 * @param[in] info         PPE QoS interface information.
 * @return
 * PPE QoS request's return status.
 */
ppe_qos_ret_t ppe_qos_create_interface_queues(struct ppe_qos_interface_queues_info *info);

#endif /* _PPE_QOS_H_ */
