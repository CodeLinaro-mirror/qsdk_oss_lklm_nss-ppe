/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file ppe_drv.h
 *	NSS PPE driver definitions.
 */

#ifndef _PPE_DRV_H_
#define _PPE_DRV_H_

/**
 * @addtogroup ppe_drv_subsystem
 * @{
 */
#include <linux/if_ether.h>
#include "ppe_drv_iface.h"

/**
 * ppe_drv_pppoe_session
 *	Information for PPPoE session.
 */
struct ppe_drv_pppoe_session {
	uint16_t session_id;				/**< Session id */
	uint8_t server_mac[ETH_ALEN];			/**< Server MAC address */
};

/**
 * ppe_drv_pppoe_rule
 *	Information for PPPoE connection rules.
 */
struct ppe_drv_pppoe_rule {
	struct ppe_drv_pppoe_session flow_session;		/**< Flow PPPoE session */
	struct ppe_drv_pppoe_session return_session;		/**< Return PPPoE session */
};

/**
 * ppe_drv_dscp_rule
 *	Information for DSCP connection rules.
 */
struct ppe_drv_dscp_rule {
	uint8_t flow_dscp;		/**< Egress DSCP value for the flow direction. */
	uint8_t return_dscp;		/**< Egress DSCP value for the return direction. */
};

/**
 * ppe_drv_vlan_info
 *	Information for ingress and egress VLANs.
 */
struct ppe_drv_vlan_info {
	uint32_t ingress_vlan_tag;	/**< VLAN tag for the ingress packets. */
	uint32_t egress_vlan_tag;	/**< VLAN tag for egress packets. */
};

/**
 * ppe_drv_vlan_rule
 *	Information for VLAN connection rules.
 */
struct ppe_drv_vlan_rule {
	struct ppe_drv_vlan_info primary_vlan;		/* Primary VLAN info */
	struct ppe_drv_vlan_info secondary_vlan;	/* Secondary VLAN info */
};

/**
 * ppe_drv_qos_rule
 *	Information for QoS connection rules.
 */
struct ppe_drv_qos_rule {
	uint32_t flow_qos_tag;		/**< QoS tag associated with this rule for the flow direction. */
	uint32_t return_qos_tag;	/**< QoS tag associated with this rule for the return direction. */
};

/**
 * ppe_drv_top_if_rule
 *	Information for top interface in hierarchy.
 */
struct ppe_drv_top_if_rule {
	ppe_drv_iface_t rx_if;		/**< Top PPE interface for from direction */
	ppe_drv_iface_t tx_if;		/**< Top PPE interface for return direction */
};

/*
 * ppe_drv_stats_sync_reason
 *	Stats sync reasons.
 */
enum ppe_drv_stats_sync_reason {
	PPE_DRV_STATS_SYNC_REASON_STATS,	/* Sync is to synchronize stats */
	PPE_DRV_STATS_SYNC_REASON_FLUSH,	/* Sync is to flush a connection entry */
	PPE_DRV_STATS_SYNC_REASON_EVICT,	/* Sync is to evict a connection entry */
	PPE_DRV_STATS_SYNC_REASON_DESTROY,	/* Sync is to destroy a connection entry */
};

/**
 * PPE return status
 */
typedef enum {
	PPE_DRV_RET_SUCCESS = 0,			/**< Success */
	PPE_DRV_RET_FAILURE_NO_RESOURCE,		/**< Failure due to out of resource */
	PPE_DRV_RET_FAILURE_INVALID_PARAM,		/**< Failure due to invalid parameter */
} ppe_drv_ret_t;

/** @} */ /* end_addtogroup ppe_drv_subsystem */

#endif /* _PPE_DRV_H_ */
