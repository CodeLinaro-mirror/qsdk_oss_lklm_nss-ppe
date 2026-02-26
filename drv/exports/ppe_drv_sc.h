/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * @addtogroup ppe_drv_subsystem
 * @{
 */

#ifndef _PPE_DRV_SC_H_
#define _PPE_DRV_SC_H_

struct ppe_drv;
struct ppe_drv_nsm_stats;

/*
 * FLOW ACL rule service code range.
 */
#define PPE_DRV_SC_FLOW_ACL_MAX 128
#define PPE_DRV_SC_FLOW_ACL_START 128
#define PPE_DRV_SC_FLOW_ACL_END (PPE_DRV_SC_FLOW_ACL_START + PPE_DRV_SC_FLOW_ACL_MAX - 1)

/*
 * ppe_drv_sc_type
 *	Service code types
 */
typedef enum ppe_drv_sc_type {
	PPE_DRV_SC_NONE = 0,			/* Normal PPE processing */
	PPE_DRV_SC_BYPASS_ALL = 1,		/* Bypasses all stages in PPE */
	PPE_DRV_SC_ADV_QOS_BRIDGED = 2,		/* Adv QoS redirection for bridged flow */
	PPE_DRV_SC_LOOPBACK_QOS = 3,		/* Bridge or IGS QoS redirection */
	PPE_DRV_SC_BNC_0 = 4,			/* QoS bounce */
	PPE_DRV_SC_BNC_CMPL_0 = 5,		/* QoS bounce complete */
	PPE_DRV_SC_ADV_QOS_ROUTED = 6,		/* Adv QoS redirection for routed flow */
	PPE_DRV_SC_IPSEC_PPE2EIP_DECAP = 7,		/* Inline IPsec redirection from PPE TO EIP */
	PPE_DRV_SC_IPSEC_EIP2PPE = 8,		/* Inline IPsec redirection from EIP to PPE */
	PPE_DRV_SC_PTP = 9,			/* Service Code for PTP packets */
	PPE_DRV_SC_VLAN_FILTER_BYPASS = 10,	/* VLAN filter bypass for bridge flows between 2 different VSIs */
	PPE_DRV_SC_L3_EXCEPT = 11,		/* Indicate exception post tunnel/tap operation */
	PPE_DRV_SC_SPF_BYPASS = 12,		/* Source port filtering bypass */
	PPE_DRV_SC_NOEDIT_REDIR_CORE0 = 13,	/* Service code to re-direct packets to core 0 without editing the packet */
	PPE_DRV_SC_NOEDIT_REDIR_CORE1 = 14,	/* Service code to re-direct packets to core 1 without editing the packet */
	PPE_DRV_SC_NOEDIT_REDIR_CORE2 = 15,	/* Service code to re-direct packets to core 2 without editing the packet */
	PPE_DRV_SC_NOEDIT_REDIR_CORE3 = 16,	/* Service code to re-direct packets to core 3 without editing the packet */
	PPE_DRV_SC_EDIT_REDIR_CORE0 = 17,	/* Service code to re-direct packets to core 0 with editing required for regular forwarding */
	PPE_DRV_SC_EDIT_REDIR_CORE1 = 18,	/* Service code to re-direct packets to core 1 with editing required for regular forwarding */
	PPE_DRV_SC_EDIT_REDIR_CORE2 = 19,	/* Service code to re-direct packets to core 2 with editing required for regular forwarding */
	PPE_DRV_SC_EDIT_REDIR_CORE3 = 20,	/* Service code to re-direct packets to core 3 with editing required for regular forwarding */
	PPE_DRV_SC_NOEDIT_RFS_RULE = 21,	/* Service code to re-direct packets without editing the packet */
	PPE_DRV_SC_EDIT_RFS_RULE = 22,		/* Service code to re-direct packets with editing required for regular forwarding */
	PPE_DRV_SC_VP_RPS = 23,			/* Service code to allow RPS for special VP flows when user type is DS and core_mask is 0 */
	PPE_DRV_SC_NOEDIT_ACL_POLICER = 24,	/* Service code to allow Policing but no packet editing */
	PPE_DRV_SC_L2_TUNNEL_EXCEPTION = 25,	/* Service code to allow decapsulated VXLAN/GRE tunnel exception. */
	PPE_DRV_SC_NOEDIT_PRIORITY_SET = 26,	/* Service code to prioritize packets without editing and redirection */
	PPE_DRV_SC_NOEDIT_RULE = 27,		/* Service code to redirect packets without editing */
	PPE_DRV_SC_FMAC_BYPASS = 28,		/* Service code to bypasses fake mac check in PPE */
	PPE_DRV_SC_VP_MPSK = 29,			/* Service code to bypass the egress VLAN table in case of MPSK */
	PPE_DRV_SC_LOOPBACK_RING = 30,			/* Service code for EDMA LOOPBACK ring */
	PPE_DRV_SC_LOOPBACK_RING_NEXT = 31,		/* Next Service code for EDMA LOOPBACK ring */
	PPE_DRV_SC_LOOPBACK_RING_GRETAP_MAPT = 32,	/* Next Service code for EDMA LOOPBACK ring GRETAP to MAPT */
	PPE_DRV_SC_LOOPBACK_RING_MAPT_GRETAP = 33,	/* Service code for EDMA LOOPBACK ring MAPT to GRETAP*/
	PPE_DRV_SC_LOOPBACK_RING_NEXT_GRETAP_N_MAPT = 34,
			/* Next Service code to dequeue packets from loopback ring and queue to GRETAP or MAPT ring */
	PPE_DRV_SC_NPT66_HAIRPIN_NAT = 35,	/* Service code for EDMA LOOPBACK ring for Hairpin NAT */
	PPE_DRV_SC_NPT66_HAIRPIN_NAT_NEXT = 36,	/* Next Service code for EDMA LOOPBACK ring for Hairpin NAT */
	PPE_DRV_SC_PKT_EXCEPTION_EDIT_EN = 37,
			/* Service code to edit/commit packet when exceptioned */
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_FLOW_SC  = 38,		/* Service code for loopback port first pass */
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_FLOW_SC_NEXT = 39,		/* Service code for loopback port second pass */
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_RETURN_SC  = 40,		/* Service code for loopback port first pass */
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_RETURN_SC_NEXT = 41,	/* Service code for loopback port second pass */

	PPE_DRV_SC_IPSEC_PPE2EIP_ENCAP = 42,	/* Inline IPsec redirection from PPE TO EIP for encap direction*/
	PPE_DRV_SC_IPSEC_PPE2EIP_ACL_MATCH = 43,	/* Inline IPsec redirection from PPE TO EIP for decap direction ACL match*/
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_SC = 44,		/* Service code for loopback port for pon pass */
	PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_SC_NEXT = 45,		/* Service code for loopback port for second pass */
	PPE_DRV_SC_FDB_BYPASS = 46,		/* Service code for gem port table to bypass destination selection from FDB table */

	PPE_DRV_SC_FLOW_ACL_FIRST = PPE_DRV_SC_FLOW_ACL_START,
					/* First service code for combining flow and ACL rule */
	PPE_DRV_SC_FLOW_ACL_LAST = PPE_DRV_SC_FLOW_ACL_END,
					/* Last service code for combining flow and ACL rule */
	PPE_DRV_SC_MAX = 256,		/* Max service code */
} ppe_drv_sc_t;

/*
 * ppe_drv_sc_metadata
 *	metadata for service codes.
 */
struct ppe_drv_sc_metadata {
	uint32_t tree_id;		/* Tree id from EDMA descriptor */
	uint32_t wifi_qos;		/* WiFi-qos from EDMA descriptor*/
	uint32_t int_pri;		/* Priority from EDMA descriptor*/
	uint8_t vp_num;			/* Destination VP number */
	uint8_t service_code;		/* Service code from EDMA descriptor*/
};

typedef bool (*ppe_drv_sc_callback_t)(void *app_data, struct sk_buff *skb, void *sc_data);

/*
 * ppe_drv_sc_process_skbuff()
 *	Register callback for a specific service code
 *
 * @param[IN] sc   Service code related metadata.
 * @param[IN] skb  Socket buffer with service code.
 *
 * @return
 * true if packet is consumed by the API or false if the packet is not consumed.
 */
extern bool ppe_drv_sc_process_skbuff(struct ppe_drv_sc_metadata *sc, struct sk_buff *skb);

/*
 * ppe_drv_sc_unregister_cb()
 *	Unregister callback for a specific service code
 *
 * @param[IN] sc   Service code number.
 *
 * @return
 * void
 */
extern void ppe_drv_sc_unregister_cb(ppe_drv_sc_t sc);

/*
 * ppe_drv_sc_register_cb()
 *	Register callback for a specific service code
 *
 * @param[IN] sc   Service code number.
 * @param[IN] cb   Callback API.
 * @param[IN] app_data   Application data to be passed to callback.
 *
 * @return
 * void
 */
extern void ppe_drv_sc_register_cb(ppe_drv_sc_t sc, ppe_drv_sc_callback_t cb, void *app_data);

/*
 * ppe_drv_sc_unregister_vp_cb()
 *	Unregister vp callback for a specific service code
 *
 * @param[IN] sc   Service code number.
 * @param[IN] vp_num   Virtual port number unregistering the callback.
 *
 * @return
 * void
 */
extern void ppe_drv_sc_unregister_vp_cb(ppe_drv_sc_t sc, uint16_t vp_num);

/*
 * ppe_drv_sc_register_vp_cb()
 *	Register vp callback for a specific service code
 *
 * @param[IN] sc   Service code number.
 * @param[IN] cb   Callback API.
 * @param[IN] app_data   Application data to be passed to callback.
 * @param[IN] vp_num     Virtual port number registering the callback.
 *
 * @return
 * void
 */
extern bool ppe_drv_sc_register_vp_cb(ppe_drv_sc_t sc, ppe_drv_sc_callback_t cb, void *app_data, uint16_t vp_num);

/** @} */ /* end_addtogroup ppe_drv_sc_subsystem */

#endif /* _PPE_DRV_SC_H_ */

