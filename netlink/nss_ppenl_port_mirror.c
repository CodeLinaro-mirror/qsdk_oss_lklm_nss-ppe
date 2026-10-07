/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

/*
 * nss_ppenl_port_mirror.c
 *	NSS Netlink port-mirror handler.
 *
 * Exposes the "nss_ppenl_pmirr" Generic Netlink family so that user-space
 * tools (ppecfg) can configure port mirroring on physical or virtual ports.
 *
 * Supported operations
 * --------------------
 * NSS_PPE_PORT_MIRROR_ENABLE_MSG
 *   Enable ingress / egress L2 VP mirroring on a named port via
 *   ppe_mirror_port_mirror_set().
 *
 * NSS_PPE_PORT_MIRROR_DISABLE_MSG
 *   Disable ingress / egress L2 VP mirroring on a named port via
 *   ppe_mirror_port_mirror_set().
 *
 * NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG
 *   Set the mirror analysis (capture) port for a given direction via
 *   ppe_drv_dp_set_mirr_analysis_port().
 *
 * NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG
 *   Clear (disable) the mirror analysis port for a given direction via
 *   ppe_drv_dp_set_mirr_analysis_port() with enable=false.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/netlink.h>

#include <net/genetlink.h>
#include <net/sock.h>

#include <nss_ppenl_cmn_if.h>
#include <nss_ppenl_port_mirror_if.h>
#include "nss_ppenl.h"
#include "nss_ppenl_port_mirror.h"

#include <ppe_mirror.h>

/*
 * Forward declarations
 */
static int nss_ppenl_port_mirror_ops_enable(struct sk_buff *skb, struct genl_info *info);
static int nss_ppenl_port_mirror_ops_disable(struct sk_buff *skb, struct genl_info *info);
static int nss_ppenl_port_mirror_ops_set_analyst_port(struct sk_buff *skb, struct genl_info *info);
static int nss_ppenl_port_mirror_ops_clr_analyst_port(struct sk_buff *skb, struct genl_info *info);

/*
 * Operation table – one entry per message type.
 */
static struct genl_ops nss_ppenl_port_mirror_ops[] = {
	{
		.cmd  = NSS_PPE_PORT_MIRROR_ENABLE_MSG,
		.doit = nss_ppenl_port_mirror_ops_enable,
	},
	{
		.cmd  = NSS_PPE_PORT_MIRROR_DISABLE_MSG,
		.doit = nss_ppenl_port_mirror_ops_disable,
	},
	{
		.cmd  = NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG,
		.doit = nss_ppenl_port_mirror_ops_set_analyst_port,
	},
	{
		.cmd  = NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG,
		.doit = nss_ppenl_port_mirror_ops_clr_analyst_port,
	},
};

/*
 * Generic Netlink family definition for port-mirror.
 */
static struct genl_family nss_ppenl_port_mirror_family = {
	.name    = NSS_PPENL_PORT_MIRROR_FAMILY,
	.hdrsize = sizeof(struct nss_ppenl_port_mirror_rule),
	.version = NSS_PPENL_VER,
	.maxattr = NSS_PPE_PORT_MIRROR_MAX_MSG_TYPES,
	.netnsok = true,
	.pre_doit  = NULL,
	.post_doit = NULL,
	.ops   = nss_ppenl_port_mirror_ops,
	.n_ops = ARRAY_SIZE(nss_ppenl_port_mirror_ops),
};

/*
 * nss_ppenl_port_mirror_ops_enable()
 *	Handle NSS_PPE_PORT_MIRROR_ENABLE_MSG – enable ingress/egress
 *	L2 VP mirroring on a named port.
 */
