/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <ppe_drv/ppe_drv.h>
#include <fal_tunnel.h>
#include "ppe_drv_tun_tpr.h"
#include "sw_error.h"

/*
 * ppe_drv_tun_tpr_entry_exists
 *	Check if tpr tuple configuration exists.
 *
 * Note: This function does not check for the context type here and only 3/5 tuple information
 * 	 is checked to see if an entry already exists. The output of the function can be used
 * 	 to determine if a new instance is required to be allocated or existing entry can be
 * 	 re-used based on requirement
 */
struct ppe_drv_tun_tpr *ppe_drv_tun_tpr_entry_exists(struct ppe_drv *p, struct ppe_drv_tun_tpr_entry *tpr)
{
	uint8_t index = 0;
	struct ppe_drv_tun_tpr *tun_tpr;
	struct ppe_drv_tun_tpr_entry *t_tpre = NULL;

	if (!tpr) {
		ppe_drv_warn("%p: Invalid parameter: tpr is NULL", p);
		return NULL;
	}

	if (tpr->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4 &&
	    tpr->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
		ppe_drv_warn("%p: Invalid IP version: %d", tpr, tpr->ip_version);
		return NULL;
	}

	if (tpr->ctx_type != PPE_DRV_TUN_TPR_CONTEXT_TUNNEL &&
	    tpr->ctx_type != PPE_DRV_TUN_TPR_CONTEXT_TUPLE_ID) {
		ppe_drv_warn("%p: Invalid context type: %d", tpr, tpr->ctx_type);
		return NULL;
	}

	for (index = 0; index < PPE_DRV_TUN_TPR_ENTRY_MAX; index++) {
		tun_tpr = &p->tun_tpr[index];
		if (kref_read(&tun_tpr->ref)) {
			t_tpre = &tun_tpr->tpre;
			if (t_tpre->ip_version != tpr->ip_version) {
				continue;
			}

			if (t_tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4) {
				if (memcmp(t_tpre->src_ip, tpr->src_ip, sizeof(uint32_t)) ||
						memcmp(t_tpre->dest_ip, tpr->dest_ip, sizeof(uint32_t))) {
					continue;
				}
			} else if (t_tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
				if (memcmp(t_tpre->src_ip, tpr->src_ip, sizeof(tpr->src_ip)) ||
						memcmp(t_tpre->dest_ip, tpr->dest_ip, sizeof(tpr->dest_ip))) {
					continue;
				}
			}

			if ((t_tpre->src_port != tpr->src_port) || (t_tpre->dest_port != tpr->dest_port) ||
					(t_tpre->protocol != tpr->protocol) || (t_tpre->tpr_bitmap != tpr->tpr_bitmap)) {
				continue;
			}

			return tun_tpr;
		}
	}

	return NULL;
}

/*
 * ppe_drv_tun_tpr_dump_tuple_entry
 *	Dumps contents of fal_tunnel_tuple_entry_t
 */
static void ppe_drv_tun_tpr_dump_tuple_entry(struct ppe_drv_tun_tpr_entry *tpre, fal_tunnel_tuple_entry_t *tuple_entry)
{
	ppe_drv_trace("%p: IP ver:%d, Key bitmap:0x%x, L4 Proto:%d", tpre, tuple_entry->ip_ver, tuple_entry->key_bmp, tuple_entry->l4_proto);
	ppe_drv_trace("%p: sport:%d, dport:%d",  tpre, tuple_entry->sport, tuple_entry->dport);

	if (tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4) {
		ppe_drv_trace("%p: SIP: %pI4, DIP: %pI4", tpre, &tuple_entry->sip.ip4_addr, &tuple_entry->dip.ip4_addr);
	} else {
		ppe_drv_trace("%p: SIP: %pI6, DIP: %pI6", tpre, &tuple_entry->sip.ip6_addr, &tuple_entry->dip.ip6_addr);
	}

	if (tpre->ctx_type == PPE_DRV_TUN_TPR_CONTEXT_TUPLE_ID) {
		ppe_drv_trace("%p: Context Type: Tuple ID, Value:%d", tpre, tuple_entry->context.tuple_id);
	} else {
		ppe_drv_trace("%p: Context Type: Tunnel Type, Value:%d", tpre, tuple_entry->context.tunnel_type);
	}
}

/*
 * ppe_drv_tun_tpr_entry_configure
 *	Configure tunnel tpr entry
 */
