/*
 *******************************************************************************
 * Copyright (c) 2020, The Linux Foundation. All rights reserved.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ********************************************************************************
 **/
#include "nss_match_db.h"
#include "nss_match_priv.h"

#define MAX_DSCP 63
#define MAX_PRIORITY 7
#define NSS_MATCH_VOW_KEY_IFNUM_SHIFT 0
#define NSS_MATCH_VOW_KEY_DSCP_SHIFT 16
#define NSS_MATCH_VOW_KEY_INNER_8021P_SHIFT 22
#define NSS_MATCH_VOW_KEY_OUTER_8021P_SHIFT 25

/*
 * nss_match_vow_rule_id_generate()
 *	Check if any slot is available for new entry.
 */
static int nss_match_vow_rule_id_generate(struct nss_match_instance *db_instance)
{
	uint16_t index;

	for (index = 0; index < NSS_MATCH_INSTANCE_RULE_MAX; index++) {
		if (db_instance->rules.vow[index].valid_rule) {
			continue;
		}

		return (index + 1);
	}

	nss_match_warn("Rule table full with NSS_MATCH_INSTANCE_RULE_MAX:%d entries.\n",
			NSS_MATCH_INSTANCE_RULE_MAX);
	return -1;
}

/*
 * nss_match_vow_rule_find()
 * 	Check if rule exists already.
 */
static bool nss_match_vow_rule_find(struct nss_match_instance *db_instance, void *rule)
{
	uint16_t index;
	struct nss_match_rule_vow_msg *vow_rule = (struct nss_match_rule_vow_msg *)rule;

	/*
	 * Check if entry is present already.
	 */
	for (index = 0; index < NSS_MATCH_INSTANCE_RULE_MAX; index++) {
		struct nss_match_vow_rule_info rule_info = db_instance->rules.vow[index];

		if (rule_info.valid_rule &&
				rule_info.rule.if_num == vow_rule->if_num &&
				rule_info.rule.dscp == vow_rule->dscp &&
				rule_info.rule.inner_8021p == vow_rule->inner_8021p &&
				rule_info.rule.outer_8021p == vow_rule->outer_8021p &&
				rule_info.rule.mask_id == vow_rule->mask_id) {
			nss_match_info("Rule matched.\n");
			return true;
		}
	}

	return false;
}

/*
 * nss_match_vow_db_rule_add()
 * 	Store VoW rule information.
 */
static bool nss_match_vow_db_rule_add(struct nss_match_instance *db_instance, void *rule)
{
	struct nss_match_rule_vow_msg *vow_rule = (struct nss_match_rule_vow_msg *)rule;
	uint8_t rule_id = vow_rule->rule_id;

	if (rule_id == 0 || rule_id > NSS_MATCH_INSTANCE_RULE_MAX) {
		nss_match_warn("Invalid rule id: %d\n", rule_id);
		return false;
	}

	if (db_instance->rules.vow[rule_id - 1].valid_rule) {
		nss_match_warn("Rule exists for rule id: %d\n", rule_id);
		return false;
	}

	db_instance->rules.vow[rule_id - 1].rule.if_num = vow_rule->if_num;
	db_instance->rules.vow[rule_id - 1].rule.dscp = vow_rule->dscp;
	db_instance->rules.vow[rule_id - 1].rule.inner_8021p = vow_rule->inner_8021p;
	db_instance->rules.vow[rule_id - 1].rule.outer_8021p = vow_rule->outer_8021p;
	db_instance->rules.vow[rule_id - 1].rule.mask_id = vow_rule->mask_id;
	db_instance->rules.vow[rule_id - 1].rule.action.action_flag = vow_rule->action.action_flag;
	db_instance->rules.vow[rule_id - 1].rule.action.setprio = vow_rule->action.setprio;
	db_instance->rules.vow[rule_id - 1].rule.action.forward_ifnum = vow_rule->action.forward_ifnum;
	db_instance->rules.vow[rule_id - 1].valid_rule = true;
	db_instance->rules.vow[rule_id - 1].rule.rule_id = rule_id;
	db_instance->rule_count++;

	return true;
}