static int nss_ppenl_port_mirror_ops_enable(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_ppenl_port_mirror_rule *nl_rule;
	struct nss_ppenl_port_mirror_cfg  *cfg;
	struct nss_ppenl_cmn *nl_cm;
	struct sk_buff *resp;
	ppe_mirror_ret_t ret;
	uint32_t pid;

	nl_cm = nss_ppenl_get_msg(&nss_ppenl_port_mirror_family, info,
				  NSS_PPE_PORT_MIRROR_ENABLE_MSG);
	if (!nl_cm) {
		nss_ppenl_warn("unable to extract port-mirror cfg message\n");
		nss_ppenl_ucast_resp(skb);
		return -EINVAL;
	}

	nl_rule = container_of(nl_cm, struct nss_ppenl_port_mirror_rule, cm);
	cfg     = &nl_rule->cfg;
	pid     = nl_cm->pid;

	cfg->port_name[IFNAMSIZ - 1] = '\0';

	nss_ppenl_info("%s: pid %u port '%s' dir %u enable mirror\n",
		       __func__, pid, cfg->port_name, cfg->dir);

	resp = nss_ppenl_copy_msg(skb);
	if (!resp) {
		nss_ppenl_warn("%u: unable to copy NL message for response\n", pid);
		nss_ppenl_ucast_resp(skb);
		return -ENOMEM;
	}

	ret = ppe_mirror_port_mirror_set(cfg->port_name,
					 (enum ppe_mirror_port_dir)cfg->dir,
					 true);
	if (ret != PPE_MIRROR_RET_SUCCESS) {
		nss_ppenl_warn("%u: ppe_mirror_port_mirror_set failed for '%s' (ret %d)\n",
			       pid, cfg->port_name, ret);
		cfg->ret = -EIO;
		goto send_resp;
	}

	nss_ppenl_info("%u: mirror dir=%u enabled on port '%s' OK\n",
		       pid, cfg->dir, cfg->port_name);
	cfg->ret = 0;

send_resp:
	nl_rule = nss_ppenl_get_data(resp);
	nl_rule->cfg.ret = cfg->ret;
	nss_ppenl_ucast_resp(resp);
	return 0;
}

/*
 * nss_ppenl_port_mirror_ops_disable()
 *	Handle NSS_PPE_PORT_MIRROR_DISABLE_MSG – disable ingress/egress
 *	L2 VP mirroring on a named port.
 */
static int nss_ppenl_port_mirror_ops_disable(struct sk_buff *skb, struct genl_info *info)
{
	struct nss_ppenl_port_mirror_rule *nl_rule;
	struct nss_ppenl_port_mirror_cfg  *cfg;
	struct nss_ppenl_cmn *nl_cm;
	struct sk_buff *resp;
	ppe_mirror_ret_t ret;
	uint32_t pid;

	nl_cm = nss_ppenl_get_msg(&nss_ppenl_port_mirror_family, info,
				  NSS_PPE_PORT_MIRROR_DISABLE_MSG);
	if (!nl_cm) {
		nss_ppenl_warn("unable to extract port-mirror cfg message\n");
		nss_ppenl_ucast_resp(skb);
		return -EINVAL;
	}

	nl_rule = container_of(nl_cm, struct nss_ppenl_port_mirror_rule, cm);
	cfg     = &nl_rule->cfg;
	pid     = nl_cm->pid;

	cfg->port_name[IFNAMSIZ - 1] = '\0';

	nss_ppenl_info("%s: pid %u port '%s' dir %u disable mirror\n",
		       __func__, pid, cfg->port_name, cfg->dir);

	resp = nss_ppenl_copy_msg(skb);
	if (!resp) {
		nss_ppenl_warn("%u: unable to copy NL message for response\n", pid);
		nss_ppenl_ucast_resp(skb);
		return -ENOMEM;
	}

	ret = ppe_mirror_port_mirror_set(cfg->port_name,
					 (enum ppe_mirror_port_dir)cfg->dir,
					 false);
	if (ret != PPE_MIRROR_RET_SUCCESS) {
		nss_ppenl_warn("%u: ppe_mirror_port_mirror_set failed for '%s' (ret %d)\n",
			       pid, cfg->port_name, ret);
		cfg->ret = -EIO;
		goto send_resp;
	}

	nss_ppenl_info("%u: mirror dir=%u disabled on port '%s' OK\n",
		       pid, cfg->dir, cfg->port_name);
	cfg->ret = 0;

send_resp:
	nl_rule = nss_ppenl_get_data(resp);
	nl_rule->cfg.ret = cfg->ret;
	nss_ppenl_ucast_resp(resp);
	return 0;
}

/*
 * nss_ppenl_port_mirror_analyst_configure()
 *	Common helper: set or clear the mirror analysis port for a direction.
 *
 * @skb:    Incoming socket buffer.
 * @info:   Generic Netlink info.
 * @enable: true to set the analyst port, false to clear it.
 * @cmd:    Message type (SET_ANALYST_PORT_MSG or CLR_ANALYST_PORT_MSG).
 */