bool ppe_drv_tun_tpr_entry_configure(struct ppe_drv_tun_tpr_entry *tpre)
{
	fal_tunnel_tuple_entry_t tuple_entry = {0};
	sw_error_t err;

	if (!tpre) {
		ppe_drv_warn("Invalid parameter: tpre is NULL\n");
		return false;
	}

	if (tpre->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4 &&
	    tpre->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
		ppe_drv_warn("%p: Invalid IP version: %d", tpre, tpre->ip_version);
		return false;
	}

	if (tpre->ctx_type != PPE_DRV_TUN_TPR_CONTEXT_TUNNEL &&
	    tpre->ctx_type != PPE_DRV_TUN_TPR_CONTEXT_TUPLE_ID) {
		ppe_drv_warn("%p: Invalid context type: %d", tpre, tpre->ctx_type);
		return false;
	}

	tuple_entry.ip_ver = tpre->ip_version;
	tuple_entry.key_bmp = tpre->tpr_bitmap;

	if (tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4) {
		tuple_entry.sip.ip4_addr = tpre->src_ip[0];
		tuple_entry.dip.ip4_addr = tpre->dest_ip[0];
	} else if (tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
		tuple_entry.sip.ip6_addr.ul[0] = tpre->src_ip[0];
		tuple_entry.sip.ip6_addr.ul[1] = tpre->src_ip[1];
		tuple_entry.sip.ip6_addr.ul[2] = tpre->src_ip[2];
		tuple_entry.sip.ip6_addr.ul[3] = tpre->src_ip[3];

		tuple_entry.dip.ip6_addr.ul[0] = tpre->dest_ip[0];
		tuple_entry.dip.ip6_addr.ul[1] = tpre->dest_ip[1];
		tuple_entry.dip.ip6_addr.ul[2] = tpre->dest_ip[2];
		tuple_entry.dip.ip6_addr.ul[3] = tpre->dest_ip[3];
	}

	tuple_entry.context_type = (fal_tunnel_tuple_context_type_t)tpre->ctx_type;
	if (tpre->ctx_type == PPE_DRV_TUN_TPR_CONTEXT_TUPLE_ID) {
		tuple_entry.context.tuple_id = tpre->tuple_id;
	} else {
		tuple_entry.context.tunnel_type = tpre->tunnel_type;
	}

	tuple_entry.sport = tpre->src_port;
	tuple_entry.dport = tpre->dest_port;
	tuple_entry.l4_proto = tpre->protocol;

	ppe_drv_tun_tpr_dump_tuple_entry(tpre, &tuple_entry);

	/*
	 * Configure TPR-TUPLE table to match 5 tuple information
	 */
	err = fal_tunnel_tuple_entry_add(PPE_DRV_SWITCH_ID, &tuple_entry);
	if (err != SW_OK) {
		ppe_drv_warn("%p: Tunnel TPR entry configuration failed with error %d", tpre, err);
		return false;
	}

	ppe_drv_trace("%p: TPR configuration successful", tpre);
	return true;
}

/*
 * ppe_drv_tun_tpr_entry_free
 *	free tunnel tpr entry
 */
static void ppe_drv_tun_tpr_entry_free(struct kref *kref)
{
	struct ppe_drv_tun_tpr  *tun_tpr = container_of(kref, struct ppe_drv_tun_tpr, ref);
	struct ppe_drv_tun_tpr_entry *tpre = &tun_tpr->tpre;
	fal_tunnel_tuple_entry_t tuple_entry = {0};
	sw_error_t err;

	if (tpre->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4 &&
			tpre->ip_version != PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
		ppe_drv_warn("%p: Invalid IP version: %d during TPR entry free", tpre, tpre->ip_version);
		return;
	}

	if (tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV4) {
		memcpy(&tuple_entry.sip.ip4_addr, tpre->src_ip, sizeof(uint32_t));
		memcpy(&tuple_entry.dip.ip4_addr, tpre->dest_ip, sizeof(uint32_t));
	} else if (tpre->ip_version == PPE_DRV_TUN_TPR_ENTRY_TYPE_IPV6) {
		memcpy(&tuple_entry.sip.ip6_addr, tpre->src_ip, sizeof(tpre->src_ip));
		memcpy(&tuple_entry.dip.ip6_addr, tpre->dest_ip, sizeof(tpre->dest_ip));
	}

	tuple_entry.context_type = (fal_tunnel_tuple_context_type_t)tpre->ctx_type;
	if (tpre->ctx_type == PPE_DRV_TUN_TPR_CONTEXT_TUPLE_ID) {
		tuple_entry.context.tuple_id = tpre->tuple_id;
	} else {
		tuple_entry.context.tunnel_type = tpre->tunnel_type;
	}

	tuple_entry.sport = tpre->src_port;
	tuple_entry.dport = tpre->dest_port;
	tuple_entry.l4_proto = tpre->protocol;

	ppe_drv_tun_tpr_dump_tuple_entry(tpre, &tuple_entry);

	err = fal_tunnel_tuple_entry_del(PPE_DRV_SWITCH_ID, &tuple_entry);
	if (err != SW_OK) {
		ppe_drv_warn("%p: Tunnel TPR entry deletion failed with error %d", tpre, err);
		return;
	}

	/*
	 * Reset the memory of the entry.
	 */
	memset(tpre, 0, sizeof(*tpre));
	ppe_drv_trace("%p: TPR[%d] freed successfully", tpre, tun_tpr->tpr_index);
}

