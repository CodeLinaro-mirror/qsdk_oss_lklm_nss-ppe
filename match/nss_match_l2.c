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

#define MATCH_L2_KEY0_IFNUM_SHIFT 0
#define MATCH_L2_KEY0_DMAC_HW0_SHIFT 16
#define MATCH_L2_KEY1_DMAC_HW1_SHIFT 0
#define MATCH_L2_KEY1_DMAC_HW2_SHIFT 16
#define MATCH_L2_KEY2_SMAC_HW0_SHIFT 0
#define MATCH_L2_KEY2_SMAC_HW1_SHIFT 16
#define MATCH_L2_KEY3_SMAC_HW2_SHIFT 0
#define MATCH_L2_KEY3_ETHERTYPE_SHIFT 16

/*
 * nss_match_l2_rule_id_generate()
 *	Check if any slot is available for new entry.
 */
static int nss_match_l2_rule_id_generate(struct nss_match_instance *db_instance)
{
	uint16_t index;

	for (index = 0; index < NSS_MATCH_INSTANCE_RULE_MAX; index++) {
		if (db_instance->rules.l2[index].valid_rule) {
			continue;
		}

		return (index + 1);
	}

	nss_match_warn("Rule table full with NSS_MATCH_INSTANCE_RULE_MAX:%d entries.\n",
			NSS_MATCH_INSTANCE_RULE_MAX);
	return -1;
}

/*
 * nss_match_l2_rule_find()
 *	Check if any slot is available for new entry.
 */
static bool nss_match_l2_rule_find(struct nss_match_instance *db_instance, void *rule)
{
	uint16_t index;
	struct nss_match_rule_l2_msg *l2_rule = (struct nss_match_rule_l2_msg *)rule;

	/*
	 * Check if entry is duplicate.
	 */
	for (index = 0; index < NSS_MATCH_INSTANCE_RULE_MAX; index++) {
		struct nss_match_l2_rule_info rule_info = db_instance->rules.l2[index];

		if (rule_info.valid_rule &&
			rule_info.rule.if_num == l2_rule->if_num &&
			rule_info.rule.ethertype == l2_rule->ethertype &&
			rule_info.rule.smac[0] == l2_rule->smac[0] &&
			rule_info.rule.smac[1] == l2_rule->smac[1] &&
			rule_info.rule.smac[2] == l2_rule->smac[2] &&
			rule_info.rule.dmac[0] == l2_rule->dmac[0] &&
			rule_info.rule.dmac[1] == l2_rule->dmac[1] &&
			rule_info.rule.dmac[2] == l2_rule->dmac[2] &&
			rule_info.rule.mask_id == l2_rule->mask_id) {
			nss_match_info("Rule matched\n");
			return true;
		}
	}

	return false;
}

/*
 * nss_match_l2_db_rule_add()
 * 	Store L2 rule information.
 */
static bool nss_match_l2_db_rule_add(struct nss_match_instance *db_instance, void *rule)
{
	struct nss_match_rule_l2_msg *l2_rule = (struct nss_match_rule_l2_msg *)rule;
	uint8_t rule_id = l2_rule->rule_id;

	if (rule_id == 0 || rule_id > NSS_MATCH_INSTANCE_RULE_MAX) {
		nss_match_warn("Invalid rule id: %d\n", rule_id);
		return false;
	}

	if (db_instance->rules.l2[rule_id - 1].valid_rule) {
		nss_match_warn("Rule exists for rule id: %d\n", rule_id);
		return false;
	}

	db_instance->rules.l2[rule_id - 1].rule.if_num =  l2_rule->if_num;
	db_instance->rules.l2[rule_id - 1].rule.smac[0] = l2_rule->smac[0];
	db_instance->rules.l2[rule_id - 1].rule.smac[1] = l2_rule->smac[1];
	db_instance->rules.l2[rule_id - 1].rule.smac[2] = l2_rule->smac[2];
	db_instance->rules.l2[rule_id - 1].rule.dmac[0] = l2_rule->dmac[0];
	db_instance->rules.l2[rule_id - 1].rule.dmac[1] = l2_rule->dmac[1];
	db_instance->rules.l2[rule_id - 1].rule.dmac[2] = l2_rule->dmac[2];
	db_instance->rules.l2[rule_id - 1].rule.ethertype = l2_rule->ethertype;
	db_instance->rules.l2[rule_id - 1].rule.mask_id = l2_rule->mask_id;
	db_instance->rules.l2[rule_id - 1].rule.action.action_flag = l2_rule->action.action_flag;
	db_instance->rules.l2[rule_id - 1].rule.action.setprio = l2_rule->action.setprio;
	db_instance->rules.l2[rule_id - 1].rule.action.forward_ifnum = l2_rule->action.forward_ifnum;
	db_instance->rules.l2[rule_id - 1].valid_rule = true;
	db_instance->rules.l2[rule_id - 1].rule.rule_id = rule_id;
	db_instance->rule_count++;

	return true;
}