/*
 * nss_match_vow_db_rule_delete()
 *	Clears stored rule information.
 */
static bool nss_match_vow_db_rule_delete(struct nss_match_instance *db_instance, uint32_t rule_id)
{
	if (rule_id == 0 || rule_id > NSS_MATCH_INSTANCE_RULE_MAX) {
		nss_match_warn("Invalid rule id: %d\n", rule_id);
		return false;
	}

	if (!(db_instance->rules.vow[rule_id - 1].valid_rule)) {
		nss_match_warn("Rule does not exist for rule id: %d\n", rule_id);
		return false;
	}

	db_instance->rule_count--;
	db_instance->stats.hit_count[rule_id - 1] = 0;
	memset(db_instance->rules.vow + rule_id - 1, 0, sizeof(struct nss_match_vow_rule_info));

	return true;
}

/*
 * nss_match_vow_rule_read()
 *	Reads rule parameters by rule id.
 */
static bool nss_match_vow_rule_read(struct nss_match_instance *db_instance, struct nss_match_msg *rule, uint16_t rule_id)
{
	if (rule_id == 0 || rule_id > NSS_MATCH_INSTANCE_RULE_MAX) {
		nss_match_warn("Invalid rule id: %d\n", rule_id);
		return false;
	}

	if (!db_instance->rules.vow[rule_id - 1].valid_rule) {
		nss_match_warn("rule_id doesnot exist, rule_id = %d", rule_id);
		return false;
	}

	rule->msg.vow_rule.if_num = db_instance->rules.vow[rule_id - 1].rule.if_num;
	rule->msg.vow_rule.dscp = db_instance->rules.vow[rule_id - 1].rule.dscp;
	rule->msg.vow_rule.outer_8021p = db_instance->rules.vow[rule_id - 1].rule.outer_8021p;
	rule->msg.vow_rule.inner_8021p = db_instance->rules.vow[rule_id - 1].rule.inner_8021p;
	rule->msg.vow_rule.mask_id = db_instance->rules.vow[rule_id - 1].rule.mask_id;
	rule->msg.vow_rule.rule_id = db_instance->rules.vow[rule_id - 1].rule.rule_id;
	return true;
}

/*
 * nss_match_vow_cmd_parse()
 *	Adds new rules to the list
 */
