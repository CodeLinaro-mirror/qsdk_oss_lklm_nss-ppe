/*
 * Copyright (c) 2024, Qualcomm Innovation Center, Inc. All rights reserved.
 * SPDX-License-Identifier: ISC
 */

/**
 * @file ppe_qos.h
 *	NSS PPE RFS definitions.
 */

#ifndef _PPE_QOS_H_
#define _PPE_QOS_H_

#include <ppe_drv_public.h>


/*
 * ppe_qos_status
 *	ppe req status
 */
typedef enum ppe_qos_ret {
	PPE_QOS_SUCCESS = 0,	/**< Success */
	PPE_QOS_FAIL,		/**< Failure*/
} ppe_qos_ret_t;


/*
 * ppe_qos_req
 *	ppe_qos_req: Send request to get qos data
 */
struct ppe_qos_req {
	uint8_t int_pri;	/**< int_pri value of PPE Queue */
	uint8_t port_id;	/**< PPE port id */
	uint16_t ucast_qid;	/**< unicast queue id of PPE */
	uint32_t class_id;	/**< classid corresponding to the PPE Queue */
	char dev[IFNAMSIZ];	/**< Netdevice */
};

/**
 * ppe_qos_get_int_pri_func
 *	Create PPE qos req.
 *
 *
 * @param[in]
 *
 * dev: netdev given by user
 * class_id: Clas Id for which int_pri is needed
 *
 * @return
 * internal priority assigned to the class
 */
ppe_qos_ret_t ppe_qos_get_int_pri_func(struct ppe_qos_req *req);

#endif /* _PPE_QOS_H_ */
