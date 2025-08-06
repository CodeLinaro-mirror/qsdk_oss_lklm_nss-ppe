/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef _NSS_PLUGINS_H_
#define _NSS_PLUGINS_H_

#include <linux/netdevice.h>
#include <linux/module.h>
#include <ppe_vp_public.h>

/**
 * ppe_ds_wlan_node_type_t
 *	PPE-DS node type
 */
typedef enum {
	PPE_DS_NODE_TYPE_2G,		/**< radio 2G */
	PPE_DS_NODE_TYPE_5G,		/**< radio 5G */
	PPE_DS_NODE_TYPE_6G,		/**< radio 6G */
	PPE_DS_NODE_TYPE_MAX,
} ppe_ds_wlan_node_type_t;

/**
 * ppe_ds_wlan_txdesc_elem
 *	PPEDS WLAN Tx descriptor element information
 */
struct ppe_ds_wlan_txdesc_elem {
	uint32_t opaque_lo;		/**< Low 32-bit opaque field content */
	uint32_t opaque_hi;		/**< High 32-bit opaque field content */
	dma_addr_t buff_addr;		/**< Buffer address */
};

/**
 * ppe_ds_wlan_rxdesc_elem
 *	PPEDS WLAN Rx descriptor element information
 */
struct ppe_ds_wlan_rxdesc_elem {
	unsigned long cookie;		/**< cookie information */
};

struct ppe_wlan_plugin_stats;

/**
 * ppe_ds_wlan_handle
 *	PPE-DS WLAN handle
 */
typedef struct ppe_ds_wlan_handle {
	uint32_t reserved;			/**< Reserved */
	char priv[] __aligned(NETDEV_ALIGN);	/**< contains the address of WLAN SoC base address */
} ppe_ds_wlan_handle_t;

/**
 * ppe_ds_wlan_ctx_info_handle
 *	PPE-DS wlan umac reset handle
 */
struct ppe_ds_wlan_ctx_info_handle {
	uint32_t umac_reset_inprogress;		/**< umac reset in progress information */
};

/**
 * ppe_ds_wlan_reg_info
 *	PPE-DS WLAN rings information
 */
struct ppe_ds_wlan_reg_info {
	dma_addr_t ppe2tcl_ba;		/**< PPE2TCL ring base address */
	dma_addr_t reo2ppe_ba;		/**< REO2PPE ring base address */
	uint32_t ppe2tcl_num_desc;	/**< PPE2TCL ring descriptor count */
	uint32_t reo2ppe_num_desc;	/**< REO2PPE ring descriptor count */
	ppe_ds_wlan_node_type_t node_type;	/**< PPE-DS node type */
	uint32_t ppe2tcl_start_idx;		/**< PPE2TCL ring index */
	uint32_t reo2ppe_start_idx;		/**< REO2PPE ring index */
	bool ppe_ds_int_mode_enabled;  /**< Interrupt mode to process PPE2TCL */
	uint8_t dp_ppeds_node_id;	/**< Node id of ds node */
};

struct ppe_vp_ui;

/**
 * ppe_ds_wlan_ops
 *	PPE-DS WLAN operations
 */
struct ppe_ds_wlan_ops {
	uint32_t (*get_tx_desc_many)(ppe_ds_wlan_handle_t *, struct ppe_ds_wlan_txdesc_elem *,
			uint32_t num_buff_req, uint32_t buff_size, uint32_t headroom);
	/**< Callback to get WLAN Tx descriptors and buffers */
	void (*release_tx_desc_single)(ppe_ds_wlan_handle_t *, uint32_t cookie);
	/**< Callback to release WLAN Tx descriptor and buffer */
	void (*set_tcl_prod_idx)(ppe_ds_wlan_handle_t *, uint16_t tcl_prod_idx);
	/**< Callback to set PPE2TCL ring's producer index */
	void (*set_reo_cons_idx)(ppe_ds_wlan_handle_t *, uint16_t reo_cons_idx);
	/**< Callback to set REO2PPE ring's consumer index */
	uint16_t (*get_tcl_cons_idx)(ppe_ds_wlan_handle_t *);
	/**< Callback to get PPE2TCL ring's consumer index */
	uint16_t (*get_reo_prod_idx)(ppe_ds_wlan_handle_t *);
	/**< Callback to get REO2PPE ring's producer index */
	void (*release_rx_desc)(ppe_ds_wlan_handle_t *ppeds_handle,
			struct ppe_ds_wlan_rxdesc_elem *arr, uint16_t count);
	/**< Callback to release WLAN Rx descriptors and buffers */
	void (*enable_tx_consume_intr)(ppe_ds_wlan_handle_t *ppeds_handle,
			bool enable);
	/**< Callback to toggle wlan interrupt */
	void (*notify_napi_done)(ppe_ds_wlan_handle_t *ppeds_handle);
	/**< Callback to trigger after ppeds ring process completes */
};

