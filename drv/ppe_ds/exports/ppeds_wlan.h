/*
 * Copyright (c) 2022, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/**
 * @file ppeds_wlan.h
 *	PPE-DS WLAN specific definitions.
 */

#ifndef _PPE_DS_WLAN_H_
#define _PPE_DS_WLAN_H_
#include <ppe_vp_public.h>

/**
 * ppeds_wlan_txdesc_elem
 *	PPEDS WLAN Tx descriptor element information
 */
struct ppeds_wlan_txdesc_elem {
	uint32_t opaque_lo;		/**< Low 32-bit opaque field content */
	uint32_t opaque_hi;		/**< High 32-bit opaque field content */
	dma_addr_t buff_addr;		/**< Buffer address */
};

/**
 * ppeds_wlan_rxdesc_elem
 *	PPEDS WLAN Rx descriptor element information
 */
struct ppeds_wlan_rxdesc_elem {
	unsigned long cookie;		/**< cookie information */
};

/**
 * ppeds_wlan_reg_info
 *	PPE-DS WLAN rings information
 */
struct ppeds_wlan_reg_info {
	dma_addr_t ppe2tcl_ba;		/**< PPE2TCL ring base address */
	dma_addr_t reo2ppe_ba;		/**< REO2PPE ring base address */
	uint32_t ppe2tcl_num_desc;	/**< PPE2TCL ring descriptor count */
	uint32_t reo2ppe_num_desc;	/**< REO2PPE ring descriptor count */
};

/**
 * ppeds_wlan_handle
 *	PPE-DS WLAN handle
 */
typedef struct ppeds_wlan_handle {
	uint32_t reserved;			/**< Reserved */
	char priv[] __aligned(NETDEV_ALIGN);	/**< contains the address of WLAN SoC base address */
} ppeds_wlan_handle_t;

/**
 * ppeds_wlan_ops
 *	PPE-DS WLAN operations
 */
struct ppeds_wlan_ops {
	uint32_t (*get_tx_desc_many)(ppeds_wlan_handle_t *, struct ppeds_wlan_txdesc_elem *,
			 uint32_t num_buff_req, uint32_t buff_size, uint32_t headroom);
				/**< Callback to get WLAN Tx descriptors and buffers */
	void (*release_tx_desc_single)(ppeds_wlan_handle_t *, uint32_t cookie);
				/**< Callback to release WLAN Tx descriptor and buffer */
	void (*set_tcl_prod_idx)(ppeds_wlan_handle_t *, uint16_t tcl_prod_idx);
				/**< Callback to set PPE2TCL ring's producer index */
	void (*set_reo_cons_idx)(ppeds_wlan_handle_t *, uint16_t reo_cons_idx);
				/**< Callback to set REO2PPE ring's consumer index */
	uint16_t (*get_tcl_cons_idx)(ppeds_wlan_handle_t *);
				/**< Callback to get PPE2TCL ring's consumer index */
	uint16_t (*get_reo_prod_idx)(ppeds_wlan_handle_t *);
				/**< Callback to get REO2PPE ring's producer index */
	void (*release_rx_desc)(ppeds_wlan_handle_t *ppeds_handle,
			struct ppeds_wlan_rxdesc_elem *arr, uint16_t count);
				/**< Callback to release WLAN Rx descriptors and buffers */
};

/**
 * ppeds_wlan_priv
 *	Wrapper to return PPE-DS WLAN handle's private area pointer.
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
static inline void *ppeds_wlan_priv(ppeds_wlan_handle_t *wlan_handle)
{
	return ((void *)&wlan_handle->priv);
}

/**
 * nss_ppe_ds_wlan_inst_alloc
 *	PPE-DS WLAN instance allocation API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 * ppeds_wlan_ops
 *
 * @param[in] ops         PPE-DS WLAN operation callbacks
 * @param[in] priv_size   Size of PPE-DS WLAN handle's private area
 *
 * @return
 * Status of the PPE-DS WLAN instance allcation
 */
ppeds_wlan_handle_t *nss_ppe_ds_wlan_inst_alloc(struct ppeds_wlan_ops *ops, size_t priv_size);

/**
 * nss_ppe_ds_wlan_inst_register
 *	PPE-DS WLAN instance registration API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 * ppeds_wlan_reg_info
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] ring_info     PPE-DS ring information
 *
 * @return
 * Status of the PPE-DS WLAN instance registration
 */
bool nss_ppe_ds_wlan_inst_register(ppeds_wlan_handle_t *wlan_handle, struct ppeds_wlan_reg_info *ring_info);

/**
 * nss_ppe_ds_wlan_inst_start
 *	PPE-DS WLAN instance start API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 *
 * @return
 * Status of the PPE-DS WLAN instance start
 */
int nss_ppe_ds_wlan_inst_start(ppeds_wlan_handle_t *wlan_handle);

/**
 * nss_ppe_ds_wlan_inst_stop
 *	PPE-DS WLAN instance stop API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
void nss_ppe_ds_wlan_inst_stop(ppeds_wlan_handle_t *wlan_handle);

/**
 * nss_ppe_ds_wlan_inst_del
 *	PPE-DS WLAN instance delete API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 */
void nss_ppe_ds_wlan_inst_del(ppeds_wlan_handle_t *wlan_handle);

/**
 * nss_ppe_ds_wlan_rx
 *	PPE-DS WLAN REO2PPE processing API
 *
 * @datatypes
 * ppeds_wlan_handle_t
 *
 * @param[in] wlan_handle    PPE-DS WLAN handle
 * @param[in] reo_prod_idx   REO2PPE producer index
 */
void nss_ppe_ds_wlan_rx(ppeds_wlan_handle_t *wlan_handle, uint16_t reo_prod_idx);

/**
 * nss_ppe_ds_wlan_vp_alloc
 *	PPE-DS WLAN VP alloc API
 *
 * @datatypes
 * ppeds_wlan_handle_t
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
ppe_vp_num_t nss_ppe_ds_wlan_vp_alloc(ppeds_wlan_handle_t *wlan_handle, struct net_device *dev, struct ppe_vp_ai *vpai);

/**
 * nss_ppe_ds_wlan_vp_free
 *	PPE-DS WLAN VP free API
 *
 * @datatypes
 * ppe_vp_status_t
 * ppeds_wlan_handle_t
 * ppe_vp_num_t
 *
 * @param[in] wlan_handle   PPE-DS WLAN handle
 * @param[in] vp_num        PPE-VP port number
 *
 * @return
 *  0 on success
 */
ppe_vp_status_t nss_ppe_ds_wlan_vp_free(ppeds_wlan_handle_t *wlan_handle, ppe_vp_num_t vp_num);

#endif	/* _PPE_DS_WLAN_H_ */
