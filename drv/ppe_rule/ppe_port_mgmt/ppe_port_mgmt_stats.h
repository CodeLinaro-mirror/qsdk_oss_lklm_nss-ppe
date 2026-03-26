/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#ifndef __PPE_PORT_MGMT_STATS_H
#define __PPE_PORT_MGMT_STATS_H

#include <linux/atomic.h>
#include <linux/debugfs.h>

/*
 * ppe_port_mgmt_stats
 *	Statistics counters for OMCI port management operations.
 */
struct ppe_port_mgmt_stats {
	atomic64_t omci_port_add_req;		/**< OMCI port add requests */
	atomic64_t omci_port_add_success;	/**< OMCI port add successes */
	atomic64_t omci_port_add_fail;		/**< OMCI port add failures */
	atomic64_t omci_port_del_req;		/**< OMCI port delete requests */
	atomic64_t omci_port_del_success;	/**< OMCI port delete successes */
	atomic64_t omci_port_del_fail;		/**< OMCI port delete failures */
	atomic64_t omci_port_flush_req;		/**< OMCI port flush requests */
	atomic64_t omci_port_flush_success;	/**< OMCI port flush successes */
};

/*
 * ppe_port_mgmt_stats_inc()
 *	Increment a stats counter.
 */
static inline void ppe_port_mgmt_stats_inc(atomic64_t *stat)
{
	atomic64_inc(stat);
}

/*
 * Debugfs stats APIs
 */
void ppe_port_mgmt_stats_debugfs_init(struct dentry *d_rule);
void ppe_port_mgmt_stats_debugfs_exit(void);

#endif