/*
 * nss_match_l2_db_rule_delete()
 *	Clears stored rule information.
 */
static bool nss_match_l2_db_rule_delete(struct nss_match_instance *db_instance, uint32_t rule_id)
{
	if (rule_id == 0 || rule_id > NSS_MATCH_INSTANCE_RULE_MAX) {
		nss_match_warn("Invalid rule id: %d\n", rule_id);
		return false;
	}

	if (!(db_instance->rules.l2[rule_id - 1].valid_rule)) {
		nss_match_warn("Rule dosn't exist for rule id: %d\n", rule_id);
		return false;
	}

	db_instance->rule_count--;
	db_instance->stats.hit_count[rule_id - 1] = 0;
	memset(db_instance->rules.l2 + rule_id - 1, 0, sizeof(struct nss_match_l2_rule_info));

	return true;
}

/*
 * nss_match_l2_rule_read()
 *	Reads rule parameters by rule id.
 */
static bool nss_match_l2_rule_read(struct nss_match_instance *db_instance, struct nss_match_msg *rule, uint16_t rule_id)
{
	if (!db_instance->rules.l2[rule_id - 1].valid_rule) {
		nss_match_warn("rule_id doesnot exist, rule_id = %d", rule_id);
		return false;
	}

	rule->msg.l2_rule.if_num = db_instance->rules.l2[rule_id - 1].rule.if_num;
	rule->msg.l2_rule.smac[0] = db_instance->rules.l2[rule_id - 1].rule.smac[0];
	rule->msg.l2_rule.smac[1] = db_instance->rules.l2[rule_id - 1].rule.smac[1];
	rule->msg.l2_rule.smac[2] = db_instance->rules.l2[rule_id - 1].rule.smac[2];
	rule->msg.l2_rule.dmac[0] = db_instance->rules.l2[rule_id - 1].rule.dmac[0];
	rule->msg.l2_rule.dmac[1] = db_instance->rules.l2[rule_id - 1].rule.dmac[1];
	rule->msg.l2_rule.dmac[2] = db_instance->rules.l2[rule_id - 1].rule.dmac[2];
	rule->msg.l2_rule.ethertype = db_instance->rules.l2[rule_id - 1].rule.ethertype;
	rule->msg.l2_rule.mask_id = db_instance->rules.l2[rule_id - 1].rule.mask_id;
	rule->msg.l2_rule.rule_id = db_instance->rules.l2[rule_id - 1].rule.rule_id;

	return true;
}

/*
 * nss_match_l2_cmd_parse()
 *	Adds new rules to the list
 */
