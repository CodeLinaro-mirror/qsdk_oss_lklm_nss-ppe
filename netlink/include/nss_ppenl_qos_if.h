/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */
 #include <ppe_qos.h>

/*
 * @file nss_ppenl_qos_if.h
 *	NSS PPE Netlink QOS
 */
#ifndef __NSS_PPENL_QOS_IF_H
#define __NSS_PPENL_QOS_IF_H

/*
 * QOS Configure Family.
 */
#define NSS_PPENL_QOS_FAMILY "nss_ppenl_qos"
#define NSS_PPE_NL_SHAPER_MAX_NAME_LENGTH 64

/*
 * nss_ppenl_qos_interface
 *	PPE QoS interface information.
 * TODO: Extend the support for virtual ports
 */
struct nss_ppenl_qos_interface {
	union {
		char dev[IFNAMSIZ];	/** Device name */
		uint32_t tcont_id;			/** Tcont ID */
	} interface;					/** interface info. */
	uint32_t type;		/** interface type */
};

/*
 * @brief QOS config.
 */
struct nss_ppenl_qos_config {
	uint32_t handle_id;	/* User given qos ID. */
	char dev[IFNAMSIZ];	/* Dev name for port qos. */
	uint8_t int_pri;	/* INT_PRI value of ppe queue. */
	uint8_t port_id;	/* PPE port ID. */
	uint16_t ucast_qid;	/* Unicast queue id of ppe queue. */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief PPE QoS interfcae shaper info
 */
struct nss_ppenl_qos_interface_shaper_info {
	struct nss_ppenl_qos_interface if_data;		/** Interface info. */
	char shaper_name[NSS_PPE_NL_SHAPER_MAX_NAME_LENGTH];	/** Shaper name assigned. */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief PPE QoS queue traffic management info
 */
struct nss_ppenl_qos_interface_queues_info {
	struct nss_ppenl_qos_interface if_data;		/** Interface info. */
	uint32_t num_queues;	/** Number of queues. */
	uint32_t queue_type;	/** Queue type: 0=ucast, 1=mcast. */
	int ret;		/* Return value to userspace. */
};

#if defined(CONFIG_NSS_PPENL_PON_PORT)
/*
 * @brief PPE QoS priority queue mapping info
 */
struct nss_ppenl_qos_map_pq_info {
	uint32_t queue_id;	/** Queue ID. */
	uint32_t tcont_id;	/** T-cont port ID. */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief PPE QoS Tcont statistics information.
 */
struct nss_ppenl_qos_tcont_stats_info {
	uint32_t tcont_id;	/** T-cont ID. */
	uint32_t credit;	/** T-cont credit. */
	uint64_t bytes;	/** T-cont pending bytes. */
	int ret;		/* Return value to userspace. */
};
#endif

/*
 * @brief PPE QoS queue traffic management info
 */
struct nss_ppenl_qos_queue_tm_info {
	struct nss_ppenl_qos_interface if_data;	/** Interface info. */
	uint32_t queue_id;	/** Queue number. */
	uint32_t priority;	/** Priority of the queue. */
	uint32_t weight;	/** Weight assigned to the queue. */
	uint32_t queue_type;	/** Queue type: 0=ucast, 1=mcast. */
	int ret;		/* Return value to userspace. */
};

/*
 * ppe_qos_queue_limit_info
 *	PPE QoS queue limit configuration.
 */
struct nss_ppenl_qos_queue_limit_info {
	struct nss_ppenl_qos_interface if_data;	/** Interface info. */
	uint32_t queue_id;	/** Queue number. */
	uint32_t ceiling;	/** Queue ceiling. */
	bool color_en;		/** Color enable. */
	bool wred_en;		/** WRED enable. */
	uint32_t green_min_off;		/** Green minimum offset. */
	uint32_t yellow_max_off;	/** Yellow maximum offset. */
	uint32_t yellow_min_off;	/** Yellow minimum offset. */
	uint32_t red_max_off;		/** Red maximum offset. */
	uint32_t red_min_off;		/** Red minimum offset. */
	uint32_t green_resume_off;	/** Green resume offset. */
	uint32_t yellow_resume_off;	/** Yellow resume offset. */
	uint32_t red_resume_off;	/** Red resume offset. */
	uint32_t queue_type;	/** Queue type: 0=ucast, 1=mcast. */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief PPE QoS queue traffic management info
 */
struct nss_ppenl_qos_shaper_info {
	char name[NSS_PPE_NL_SHAPER_MAX_NAME_LENGTH];	/** Shaper name. */
	uint32_t cir;	/** Committed Information Rate. */
	uint32_t eir;	/** Exceed Information Rate. */
	uint32_t cbs;	/** Committed burst size. */
	uint32_t ebs;	/** Exceed Burst size. */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief PPE QoS interface queue control info
 */
struct nss_ppenl_qos_queue_ctrl_info {
	struct nss_ppenl_qos_interface if_data;	/** Interface info. */
	uint32_t mode;		/** 0=enqueue, 1=dequeue */
	uint32_t state;		/** 0=disable, 1=enable */
	int ret;		/* Return value to userspace. */
};

/*
 * @brief QOS req.
 */
struct nss_ppenl_qos_req {
	struct nss_ppenl_cmn cm;	/*< Common message header. */
	union {
		struct nss_ppenl_qos_config config;	/* PPE QoS config. */
		struct nss_ppenl_qos_shaper_info shaper_info; /* PPE QoS shaper information */
		struct nss_ppenl_qos_interface_queues_info if_info;	/* PPE QoS interface's queues information */
		struct nss_ppenl_qos_interface_shaper_info if_shaper_info;	/* PPE QoS interface's shaper information */
#if defined(CONFIG_NSS_PPENL_PON_PORT)
		struct nss_ppenl_qos_tcont_stats_info stats_info;	/* PPE QoS Tcont stats information */
		struct nss_ppenl_qos_map_pq_info pq_info;	/* PPE QoS priority queue mapping information */
#endif
		struct nss_ppenl_qos_queue_tm_info tm_info;	/* PPE QoS traffic management information */
		struct nss_ppenl_qos_queue_limit_info limit_info;	/* PPE QoS limit and threshold information */
		struct nss_ppenl_qos_queue_ctrl_info queue_ctrl_info;	/* PPE QoS queue control information */
	} msg;
};

/*
 * @brief Message types.
 */
enum nss_ppe_qos_message_types {
	NSS_PPE_QOS_GET_INT_PRI,	/* QoS request create message. */
	NSS_PPE_QOS_CREATE_SHAPER,	/* QoS create shaper profile. */
	NSS_PPE_QOS_DELETE_SHAPER,	/* QoS delete shaper profile. */
	NSS_PPE_QOS_CREATE_INTERFACE_QUEUES,	/* QoS create interface queues. */
	NSS_PPE_QOS_FLUSH_INTERFACE_QUEUES,	/* QoS delete interface queues. */
	NSS_PPE_QOS_SET_INTERFACE_SHAPER,	/* QoS configure interface shaper. */
	NSS_PPE_QOS_GET_TCONT_STATS,	/* QoS get Tcont stats */
	NSS_PPE_QOS_RESET_TCONT_CREDIT,	/* QoS reset Tcont statistics */
	NSS_PPE_QOS_MAP_PQ_TO_TCONT,	/* QoS priority queue to Tcont mapping message. */
	NSS_PPE_QOS_SET_QUEUE_TM,	/* QoS set queue's traffic management message. */
	NSS_PPE_QOS_SET_QUEUE_LIMIT,	/* QoS set queue's limit and threshold message. */
	NSS_PPE_QOS_SET_INTERFACE_QUEUE_CTRL,	/* QoS set interface queue control message. */
	NSS_PPE_QOS_MAX_MSG_TYPES		/* Maximum message type. */
};

/**
 * @brief NETLINK QOS message init.
 *
 * @param[IN] req  NSS NETLINK QOS req.
 * @param[IN] type QOS message type.
 * @return
 * None
 */
static inline void nss_ppenl_qos_req_init(struct nss_ppenl_qos_req *req, enum nss_ppe_qos_message_types type)
{
	nss_ppenl_cmn_set_ver(&req->cm, NSS_PPENL_VER);
	nss_ppenl_cmn_init_cmd(&req->cm, sizeof(struct nss_ppenl_qos_req), type);
}

#endif /* __NSS_PPENL_QOS_IF_H */