/**
 * ppe_ds_wlan_ops_v2
 *	PPE-DS WLAN operations
 */
struct ppe_ds_wlan_ops_v2 {
	uint32_t (*get_tx_desc_many)(int ppeds_node_id, struct ppe_ds_wlan_txdesc_elem *,
			uint32_t num_buff_req, uint32_t buff_size, uint32_t headroom);
	/**< Callback to get WLAN Tx descriptors and buffers */
	void (*release_tx_desc_single)(int ppeds_node_id, uint32_t cookie);
	/**< Callback to release WLAN Tx descriptor and buffer */
	void (*set_tcl_prod_idx)(int ppeds_node_id, uint16_t tcl_prod_idx);
	/**< Callback to set PPE2TCL ring's producer index */
	void (*set_reo_cons_idx)(int ppeds_node_id, uint16_t reo_cons_idx);
	/**< Callback to set REO2PPE ring's consumer index */
	uint16_t (*get_tcl_cons_idx)(int ppeds_node_id);
	/**< Callback to get PPE2TCL ring's consumer index */
	uint16_t (*get_reo_prod_idx)(int ppeds_node_id);
	/**< Callback to get REO2PPE ring's producer index */
	void (*release_rx_desc)(int ppeds_node_id,
			struct ppe_ds_wlan_rxdesc_elem *arr, uint16_t count);
	/**< Callback to release WLAN Rx descriptors and buffers */
	void (*enable_tx_consume_intr)(int ppeds_node_id,
			bool enable);
	/**< Callback to toggle wlan interrupt */
	void (*notify_napi_done)(int ppeds_node_id);
	/**< Callback to trigger after ppeds ring process completes */
};

/*
 * ds_inst_alloc_func_t
 * 	Callback for PPE_DS WLAN instance allocation
 *
 * @param[in] ppe_ds_wlan_ops PPE-DS WLAN operations
 * @param[in] priv_size sizeof dp_soc_be
 *
 * @return
 * PPE-DS node id
 *
 */
typedef int (*ds_inst_alloc_func_t)(struct ppe_ds_wlan_ops_v2 *ops, size_t priv_size);

/*
 * ds_inst_start_func_t
 * 	Callback for ppe_ds_wlan_instance_start
 *
 * @param[in] ppe_ds_wlan_ctx_info_handle PPE-DS wlan umac reset handle
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * Status of PPE-DS instance registration operation
 *
 */
typedef int (*ds_inst_start_func_t)(struct ppe_ds_wlan_ctx_info_handle *wlan_info_hd, int ppeds_node_id);

/*
 * ds_inst_register_func_t
 * 	Callback for ppe_ds_wlan_inst_register
 *
 * @param[in] ppe_ds_wlan_reg_info PPE-DS WLAN rings information
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * True/False
 *
 */
typedef bool (*ds_inst_register_func_t)(struct ppe_ds_wlan_reg_info *reg_info, int ppeds_node_id);

/*
 * ds_inst_stop_func_t
 * 	Callback for ppe_ds_wlan_instance_stop
 *
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * None
 *
 */
typedef void (*ds_inst_stop_func_t)(struct ppe_ds_wlan_ctx_info_handle *wlan_info_hd, int ppeds_node_id);

/*
 * ds_inst_free_func_t
 * 	Callback for ppe_ds_wlan_inst_free
 *
 * @param[in] ppe_ds_wlan_ctx_info_handle PPE-DS umac reset Information handle
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * None
 *
 */
