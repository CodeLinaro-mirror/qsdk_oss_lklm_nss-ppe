/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <ppe_drv.h>
#include "ppe_port_mgmt.h"
#include "ppe_port_mgmt_stats.h"

/*
 * debugfs directory
 */
static struct dentry *ppe_port_mgmt_stats_dentry;

/*
 * ppe_port_mgmt_stats_debugfs_init()
 *	Creates debugfs files for PPE PORT_MGMT stats
 */
void ppe_port_mgmt_stats_debugfs_init(struct dentry *d_rule)
{
	ppe_port_mgmt_stats_dentry = debugfs_create_dir("ppe_port_mgmt", d_rule);
	if (!ppe_port_mgmt_stats_dentry) {
		ppe_port_mgmt_warn("Failed to create ppe port mgmt debugfs directory\n");
		return;
	}
}

/*
 * ppe_port_mgmt_stats_debugfs_exit()
 *	Removes PPE PORT_MGMT stats debugfs files
 */
void ppe_port_mgmt_stats_debugfs_exit(void)
{
	debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
}
