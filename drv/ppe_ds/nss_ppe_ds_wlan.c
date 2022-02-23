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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/debugfs.h>

#include "ppe_vp_public.h"
#include "nss_ppe_ds_wlan.h"

extern unsigned int idx_mgmt_freq;
extern unsigned int max_move;

#define IDX_MGMT_PERIOD max_t(u64, 10000, NSEC_PER_SEC / idx_mgmt_freq)

/*
 * nss_ppe_ds_wlan_ppe2tcl_rx()
 *	PPE-DS PPE2TCL Rx processing API
 */
static void nss_ppe_ds_wlan_ppe2tcl_rx(edma_ppeds_handle_t *edma_handle, uint16_t hw_prod_idx)
{
	struct nss_ppe_ds_wlan *node = edma_ppeds_priv(edma_handle);
	ppeds_wlan_handle_t *wlan_handle = &node->wlan_handle;

	return node->ops->set_tcl_prod_idx(wlan_handle, hw_prod_idx);
}

/*
 * nss_ppe_ds_wlan_ppe2tcl_fill()
 *	PPE-DS WLAN Tx descriptor and buffer fill API
 */
static uint32_t nss_ppe_ds_wlan_ppe2tcl_fill(edma_ppeds_handle_t *edma_handle, uint32_t num_buff_req, uint32_t buff_size,
		uint32_t headroom)
{
	struct nss_ppe_ds_wlan *node = edma_ppeds_priv(edma_handle);
	ppeds_wlan_handle_t *wlan_handle = &node->wlan_handle;

	struct ppeds_wlan_txdesc_elem *tx_desc_arr = (struct ppeds_wlan_txdesc_elem *)edma_ppeds_get_rx_fill_arr(edma_handle);

	return node->ops->get_tx_desc_many(wlan_handle, tx_desc_arr, num_buff_req, buff_size, headroom);
}

/*
 * nss_ppe_ds_wlan_reo2ppe_tx_cmpl()
 *	PPE-DS WLAN Rx descriptor and buffer fill API
 */
static void nss_ppe_ds_wlan_reo2ppe_tx_cmpl(edma_ppeds_handle_t *edma_handle, uint16_t count)
{
	struct nss_ppe_ds_wlan *node = edma_ppeds_priv(edma_handle);
	ppeds_wlan_handle_t *wlan_handle = &node->wlan_handle;

	struct ppeds_wlan_rxdesc_elem *rx_desc_arr = (struct ppeds_wlan_rxdesc_elem *)edma_ppeds_get_tx_cmpl_arr(edma_handle);

	return node->ops->release_rx_desc(wlan_handle, rx_desc_arr, count);
}

/*
 * nss_ppe_ds_wlan_ppe2tcl_rel()
 *	PPE-DS WLAN Tx descriptor and buffer release API
 */
static void nss_ppe_ds_wlan_ppe2tcl_rel(edma_ppeds_handle_t *edma_handle, uint64_t rx_opaque)
{
	struct nss_ppe_ds_wlan *node = edma_ppeds_priv(edma_handle);
	ppeds_wlan_handle_t *wlan_handle = &node->wlan_handle;
	uint32_t cookie = (uint32_t)(rx_opaque) & 0x000fffff;

	return node->ops->release_tx_desc_single(wlan_handle, cookie);
}

/*
 * nss_ppe_ds_wlan_timer()
 *	PPE-DS WLAN timer callback
 */
static enum hrtimer_restart nss_ppe_ds_wlan_timer(struct hrtimer *hrtimer)
{
	uint16_t cons_idx, prod_idx, move;
        struct nss_ppe_ds_wlan *node =
                container_of(hrtimer,  struct nss_ppe_ds_wlan, timer);
	ppeds_wlan_handle_t *wlan_handle = &node->wlan_handle;
	edma_ppeds_handle_t *edma_handle = node->edma_handle;
        uint16_t size = edma_handle->ppe2tcl_num_desc;
	uint16_t reo2ppe_size = edma_handle->reo2ppe_num_desc;

