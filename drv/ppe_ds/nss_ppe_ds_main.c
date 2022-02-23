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

#include <linux/kernel.h>
#include <linux/module.h>

#include "nss_ppe_ds_wlan.h"

/*
 * PPE-DS timer frequency for polling function.
 * The final timer frequency will be in nanosecond and calculated as
 * NSEC_PER_SEC/idx_mgmt_freq
 */
unsigned int idx_mgmt_freq = 32768;
module_param(idx_mgmt_freq, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(idx_mgmt_freq, "Idx Management hrtimer freq");

/*
 * Maximum PPE2TCL producer index count to be indicated to the Waikiki
 * hardware to handle its slow Tx complete
 */
unsigned int max_move = 1024;
module_param(max_move, uint, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(max_move, "Max movement of the Prod idx that is allowed");

/*
 * nss_ppe_ds_module_init()
 *	module init for PPE-DS driver
 */
static int __init nss_ppe_ds_module_init(void)
{
	ppe_ds_info("PPE-DS module loaded successfully %d", idx_mgmt_freq);
	return 0;
}
module_init(nss_ppe_ds_module_init);

/*
 * nss_ppe_ds_module_exit()
 *	module exit for PPE-DS driver
 */
static void __exit nss_ppe_ds_module_exit(void)
{
	ppe_ds_info("PPE-DS module unloaded successfully %d", idx_mgmt_freq);
}
module_exit(nss_ppe_ds_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("QCA NSS PPE DS driver");
