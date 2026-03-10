/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef __PPE_PORT_MGMT_STATS_H
#define __PPE_PORT_MGMT_STATS_H

#include <linux/debugfs.h>

/*
 * Debugfs stats APIs
 */
void ppe_port_mgmt_stats_debugfs_init(struct dentry *d_rule);
void ppe_port_mgmt_stats_debugfs_exit(void);

#endif