	/*
	 * Move Prod Idx
	 */
	prod_idx = edma_ppeds_get_rx_prod_idx(edma_handle);
	cons_idx = node->ops->get_tcl_cons_idx(wlan_handle);
	move = (prod_idx - cons_idx  + size) & (size - 1);
	if (move > 0) {
		/*
		 * Limit Tx because of the slow TxComp
		 */
		if (move > max_move) {
			prod_idx = (cons_idx + max_move) & (size - 1);
		}
		node->ops->set_tcl_prod_idx(wlan_handle, prod_idx);
	}

	/*
	 * Move Cons Index
	 */
	edma_ppeds_set_rx_cons_idx(edma_handle, cons_idx);

	/*
	 * Move consumer indices for UL
	 */
	prod_idx = node->ops->get_reo_prod_idx(wlan_handle);
	cons_idx = edma_ppeds_get_tx_cons_idx(edma_handle);
	move = (prod_idx - cons_idx  + reo2ppe_size) & (reo2ppe_size - 1);
	if (move > 0) {
		edma_ppeds_set_tx_prod_idx(edma_handle, prod_idx);
	}

	if (cons_idx != node->last_wlan_cons_idx) {
		node->ops->set_reo_cons_idx(wlan_handle, cons_idx);
		node->last_wlan_cons_idx = cons_idx;
	}

        hrtimer_forward(hrtimer, ktime_get(), ns_to_ktime(IDX_MGMT_PERIOD));
        return HRTIMER_RESTART;
}

/*
 * edma_ops
 *	EDMA PPE-DS operation callbacks
 */
static const struct edma_ppeds_ops edma_ops =
{
	.rx = nss_ppe_ds_wlan_ppe2tcl_rx,
	.rx_fill = nss_ppe_ds_wlan_ppe2tcl_fill,
	.rx_release = nss_ppe_ds_wlan_ppe2tcl_rel,
	.tx_cmpl = nss_ppe_ds_wlan_reo2ppe_tx_cmpl,
};

/*
 * nss_ppe_ds_wlan_rx()
 *	PPE-DS REO2PPE Rx processing API
 */
void nss_ppe_ds_wlan_rx(ppeds_wlan_handle_t *wlan_handle, uint16_t reo_prod_idx)
{
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	edma_ppeds_set_tx_prod_idx(edma_handle, reo_prod_idx);
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_rx);

/*
 * nss_ppe_ds_wlan_inst_register()
 *	PPE-DS WLAN instance registration API
 */
