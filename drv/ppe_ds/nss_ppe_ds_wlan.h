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

#ifndef __NSS_PPE_DS_WLAN__
#define __NSS_PPE_DS_WLAN__
#include <linux/netdevice.h>

#include "edma_ppeds.h"
#include "ppeds_wlan.h"

#if defined(CONFIG_DYNAMIC_DEBUG)
/*
 * If dynamic debug is enabled, use pr_debug.
 */
#define ppe_ds_err(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ppe_ds_warn(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ppe_ds_info(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ppe_ds_trace(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else

/*
 * Statically compile messages at different levels, when dynamic debug is disabled.
 */
#if (PPE_DS_DEBUG_LEVEL < 1)
#define ppe_ds_err(s, ...)
#else
#define ppe_ds_err(s, ...) pr_err("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if (PPE_DS_DEBUG_LEVEL < 2)
#define ppe_ds_warn(s, ...)
#else
#define ppe_ds_warn(s, ...) pr_warn("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if (PPE_DS_DEBUG_LEVEL < 3)
#define ppe_ds_info(s, ...)
#else
#define ppe_ds_info(s, ...) pr_notice("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if (PPE_DS_DEBUG_LEVEL < 4)
#define ppe_ds_trace(s, ...)
#else
#define ppe_ds_trace(s, ...) pr_info("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
#endif

#define PPE_DS_INTR_ENABLE	0

/*
 * nss_ppe_ds_wlan
 *	PPE-DS WLAN node information
 */
struct nss_ppe_ds_wlan {
	struct napi_struct napi;		/* NAPI structure */
	struct hrtimer timer;			/* HR timer */
	struct net_device napi_ndev;		/* Dummy NAPI device */
	bool timer_enabled;			/* Timer enabled flag */
	struct ppeds_wlan_ops *ops;		/* PPE-DS WLAN operations */
	uint16_t last_edma_cons_idx;		/* Last read EDMA Rx consumer index */
	uint16_t last_wlan_cons_idx;		/* Last read WLAN REO2PPE consumer index */
	uint16_t last_edma_prod_idx;		/* Last read EDMA producer index */
	edma_ppeds_handle_t *edma_handle;	/* EDMA handle */
	ppeds_wlan_handle_t wlan_handle;	/* WLAN handle */
};
#endif	/* __NSS_PPE_DS_WLAN__ */
