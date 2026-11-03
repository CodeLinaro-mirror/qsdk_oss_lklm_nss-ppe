/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/*
 * @file nss_ppenl_port_mirror_if.h
 *	NSS PPE Netlink port-mirror interface definitions.
 *
 * This header is shared between kernel and user space.  It defines the
 * Generic Netlink family name, the per-message payload structures, the
 * message-type enum, and the inline init helper used by user-space tools
 * (e.g. ppecfg) to prepare a Netlink message before sending it.
 *
 * Supported commands
 * ------------------
 * NSS_PPE_PORT_MIRROR_ENABLE_MSG
 *   Enable L2 VP mirroring (ingress or egress) on a named port.
 *   Uses nss_ppenl_port_mirror_cfg.
 *
 * NSS_PPE_PORT_MIRROR_DISABLE_MSG
 *   Disable L2 VP mirroring (ingress or egress) on a named port.
 *   Uses nss_ppenl_port_mirror_cfg.
 *
 * NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG
 *   Set the mirror analysis (analyst) port for a given direction.
 *   Uses nss_ppenl_port_mirror_analyst.
 *
 * NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG
 *   Clear (disable) the mirror analysis port for a given direction.
 *   Uses nss_ppenl_port_mirror_analyst.
 */
#ifndef __NSS_PPENL_PORT_MIRROR_IF_H
#define __NSS_PPENL_PORT_MIRROR_IF_H

#include <linux/if.h>

/**
 * NSS_PPENL_PORT_MIRROR_FAMILY
 *	Generic Netlink family name for the port-mirror ppecfg family.
 */
#define NSS_PPENL_PORT_MIRROR_FAMILY	"nss_ppenl_pmirr"

/**
 * nss_ppe_port_mirror_dir
 *	Mirror direction for port mirroring / analysis port selection.
 */
enum nss_ppe_port_mirror_dir {
	NSS_PPE_PORT_MIRROR_DIR_INGRESS = 0,	/**< Ingress mirroring */
	NSS_PPE_PORT_MIRROR_DIR_EGRESS,		/**< Egress mirroring */
};

/**
 * nss_ppenl_port_mirror_cfg
 *	Payload for NSS_PPE_PORT_MIRROR_ENABLE_MSG and NSS_PPE_PORT_MIRROR_DISABLE_MSG.
 *
 * @port_name:	Null-terminated name of the physical / VP net device to mirror.
 * @dir:	Mirror direction (nss_ppe_port_mirror_dir).
 * @ret:	Return status written by the kernel and echoed back to user space.
 */
struct nss_ppenl_port_mirror_cfg {
	char port_name[IFNAMSIZ];	/**< Net device name */
	uint8_t dir;			/**< nss_ppe_port_mirror_dir */
	int  ret;			/**< Return status (kernel to user) */
};

/**
 * nss_ppenl_port_mirror_analyst
 *	Payload for NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG and
 *	NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG.
 *
 * @analyst_port_name:	Null-terminated name of the net device that will
 *			receive mirrored copies (the "analyst" / capture port).
 * @dir:		Direction for which this analyst port is configured
 *			(nss_ppe_port_mirror_dir).
 * @priority:		Analysis-port priority (0 = default).  Ignored on clear.
 * @ret:		Return status written by the kernel and echoed back.
 */
struct nss_ppenl_port_mirror_analyst {
	char analyst_port_name[IFNAMSIZ];	/**< Analyst / capture port name */
	uint8_t dir;				/**< nss_ppe_port_mirror_dir */
	uint8_t priority;			/**< Analysis port priority */
	int ret;				/**< Return status (kernel to user) */
};

/**
 * nss_ppenl_port_mirror_rule
 *	Top-level Netlink message structure for the port-mirror family.
 *
 * Only one of @cfg or @analyst is meaningful for any given message type.
 */
struct nss_ppenl_port_mirror_rule {
	struct nss_ppenl_cmn cm;			/**< Common Netlink message header */
	struct nss_ppenl_port_mirror_cfg cfg;		/**< Used by ENABLE_MSG / DISABLE_MSG */
	struct nss_ppenl_port_mirror_analyst analyst;	/**< Used by ANALYST_SET/CLR_MSG */
};

/**
 * nss_ppe_port_mirror_message_types
 *	Netlink command identifiers for the port-mirror family.
 */
enum nss_ppe_port_mirror_message_types {
	NSS_PPE_PORT_MIRROR_ENABLE_MSG,			/**< Enable ingress or egress mirroring on a port */
	NSS_PPE_PORT_MIRROR_DISABLE_MSG,		/**< Disable ingress or egress mirroring on a port */
	NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG,	/**< Set the mirror analysis port for a direction */
	NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG,	/**< Clear the mirror analysis port for a direction */
	NSS_PPE_PORT_MIRROR_MAX_MSG_TYPES,	/**< Sentinel - must be last */
};

/**
 * nss_ppenl_port_mirror_rule_init()
 *	Initialise a port-mirror Netlink message (called from user space / ppecfg).
 *
 * @param rule[IN]  Pointer to the message structure to initialise.
 * @param type[IN]  Command type (one of nss_ppe_port_mirror_message_types).
 */
static inline void nss_ppenl_port_mirror_rule_init(struct nss_ppenl_port_mirror_rule *rule,
						    enum nss_ppe_port_mirror_message_types type)
{
	nss_ppenl_cmn_set_ver(&rule->cm, NSS_PPENL_VER);
	nss_ppenl_cmn_init_cmd(&rule->cm, sizeof(struct nss_ppenl_port_mirror_rule), type);
}

#endif /* __NSS_PPENL_PORT_MIRROR_IF_H */
