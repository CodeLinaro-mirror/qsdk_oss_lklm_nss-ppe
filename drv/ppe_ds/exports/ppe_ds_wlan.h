/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/**
 * @file ppe_ds_wlan.h
 *	PPE-DS WLAN specific definitions.
 */

#ifndef _PPE_DS_WLAN_H_
#define _PPE_DS_WLAN_H_
#include <ppe_vp_public.h>
#include <nss_plugins.h>

struct ppe_ds;
/**
 * ppe_ds_wlan_priv
 *	Wrapper to return PPE-DS WLAN handle's private area pointer.
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
static inline void *ppe_ds_wlan_priv(ppe_ds_wlan_handle_t *wlan_handle)
{
	return ((void *)&wlan_handle->priv);
}

/**
 * ppe_ds_wlan_rx
 *	PPE-DS WLAN REO2PPE processing API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle    PPE-DS WLAN handle
 * @param[in] reo_prod_idx   REO2PPE producer index
 */
void ppe_ds_wlan_rx(ppe_ds_wlan_handle_t *wlan_handle, uint16_t reo_prod_idx);

/**
 * ppe_ds_wlan_vp_alloc
 *	PPE-DS WLAN VP alloc API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * net_device
 * ppe_vp_ai
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] dev           WLAN VAP interface
 * @param[in] vpai          PPE-VP alloc parameter
 *
 * @return
 * PPE-VP port number if success, -1 if error
 */
ppe_vp_num_t ppe_ds_wlan_vp_alloc(ppe_ds_wlan_handle_t *wlan_handle, struct net_device *dev, struct ppe_vp_ai *vpai);

/**
 * ppe_ds_wlan_get_node_id
 *	PPE-DS WLAN get node id API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 *
 * @return
 * valid node id if success, invalid node id if error
 */
uint32_t ppe_ds_wlan_get_node_id(ppe_ds_wlan_handle_t *wlan_handle);

/**
 * ppe_ds_wlan_vp_free
 *	PPE-DS WLAN VP free API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_vp_num_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] vp_num        PPE-VP port number
 *
 * @return
 *  0 on success
 */
ppe_vp_status_t ppe_ds_wlan_vp_free(ppe_ds_wlan_handle_t *wlan_handle, ppe_vp_num_t vp_num);

/**
 * ppe_ds_wlan_inst_register
 *	PPE-DS WLAN instance registration API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_ds_wlan_reg_info
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] ring_info     PPE-DS ring information
 *
 * @return
 * Status of the PPE-DS WLAN instance registration
 */
bool ppe_ds_wlan_inst_register(ppe_ds_wlan_handle_t *wlan_handle, struct ppe_ds_wlan_reg_info *ring_info);

/**
 * ppe_ds_wlan_inst_register_v2
 *	PPE-DS WLAN instance registration API
 *
 * @datatypes
 * ppe_ds
 * ppe_ds_wlan_reg_info
 *
 * @param[in] node   PPE-DS node
 * @param[in] ring_info     PPE-DS ring information
 *
 * @return
 * Status of the PPE-DS WLAN instance registration
 */
bool ppe_ds_wlan_inst_register_v2(struct ppe_ds *node, struct ppe_ds_wlan_reg_info *ring_info);

/**
 * ppe_ds_wlan_instance_stop
 *	PPE-DS WLAN instance stop API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_ds_wlan_ctx_info_handle
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] wlan_info_hdl    WLAN ctx information handle
 */
void ppe_ds_wlan_instance_stop(ppe_ds_wlan_handle_t *wlan_handle,
		struct ppe_ds_wlan_ctx_info_handle *wlan_info_hdl);

/**
 * ppe_ds_wlan_instance_stop_v2
 *	PPE-DS WLAN instance stop API
 *
 * @datatypes
 * ppe_ds
 * ppe_ds_wlan_ctx_info_handle
 *
 * @param[in] node   PPE-DS node
 * @param[in] wlan_info_hdl    WLAN ctx information handle
 */
void ppe_ds_wlan_instance_stop_v2(struct ppe_ds *node,
		struct ppe_ds_wlan_ctx_info_handle *wlan_info_hdl);

/**
 * ppe_ds_wlan_inst_stop
 *	PPE-DS WLAN instance stop API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
void ppe_ds_wlan_inst_stop(ppe_ds_wlan_handle_t *wlan_handle);

/**
 * ppe_ds_wlan_instance_start
 *	PPE-DS WLAN instance start API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_ds_wlan_ctx_info_handle
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] wlan_info_hdl    WLAN ctx information handle
 *
 * @return
 * Status of the PPE-DS WLAN instance start
 */
int ppe_ds_wlan_instance_start(ppe_ds_wlan_handle_t *wlan_handle,
		struct ppe_ds_wlan_ctx_info_handle *wlan_info_hdl);

/**
 * ppe_ds_wlan_instance_start_v2
 *	PPE-DS WLAN instance start API
 *
 * @datatypes
 * ppe_ds
 * ppe_ds_wlan_ctx_info_handle
 *
 * @param[in] node   PPE-DS node
 * @param[in] wlan_info_hdl    WLAN ctx information handle
 *
 * @return
 * Status of the PPE-DS WLAN instance start
 */
int ppe_ds_wlan_instance_start_v2(struct ppe_ds *node,
		struct ppe_ds_wlan_ctx_info_handle *wlan_info_hdl);