static int nss_match_vow_cmd_parse(char *input_msg, struct nss_match_msg *rule_msg, nss_match_cmd_t type)
{
	char *token, *param, *value;
	struct nss_ctx_instance *nss_ctx = nss_match_get_context();
	int ret = 0;
	uint32_t actions = 0, if_num = 0, dscp = 0, outer_prio = 0, inner_prio = 0, setprio = 0, nexthop = 0;
	uint16_t mask_id = 0;
	uint32_t mask_val = 0;

	while (input_msg != NULL) {
		token = strsep(&input_msg, " ");
		param = strsep(&token, "=");
		value = token;
		if (!param || !value) {
			goto fail;
		}

		if (!(strncasecmp(param, "mask", strlen("mask")))) {
			if (!sscanf(value, "%hu", &mask_id)) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (mask_id > NSS_MATCH_MASK_MAX) {
				nss_match_warn("%p: Maskset num exceeds allowed value: %d\n", nss_ctx, mask_id);
				return -EINVAL;
			}

			continue;
		}

		if (!(strncasecmp(param, "ifname", strlen("ifname")))) {
			struct net_device *dev;

			if (type == NSS_MATCH_ADD_MASK) {
				if (!sscanf(value, "%x", &if_num)) {
					nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
					return -EINVAL;
				}
				continue;
			}

			if (type == NSS_MATCH_ADD_RULE) {
				dev = dev_get_by_name(&init_net, value);
				if (!dev) {
					nss_match_warn("%p: Cannot find the net device\n", nss_ctx);
					return -ENODEV;
				}

				if_num = nss_cmn_get_interface_number_by_dev(dev);
				dev_put(dev);
				continue;
			}
		}

		/*
		 * Parsing Dscp value from the message.
		 */
		if (!(strncasecmp(param, "dscp", strlen("dscp")))) {

			if (type == NSS_MATCH_ADD_RULE) {
				ret = sscanf(value, "%u", &dscp);
			} else if (type == NSS_MATCH_ADD_MASK) {
				ret = sscanf(value, "%x", &dscp);
			}

			if (!ret) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (dscp > MAX_DSCP) {
				nss_match_warn("%p: Dscp value %d cannot go beyong %d\n", nss_ctx, dscp, MAX_DSCP);
				return -EINVAL;
			}

			continue;
		}

		/*
		 * Parsing 8021.p from the message given by host.
		 */
		if (!(strncasecmp(param, "802.1p_inner", strlen("802.1p_inner")))) {
			if (type == NSS_MATCH_ADD_RULE) {
				ret = sscanf(value, "%u", &inner_prio);
			} else if (type == NSS_MATCH_ADD_MASK) {
				ret = sscanf(value, "%x", &inner_prio);
			}

			if (!ret) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input!!\n", nss_ctx);
				return -EINVAL;
			}

			if (inner_prio > MAX_PRIORITY) {
				nss_match_warn("%p: Priority %d value cannot go beyong 7\n", nss_ctx, inner_prio);
				return -EINVAL;
			}

			continue;
		}

		if (!(strncasecmp(param, "802.1p_outer", strlen("802.1p_outer")))) {

			if (type == NSS_MATCH_ADD_RULE) {
				ret = sscanf(value, "%u", &outer_prio);
			} else if (type == NSS_MATCH_ADD_MASK) {
				ret = sscanf(value, "%x", &outer_prio);
			}

			if (!ret) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (outer_prio > MAX_PRIORITY) {
				nss_match_warn("%p: vlan_priority = %d, priority value cannot go beyong 8"
					" 1 extra for wildcard\n", nss_ctx, outer_prio);
				return -EINVAL;
			}

			continue;
		}

		/*
		 * Parsing action from the message provided by user.
		 */
		if (!(strncasecmp(param, "action", strlen("action")))) {
			if (!sscanf(value, "%u", &actions)) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (actions >= NSS_MATCH_ACTION_MAX ) {
				nss_match_warn("Inavlid action type: %d, action type < %d is correct.",
						actions, NSS_MATCH_ACTION_MAX);
			}

			if (actions == 1 || actions == 3) {
				token = strsep(&input_msg, " ");
				param = strsep(&token, "=");
				value = token;
				if (!param || !value) {
					goto fail;
				}

				if (!(strncasecmp(param, "priority", strlen("priority")))) {
					if (!sscanf(value, "%u", &setprio)) {
						nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
						return -EINVAL;
					}

					if (setprio >= NSS_MAX_NUM_PRI) {
						nss_match_warn("Invalid priority: %d", setprio);
					}
				}
			}

			if (actions == 2 || actions == 3) {
				token = strsep(&input_msg, " ");
				param = strsep(&token, "=");
				value = token;
				if (!param || !value) {
					goto fail;
				}

				if (!(strncasecmp(param, "nexthop", strlen("nexthop")))) {
					if (!sscanf(value, "%u", &nexthop)) {
						nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
						return -EINVAL;
					}
				}
			}

			continue;
		}

		nss_match_warn("%p: Not a valid input\n", nss_ctx);
		return -EINVAL;
	}

	/*
	 * Verify 802.1 outer priority exists with inner priority.
	 */
	switch (type) {
	case NSS_MATCH_ADD_RULE:
		if (!mask_id || !actions) {
			goto fail;
		}

		if (!outer_prio && inner_prio) {
			nss_match_warn("802p_inner priority = %u can not exist without 802p_outer priority = %u", inner_prio, outer_prio);
			return -EINVAL;
		}

		rule_msg->msg.vow_rule.if_num = if_num;
		rule_msg->msg.vow_rule.dscp = dscp;
		rule_msg->msg.vow_rule.outer_8021p = outer_prio;
		rule_msg->msg.vow_rule.inner_8021p = inner_prio;
		rule_msg->msg.vow_rule.mask_id = mask_id;
		rule_msg->msg.vow_rule.action.setprio = setprio;
		rule_msg->msg.vow_rule.action.action_flag = actions;
		rule_msg->msg.vow_rule.action.forward_ifnum = nexthop;
		break;
	case NSS_MATCH_ADD_MASK:
		if (!mask_id) {
			goto fail;
		}

		mask_val = (if_num << NSS_MATCH_VOW_KEY_IFNUM_SHIFT);
	 	mask_val |= (dscp << NSS_MATCH_VOW_KEY_DSCP_SHIFT);
	 	mask_val |= (inner_prio << NSS_MATCH_VOW_KEY_INNER_8021P_SHIFT);
	 	mask_val |= (outer_prio << NSS_MATCH_VOW_KEY_OUTER_8021P_SHIFT);

		rule_msg->msg.configure_msg.profile_type = NSS_MATCH_PROFILE_TYPE_VOW;
		rule_msg->msg.configure_msg.maskset[mask_id-1][0] = mask_val;
		rule_msg->msg.configure_msg.valid_mask_flag = mask_id;
		break;
	default:
		nss_match_warn("Invalid parse type: %d", type);
		return -EINVAL;
	}

	return 0;

