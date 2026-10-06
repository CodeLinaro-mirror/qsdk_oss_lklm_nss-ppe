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
 * ppe_port_mgmt_stats_dentry
 *	Debugfs directory for PPE port-management statistics.
 */
static struct dentry *ppe_port_mgmt_stats_dentry;

/*
 * ppe_port_mgmt_stats_str
 *	Human-readable names for each counter in struct ppe_port_mgmt_stats.
 *	Order must exactly match the struct field order.
 */
static const char *ppe_port_mgmt_stats_str[] = {
	/*
	 * OMCI port operations [0..7].
	 */
	"omci_port_add_req",		/* OMCI port add requests */
	"omci_port_add_success",	/* OMCI port add successes */
	"omci_port_add_fail",		/* OMCI port add failures */
	"omci_port_del_req",		/* OMCI port delete requests */
	"omci_port_del_success",	/* OMCI port delete successes */
	"omci_port_del_fail",		/* OMCI port delete failures */
	"omci_port_flush_req",		/* OMCI port flush requests */
	"omci_port_flush_success",	/* OMCI port flush successes */

	/*
	 * MAC learn-limit operations [8..16].
	 * Aggregate totals followed by per-direction (enable / disable) breakdown.
	 */
	"mac_lrn_limit_req",		/* MAC learn limit set requests (aggregate) */
	"mac_lrn_limit_success",	/* MAC learn limit set successes (aggregate) */
	"mac_lrn_limit_fail",		/* MAC learn limit set failures (aggregate) */
	"mac_lrn_limit_en_req",		/* MAC learn limit enable requests */
	"mac_lrn_limit_en_success",	/* MAC learn limit enable successes */
	"mac_lrn_limit_en_fail",	/* MAC learn limit enable failures */
	"mac_lrn_limit_dis_req",	/* MAC learn limit disable requests */
	"mac_lrn_limit_dis_success",	/* MAC learn limit disable successes */
	"mac_lrn_limit_dis_fail",	/* MAC learn limit disable failures */

	/*
	 * MAC filter (FDB DROP entry) operations [17..22].
	 */
	"mac_filter_set_req",		/* MAC filter set requests */
	"mac_filter_set_success",	/* MAC filter set successes */
	"mac_filter_set_fail",		/* MAC filter set failures */
	"mac_filter_clr_req",		/* MAC filter clear requests */
	"mac_filter_clr_success",	/* MAC filter clear successes */
	"mac_filter_clr_fail",		/* MAC filter clear failures */

	/*
	 * Port isolation operations [23..25].
	 */
	"port_isol_req",		/* Port isolation set requests */
	"port_isol_success",		/* Port isolation set successes */
	"port_isol_fail",		/* Port isolation set failures */
};

extern struct ppe_port_mgmt_base ppe_port_mgmt_gbl;

/*
 * ppe_port_mgmt_stats_show()
 *	seq_file show callback - Section 1: operation counters.
 */
static int ppe_port_mgmt_stats_show(struct seq_file *m, void __attribute__((unused)) *ptr)
{
	struct ppe_port_mgmt_base *g = &ppe_port_mgmt_gbl;
	struct ppe_port_mgmt_stats stats_copy;
	uint64_t *stats_shadow;
	int i;

	/*
	 * Take a snapshot under the lock so the values are consistent.
	 */
	spin_lock_bh(&g->lock);
	memcpy(&stats_copy, &g->stats, sizeof(stats_copy));
	spin_unlock_bh(&g->lock);

	seq_puts(m, "\n========================================\n");
	seq_puts(m, " PPE Port Mgmt - Operation Counters\n");
	seq_puts(m, "========================================\n");

	stats_shadow = (uint64_t *)&stats_copy;

#ifdef NSS_PPE_PON_SUPPORT
	seq_puts(m, "\n  [OMCI managed Ports stats]\n");
	for (i = 0; i < 8; i++)
		seq_printf(m, "    %-35s: %llu\n", ppe_port_mgmt_stats_str[i], stats_shadow[i]);
#endif
	seq_puts(m, "\n  [MAC Learn Limit]\n");
	for (i = 8; i < 17; i++)
		seq_printf(m, "    %-35s: %llu\n", ppe_port_mgmt_stats_str[i], stats_shadow[i]);

	seq_puts(m, "\n  [MAC Filter]\n");
	for (i = 17; i < 23; i++)
		seq_printf(m, "    %-35s: %llu\n", ppe_port_mgmt_stats_str[i], stats_shadow[i]);

	seq_puts(m, "\n  [Port Isolation]\n");
	for (i = 23; i < 26; i++)
		seq_printf(m, "    %-35s: %llu\n", ppe_port_mgmt_stats_str[i], stats_shadow[i]);

	return 0;
}