/*
 * ppe_drv_tun_tpr_entry_deref
 *	deref tunnel tpr entry
 */
bool ppe_drv_tun_tpr_entry_deref(struct ppe_drv_tun_tpr  *tun_tpr)
{
	int index __maybe_unused;

	if (!tun_tpr) {
		ppe_drv_warn("Invalid parameter: tun_tpr is NULL");
		return false;
	}

	index = tun_tpr->tpr_index;

	ppe_drv_assert(kref_read(&tun_tpr->ref), "ref count under run for tpr index: %d", index);
	if (kref_put(&tun_tpr->ref, ppe_drv_tun_tpr_entry_free)) {
		ppe_drv_trace("%p: reference count is 0 for tpr index: %d",tun_tpr, index);
		return true;
	}
	ppe_drv_trace("%p: index: %u ref dnc:%u", tun_tpr, index, kref_read(&tun_tpr->ref));

	return true;
}

/*
 * ppe_drv_tun_tpr_entry_ref
 *	take ref on tunnel tpr entry
 */
void ppe_drv_tun_tpr_entry_ref(struct ppe_drv_tun_tpr  *tun_tpr)
{
	/*
	 * Input validation
	 */
	if (!tun_tpr) {
		ppe_drv_warn("Invalid parameter: tunnel tpr context is NULL");
		return;
	}

	kref_get(&tun_tpr->ref);

	ppe_drv_assert(kref_read(&tun_tpr->ref), "%p: ref count rollover for tpr index:%d", tun_tpr, tun_tpr->tpr_index);
	ppe_drv_trace("%p: idx: %u ref inc:%u", tun_tpr, tun_tpr->tpr_index, kref_read(&tun_tpr->ref));
}

/*
 * ppe_drv_tun_tpr_entry_alloc
 *	Return first free tunnel tpr entry
 *
 * Note: This function does not take into account of duplicate entries.
 * 	 Its expected that the user of the function needs to manage duplicate entries
 * 	 as required based on use case.
 */
struct ppe_drv_tun_tpr *ppe_drv_tun_tpr_entry_alloc(struct ppe_drv *p)
{
	uint8_t index = 0;
	struct ppe_drv_tun_tpr *tpr;

	/*
	 * Return first free instance
	 */
	for (index = 0; index < PPE_DRV_TUN_TPR_ENTRY_MAX; index++) {
		tpr = &p->tun_tpr[index];
		if (kref_read(&tpr->ref)) {
			continue;
		}

		kref_init(&tpr->ref);
		ppe_drv_trace("%p: Free tunnel tpr instance found, index: %d", tpr, index);
		return tpr;
	}

	ppe_drv_warn("%p: Free tunnel tpr instance is not found", p);
	return NULL;
}

/*
 * ppe_drv_tun_tpr_free
 *	free tunnel tpr entries
 */
void ppe_drv_tun_tpr_free(struct ppe_drv_tun_tpr *tun_tpr)
{
	vfree(tun_tpr);
}

/*
 * ppe_drv_tun_tpr_alloc
 *	Allocate and Initialize tunnel TPR entries
 */
struct ppe_drv_tun_tpr *ppe_drv_tun_tpr_alloc(struct ppe_drv *p)
{
	struct ppe_drv_tun_tpr *tpr;
	int index;

	ppe_drv_assert(!p->tun_tpr, "%p: tunnel tpr entries already allocated", p);

	tpr = vzalloc(sizeof(struct ppe_drv_tun_tpr) * PPE_DRV_TUN_TPR_ENTRY_MAX);
	if (!tpr) {
		ppe_drv_warn("%p: failed to allocate tunnel tpr entries", p);
		return NULL;
	}

	for (index = 0; index < PPE_DRV_TUN_TPR_ENTRY_MAX; index++) {
		tpr[index].tpr_index = index;

		/*
		 * Initialize the tuple id generated by the TPR tuple configurations
		 * to index value. Valid only in case the context is set to tuple id.
		 */
		tpr[index].tpre.tuple_id = index;

		/*
		 * For context being set to tunnel type. The value can be any valid TUPLE tunnel type.
		 * During initialization its configured to "FAL_TUNNEL_TYPE_INVALID_TUNNEL" to avoid
		 * initializing to tunnel type 0(ie. FAL_TUNNEL_TYPE_GRE_TAP_OVER_IPV4)
		 */
		tpr[index].tpre.tunnel_type = FAL_TUNNEL_TYPE_INVALID_TUNNEL;
	}

	nss_ppe_drv_minidump_log(tpr, sizeof(struct ppe_drv_tun_tpr) * PPE_DRV_TUN_TPR_ENTRY_MAX, "ppe_drv_tun_tpr");

	return tpr;
}