/**
 * ppe_ds_wlan_inst_start
 *	PPE-DS WLAN instance start API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 *
 * @return
 * Status of the PPE-DS WLAN instance start
 */
int ppe_ds_wlan_inst_start(ppe_ds_wlan_handle_t *wlan_handle);

/**
 * ppe_ds_wlan_inst_free
 *	PPE-DS WLAN instance free API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
void ppe_ds_wlan_inst_free(ppe_ds_wlan_handle_t *wlan_handle);

/**
 * ppe_ds_wlan_inst_free_v2
 *	PPE-DS WLAN instance free API
 *
 * @datatypes
 * ppe_ds
 *
 * @param[in] node   PPE-DS node
 */
void ppe_ds_wlan_inst_free_v2(struct ppe_ds *node);

/**
 * ppe_ds_wlan_inst_alloc
 *	PPE-DS WLAN instance allocation API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_ds_wlan_ops
 *
 * @param[in] ops         PPE-DS WLAN operation callbacks
 * @param[in] priv_size   Size of PPE-DS WLAN handle's private area
 *
 * @return
 * Status of the PPE-DS WLAN instance allcation
 */
ppe_ds_wlan_handle_t *ppe_ds_wlan_inst_alloc(struct ppe_ds_wlan_ops *ops, size_t priv_size);

/**
 * ppe_ds_wlan_inst_alloc_v2
 *	PPE-DS WLAN instance allocation API
 *
 * @datatypes
 * ppe_ds_wlan_handle_t
 * ppe_ds_wlan_ops
 *
 * @param[in] ops         PPE-DS WLAN operation callbacks
 * @param[in] priv_size   Size of PPE-DS WLAN handle's private area
 *
 * @return
 * PPE-DS node
 */
struct ppe_ds *ppe_ds_wlan_inst_alloc_v2(struct ppe_ds_wlan_ops_v2 *ops, size_t priv_size);

/**
 * ppe_ds_get_node_id
 *	Return PPEDS node id for a ppeds node
 *
 * @datatypes
 * ppe_ds
 *
 * @param[in] node	PPE-DS node
 *
 * @return
 * valid node id if success, invalid node id if error
 */
uint32_t ppe_ds_get_node_id(struct ppe_ds *node);

/**
 * ppe_ds_ppe2tcl_wlan_handle_intr
 *	PPE-DS WLAN irq handling for ppe2tcl ring
 *
 * @param[in] ctxt IRQ context
 *
 */
int ppe_ds_ppe2tcl_wlan_handle_intr(void *ctxt);

/**
 * ppe_ds_ppe2tcl_wlan_handle_intr_v2
 *	PPE-DS WLAN irq handling for ppe2tcl ring
 *
 * @param[in] ctxt IRQ context
 *
 */
int ppe_ds_ppe2tcl_wlan_handle_intr_v2(void *ctxt);

/**
 * ppe_ds_reo2ppe_wlan_handle_intr
 *	PPE-DS WLAN irq handling for reo2ppe ring
 *
 * @param[in] ctxt IRQ context
 *
 */
int ppe_ds_reo2ppe_wlan_handle_intr(void *ctxt);

/**
 * ppe_ds_reo2ppe_wlan_handle_intr_v2
 *	PPE-DS WLAN irq handling for reo2ppe ring
 *
 * @param[in] ctxt IRQ context
 *
 */
int ppe_ds_reo2ppe_wlan_handle_intr_v2(void *ctxt);

/**
 * ppe_ds_wlan_get_intr_ctxt
 *	PPE-DS get wlan context
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
void *ppe_ds_wlan_get_intr_ctxt(ppe_ds_wlan_handle_t *wlan_handle);

/**
 * ppe_ds_wlan_get_intr_ctxt_v2
 *	PPE-DS get wlan context
 *
 * @datatypes
 * ppe_ds
 *
 * @param[in] node   PPE-DS node
 */
void *ppe_ds_wlan_get_intr_ctxt_v2(struct ppe_ds *node);

/**
 * ppe_ds_wlan_service_status_update
 *	PPE-DS ring service update
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] enable        Enable/Disable service
 */
void ppe_ds_wlan_service_status_update(ppe_ds_wlan_handle_t *wlan_handle, bool enable);

/**
 * ppe_ds_wlan_service_status_update_v2
 *	PPE-DS ring service update
 *
 * @datatypes
 * ppe_ds
 *
 * @param[in] node   PPE-DS node
 * @param[in] enable        Enable/Disable service
 */
void ppe_ds_wlan_service_status_update_v2(struct ppe_ds *node, bool enable);

/**
 * ppe_ds_wlan_plugins_cb_register
 * 	Callback for ppeds plugin registration
 *
 * @datatypes
 * ppe_ds_wlan_ops_v2
 *
 * @return
 * status of registration
 *
 */
int ppe_ds_wlan_plugins_cb_register(struct ppe_ds_wlan_ops_v2 *wlan_ops);

/**
 * ppe_ds_wlan_plugins_cb_unregister
 * 	Callback for ppeds plugin unregistration
 *
 * @return
 * none
 *
 */
void ppe_ds_wlan_plugins_cb_unregister(void);

#endif	/* _PPE_DS_WLAN_H_ */