static int nss_ppenl_port_mirror_analyst_configure(struct sk_buff *skb,
						   struct genl_info *info,
						   bool enable,
						   enum nss_ppe_port_mirror_message_types cmd)
{
	struct nss_ppenl_port_mirror_rule    *nl_rule;
	struct nss_ppenl_port_mirror_analyst *analyst;
	struct nss_ppenl_cmn *nl_cm;
	ppe_mirror_ret_t ret;
	struct sk_buff *resp;
	uint32_t pid;

	nl_cm = nss_ppenl_get_msg(&nss_ppenl_port_mirror_family, info, cmd);
	if (!nl_cm) {
		nss_ppenl_warn("unable to extract port-mirror analyst message\n");
		nss_ppenl_ucast_resp(skb);
		return -EINVAL;
	}

	nl_rule = container_of(nl_cm, struct nss_ppenl_port_mirror_rule, cm);
	analyst = &nl_rule->analyst;
	pid     = nl_cm->pid;

	analyst->analyst_port_name[IFNAMSIZ - 1] = '\0';

	nss_ppenl_info("%s: pid %u analyst_port '%s' dir %u %s priority %u\n",
		       __func__, pid, analyst->analyst_port_name,
		       analyst->dir, enable ? "set" : "clear", analyst->priority);

	resp = nss_ppenl_copy_msg(skb);
	if (!resp) {
		nss_ppenl_warn("%u: unable to copy NL message for response\n", pid);
		nss_ppenl_ucast_resp(skb);
		return -ENOMEM;
	}

	ret = ppe_mirror_analyst_port_set(analyst->analyst_port_name,
					  (enum ppe_mirror_port_dir)analyst->dir,
					  enable, analyst->priority);
	if (ret != PPE_MIRROR_RET_SUCCESS) {
		nss_ppenl_warn("%u: ppe_mirror_analyst_port_set failed for '%s' (ret %d)\n",
			       pid, analyst->analyst_port_name, ret);
		analyst->ret = -EIO;
		goto send_resp;
	}

	nss_ppenl_info("%u: analyst port '%s' dir=%u %s OK\n",
		       pid, analyst->analyst_port_name, analyst->dir,
		       enable ? "set" : "cleared");
	analyst->ret = 0;

send_resp:
	nl_rule = nss_ppenl_get_data(resp);
	nl_rule->analyst.ret = analyst->ret;
	nss_ppenl_ucast_resp(resp);
	return 0;
}

/*
 * nss_ppenl_port_mirror_ops_set_analyst_port()
 *	Handle NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG – set the analysis port.
 */
static int nss_ppenl_port_mirror_ops_set_analyst_port(struct sk_buff *skb, struct genl_info *info)
{
	return nss_ppenl_port_mirror_analyst_configure(skb, info, true,
						       NSS_PPE_PORT_MIRROR_SET_ANALYST_PORT_MSG);
}

/*
 * nss_ppenl_port_mirror_ops_clr_analyst_port()
 *	Handle NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG – clear the analysis port.
 */
static int nss_ppenl_port_mirror_ops_clr_analyst_port(struct sk_buff *skb, struct genl_info *info)
{
	return nss_ppenl_port_mirror_analyst_configure(skb, info, false,
						       NSS_PPE_PORT_MIRROR_CLR_ANALYST_PORT_MSG);
}

/*
 * nss_ppenl_port_mirror_init()
 *	Register the port-mirror Generic Netlink family.
 */
bool nss_ppenl_port_mirror_init(void)
{
	int error;

	nss_ppenl_info("Init NSS PPE port-mirror handler\n");

	error = genl_register_family(&nss_ppenl_port_mirror_family);
	if (error != 0) {
		nss_ppenl_info_always("Error: unable to register port-mirror family\n");
		return false;
	}

	nss_ppenl_info("port-mirror netlink msg size = %zu\n",
		       sizeof(struct nss_ppenl_port_mirror_rule));
	return true;
}
EXPORT_SYMBOL(nss_ppenl_port_mirror_init);

/*
 * nss_ppenl_port_mirror_exit()
 *	Unregister the port-mirror Generic Netlink family.
 */
bool nss_ppenl_port_mirror_exit(void)
{
	int error;

	nss_ppenl_info("Exit NSS PPE port-mirror handler\n");

	error = genl_unregister_family(&nss_ppenl_port_mirror_family);
	if (error != 0) {
		nss_ppenl_info_always("unable to unregister port-mirror NETLINK family\n");
		return false;
	}

	return true;
}
EXPORT_SYMBOL(nss_ppenl_port_mirror_exit);