fail:
	pr_warn("Invalid input, Check help.(cat /sys/kernel/debug/match/help)");
	return -EINVAL;
}

/*
 * nss_match_vow_table_read()
 * 	Reads stats for VoW profile.
 */
static size_t nss_match_vow_table_read(struct nss_match_instance *db_instance, size_t buflen, char *bufp)
{
	int i, j;
	struct nss_ctx_instance *nss_ctx = nss_match_get_context();
	struct net_device *net_dev;
	size_t size_wr = 0;
	char *dev_name;

	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Match if_num = %d\n\n", db_instance->if_num);
	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Profile Type = %d\n\n", db_instance->profile_type);

	for (i = 0; i < NSS_MATCH_MASK_MAX; i++) {
		if (!(db_instance->valid_mask_flag & (1 << i))) {
			size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Mask %d = Invalid\n", i+1);
			continue;
		}

		size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Mask %d = %x\n", i+1, db_instance->maskset[i][0]);
	}

	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "rule_id\t hit_count\t mask_id\t if_name\t dscp\t outer 802.1p\t inner 802.1p\t action\t priority\t nexthop\n\n");
	for (j = 0; j < NSS_MATCH_INSTANCE_RULE_MAX; j++) {
		if (!db_instance->rules.vow[j].valid_rule)
			continue;

		dev_name = "N/A";
		net_dev = nss_cmn_get_interface_dev(nss_ctx, db_instance->rules.vow[j].rule.if_num);
		if (net_dev) {
			dev_name = net_dev->name;
		}

		size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "%d\t\t %llu\t\t %d\t\t %s\t\t %d\t\t %d\t\t %d\t\t %u\t\t %d\t\t %d\n",
				j + 1,
				db_instance->stats.hit_count[j],
				db_instance->rules.vow[j].rule.mask_id,
				dev_name,
				db_instance->rules.vow[j].rule.dscp,
				db_instance->rules.vow[j].rule.outer_8021p,
				db_instance->rules.vow[j].rule.inner_8021p,
				db_instance->rules.vow[j].rule.action.action_flag,
				db_instance->rules.vow[j].rule.action.setprio,
				db_instance->rules.vow[j].rule.action.forward_ifnum);
	}

	return size_wr;
}

/*
 * Match ops for VoW profile.
 */
static struct match_profile_ops match_profile_vow_ops = {
	nss_match_vow_rule_id_generate,
	nss_match_vow_rule_find,
	nss_match_vow_db_rule_add,
	nss_match_vow_db_rule_delete,
	nss_match_vow_rule_read,
	nss_match_vow_table_read,
	nss_match_vow_cmd_parse,
};

/*
 * nss_match_vow_init()
 * 	Initializes the VoW profile.
 */
void nss_match_vow_init(void) {

	/*
	 * Register the VoW profile ops.
	 */
	nss_match_profile_ops_register(NSS_MATCH_PROFILE_TYPE_VOW, &match_profile_vow_ops);
	nss_match_info("VoW profile initialization done\n");
}
