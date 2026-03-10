/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/atomic.h>
#include <linux/debugfs.h>
#include <linux/slab.h>
#include "ppe_tun_rps.h"

/*
 * ppe_tun_rps_stats_gen_str
 * 	PPE tunnel RPS connection statistics
 */
static const char *ppe_tun_rps_stats_str[] = {
	"v4_create_ppe_tun_rps_rule",		/* v4 create tun RPS rule request */
	"v4_create_ppe_tun_rps_rule_fail",	/* v4 create tun RPS ppe rule addition failed */
	"v4_destroy_ppe_tun_rps_rule",		/* v4 destroy tun RPS rule request */
	"v4_destroy_ppe_tun_rps_rule_fail",	/* v4 destroy tun RPS ppe rule addition failed */
	"v6_create_ppe_tun_rps_rule",		/* v6 create tun RPS rule request */
	"v6_create_ppe_tun_rps_rule_fail",	/* v6 create tun RPS ppe rule addition failed */
	"v6_destroy_ppe_tun_rps_rule",		/* v6 destroy tun RPS rule request */
	"v6_destroy_ppe_tun_rps_rule_fail"	/* v6 destroy tun RPS ppe rule addition failed */
};

/*
 * ppe_tun_rps_stats_show()
 *	Read ppe tunnel RPS connection statistics
 */
static int ppe_tun_rps_stats_show(struct seq_file *m, void __attribute__((unused))*ptr)
{
	struct ppe_tun_rps *g_tun_rps = &gbl_tun_rps;
	uint64_t stats_shadow[8];
	int i;

	spin_lock_bh(&g_tun_rps->lock);
	stats_shadow[0] = atomic64_read(&g_tun_rps->stats.v4_create_ppe_tun_rps_rule);
	stats_shadow[1] = atomic64_read(&g_tun_rps->stats.v4_create_ppe_tun_rps_rule_fail);
	stats_shadow[2] = atomic64_read(&g_tun_rps->stats.v4_destroy_ppe_tun_rps_rule);
	stats_shadow[3] = atomic64_read(&g_tun_rps->stats.v4_destroy_ppe_tun_rps_rule_fail);
	stats_shadow[4] = atomic64_read(&g_tun_rps->stats.v6_create_ppe_tun_rps_rule);
	stats_shadow[5] = atomic64_read(&g_tun_rps->stats.v6_create_ppe_tun_rps_rule_fail);
	stats_shadow[6] = atomic64_read(&g_tun_rps->stats.v6_destroy_ppe_tun_rps_rule);
	stats_shadow[7] = atomic64_read(&g_tun_rps->stats.v6_destroy_ppe_tun_rps_rule_fail);
	spin_unlock_bh(&g_tun_rps->lock);

	seq_puts(m, "\nPPE stats:\n\n");
	for (i = 0; i < 8; i++) {
		seq_printf(m, "\t\t [%s]:  %llu\n", ppe_tun_rps_stats_str[i], stats_shadow[i]);
	}

	return 0;
}

/*
 * ppe_tun_rps_stats_general_open()
 *	PPE tun_rps gen open callback API
 */
static int ppe_tun_rps_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppe_tun_rps_stats_show, inode->i_private);
}

/*
 * ppe_tun_rps_stats_general_file_ops
 *	File operations for EDMA common stats
 */
const struct file_operations ppe_tun_rps_stats_file_ops = {
	.open = ppe_tun_rps_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = seq_release,
};

/*
 * ppe_tun_rps_stats_debugfs_init()
 *	Create PPE tun_rps statistics debug entry.
 */
int ppe_tun_rps_stats_debugfs_init(struct dentry *root)
{
	struct ppe_tun_rps *g_tun_rps = &gbl_tun_rps;
	g_tun_rps->dentry = debugfs_create_dir("ppe-tun-rps", root);
	if (!g_tun_rps->dentry) {
		ppe_tun_rps_warn("%p: Unable to create debugfs stats directory in debugfs\n", g_tun_rps);
		return -1;
	}

	if (!debugfs_create_file("stats", S_IRUGO, g_tun_rps->dentry,
			NULL, &ppe_tun_rps_stats_file_ops)) {
		ppe_tun_rps_warn("%p: Unable to create common statistics file entry in debugfs\n", g_tun_rps);
		goto debugfs_dir_failed;
	}

	return 0;

debugfs_dir_failed:
	debugfs_remove_recursive(g_tun_rps->dentry);
	g_tun_rps->dentry = NULL;
	return -1;
}

/*
 * ppe_tun_rps_stats_debugfs_exit()
 *	PPE debugfs exit api
 */
void ppe_tun_rps_stats_debugfs_exit(void)
{
	struct ppe_tun_rps *g_tun_rps = &gbl_tun_rps;
	if (g_tun_rps->dentry) {
		debugfs_remove_recursive(g_tun_rps->dentry);
		g_tun_rps->dentry = NULL;
	}
}