/*
 * ppe_port_mgmt_stats_open()
 *	open() callback for the port-management statistics debugfs file.
 */
static int ppe_port_mgmt_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppe_port_mgmt_stats_show, inode->i_private);
}

/*
 * ppe_port_mgmt_stats_file_ops
 *	File operations for the port-management statistics file.
 */
static const struct file_operations ppe_port_mgmt_stats_file_ops = {
	.open    = ppe_port_mgmt_stats_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * ppe_port_mgmt_dump_show()
 *      Dump PPE port-management state.
 */
static int ppe_port_mgmt_dump_show(struct seq_file *m,
		void __attribute__((unused)) *ptr)
{
	seq_puts(m, "\n========================================\n");
	seq_puts(m, " PPE Port Mgmt Dump\n");
	seq_puts(m, "========================================\n");

#ifdef NSS_PPE_PON_SUPPORT
	/*
	 * Dump OMCI-managed ports.
	 */
	ppe_drv_port_mgmt_omci_port_list_show(m);
#endif

#ifdef NSS_PPE_PORT_ISOL_SUPPORT
	/*
	 * Dump isolation for ports.
	 */
	ppe_drv_port_mgmt_isol_dump(m);
#endif

	/*
	 * Dump MAC learn-limit configuration.
	 */
	ppe_drv_port_mgmt_mac_lrn_limit_dump(m);

	/*
	 * Dump MAC filter entries.
	 */
	ppe_drv_port_mgmt_mac_filter_dump(m);

	return 0;
}

/*
 * ppe_port_mgmt_dump_open()
 *	open() callback for the port-management hardware-state dump debugfs file.
 */
static int ppe_port_mgmt_dump_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppe_port_mgmt_dump_show, inode->i_private);
}

/*
 * ppe_port_mgmt_dump_file_ops
 *	File operations for the port-management hardware-state dump file.
 */
static const struct file_operations ppe_port_mgmt_dump_file_ops = {
	.open    = ppe_port_mgmt_dump_open,
	.read    = seq_read,
	.llseek  = seq_lseek,
	.release = seq_release,
};

/*
 * ppe_port_mgmt_stats_debugfs_init()
 *	Create the ppe_port_mgmt debugfs directory under d_rule and register
 *	the stats and port_mgmt_dump files inside it.
 */
void ppe_port_mgmt_stats_debugfs_init(struct dentry *d_rule)
{
	struct ppe_port_mgmt_base *g = &ppe_port_mgmt_gbl;

	ppe_port_mgmt_stats_dentry = debugfs_create_dir("ppe_port_mgmt", d_rule);
	if (!ppe_port_mgmt_stats_dentry) {
		ppe_port_mgmt_warn("Failed to create ppe_port_mgmt debugfs directory\n");
		return;
	}

	g->dentry = ppe_port_mgmt_stats_dentry;

	if (!debugfs_create_file("stats", S_IRUGO, ppe_port_mgmt_stats_dentry,
				 NULL, &ppe_port_mgmt_stats_file_ops)) {
		ppe_port_mgmt_warn("Failed to create stats debugfs file\n");
		debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
		ppe_port_mgmt_stats_dentry = NULL;
		g->dentry = NULL;
		return;
	}

	if (!debugfs_create_file("port_mgmt_dump", S_IRUGO, ppe_port_mgmt_stats_dentry,
				 NULL, &ppe_port_mgmt_dump_file_ops)) {
		ppe_port_mgmt_warn("Failed to create port_mgmt_dump debugfs file\n");
		debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
		ppe_port_mgmt_stats_dentry = NULL;
		g->dentry = NULL;
		return;
	}
}

/*
 * ppe_port_mgmt_stats_debugfs_exit()
 *	Remove the ppe_port_mgmt debugfs directory and all files under it.
 */
void ppe_port_mgmt_stats_debugfs_exit(void)
{
	debugfs_remove_recursive(ppe_port_mgmt_stats_dentry);
	ppe_port_mgmt_stats_dentry = NULL;
}
