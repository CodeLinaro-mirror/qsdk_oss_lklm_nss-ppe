/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include "ppe_tun_rps_stats.h"

/*
 * PPE Tunnel RPS related macros
 */
#if (PPE_TUN_RPS_DEBUG_LEVEL == 3)
#define ppe_tun_rps_assert(c, s, ...)
#else
#define ppe_tun_rps_assert(c, s, ...) if (!(c)) { printk(KERN_CRIT "%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__); BUG_ON(!(c)); }
#endif

#if defined(CONFIG_DYNAMIC_DEBUG)
/*
 * If dynamic debug is enabled, use pr_debug.
 */
#define ppe_tun_rps_warn(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ppe_tun_rps_info(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#define ppe_tun_rps_trace(s, ...) pr_debug("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#else

/*
 * Statically compile messages at different levels, when dynamic debug is disabled.
 */
#if (PPE_TUN_RPS_DEBUG_LEVEL < 2)
#define ppe_tun_rps_warn(s, ...)
#else
#define ppe_tun_rps_warn(s, ...) pr_warn("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if (PPE_TUN_RPS_DEBUG_LEVEL < 3)
#define ppe_tun_rps_info(s, ...)
#else
#define ppe_tun_rps_info(s, ...) pr_notice("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif

#if (PPE_TUN_RPS_DEBUG_LEVEL < 4)
#define ppe_tun_rps_trace(s, ...)
#else
#define ppe_tun_rps_trace(s, ...) pr_info("%s[%d]:" s, __FUNCTION__, __LINE__, ##__VA_ARGS__)
#endif
#endif

/*
 * ppe_tun_rps
 *	Global ppe tun_rps global instance
 */
struct ppe_tun_rps {
	spinlock_t lock;                                /* PPE lock */
	struct ppe_tun_rps_stats stats;                /* PPE tun rps statistics */
	struct dentry *dentry;				/* Debugfs entry */
};

void ppe_tun_rps_deinit(void);
int ppe_tun_rps_init(struct dentry *dentry);

extern struct ppe_tun_rps gbl_tun_rps;

