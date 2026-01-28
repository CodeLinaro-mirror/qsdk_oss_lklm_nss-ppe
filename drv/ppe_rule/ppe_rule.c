/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/debugfs.h>
#include <linux/module.h>
#include <ppe_drv.h>
#include "ppe_rfs/ppe_rfs.h"
#if !defined(NSS_PPE_LOWMEM_PROFILE_16M)
#include "ppe_acl/ppe_acl.h"
#include "ppe_policer/ppe_policer.h"
#include "ppe_qos/ppe_qos.h"
#endif
#ifdef NSS_PPE_DSCP_PBIT_FEATURE_SUPPORT
#include "ppe_dscp/ppe_dscp.h"
#endif
#if !defined(NSS_PPE_LOWMEM_PROFILE_16M) && !defined(NSS_PPE_LOWMEM_PROFILE_256M)
#include "ppe_mirror/ppe_mirror.h"
#include "ppe_priority/ppe_priority.h"
#endif

struct dentry *d_rule;

/*
 * ppe_rule_module_init()
 *	module init for ppe rule driver.
 */
static int __init ppe_rule_module_init(void)
{
	struct dentry *root = ppe_drv_get_dentry();
	if (!root) {
		printk("Unable to get root stats directory\n");
		return -1;
	}

	d_rule = debugfs_create_dir("ppe-rule", root);
	if (!d_rule) {
		printk("Unable to create debugfs stats directory in debugfs\n");
		return -1;
	}
	ppe_rfs_init(d_rule);
#if !defined(NSS_PPE_LOWMEM_PROFILE_16M)
	ppe_acl_init(d_rule);
	ppe_policer_init(d_rule);
	ppe_qos_init(d_rule);
#endif
#ifdef NSS_PPE_DSCP_PBIT_FEATURE_SUPPORT
	ppe_dscp_init(d_rule);
#endif
#if !defined(NSS_PPE_LOWMEM_PROFILE_16M) && !defined(NSS_PPE_LOWMEM_PROFILE_256M)
	ppe_mirror_init(d_rule);
	ppe_priority_init(d_rule);
#endif

	printk("PPE-RULE module loaded successfully\n");
	return 0;
}
module_init(ppe_rule_module_init);

/*
 * ppe_rule_module_exit()
 *	module exit for ppe rule driver.
 */
static void __exit ppe_rule_module_exit(void)
{
	ppe_rfs_deinit();

#if !defined(NSS_PPE_LOWMEM_PROFILE_16M) && !defined(NSS_PPE_LOWMEM_PROFILE_256M)
	ppe_mirror_deinit();
	ppe_priority_deinit();
#endif
#if !defined(NSS_PPE_LOWMEM_PROFILE_16M)
	ppe_policer_deinit();
	ppe_acl_deinit();
	ppe_qos_deinit();
#endif
#ifdef NSS_PPE_DSCP_PBIT_FEATURE_SUPPORT
	ppe_dscp_deinit();
#endif
	debugfs_remove_recursive(d_rule);
	printk("PPE-RULE module unloaded");
}
module_exit(ppe_rule_module_exit);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("PPE RULE driver");