bool nss_ppe_ds_wlan_inst_register(ppeds_wlan_handle_t *wlan_handle, struct ppeds_wlan_reg_info *reg_info)
{
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	/*
	 * Setup dummy netdev for all the NAPIs associated with this node
	 */
	init_dummy_netdev(&node->napi_ndev);

	/*
	 * Init high res timer.
	 */
	hrtimer_init(&node->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	node->timer.function = nss_ppe_ds_wlan_timer;

	edma_handle->ppe2tcl_ba = reg_info->ppe2tcl_ba;
	edma_handle->reo2ppe_ba = reg_info->reo2ppe_ba;
	edma_handle->ppe2tcl_num_desc = reg_info->ppe2tcl_num_desc;
	edma_handle->reo2ppe_num_desc = reg_info->reo2ppe_num_desc;
	edma_handle->eth_txcomp_budget = 256;
	edma_handle->eth_rxfill_low_thr = reg_info->ppe2tcl_num_desc >> 3; //1/8th of rxfill entries

	ppe_ds_info(" ppe2tcl num desc: %d, reo2ppe num desc: %d\n",
			edma_handle->ppe2tcl_num_desc, edma_handle->reo2ppe_num_desc);
	return edma_ppeds_inst_register(edma_handle);
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_inst_register);

/*
 * nss_ppe_ds_wlan_inst_start()
 *	PPE-DS WLAN instance start API
 */
int nss_ppe_ds_wlan_inst_start(ppeds_wlan_handle_t *wlan_handle)
{
	int ret;
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	edma_ppeds_inst_refill(edma_handle, edma_handle->ppe2tcl_num_desc -1);

	node->timer_enabled = true;
	hrtimer_start_range_ns(&node->timer, ns_to_ktime(IDX_MGMT_PERIOD), 0, HRTIMER_MODE_REL_PINNED);

	ret = edma_ppeds_inst_start(edma_handle, PPE_DS_INTR_ENABLE);
	if (ret != 0) {
		node->timer_enabled = false;
		hrtimer_cancel(&node->timer);
	}

	ppe_ds_info("%px: PPE-DS started\n", wlan_handle);
	return ret;
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_inst_start);

/*
 * nss_ppe_ds_wlan_inst_stop()
 *	PPE-DS WLAN instance stop API
 */
void nss_ppe_ds_wlan_inst_stop(ppeds_wlan_handle_t *wlan_handle)
{
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	node->timer_enabled = false;
	hrtimer_cancel(&node->timer);

	/*
	 * Stop EDMA.
	 */
	edma_ppeds_inst_stop(edma_handle, PPE_DS_INTR_ENABLE);

	/*
	 * Drain RxFill and TxComplete rings
	 */
	edma_ppeds_drain_tx_cmpl_ring(edma_handle);
	edma_ppeds_drain_rxfill_ring(edma_handle);
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_inst_stop);

/*
 * nss_ppe_ds_wlan_inst_del()
 *	PPE-DS WLAN instance delete API
 */
void nss_ppe_ds_wlan_inst_del(ppeds_wlan_handle_t *wlan_handle)
{
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	edma_ppeds_inst_del(edma_handle);
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_inst_del);

/*
 * nss_ppe_ds_wlan_inst_alloc()
 *	PPE-DS WLAN instance allocation API
 */
ppeds_wlan_handle_t *nss_ppe_ds_wlan_inst_alloc(struct ppeds_wlan_ops *ops, size_t priv_size)
{
	struct nss_ppe_ds_wlan *node;
	edma_ppeds_handle_t *edma_handle;
	int size = priv_size + sizeof(struct nss_ppe_ds_wlan);

	edma_handle = edma_ppeds_inst_alloc(&edma_ops, size);
	if (!edma_handle) {
		ppe_ds_err("Failed to get edma handle\n");
		return NULL;
	}

	node = (struct nss_ppe_ds_wlan *)edma_ppeds_priv(edma_handle);
	node->ops = ops;
	node->edma_handle = edma_handle;

	return &node->wlan_handle;
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_inst_alloc);

/*
 * nss_ppe_ds_wlan_vp_free()
 *	PPE-DS WLAN VP free API
 */
ppe_vp_status_t nss_ppe_ds_wlan_vp_free(ppeds_wlan_handle_t *wlan_handle, ppe_vp_num_t vp_num)
{
	return ppe_vp_free(vp_num);

}
EXPORT_SYMBOL(nss_ppe_ds_wlan_vp_free);

/*
 * nss_ppe_ds_wlan_vp_alloc()
 *	PPE-DS WLAN VP alloc API
 */
ppe_vp_num_t nss_ppe_ds_wlan_vp_alloc(ppeds_wlan_handle_t *wlan_handle, struct net_device *dev, struct ppe_vp_ai *vpai)
{
	uint32_t ppe_queue_start, num;
	struct nss_ppe_ds_wlan *node = container_of(wlan_handle, struct nss_ppe_ds_wlan, wlan_handle);
	edma_ppeds_handle_t *edma_handle = node->edma_handle;

	edma_ppeds_get_ppe_queues(edma_handle, &ppe_queue_start, &num);
	ppe_ds_info("%px: %d %d", wlan_handle, ppe_queue_start, num);

	vpai->queue_num = ppe_queue_start;
	return ppe_vp_alloc(dev, vpai);
}
EXPORT_SYMBOL(nss_ppe_ds_wlan_vp_alloc);
