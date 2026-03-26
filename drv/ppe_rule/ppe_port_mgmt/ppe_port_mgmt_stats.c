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
#include <ppe_drv_public.h>
#include <ppe_drv.h>
#include <ppe_drv_port_mgmt.h>
#include "ppe_port_mgmt.h"
#include "ppe_port_mgmt_stats.h"

/*
 * debugfs directory
 */
static struct dentry *ppe_port_mgmt_stats_dentry;

/*
 * ppe_port_mgmt_stats_str
 *	Human-readable names for each stats counter (must match struct ppe_port_mgmt_stats order).
 */
static const char *ppe_port_mgmt_stats_str[] = {
	"omci_port_add_req",
	"omci_port_add_success",
	"omci_port_add_fail",
	"omci_port_del_req",
	"omci_port_del_success",
	"omci_port_del_fail",
	"omci_port_flush_req",
	"omci_port_flush_success",
};

extern struct ppe_port_mgmt_base ppe_port_mgmt_gbl;

/*
 * ppe_port_mgmt_stats_show()
 *	Read PPE port management statistics.
 */
static int ppe_port_mgmt_stats_show(struct seq_file *m, void __attribute__((unused)) *ptr)
{
	struct ppe_port_mgmt_base *g = &ppe_port_mgmt_gbl;
	struct ppe_port_mgmt_stats stats_copy;
	uint64_t *stats_shadow;
	int i;

	spin_lock_bh(&g->lock);
	memcpy(&stats_copy, &g->stats, sizeof(stats_copy));
	spin_unlock_bh(&g->lock);

	seq_puts(m, "\nPPE Port Mgmt stats:\n\n");
	stats_shadow = (uint64_t *)&stats_copy;
	for (i = 0; i < sizeof(struct ppe_port_mgmt_stats) / sizeof(uint64_t); i++) {
		seq_printf(m, "\t\t[%s]: %llu\n", ppe_port_mgmt_stats_str[i], stats_shadow[i]);
	}

	return 0;
}

/*
 * ppe_port_mgmt_stats_open()
 *	Open callback for PPE port management stats debugfs file.
 */
static int ppe_port_mgmt_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppe_port_mgmt_stats_show, inode->i_private);
}

/*
 * ppe_port_mgmt_stats_file_ops
 *	File operations for PPE port management stats.
 */
static const struct file_operations ppe_port_mgmt_stats_file_ops = {
	.open = ppe_port_mgmt_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

/*
 * ppe_port_mgmt_stats_omci_list_show()
 *	Show the current list of OMCI-managed ports.
 */
static int ppe_port_mgmt_stats_omci_list_show(struct seq_file *m, void __attribute__((unused)) *ptr)
{
	ppe_drv_port_mgmt_omci_port_list_show(m);
	return 0;
}

/*
 * ppe_port_mgmt_stats_omci_list_open()
 *	Open callback for OMCI port list debugfs file.
 */
static int ppe_port_mgmt_stats_omci_list_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppe_port_mgmt_stats_omci_list_show, inode->i_private);
}

/*
 * ppe_port_mgmt_omci_list_file_ops
 *	File operations for OMCI port list dump.
 */
static const struct file_operations ppe_port_mgmt_stats_omci_list_file_ops = {
	.open = ppe_port_mgmt_stats_omci_list_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

/*
 * ppe_port_mgmt_stats_debugfs_init()
 *	Creates debugfs files for PPE PORT_MGMT stats
 */
void ppe_port_mgmt_stats_debugfs_init(struct dentry *d_rule)
{
	struct ppe_port_mgmt_base *g = &ppe_port_mgmt_gbl;

	ppe_port_mgmt_stats_dentry = debugfs_create_dir("ppe_port_mgmt", d_rule);
	if (!ppe_port_mgmt_stats_dentry) {
		ppe_port_mgmt_warn("Failed to create ppe port mgmt debugfs directory\n");
		return;
	}

	g->dentry = ppe_port_mgmt_stats_dentry;

	if (!debugfs_create_file("stats", S_IRUGO, ppe_port_mgmt_stats_dentry,
				 NULL, &ppe_port_mgmt_stats_file_ops)) {
		ppe_port_mgmt_warn("Failed to create ppe port mgmt stats debugfs file\n");
		debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
		ppe_port_mgmt_stats_dentry = NULL;
		g->dentry = NULL;
	}

	if (!debugfs_create_file("omci_port_list", S_IRUGO, ppe_port_mgmt_stats_dentry,
				 NULL, &ppe_port_mgmt_stats_omci_list_file_ops)) {
		debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
		ppe_port_mgmt_stats_dentry = NULL;
		g->dentry = NULL;
		ppe_port_mgmt_warn("Failed to create ppe port mgmt stats debugfs file\n");
	}
}

/*
 * ppe_port_mgmt_stats_debugfs_exit()
 *	Removes PPE PORT_MGMT stats debugfs files
 */
void ppe_port_mgmt_stats_debugfs_exit(void)
{
	debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
	ppe_port_mgmt_stats_dentry = NULL;
}
