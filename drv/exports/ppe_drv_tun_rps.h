/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _PPE_DRV_TUN_RPS_H_
#define _PPE_DRV_TUN_RPS_H_

/*
 * ppe_drv_tun_rps_offset_type
 *	RPS offset type for UDF calculations
 */
enum ppe_drv_tun_rps_offset_type {
	PPE_DRV_TUN_RPS_OFFSET_TYPE_ETH,	/* Offset from start of L2 (Ethernet) header */
	PPE_DRV_TUN_RPS_OFFSET_TYPE_IP,		/* Offset from start of L3 (IP) header */
	PPE_DRV_TUN_RPS_OFFSET_TYPE_UDP,	/* Offset from start of L4 (UDP) header */
};

/*
 * ppe_drv_tun_rps_rule_create
 *	Structure for creating RPS rules for tunnel traffic
 */
struct ppe_drv_tun_rps_rule_create {
	uint32_t flow_ip[4];		/**< Flow IP address (index 0 for IPv4, all 4 for IPv6) */
	uint32_t flow_ident;		/**< Flow identifier (e.g., TCP or UDP port) */
	uint32_t return_ip[4];		/**< Return IP address (index 0 for IPv4, all 4 for IPv6) */
	uint32_t return_ident;		/**< Return identifier (e.g., TCP or UDP port) */
	enum ppe_drv_tun_rps_offset_type offset_type;	/**< Base offset type for UDF calculations */
	uint16_t udf0_val;		/**< UDF0 expected value for matching */
	uint16_t udf1_val;		/**< UDF1 expected value for matching */
	uint16_t udf2_val;		/**< UDF2 expected value for matching */
	uint8_t rule_type;		/**< IP version: 0 - IPv4, 1 - IPv6 */
	uint8_t protocol;		/**< IP protocol number (TCP=6, UDP=17, etc.) */
	uint8_t header_len;		/**< Tunnel outer header length in bytes */
	uint8_t udf0_en;		/**< Enable UDF0 for tunnel type identification */
	uint8_t udf1_en;		/**< Enable UDF1 for tunnel type identification */
	uint8_t udf2_en;		/**< Enable UDF2 for tunnel type identification */
	uint8_t udf0_offset;		/**< UDF0 offset in bytes from offset_type base */
	uint8_t udf1_offset;		/**< UDF1 offset in bytes from offset_type base */
	uint8_t udf2_offset;		/**< UDF2 offset in bytes from offset_type base */
};
#endif /* _PPE_DRV_TUN_RPS_H_ */