static int nss_match_l2_cmd_parse(char *input_msg, struct nss_match_msg *rule_msg, nss_match_cmd_t type)
{
	char *token, *param;
	struct nss_ctx_instance *nss_ctx = nss_match_get_context();
	int ret = 0;
	uint32_t mask_val[4] = {0};
	uint32_t actions = 0, if_num = 0, setprio = 0, nexthop = 0;
	uint16_t smac[3] = {0}, dmac[3] = {0}, mask_id = 0, ethertype = 0;
	char tmp[4];

	while (input_msg != NULL) {
		token = strsep(&input_msg, " ");
		param = strsep(&token, "=");
		if (!param || !token) {
			goto fail;
		}

		if (!(strncasecmp(param, "mask", strlen("mask")))) {
			if (!sscanf(token, "%hu", &mask_id)) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (mask_id > NSS_MATCH_MASK_MAX) {
				nss_match_warn("%p: Maskset num %d, exceeds max allowed value %d\n", nss_ctx, mask_id, NSS_MATCH_MASK_MAX);
				return -EINVAL;
			}

			continue;
		}

		if (!(strncasecmp(param, "ifname", strlen("ifname")))) {
			struct net_device *dev;
			if (type == NSS_MATCH_ADD_MASK) {
				if (!sscanf(token, "%x", &if_num)) {
					nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
					return -EINVAL;
				}

				continue;
			}

			if (type == NSS_MATCH_ADD_RULE) {
				dev = dev_get_by_name(&init_net, token);
				if (!dev) {
					nss_match_warn("%p: Cannot find the net device\n", nss_ctx);
					return -ENODEV;
				}

				if_num = nss_cmn_get_interface_number_by_dev(dev);
				dev_put(dev);
				continue;
			}
		}

		if (!(strncasecmp(param, "smac", strlen("smac")))) {
			tmp[0] = token[0];
			tmp[1] = token[1];
			tmp[2] = token[2];
			tmp[3] = token[3];
			sscanf(tmp, "%hx", &smac[0]);

			tmp[0] = token[4];
			tmp[1] = token[5];
			tmp[2] = token[6];
			tmp[3] = token[7];
			sscanf(tmp, "%hx", &smac[1]);

			tmp[0] = token[8];
			tmp[1] = token[9];
			tmp[2] = token[10];
			tmp[3] = token[11];
			sscanf(tmp, "%hx", &smac[2]);

			nss_match_info("%p: src mac %x %x %x ", nss_ctx, smac[0], smac[1], smac[2]);
			continue;
		}

		if (!(strncasecmp(param, "dmac", strlen("dmac")))) {
			tmp[0] = token[0];
			tmp[1] = token[1];
			tmp[2] = token[2];
			tmp[3] = token[3];
			sscanf(tmp, "%hx", &dmac[0]);

			tmp[0] = token[4];
			tmp[1] = token[5];
			tmp[2] = token[6];
			tmp[3] = token[7];
			sscanf(tmp, "%hx", &dmac[1]);

			tmp[0] = token[8];
			tmp[1] = token[9];
			tmp[2] = token[10];
			tmp[3] = token[11];
			sscanf(tmp, "%hx", &dmac[2]);

			nss_match_info("%p: dest mac %x %x %x ", nss_ctx, dmac[0], dmac[1], dmac[2]);
			continue;
		}

		if (!(strncasecmp(param, "ethertype", strlen("ethertype")))) {
			if (type == NSS_MATCH_ADD_RULE) {
				ret = sscanf(token, "%hu", &ethertype);
			} else if (type == NSS_MATCH_ADD_MASK) {
				ret = sscanf(token, "%hx", &ethertype);
			}

			if (!ret) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			continue;
		}

		/*
		 * Parsing action from the message provided by user.
		 */
		if (!(strncasecmp(param, "action", strlen("action")))) {
			if (!sscanf(token, "%u", &actions)) {
				nss_match_warn("%p: Cannot convert to integer. Wrong input\n", nss_ctx);
				return -EINVAL;
			}

			if (actions >= NSS_MATCH_ACTION_MAX ) {
				nss_match_warn("Invalid action type: %d", actions);
			}

			if (actions == 1 || actions == 3) {
				token = strsep(&input_msg, " ");
				param = strsep(&token, "=");
				if (!token || !param) {
					goto fail;
				}

				if (!(strncasecmp(param, "priority", strlen("priority")))) {
					if (!sscanf(token, "%u", &setprio)) {
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
				if (!token || !param) {
					goto fail;
				}

				if (!(strncasecmp(param, "nexthop", strlen("nexthop")))) {
					if (!sscanf(token, "%u", &nexthop)) {
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

	switch (type) {
	case NSS_MATCH_ADD_RULE:
		if (!mask_id || !actions) {
			goto fail;
		}

		rule_msg->msg.l2_rule.if_num = if_num;
		rule_msg->msg.l2_rule.smac[0] = smac[0];
		rule_msg->msg.l2_rule.smac[1] = smac[1];
		rule_msg->msg.l2_rule.smac[2] = smac[2];
		rule_msg->msg.l2_rule.dmac[0] = dmac[0];
		rule_msg->msg.l2_rule.dmac[1] = dmac[1];
		rule_msg->msg.l2_rule.dmac[2] = dmac[2];
		rule_msg->msg.l2_rule.ethertype = ethertype;
		rule_msg->msg.l2_rule.mask_id = mask_id;
		rule_msg->msg.l2_rule.action.setprio = setprio;
		rule_msg->msg.l2_rule.action.action_flag = actions;
		rule_msg->msg.l2_rule.action.forward_ifnum = nexthop;
		break;
	case NSS_MATCH_ADD_MASK:
		mask_val[0] = (if_num << MATCH_L2_KEY0_IFNUM_SHIFT);
		mask_val[0] |= (dmac[0] << MATCH_L2_KEY0_DMAC_HW0_SHIFT);
		mask_val[1] = (dmac[1] << MATCH_L2_KEY1_DMAC_HW1_SHIFT);
		mask_val[1] |= (dmac[2] << MATCH_L2_KEY1_DMAC_HW2_SHIFT);
		mask_val[2] = (smac[0] << MATCH_L2_KEY2_SMAC_HW0_SHIFT);
		mask_val[2] |= (smac[1] << MATCH_L2_KEY2_SMAC_HW1_SHIFT);
		mask_val[3] = (smac[2] << MATCH_L2_KEY3_SMAC_HW2_SHIFT);
		mask_val[3] |= (ethertype << MATCH_L2_KEY3_ETHERTYPE_SHIFT);

		rule_msg->msg.configure_msg.maskset[mask_id-1][0] = mask_val[0];
		rule_msg->msg.configure_msg.maskset[mask_id-1][1] = mask_val[1];
		rule_msg->msg.configure_msg.maskset[mask_id-1][2] = mask_val[2];
		rule_msg->msg.configure_msg.maskset[mask_id-1][3] = mask_val[3];
		rule_msg->msg.configure_msg.valid_mask_flag = mask_id;
		break;
	default:
		nss_match_warn("Invalid parse type: %d", type);
		return -EINVAL;
	}

	return 0;

fail:
	pr_warn("Invalid input, Check help: cat /sys/kernel/debug/match/help");
	return -EINVAL;
}

/*
 * nss_match_l2_table_read()
 * 	Reads stats for l2 profile.
 */
static size_t nss_match_l2_table_read(struct nss_match_instance *db_instance, size_t buflen, char *bufp) {
	int i, j;
	struct nss_ctx_instance *nss_ctx = nss_match_get_context();
	struct net_device *net_dev;
	size_t size_wr = 0;
	char *dev_name;

	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "\nMatch if_num = %d\n", db_instance->if_num);
	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "\nProfile Type = %d\n", db_instance->profile_type);

	for (i = 0; i < NSS_MATCH_MASK_MAX; i++) {
		if (!(db_instance->valid_mask_flag & (1 << i))) {
			size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Mask %d = Invalid\n\n", i+1);
			continue;
		}

		size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "Mask %d =%x %x %x %x \n\n", i+1, db_instance->maskset[i][3],
				db_instance->maskset[i][2], db_instance->maskset[i][1], db_instance->maskset[i][0]);
	}

	size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "rule_id\t\t hit_count\t\t mask_id\t\t if_name\t\t DMAC \t\t\t SMAC \t\t\t ethertype\t\t action\t\t priority\t\t nexthop\n");
	for (j = 0; j < NSS_MATCH_INSTANCE_RULE_MAX; j++) {
		if (!db_instance->rules.l2[j].valid_rule)
			continue;

		dev_name = "N/A";
		net_dev = nss_cmn_get_interface_dev(nss_ctx, db_instance->rules.l2[j].rule.if_num);
		if (net_dev) {
			dev_name = net_dev->name;
		}

		size_wr += scnprintf(bufp + size_wr, buflen - size_wr, "%d\t\t %llu\t\t %d\t\t %s\t\t%pM  %pM\t\t %d\t\t %u\t\t %d\t\t %d\n",
				j + 1,
				db_instance->stats.hit_count[j],
				db_instance->rules.l2[j].rule.mask_id,
				dev_name,
				&db_instance->rules.l2[j].rule.dmac,
				&db_instance->rules.l2[j].rule.smac,
				db_instance->rules.l2[j].rule.ethertype,
				db_instance->rules.l2[j].rule.action.action_flag,
				db_instance->rules.l2[j].rule.action.setprio,
				db_instance->rules.l2[j].rule.action.forward_ifnum);
	}

	return size_wr;
}

/*
 * Match ops for L2 profile.
 */
static struct match_profile_ops match_profile_ops_l2 = {
	nss_match_l2_rule_id_generate,
	nss_match_l2_rule_find,
	nss_match_l2_db_rule_add,
	nss_match_l2_db_rule_delete,
	nss_match_l2_rule_read,
	nss_match_l2_table_read,
	nss_match_l2_cmd_parse,
};


void nss_match_l2_init(void) {
	/*
	 * Register the L2 profile ops.
	 */
	nss_match_profile_ops_register(NSS_MATCH_PROFILE_TYPE_L2, &match_profile_ops_l2);
	nss_match_info("L2 profile initialization done\n");
}