typedef void (*ds_inst_free_func_t)(int ppeds_node_id);

/*
 * ds_inst_get_ctx_func_t
 * 	Callback for ppe_ds_wlan_get_intr_ctxt
 *
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * Context from ppe
 *
 */
typedef void* (*ds_inst_get_ctx_func_t)(int ppeds_node_id);

/*
 * ds_inst_ppe2tcl_intr_func_t
 * 	Callback for ppe_ds_ppe2tcl_wlan_handle_intr
 *
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * 0
 *
 */
typedef int (*ds_inst_ppe2tcl_intr_func_t)(int ppeds_node_id);

/*
 * ds_inst_reo2ppe_intr_func_t
 * 	Callback for ppe_ds_reo2ppe_wlan_handle_intr
 *
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 *
 * @return
 * 0
 *
 */
typedef int (*ds_inst_reo2ppe_intr_func_t)(int ppeds_node_id);

/*
 * ds_inst_stats_update_func_t
 * 	Callback for stat update
 * @param[in] net_device ath dev
 * @param[in] ppe_wlan_plugin_stats Plugin stat
 *
 * @return
 * None
 */
typedef void (*ds_inst_stats_update_func_t)(struct net_device *ath_dev, struct ppe_wlan_plugin_stats *stats);

/*
 * get_vp_num_func_t
 * 	Callback for ppe_vp_get_vp
 *
 * @param[in] net_device ath dev
 *
 * @return
 * VP number for the netdev
 */
typedef int16_t (*get_vp_num_func_t)(struct net_device *ath_dev);

/*
 * service_status_update_func_t
 * 	Callback for ppe_ds_wlan_service_status_update
 *
 * @param[in] ppeds_node_id Index of PPE-DS node configuration
 * @param[in] enable enable/disable bit
 *
 * @return
 * None
 */
typedef void (*service_status_update_func_t)(int ppeds_node_id, bool enable);

/*
 * update_vp_config_func_t
 * 	Callback for ppe_vp_status_update
 *
 * @param[in] net_device ath dev
 * @param[in] ppe_vp_ui VP update info
 *
 * @return
 * ppe_vp_status_t
 */
typedef ppe_vp_status_t (*update_vp_config_func_t)(struct net_device *ath_dev, struct ppe_vp_ui *vpui);

/**
 * nss_plugins_ops
 * 	Data structure for NSS Plugin ops
 */
struct nss_plugins_ops {
	ds_inst_alloc_func_t ds_inst_alloc;		/**< Callback for PPE-DS WLAN instance allocation API */
	ds_inst_start_func_t ds_inst_start;		/**< Callback for PPE-DS WLAN instance start API */
	ds_inst_register_func_t ds_inst_register;		/**< Callback for PPE-DS WLAN instance registration API */
	ds_inst_stop_func_t ds_inst_stop;		/**< Callback for PPE-DS WLAN instance stop API */
	ds_inst_free_func_t ds_inst_free;		/**< Callback for PPE-DS WLAN instance free API */
	ds_inst_get_ctx_func_t ds_inst_get_ctx;		/**< Callback for ppe_ds_wlan_get_intr_ctxt API */
	ds_inst_ppe2tcl_intr_func_t ds_inst_ppe2tcl_intr;		/**< Callback for PPE-DS PPE2TCL IRQ Tx processing API */
	ds_inst_reo2ppe_intr_func_t ds_inst_reo2ppe_intr;		/**< Callback for PPE-DS REO2PPE IRQ Rx processing API */
	ds_inst_stats_update_func_t ds_inst_stats_update;		/**< Callback for stat update */
	get_vp_num_func_t get_vp_num;		/**< Callback to get vp number */
	service_status_update_func_t service_status_update;		/**< Callback to get status update */
	update_vp_config_func_t vp_cfg_update; 		/**< Callback to update VP config */
};

/*
 * qca_nss_wifi_plugins_get_ops
 *	API to get the nss plugin operations
 *
 * @datatypes
 * nss_plugins_ops
 *
 * @return
 * nss_plugins_ops operation structure, else NULL
 */
struct nss_plugins_ops* qca_nss_wifi_plugins_get_ops(void);

#endif
