/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_net.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/vmalloc.h>
#include <linux/debugfs.h>
#include <linux/module.h>
#include <linux/version.h>
#include <fal/fal_rss_hash.h>
#include <fal/fal_ip.h>
#include <fal/fal_init.h>
#include <fal/fal_qm.h>
#include <fal/fal_servcode.h>
#include <fal/fal_fdb.h>
#include <fal/fal_portvlan.h>
#ifdef NSS_PPE_PON_SUPPORT
#include <fal/fal_pon.h>
#endif
#include <fal/fal_qos.h>
#include <fal/fal_flow.h>
#ifdef PPE_DRV_PKT_PADDING_STRIP
#include <fal/fal_pktedit.h>
#endif
#include "ppe_drv.h"
#include "tun/ppe_drv_tun.h"
#include <nss_debug.h>

#define PPE_DRV_STATIC_DBG_LEVEL_STR_LEN 8
#define PPE_DRV_UPSTREAM_DEV_LEVEL_STR_LEN 32
#define PPE_DRV_SRC2UNI_LEVEL_STR_LEN 128
#define NSS_PPE_DRV_WHITESPACE		" \t\v\f\n,"
#ifdef PPE_DRV_PKT_PADDING_STRIP
#define PPE_DRV_PACKET_PADDING_STR_LEN 128
#endif

#define PPE_DRV_RFS_COREMASK_MIN	1
#define PPE_DRV_RFS_COREMASK_MAX	((1 << NR_CPUS) - 1)

#ifdef NSS_PPE_DRV_HW_GRO
#define PPE_DRV_GRO_COREMASK_MIN	1
#define PPE_DRV_GRO_COREMASK_MAX	((1 << NR_CPUS) - 1)
#endif

/*
 * Module parameter to enable/disable 2-tuple RSS hash for IP fragments.
 */
static bool ipfrag_2tuple_hash = true;
module_param(ipfrag_2tuple_hash, bool, 0644);
MODULE_PARM_DESC(ipfrag_2tuple_hash, "RSS hash for IP fragments based on SIP & DIP");

/*
 * Module parameter to enable/disable Flush & Deacceleration.
 */
bool flow_deacclr_dis = false;
module_param(flow_deacclr_dis, bool, 0644);
MODULE_PARM_DESC(flow_deacclr_dis, "Disable Flow deacceleration & Flush on Exception");

/*
 * Module parameter to enable/disable passive VP creation for SFE flows.
 */
static bool passive_vp_enable = false;
MODULE_PARM_DESC(passive_vp_enable, "Passive VP creation enable/disable");

/*
 * Module parameter to set eth coremask.
 */
static unsigned int eth_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
MODULE_PARM_DESC(eth_coremask, "Coremask for Ethernet to Ethernet Flows");

#ifdef NSS_PPE_DRV_HW_GRO
/*
 * Module parameter to set eth coremask.
 */
static unsigned int eth_gro_coremask = PPE_DRV_GRO_COREMASK_DEFAULT;
MODULE_PARM_DESC(eth_gro_coremask, "Coremask for Ethernet GRO Flows");
#endif

/*
 * Module parameter to set wlan common coremask.
 */
static unsigned int wlan_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
MODULE_PARM_DESC(wlan_coremask, "Coremask for Ethernet to WLAN Flows");

/*
 * Module parameter to set coremask for tunnel, etc.
 */
static unsigned int tun_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
MODULE_PARM_DESC(tun_coremask, "Coremask for Ethernet to Tunnel, etc Flows");

uint32_t if_bm_to_offload;
#ifdef NSS_PPE_PON_SUPPORT
uint32_t gem_port_bitmap;

/*
 * Module parameter to set max number of PQ needed for PON flows.
 */
int pon_max_pq = 110;
module_param(pon_max_pq, int, 0644);
MODULE_PARM_DESC(pon_max_pq, "Max number of PON PQs needed by OMCI");
#endif

int disable_port_mtu_check = true;
uint32_t static_dbg_level = 0;
static char static_dbg_level_str[PPE_DRV_STATIC_DBG_LEVEL_STR_LEN];
uint8_t ppe_drv_redir_prio_map[PPE_DRV_MAX_PRIORITY] = {0, 1, 2, 3, 4, 5, 6, 7, 7, 7, 7, 7, 7, 7, 7, 7};
#ifdef NSS_PPE_DRV_HW_GRO
uint8_t ppe_drv_gro_prio_map[PPE_DRV_MAX_PRIORITY] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
#endif
uint8_t ppe_drv_16_prio_map[PPE_DRV_MAX_PRIORITY] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static int eth2eth_offload_if_bitmap;
static char upstream_dev_str[PPE_DRV_UPSTREAM_DEV_LEVEL_STR_LEN];
static char src2uni_map[PPE_DRV_SRC2UNI_LEVEL_STR_LEN];
#ifdef PPE_DRV_PKT_PADDING_STRIP
static char packet_padding[PPE_DRV_PACKET_PADDING_STR_LEN];
#endif

int l4_checksum_exception_enable = false;
int mac_lrn_exception_en = true;

#ifdef PPE_DRV_ESP_SPI_PASSTH_ENABLE
int ppe_drv_ipsec_passth_en = 0;
#endif

#if defined(PPE_LOOPBACK_PORT_SUPPORT)
static uint32_t loopback_port_ft_type;
#endif

/*
 * Define the filename to be used for assertions.
 */
struct ppe_drv *ppe_drv_gbl = NULL;

/*
 * ppe_drv_get_vxlan_dport()
 *	Get the VXLAN destination port.
 */
int ppe_drv_get_vxlan_dport(void)
{
	return ppe_drv_gbl->vxlan_dport;
}

/*
 * ppe_drv_get_vxlan_gpe_dport()
 *	Get the VXLAN-GPE destination port.
 */
int ppe_drv_get_vxlan_gpe_dport(void)
{
	return ppe_drv_gbl->vxlan_gpe_dport;
}

#ifdef NSS_PPE_EXT_VLAN_FEATURE_SUPPORT
/*
 * ppe_drv_get_tpid_index()
 *      Get PPE VLAN TPID indexes by TPID values.
 */
int ppe_drv_get_tpid_index(uint16_t tpid, const char *type)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int index = -1;

	spin_lock_bh(&p->lock);

	if (strcmp(type, "ctpid") == 0) {
		if (tpid == p->gbl_ctpid)
			index = PPE_DRV_VLAN_CTPID_IDX;
		if (tpid == p->gbl_ctpid_ext)
			index = PPE_DRV_VLAN_CTPID_EXT_IDX;
	} else if (strcmp(type, "stpid") == 0) {
		if (tpid == p->gbl_stpid)
			index = PPE_DRV_VLAN_STPID_IDX;
		if (tpid == p->gbl_stpid_ext)
			index = PPE_DRV_VLAN_STPID_EXT_IDX;
	}

	spin_unlock_bh(&p->lock);
	return index;
}
EXPORT_SYMBOL(ppe_drv_get_tpid_index);

/*
 * ppe_drv_get_vsi_num()
 *	Get VSI number.
 */
int ppe_drv_get_vsi_num(void)
{
	return ppe_drv_gbl->vsi_num;
}
EXPORT_SYMBOL(ppe_drv_get_vsi_num);
#endif

/*
 * ppe_drv_is_mht_dev()
 *	API to get MHT switch interface flag
 */
bool ppe_drv_is_mht_dev(struct net_device *dev)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_iface *iface = NULL;

	spin_lock_bh(&p->lock);
	iface = ppe_drv_iface_get_by_dev_internal(dev);
	if (!iface) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: No valid PPE interface for dev", dev);
		return false;
	}

	/*
	 * Get the MHT switch flag on the interface.
	 */
	if (!(iface->flags & PPE_DRV_IFACE_FLAG_MHT_SWITCH_VALID)) {
		spin_unlock_bh(&p->lock);
		return false;
	}
	spin_unlock_bh(&p->lock);
	return true;
}
EXPORT_SYMBOL(ppe_drv_is_mht_dev);

#ifdef PPE_DRV_DDRQ_ENABLE
/*
 * ppe_drv_isram_queue_profile_init()
 *	API to enable ISRAM queue for all the PPE ports
 */
bool ppe_drv_isram_queue_profile_init(uint8_t isram_qbase)
{
	int i;
	sw_error_t err;
	struct ppe_drv *p = ppe_drv_gbl;
	 struct ppe_drv_port *pp_local;
	fal_ucast_queue_dest_t queue_dst = {0};

	queue_dst.sram_queue_type = FAL_ISRAM_QUEUE;
	queue_dst.src_profile = PPE_DRV_PORT_SRC_PROFILE;

	/*
	 * Run the loop for PPE ports
	 * Currently assigning the same ISQ base for all the PPE ports.
	 */
	spin_lock_bh(&p->lock);
	for (i = 1; i < PPE_DRV_PORTS_MAX ; i++) {
		pp_local = &p->port[i];
		queue_dst.dst_port = pp_local->port;
		err = fal_ucast_queue_base_profile_set(PPE_DRV_SWITCH_ID, &queue_dst, isram_qbase, PPE_DRV_REDIR_PROFILE_ID);
		if (err != SW_OK) {
			spin_unlock_bh(&p->lock);
			ppe_drv_warn("%p: Unable to set queue base for %d port\n", p, queue_dst.dst_port);
			return false;
		}
	}
	spin_unlock_bh(&p->lock);

	return true;
}
EXPORT_SYMBOL(ppe_drv_isram_queue_profile_init);
#endif

/*
 * ppe_drv_hw_stats_sync()
 *	Sync PPE HW stats
 */
static void ppe_drv_hw_stats_sync(struct timer_list *tm)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_v4_conn *cn_v4;
	struct ppe_drv_v6_conn *cn_v6;
#ifdef PPE_TUNNEL_ENABLE
	struct ppe_drv_v4_conn *cn_tun_v4;
	struct ppe_drv_v6_conn *cn_tun_v6;
#endif

	struct ppe_drv_v4_conn_flow *pcf_v4;
	struct ppe_drv_v4_conn_flow *pcr_v4;
	struct ppe_drv_v6_conn_flow *pcf_v6;
	struct ppe_drv_v6_conn_flow *pcr_v6;
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	struct ppe_drv_v6_conn_flow *pcf_hp;
	struct ppe_drv_v6_conn_flow *pcr_hp;
#endif
#ifdef NSS_PPE_PM_COUNTER_FEATURE_SUPPORT
        struct ppe_drv_pm_counter_ctx *pm_ctx;
#endif
	uint16_t id;

	/*
	 * Update hw stats for flow associated with active v4 connections
	 */
	spin_lock_bh(&p->lock);
	if (!list_empty(&p->conn_v4)) {
		list_for_each_entry(cn_v4, &p->conn_v4, list) {
			pcf_v4 = &cn_v4->pcf;
			pcr_v4 = &cn_v4->pcr;

			if (!pcf_v4->no_stats_update) {
				ppe_drv_flow_v4_stats_update(pcf_v4);
			}
			if (pcr_v4 && !pcr_v4->no_stats_update) {
				ppe_drv_flow_v4_stats_update(pcr_v4);
			}
		}
	}

	/*
	 * Update hw stats for flow associated with active v6 connections
	 */
	if (!list_empty(&p->conn_v6)) {
		list_for_each_entry(cn_v6, &p->conn_v6, list) {
			pcf_v6 = &cn_v6->pcf;
			pcr_v6 = &cn_v6->pcr;

			if (!pcf_v6->no_stats_update) {
				ppe_drv_flow_v6_stats_update(pcf_v6);
			}
			if (pcr_v6 && !pcr_v6->no_stats_update) {
				ppe_drv_flow_v6_stats_update(pcr_v6);
			}
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
			if (cn_v6->is_hairpin_nat) {
				pcf_hp = &cn_v6->pcf_hp;
				pcr_hp = &cn_v6->pcr_hp;

				ppe_drv_flow_v6_stats_update(pcf_hp);
				if (pcr_hp) {
					ppe_drv_flow_v6_stats_update(pcr_hp);
				}
			}
#endif
		}
	}

#ifdef PPE_TUNNEL_ENABLE
	/*
	 * Update hw stats for tunnels associated with active v4 connections
	 */

	if (!list_empty(&p->conn_tun_v4)) {
		list_for_each_entry(cn_tun_v4, &p->conn_tun_v4, list) {
			ppe_drv_tun_v4_port_stats_update(cn_tun_v4);
		}
	}

	/*
	 * Update hw stats for tunnels associated with active v6 connections
	 */
	if (!list_empty(&p->conn_tun_v6)) {
		list_for_each_entry(cn_tun_v6, &p->conn_tun_v6, list) {
			/*
			 * Mapt outer rule for tunnel stats must not be updated as
			 * it would be mapped to the innner v4 flow stats
			 */
			if (ppe_drv_v6_conn_flags_check(cn_tun_v6, PPE_DRV_V6_CONN_FLAG_TYPE_MAPT)) {
				continue;
			}

			ppe_drv_tun_v6_port_stats_update(cn_tun_v6);
		}
	}
#endif
	for (id = 0; id < PPE_DRV_ACL_LIST_ID_MAX; id++) {
		if (p->acl->list_id[id].list_id_state == PPE_DRV_ACL_LIST_ID_USED) {
			ppe_drv_acl_stats_update(p->acl->list_id[id].ctx);
		}
	}

	/*
	 * Update h/w stats counter if policer is in use.
	 */
	for (id = 0; id < PPE_DRV_PORT_POLICER_MAX; id++) {
		if (p->pol_ctx->port_pol[id].in_use) {
			ppe_drv_port_policer_stats_update(&p->pol_ctx->port_pol[id]);
		}
	}

	/*
	 * Update h/w stats counter if acl-policer is in use.
	 */
	for (id = 0; id < PPE_DRV_ACL_POLICER_MAX; id++) {
		if (p->pol_ctx->acl_pol[id].is_configured) {
			ppe_drv_acl_policer_stats_update(&p->pol_ctx->acl_pol[id]);
		}
	}
#ifdef NSS_PPE_PM_COUNTER_FEATURE_SUPPORT
	/*
	 * Update the PM counter stats.
	 */
	for (id = 0; id < PPE_DRV_PM_COUNTER_ID_MAX; id++) {
		pm_ctx = &p->pm->in_ctx_array[id];
		if (pm_ctx->state == PPE_DRV_PM_COUNTER_ID_USED) {
			ppe_drv_pm_counter_stats_update(pm_ctx);
		}

		pm_ctx = &p->pm->eg_ctx_array[id];
		if (pm_ctx->state == PPE_DRV_PM_COUNTER_ID_USED) {
			ppe_drv_pm_counter_stats_update(pm_ctx);
		}
	}
#endif

	spin_unlock_bh(&p->lock);

	/*
	 * Re arm the hardware stats timer
	 */
	mod_timer(&p->hw_flow_stats_timer, jiffies + p->hw_flow_stats_ticks);
}

/*
 * ppe_drv_nsm_queue_stats_update()
 *	Get queue stats from fal to nsm.
 */
bool ppe_drv_nsm_queue_stats_update(struct ppe_drv_nsm_stats *nsm_stats, uint32_t queue_id, uint8_t item_id)
{
	fal_queue_stats_t queue_info;
	sw_error_t err;

	if (item_id >= FAL_QM_DROP_ITEMS) {
		ppe_drv_warn("invalid drop item id: %u", item_id);
		return false;
	}

	err = fal_queue_counter_get(PPE_DRV_SWITCH_ID, queue_id, &queue_info);
	if (err != SW_OK) {
		ppe_drv_warn("failed to get stats for queue id: %u", queue_id);
		return false;
	}

	nsm_stats->queue_stats.drop_packets = queue_info.drop_packets[item_id];
	nsm_stats->queue_stats.drop_bytes = queue_info.drop_bytes[item_id];
	ppe_drv_trace("queue id: %u, item id: %u, drop_packet = %llu, drop_bytes = %llu", queue_id, item_id, nsm_stats->queue_stats.drop_packets, nsm_stats->queue_stats.drop_bytes);

	return true;
}
EXPORT_SYMBOL(ppe_drv_nsm_queue_stats_update);

/*
 * ppe_drv_nsm_sawf_sc_stats_read()
 *	Export service-class stats to nsm.
 */
bool ppe_drv_nsm_sawf_sc_stats_read(struct ppe_drv_nsm_stats *nsm_stats, uint8_t service_class)
{
	struct ppe_drv_stats_sawf_sc *sawf_sc_stats;
	struct ppe_drv *p = ppe_drv_gbl;

	if (!PPE_DRV_SERVICE_CLASS_IS_VALID(service_class)) {
		ppe_drv_warn("%u Invalid SAWF service class", service_class);
		return false;
	}

	spin_lock_bh(&p->lock);

	sawf_sc_stats = &ppe_drv_gbl->stats.sawf_sc_stats[service_class];

	nsm_stats->sawf_sc_stats.rx_packets = atomic64_read(&sawf_sc_stats->rx_packets);
	nsm_stats->sawf_sc_stats.rx_bytes = atomic64_read(&sawf_sc_stats->rx_bytes);
	nsm_stats->sawf_sc_stats.flow_count = atomic64_read(&sawf_sc_stats->flow_count);

	spin_unlock_bh(&p->lock);

	ppe_drv_trace("Stats Updated: service class %u : packets %llu : bytes %llu : flows %u\n",
			service_class, nsm_stats->sawf_sc_stats.rx_packets, nsm_stats->sawf_sc_stats.rx_bytes, nsm_stats->sawf_sc_stats.flow_count);
	return true;
}
EXPORT_SYMBOL(ppe_drv_nsm_sawf_sc_stats_read);

/*
 * ppe_drv_hash_init()
 *	Initialize the PPE hash registers
 */
static bool ppe_drv_hash_init(void)
{
	fal_rss_hash_mode_t mode = { 0 };
	fal_rss_hash_config_t config = { 0 };

#ifdef PPE_DRV_RPS_HASH_SELECT
	fal_rss_hash_algm_t rsshash_algm = { 0 };
	rsshash_algm.hash_algm = FAL_RSS_LEGACY_HASH;

	/*
	 * TODO: Add support to select legacy hash and toeplitz hash
	 */
	if ((fal_rsshash_algm_set(PPE_DRV_SWITCH_ID, &rsshash_algm)) != SW_OK) {
		ppe_drv_warn("Failed to set hash algorithm \n");
		return false;
	}
#endif /* !PPE_DRV_RPS_HASH_SELECT */

	mode = FAL_RSS_HASH_IPV4ONLY;
	config.hash_mask = PPE_DRV_HASH_MASK;
	config.hash_fragment_mode = ipfrag_2tuple_hash;
	config.hash_seed = PPE_DRV_HASH_SEED_DEFAULT;
	config.hash_sip_mix[0] = PPE_DRV_HASH_MIX_V4_SIP;
	config.hash_dip_mix[0] = PPE_DRV_HASH_MIX_V4_DIP;
	config.hash_protocol_mix = PPE_DRV_HASH_MIX_V4_PROTO;
	config.hash_sport_mix = PPE_DRV_HASH_MIX_V4_SPORT;
	config.hash_dport_mix = PPE_DRV_HASH_MIX_V4_DPORT;

	config.hash_fin_inner[0] = (PPE_DRV_HASH_FIN_INNER_OUTER_0 & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_outer[0] = ((PPE_DRV_HASH_FIN_INNER_OUTER_0 >> 5) & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_inner[1] = (PPE_DRV_HASH_FIN_INNER_OUTER_1 & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_outer[1] = ((PPE_DRV_HASH_FIN_INNER_OUTER_1 >> 5) & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_inner[2] = (PPE_DRV_HASH_FIN_INNER_OUTER_2 & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_outer[2] = ((PPE_DRV_HASH_FIN_INNER_OUTER_2 >> 5) & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_inner[3] = (PPE_DRV_HASH_FIN_INNER_OUTER_3 & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_outer[3] = ((PPE_DRV_HASH_FIN_INNER_OUTER_3 >> 5) & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_inner[4] = (PPE_DRV_HASH_FIN_INNER_OUTER_4 & PPE_DRV_HASH_FIN_MASK);
	config.hash_fin_outer[4] = ((PPE_DRV_HASH_FIN_INNER_OUTER_4 >> 5) & PPE_DRV_HASH_FIN_MASK);

	if (fal_rss_hash_config_set(PPE_DRV_SWITCH_ID, mode, &config) != SW_OK) {
		ppe_drv_warn("IPv4 hash register initialization failed\n");
		return false;
	}

	mode = FAL_RSS_HASH_IPV6ONLY;
	config.hash_sip_mix[0] = PPE_DRV_HASH_SIPV6_MIX_0;
	config.hash_dip_mix[0] = PPE_DRV_HASH_DIPV6_MIX_0;
	config.hash_sip_mix[1] = PPE_DRV_HASH_SIPV6_MIX_1;
	config.hash_dip_mix[1] = PPE_DRV_HASH_DIPV6_MIX_1;
	config.hash_sip_mix[2] = PPE_DRV_HASH_SIPV6_MIX_2;
	config.hash_dip_mix[2] = PPE_DRV_HASH_DIPV6_MIX_2;
	config.hash_sip_mix[3] = PPE_DRV_HASH_SIPV6_MIX_3;
	config.hash_dip_mix[3] = PPE_DRV_HASH_DIPV6_MIX_3;

	if (fal_rss_hash_config_set(PPE_DRV_SWITCH_ID, mode, &config) != SW_OK) {
		ppe_drv_warn("IPv6 hash register initialization failed\n");
		return false;
	}

	return true;
}

/*
 * ppe_drv phy_port_base_queue_init()
 *	Initialize PPE port 0 - 7 base queue
 */
static bool ppe_drv_phy_port_base_queue_init(struct ppe_drv *p)
{
	fal_ucast_queue_dest_t q_dst = {0};
	uint32_t queue_id = 0;
	sw_error_t err;
	uint8_t profile = 0;
	int i = 0;

	for (i = 0; i < PPE_DRV_PHYSICAL_MAX; i++) {
		q_dst.src_profile = 0;
		q_dst.dst_port = i;

		err = fal_ucast_queue_base_profile_get(PPE_DRV_SWITCH_ID, &q_dst, &queue_id, &profile);
		if (err != SW_OK) {
			ppe_drv_warn("%p unable to get port queue base ID: %d", p, err);
			return false;
		}

		ppe_drv_port_ucast_queue_update(&p->port[i], (uint8_t)queue_id);
	}

	return true;
}

#ifdef PPE_DRV_PKT_PADDING_STRIP
/*
 * ppe_drv_pkt_edit_init()
 *      Initialize the PPE packet editor registers
 */
static bool ppe_drv_pkt_edit_init(void)
{
	fal_pktedit_padding_t padding = { 0 };

	/*
	 * Default config for removing padidng from packet
	 */
	padding.strip_padding_route_en = 1;
	padding.strip_padding_en = 1;
	padding.strip_padding_bridge_en = 0;
	padding.strip_tunnel_inner_padding_en = 0;

	if (fal_pktedit_padding_set(PPE_DRV_SWITCH_ID, &padding) != SW_OK) {
		ppe_drv_warn("Failed to update strip_padding_en config \n");
		return false;
	}

	return true;
}
#endif

/*
 * ppe_drv_fse_feature_enable()
 *	Enable PPE FSE feature
 */
void ppe_drv_fse_feature_enable()
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->fse_enable = true;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_fse_feature_enable);

/*
 * ppe_drv_fse_feature_disable()
 *	Disable PPE FSE feature
 */
void ppe_drv_fse_feature_disable()
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->fse_enable = false;
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_fse_feature_disable);

/*
 * ppe_drv_loopback_base_queue()
 */
#if defined(PPE_LOOPBACK_RING_SUPPORT)
void ppe_drv_loopback_base_queue(uint8_t queue_id, uint32_t ft_type)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);

	if (ft_type & PPE_DRV_LOOPBACK_FEATURE_TYPE_GRETAP_MAPT) {
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING_GRETAP_MAPT, queue_id,
						PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING_MAPT_GRETAP, queue_id,
						PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
	}

#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	if (ft_type & PPE_DRV_LOOPBACK_FEATURE_TYPE_V6_HAIRPIN_NAT) {
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_NPT66_HAIRPIN_NAT, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
	}
#endif

	p->loopback_ring_info.base_queue = queue_id;
	p->loopback_ring_info.ft_type = ft_type;
	p->loopback_ring_info.enabled = true;

	spin_unlock_bh(&p->lock);

	ppe_drv_info("loopback ring enabled queue_id %u feature type %x\n", queue_id, p->loopback_ring_info.ft_type);
}
EXPORT_SYMBOL(ppe_drv_loopback_base_queue);
#endif

#ifdef NSS_PPE_L2_VP_SC_ENQ_BYPASS
/*
 * ppe_drv_loopback_sc2queue_mapping()
 *	Loopback to queue mapping
 */
static bool ppe_drv_loopback_sc2queue_mapping(struct ppe_drv *p, uint8_t src_profile)
{
#if defined(PPE_LOOPBACK_RING_SUPPORT)
	int base_queue = p->loopback_ring_info.base_queue;
	ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING, base_queue, src_profile, PPE_DRV_REDIR_PROFILE_ID);
	return true;
#else
	return false;
#endif
}

#else
/*
 * ppe_drv_loopback_sc2queue_mapping()
 *	Loopback to queue mapping
 */
static bool ppe_drv_loopback_sc2queue_mapping(struct ppe_drv *p, uint8_t src_profile)
{
	int base_queue = p->loopback_ring_info.base_queue;
	struct net_device *upstream_dev;
	struct ppe_drv_port *pp = NULL;
	int next_sc_queue = -1;

	upstream_dev = p->upstream_dev;
	pp = ppe_drv_port_from_dev(upstream_dev);
	if (!pp) {
		ppe_drv_warn("%p: invalid ppe port for given upstream dev: %s", p, upstream_dev->name);
		return false;
	}

	next_sc_queue = ppe_drv_port_ucast_queue_get_by_port(pp->port);
	if (next_sc_queue == -1) {
                ppe_drv_warn("%p: ucast queue get failed for pp %d", p, pp->port);
		return false;
        }

	/*
	 * Map loopback ring service code to queue mapping
	 */
	ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING_NEXT, next_sc_queue, src_profile, PPE_DRV_REDIR_PROFILE_ID);
	ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING, base_queue, src_profile, PPE_DRV_REDIR_PROFILE_ID);
	return true;
}
#endif

/*
 * ppe_drv_queue_from_core()
 *	Get base queue for a specific core.
 */
int16_t ppe_drv_queue_from_core(uint8_t core)
{
	struct ppe_drv *p = ppe_drv_gbl;

	if (core >= NR_CPUS) {
		ppe_drv_warn("%p: invalid core-id: %d", p, core);
		return -1;
	}

	return p->core2queue[core];
}
EXPORT_SYMBOL(ppe_drv_queue_from_core);

/*
 * ppe_drv_enq_vp_queue_reset()
 *	Reset the queue ID of given enqueue vport in PPE.
 */
static bool ppe_drv_enq_vp_queue_reset(struct ppe_drv *p,
					uint32_t enq_vport)
{
        sw_error_t err;
	fal_ucast_queue_dest_t q_dst = {0};

	q_dst.src_profile = PPE_DRV_PORT_SRC_PROFILE;
	q_dst.dst_port = enq_vport;
	err = fal_ucast_queue_base_profile_set(PPE_DRV_SWITCH_ID, &q_dst, PPE_DRV_ENQ_VP_QID_NONE, PPE_DRV_REDIR_PROFILE_ID);
	if (err != SW_OK) {
		ppe_drv_warn("%p: Unable to reset PPE queue for enqueue vp :%d", p, enq_vport);
		return false;
	}

	return true;
}

/*
 * ppe_drv_enq_vp_queue_set()
 *	Set the queue ID of given enqueue vport in PPE.
 */
static bool ppe_drv_enq_vp_queue_set(struct ppe_drv *p,
					int16_t enq_vport,
					a_uint32_t queue_id, int profile_id)
{
        sw_error_t err;
	fal_ucast_queue_dest_t q_dst = {0};
	struct ppe_drv_port *pp = NULL;
	int src_profile = -1;

	q_dst.dst_port = enq_vport;

	/*
	 * Enqueue VP need to be mapped to a queue for all the source profile. This is specifically required
	 * for PON setups where interfaces can be put to a different source profile. Queue base is dependent
	 * on source profile and hence queue base need to be updated for all source profile.
	 */
	for (src_profile = 0; src_profile < PPE_DRV_PORT_SRC_PROFILE_MAX; src_profile++) {
		q_dst.src_profile = src_profile;
		err = fal_ucast_queue_base_profile_set(PPE_DRV_SWITCH_ID, &q_dst, queue_id, profile_id);
		if (err != SW_OK) {
			ppe_drv_warn("%p: Unable to map enqueue vp with queue:%d for src_profile:%d", p, queue_id, src_profile);
			return false;
		}
	}

	/*
	 * Update drv port structure with the correct ucast queue
	 * mapped to the enqueue vp port for easy access elsewhere.
	 */
	pp = &p->port[enq_vport];
	pp->ucast_queue = queue_id;

	return true;
}

/*
 * ppe_drv_enq_vp_map_to_queue()
 *	Enqueue VP to queue mapping.
 */
ppe_drv_ret_t ppe_drv_enq_vp_map_to_queue(uint8_t queue_id, int8_t enq_vp, int profile_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_enqueue_cfg_t enqueue_cfg = {0};
        sw_error_t ret;
	int8_t pri_profile;

	pri_profile = ppe_drv_port_enq_vp_to_pri_prof(enq_vp);
	if (pri_profile == PPE_DRV_PORT_ENQ_VP_PRI_PRFL_INVALID) {
		ppe_drv_warn("%p: Unable to get pri profile for enqueue vport:%d ", p, enq_vp);
		return PPE_DRV_RET_ENQ_VP_TO_PRI_PROF_FAIL;
	}

	/*
	 * Set queue_id for a given port on PPE.
	 */
	if (!ppe_drv_enq_vp_queue_set(p, enq_vp, queue_id, profile_id)) {
		ppe_drv_warn("%p: Enqueue vp queue init failed for qid:%d", p, queue_id);
		return PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
	}

	/*
	 * Configure the allocated enqueue vp number on PORT_VSI_ENQUEUE table.
	 *
	 * NOTE: The flow_pri_profile is set directly to pri_profile without adding
	 * PPE_DRV_PORT_ENQVP_VSI_TBL_START_IDX offset. This change was made as part
	 * of HW GRO support to align with hardware requirements for both RFS and GRO
	 * enqueue VP configurations.
	 */
	enqueue_cfg.rule_entry.enqueue_type = FAL_ENQUEUE_FLOW;
	enqueue_cfg.rule_entry.flow_pri_profile = pri_profile;
	enqueue_cfg.index_entry.enqueue_en = (a_bool_t)PPE_DRV_PORT_EVP_ENABLE;
	enqueue_cfg.index_entry.enqueue_vport = enq_vp;
	ret = fal_qm_enqueue_config_set(PPE_DRV_SWITCH_ID, &enqueue_cfg);
	if (ret != SW_OK) {
		ppe_drv_warn("%p: Unable to set the enqueue vp config", p);
		return PPE_DRV_RET_ENQ_VP_EN_FAIL;
	}

	ppe_drv_trace("%p: Enqueue vp node to queue map done qid:%d enq_vp:%d pri_prof:%d", p, queue_id, enq_vp, enqueue_cfg.rule_entry.flow_pri_profile);
	return PPE_DRV_RET_SUCCESS;
}

/*
 * ppe_drv_ds_map_free()
 *	Provides unmapping of node with enqueue vp and queue
 */
ppe_drv_ret_t ppe_drv_ds_map_free(uint8_t node_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_enqueue_cfg_t enqueue_cfg = {0};
        sw_error_t ret;
	int8_t pri_profile;
	int16_t enq_vp;

	spin_lock_bh(&p->lock);
	enq_vp = ppe_drv_port_metadata_to_enq_vp_internal(node_id);
	if (enq_vp == PPE_DRV_PORT_ID_INVALID) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to get enqueue vport for node_id:%d ", p, node_id);
		return PPE_DRV_RET_METADATA_TO_ENQ_VP_FAIL;
	}

	pri_profile = ppe_drv_port_enq_vp_to_pri_prof(enq_vp);
	if (pri_profile == PPE_DRV_PORT_ENQ_VP_PRI_PRFL_INVALID) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to get pri profile for enqueue vport:%d ", p, enq_vp);
		return PPE_DRV_RET_ENQ_VP_TO_PRI_PROF_FAIL;
	}

	/*
	 * Reset queue_id for a given port on PPE.
	 */
	if (!ppe_drv_enq_vp_queue_reset(p, enq_vp)) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Enqueue vp queue reset failed for node_id:%d", p, node_id);
		return PPE_DRV_RET_ENQ_VP_QID_RESET_FAIL;
	}

	/*
	 * Disable enqueue vp bit on PORT_VSI_ENQUEUE table.
	 */
	enqueue_cfg.rule_entry.enqueue_type = FAL_ENQUEUE_FLOW;
	enqueue_cfg.rule_entry.flow_pri_profile = pri_profile;
	enqueue_cfg.index_entry.enqueue_en = (a_bool_t)PPE_DRV_PORT_EVP_DISABLE;
	enqueue_cfg.index_entry.enqueue_vport = enq_vp;
	ret = fal_qm_enqueue_config_set(PPE_DRV_SWITCH_ID, &enqueue_cfg);
	if (ret != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to reset enq_vp:%d disable on PORT VSI", p, enq_vp);
		return PPE_DRV_RET_ENQ_VP_DISABLE_FAIL;
	}

	ppe_drv_port_enq_vp_free(enq_vp);
	spin_unlock_bh(&p->lock);
	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_ds_map_free);

/*
 * ppe_drv_ds_map_node_to_queue()
 *	node to queue mapping
 *
 * This API will be invoked by PPE-DS module to provide node to queue mapping.
 */
ppe_drv_ret_t ppe_drv_ds_map_node_to_queue(uint8_t node_id, uint8_t queue_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	uint8_t enq_vp;
	ppe_drv_ret_t status;

	spin_lock_bh(&p->lock);
	enq_vp = ppe_drv_port_enq_vp_alloc();
	if (enq_vp == PPE_DRV_PORT_ID_INVALID) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to get the enqueue vport ", p);
		return PPE_DRV_RET_ENQ_VP_ALLOC_FAIL;
	}

	p->port[enq_vp].evp.type = PPE_DRV_ENQ_VP_DEFAULT;

	/*
	 * Map enqueue VP to specific queue
	 */
	status = ppe_drv_enq_vp_map_to_queue(queue_id, enq_vp, PPE_DRV_REDIR_PROFILE_ID);
	if (status != PPE_DRV_RET_SUCCESS) {
		ppe_drv_port_enq_vp_free(enq_vp);
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to map enq_vp:%u to queue:%u ", p, enq_vp, queue_id);
		return status;
	}

	/*
	 * Each enqueue vp has metadata value(node_id in case of PPE-DS).
	 * Set enqueue vport metadata value.
	 * Cookie will be used as key to find the corresponding enqueue vp during PPE flow addition.
	 */
	ppe_drv_port_enq_vp_metadata_set(enq_vp, node_id);
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("%p: Enqueue vp node to queue map done qid:%d enq_vp:%d node_id:%d", p, queue_id, enq_vp, node_id);
	return status;
}
EXPORT_SYMBOL(ppe_drv_ds_map_node_to_queue);

#ifdef NSS_PPE_DRV_HW_GRO
/*
 * ppe_drv_gro_map_core_to_enqueue_vp()
 *	Core to queue mapping
 *
 * This API will be invoked at dp-init time to map each core to enq_vp
 */
static ppe_drv_ret_t ppe_drv_gro_map_core_to_enqueue_vp(uint8_t core, uint8_t queue_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	uint8_t enq_vp;
	ppe_drv_ret_t status;

	/*
	 * Default coremask for the physical interface is set.
	 */
	p->gro_ctx.gro_info.coremask = eth_gro_coremask;
	p->gro_ctx.gro_info.shadow_coremask = eth_gro_coremask;

	enq_vp = ppe_drv_port_enq_vp_alloc();
	if (enq_vp == PPE_DRV_PORT_ID_INVALID) {
		ppe_drv_warn("%p: Unable to get the enqueue vport ", p);
		return PPE_DRV_RET_ENQ_VP_ALLOC_FAIL;
	}

	/*
	 * Maps enqueue with given queue
	 */
	status = ppe_drv_enq_vp_map_to_queue(queue_id, enq_vp, PPE_DRV_GRO_PROFILE_ID);
	if (status != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p: Unable to map enq_vp:%u to queue:%u ", p, enq_vp, queue_id);
		ppe_drv_port_enq_vp_free(enq_vp);
		return status;
	}

	ppe_drv_trace("%p: GRO core mappped to enqueue VP. qid:%d enq_vp:%d core:%d", p, queue_id, enq_vp, core);

	/*
	 * Core id will be used as key to find the corresponding enqueue vp during PPE flow addition.
	 */
	p->gro_ctx.gro_info.core2enq_vp[core] = enq_vp;
	return status;
}
#endif
#ifdef NSS_PPE_PON_SUPPORT
/*
 * ppe_drv_pon_map_enqueue_vp_to_pq()
 *	maps Pqs to a enqueue vp
 *
 */
ppe_drv_ret_t ppe_drv_pon_map_enqueue_vp_to_pq(struct ppe_drv_port *port)
{
	struct ppe_drv *p = ppe_drv_gbl;
	fal_portscheduler_resource_t cfg = {0};
	ppe_drv_ret_t ppe_ret;
	int8_t enq_vp;
	uint8_t cnt_enq_vp;
	uint8_t queue_id;
	uint8_t max_enq_vp;
	int8_t allocated_enq_vps[PPE_DRV_PORT_ENQ_VP_MAX_NUM];
	uint8_t allocated_count = 0;
	int32_t pon_port_profile_id;

	pon_port_profile_id = ppe_drv_port_ucast_queue_profile_get(port->port);
	if (!ppe_drv_confgiure_ucast_prio_map_tbl(pon_port_profile_id, ppe_drv_16_prio_map)) {
		ppe_drv_warn("%p: failed to configure ucast priority class setting\n", p);
		return PPE_DRV_RET_UCAST_PRIO_TBL_MAP_FAIL;
	}

	if (fal_port_scheduler_resource_get(0, port->port, &cfg) != 0) {
		ppe_drv_warn("%px:port resources info failed for port:%u", p, port->port);
		return PPE_DRV_RET_FAILURE_NO_RESOURCE;
	}

	p->ppe_drv_pon_port_start_pq = cfg.ucastq_start;
	p->ppe_drv_pon_port_max_pq = (pon_max_pq <= cfg.ucastq_num) ? pon_max_pq : cfg.ucastq_num;

	max_enq_vp = (p->ppe_drv_pon_port_max_pq + PPE_DRV_PORT_PON_PQ_PER_ENQ_VP - 1) /
								PPE_DRV_PORT_PON_PQ_PER_ENQ_VP;

	for (cnt_enq_vp = 0; cnt_enq_vp < max_enq_vp; cnt_enq_vp++) {
		enq_vp = ppe_drv_port_enq_vp_alloc();
		if (enq_vp == PPE_DRV_PORT_ID_INVALID) {
			ppe_drv_warn("%p: Unable to get the enqueue vport ", p);
			ppe_ret = PPE_DRV_RET_ENQ_VP_ALLOC_FAIL;
			goto cleanup_allocated_enq_vps;
		}
		p->port[enq_vp].evp.type = PPE_DRV_ENQ_VP_PON;

		/*
		 * Set queue_id for a given port on PPE.
		 */
		queue_id = p->ppe_drv_pon_port_start_pq + (PPE_DRV_PORT_PON_PQ_PER_ENQ_VP * cnt_enq_vp);
		if (queue_id >= (p->ppe_drv_pon_port_start_pq + p->ppe_drv_pon_port_max_pq)) {
			ppe_drv_port_enq_vp_free(enq_vp);
			ppe_drv_warn("%p: Calculated queue_id %d exceeds available range", p, queue_id);
			ppe_ret = PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
			goto cleanup_allocated_enq_vps;
		}

		if (!ppe_drv_port_ucast_queue_profile_set(&p->port[enq_vp], PPE_DRV_PORT_SRC_PROFILE,
						queue_id, pon_port_profile_id)) {
			ppe_drv_port_enq_vp_free(enq_vp);
			ppe_drv_warn("%p: Enqueue vp queue init failed for qid:%d", p, queue_id);
			ppe_ret = PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
			goto cleanup_allocated_enq_vps;
		}

		/*
		 * Map enqueue VP to specific queue
		 */
		ppe_ret = ppe_drv_enq_vp_map_to_queue(queue_id, enq_vp, pon_port_profile_id);
		if (ppe_ret != PPE_DRV_RET_SUCCESS) {
			ppe_drv_port_enq_vp_free(enq_vp);
			ppe_drv_warn("%p: Unable to map enq_vp:%u to queue:%u ", p, enq_vp, queue_id);
			goto cleanup_allocated_enq_vps;
		}
		allocated_enq_vps[allocated_count++] = enq_vp;
		ppe_drv_trace("%p: Enqueue vp node to priority queue map done qid:%d enq_vp:%d",
					p, queue_id, enq_vp);
	}

	return PPE_DRV_RET_SUCCESS;

cleanup_allocated_enq_vps:
	/*
	 * Cleanup all successfully allocated Enq VPs
	 */
	for (uint8_t i = 0; i < allocated_count; i++) {
		ppe_drv_port_enq_vp_free(allocated_enq_vps[i]);
	}
	return ppe_ret;
}

/*
 * ppe_drv_pon_get_pq_config()
 *	map pq to enqueue vp and int_pri.
 *
 */
bool ppe_drv_pon_get_pq_config(uint8_t pq, uint8_t *enq_vp, uint8_t *int_pri)
{
	int internal_pq, q_offset;
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp = NULL;
	uint16_t i;
	fal_ucast_queue_dest_t q_dst = {0};
	uint32_t queue_id = 0;
	sw_error_t err;
	uint8_t profile = 0;
	int enqueue_vp;

	spin_lock_bh(&p->lock);
	if (pq >= p->ppe_drv_pon_port_max_pq) {
		ppe_drv_warn("pq %d is more than the max supported priority queue %d", pq, p->ppe_drv_pon_port_max_pq);
		spin_unlock_bh(&p->lock);
		return false;
	}

	internal_pq = p->ppe_drv_pon_port_start_pq + pq;
	q_offset = pq % PPE_DRV_PORT_PON_PQ_PER_ENQ_VP;
	*int_pri = q_offset;

	for (i = PPE_DRV_PORT_ENQ_VP_START; i <=PPE_DRV_PORT_ENQ_VP_END; i++) {
		pp = &p->port[i];

		if (!kref_read(&pp->ref_cnt)) {
			continue;
		}

		if (pp->evp.type == PPE_DRV_ENQ_VP_PON) {
			enqueue_vp = pp->port;
			q_dst.src_profile = 0;
			q_dst.dst_port = enqueue_vp;

			err = fal_ucast_queue_base_profile_get(PPE_DRV_SWITCH_ID, &q_dst, &queue_id, &profile);
			if (err != SW_OK) {
				spin_unlock_bh(&p->lock);
				ppe_drv_warn("unable to get base queue id for enq_vp port %d", enqueue_vp);
				return false;
			}

			if ((internal_pq >= queue_id) && (internal_pq < (queue_id + PPE_DRV_PORT_PON_PQ_PER_ENQ_VP))) {
				*enq_vp = enqueue_vp;
				spin_unlock_bh(&p->lock);
				ppe_drv_info("enq_vp =%d int_pri =%d\n", *enq_vp, *int_pri);
				return true;
			}
		}
	}
	spin_unlock_bh(&p->lock);

	ppe_drv_info("No matching enqueue VP found for pq %d", pq);
	return false;
}
EXPORT_SYMBOL(ppe_drv_pon_get_pq_config);

/*
 * ppe_drv_pon_get_enq_vp_mapped_to_base_pq()
 * 	get enqueue_vp mapped to the passed base queue
 */
static int ppe_drv_pon_get_enq_vp_mapped_to_base_pq(uint8_t internal_pq)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp = NULL;
	uint16_t i;
	fal_ucast_queue_dest_t q_dst = {0};
	uint32_t queue_id = 0;
	sw_error_t err;
	uint8_t profile = 0;
	int enqueue_vp = PPE_DRV_PORT_ENQ_VP_INVALID;

	for (i = PPE_DRV_PORT_ENQ_VP_START; i <=PPE_DRV_PORT_ENQ_VP_END; i++) {
		pp = &p->port[i];

		if (!kref_read(&pp->ref_cnt)) {
			continue;
		}

		if ((pp->evp.type == PPE_DRV_ENQ_VP_PON) ||
				(pp->evp.type == PPE_DRV_BASEQ_ENQ_VP_PON)) {
			enqueue_vp = pp->port;
			q_dst.src_profile = 0;
			q_dst.dst_port = enqueue_vp;

			err = fal_ucast_queue_base_profile_get(PPE_DRV_SWITCH_ID, &q_dst, &queue_id, &profile);
			if (err != SW_OK) {
				ppe_drv_warn("unable to get base queue id for enq_vp port %d", enqueue_vp);
				return PPE_DRV_PORT_ENQ_VP_INVALID;
			}

			if (internal_pq == queue_id) {
				ppe_drv_info("enq_vp =%d", enqueue_vp);
				return enqueue_vp;
			}
		}
	}
	return PPE_DRV_PORT_ENQ_VP_INVALID;
}

/*
 * ppe_drv_pon_map_enq_vp_to_base_pq()
 *	maps base Pq to a enqueue vp
 */
ppe_drv_ret_t ppe_drv_pon_map_enq_vp_to_base_pq(uint8_t base_pq, uint8_t *enq_vp)
{
	int internal_pq;
	struct ppe_drv *p = ppe_drv_gbl;
	int enqueue_vp;
	ppe_drv_ret_t status;
	struct ppe_drv_port *port;
	int32_t pon_port_profile_id;

	if (base_pq >= p->ppe_drv_pon_port_max_pq) {
		ppe_drv_warn("pq %d is more than the max supported priority queue %d", base_pq, p->ppe_drv_pon_port_max_pq);
		return PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
	}

	internal_pq = p->ppe_drv_pon_port_start_pq + base_pq;
	if (internal_pq >= (p->ppe_drv_pon_port_start_pq + p->ppe_drv_pon_port_max_pq)) {
		ppe_drv_warn("%p: Calculated queue_id %d exceeds available range", p, internal_pq);
		return PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
	}

	spin_lock_bh(&p->lock);
	enqueue_vp = ppe_drv_pon_get_enq_vp_mapped_to_base_pq(internal_pq);
	if (enqueue_vp != PPE_DRV_PORT_ENQ_VP_INVALID) {
		*enq_vp = enqueue_vp;
		spin_unlock_bh(&p->lock);
		return PPE_DRV_RET_SUCCESS;
	}

	enqueue_vp = ppe_drv_port_enq_vp_alloc();
	if (enqueue_vp == PPE_DRV_PORT_ID_INVALID) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: Unable to get the enqueue vport", p);
		return PPE_DRV_RET_ENQ_VP_ALLOC_FAIL;
	}
	p->port[enqueue_vp].evp.type = PPE_DRV_BASEQ_ENQ_VP_PON;

	if (!ppe_drv_port_ucast_queue_profile_set(&p->port[enqueue_vp], PPE_DRV_PORT_SRC_PROFILE,
					internal_pq, PON_PORT_ID)) {
		spin_unlock_bh(&p->lock);
		ppe_drv_port_enq_vp_free(enqueue_vp);
		ppe_drv_warn("%p: Enqueue vp queue init failed for qid:%d", p, internal_pq);
		return PPE_DRV_RET_ENQ_VP_QID_SET_FAIL;
	}

	/*
	 * Map enqueue VP to specific queue
	 */
	port = ppe_drv_iface_port_get(p->pon_iface);
	pon_port_profile_id = ppe_drv_port_ucast_queue_profile_get(port->port);
	status = ppe_drv_enq_vp_map_to_queue(internal_pq, enqueue_vp, pon_port_profile_id);
	if (status != PPE_DRV_RET_SUCCESS) {
		spin_unlock_bh(&p->lock);
		ppe_drv_port_enq_vp_free(enqueue_vp);
		ppe_drv_warn("%p: Unable to map enq_vp:%u to queue:%u ", p, enqueue_vp, internal_pq);
		return status;
	}

	spin_unlock_bh(&p->lock);
	*enq_vp = enqueue_vp;
	ppe_drv_trace("%p: Enqueue vp node to priority queue map done qid:%d enq_vp:%d", p, internal_pq, *enq_vp);

	return PPE_DRV_RET_SUCCESS;
}
EXPORT_SYMBOL(ppe_drv_pon_map_enq_vp_to_base_pq);
#endif

/*
 * ppe_drv_rfs_map_core_to_enqueue_vp()
 *	Core to queue mapping
 *
 * This API will be invoked at dp-init time to map each core to enq_vp
 */
static ppe_drv_ret_t ppe_drv_rfs_map_core_to_enqueue_vp(uint8_t core, uint8_t queue_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	uint8_t enq_vp;
	ppe_drv_ret_t status;

	/*
	 * Set passive vp enable and WLAN coremask in global structure.
	 */
	p->rfs.passive_vp_enable = passive_vp_enable;
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_WLAN] = wlan_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_WLAN] = wlan_coremask;

	/*
	 * Default coremask for the physical interface is set.
	 */
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_PHYSICAL] = eth_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_PHYSICAL] = eth_coremask;

	/*
	 * Default coremask for the default generic interface is set.
	 */
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_TUNNEL] = tun_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_TUNNEL] = tun_coremask;

	enq_vp = ppe_drv_port_enq_vp_alloc();
	if (enq_vp == PPE_DRV_PORT_ID_INVALID) {
		ppe_drv_warn("%p: Unable to get the enqueue vport ", p);
		return PPE_DRV_RET_ENQ_VP_ALLOC_FAIL;
	}

	p->port[enq_vp].evp.type = PPE_DRV_ENQ_VP_DEFAULT;

	/*
	 * Maps enqueue with given queue
	 */
	status = ppe_drv_enq_vp_map_to_queue(queue_id, enq_vp, PPE_DRV_REDIR_PROFILE_ID);
	if (status != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p: Unable to map enq_vp:%u to queue:%u ", p, enq_vp, queue_id);
		ppe_drv_port_enq_vp_free(enq_vp);
		return status;
	}

	ppe_drv_trace("%p: RFS core mappped to enqueue VP. qid:%d enq_vp:%d core:%d", p, queue_id, enq_vp, core);

	/*
	 * Core id will be used as key to find the corresponding enqueue vp during PPE flow addition.
	 */
	p->rfs.core2enq_vp[core] = enq_vp;
	return status;
}

#ifdef NSS_PPE_DRV_HW_GRO
/*
 * ppe_drv_gro_core2queue_mapping()
 *	Core to queue mapping
 *
 * This API will be invoked by DP driver to provide core to queue
 * mapping. This internally will be used to configure service code
 * to queue mapping for GRO feature.
 */
void ppe_drv_gro_core2queue_mapping(uint8_t core, uint8_t queue_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_ret_t status;

	if (core >= NR_CPUS) {
		ppe_drv_warn("%p: invalid core-id: %d", p, core);
		return;
	}

	/*
	 * This API is called to map a core to an enqueue_vp.
	 */
	spin_lock_bh(&p->lock);
	status = ppe_drv_gro_map_core_to_enqueue_vp(core, queue_id);
	if (status != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p: Core failed to allocate to an enqueue vp, core %u, fail status: %u", p, core, status);
	}

	ppe_drv_trace("%d: queue mapping called for core(%d)\n", queue_id, core);

	spin_unlock_bh(&p->lock);

	return;

}
EXPORT_SYMBOL(ppe_drv_gro_core2queue_mapping);
#endif

/*
 * ppe_drv_core2queue_mapping()
 *	Core to queue mapping
 *
 * This API will be invoked by DP driver to provide core to queue
 * mapping. This internally will be used to configure service code
 * to queue mapping for PPE RFS feature.
 */
void ppe_drv_core2queue_mapping(uint8_t core, uint8_t queue_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	ppe_drv_ret_t status;

	if (core >= NR_CPUS) {
		ppe_drv_warn("%p: invalid core-id: %d", p, core);
		return;
	}

	/*
	 * This API is called to map a core to an enqueue_vp.
	 */
	spin_lock_bh(&p->lock);
	status = ppe_drv_rfs_map_core_to_enqueue_vp(core, queue_id);
	if (status != PPE_DRV_RET_SUCCESS) {
		ppe_drv_warn("%p: Core failed to allocate to an enqueue vp, core %u, fail status: %u", p, core, status);
	}

	ppe_drv_trace("%d: queue mapping called for core(%d)\n", queue_id, core);

	switch(core) {
	case 0:
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_NOEDIT_REDIR_CORE0, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_EDIT_REDIR_CORE0, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		break;
	case 1:
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_NOEDIT_REDIR_CORE1, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_EDIT_REDIR_CORE1, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		break;
	case 2:
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_NOEDIT_REDIR_CORE2, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_EDIT_REDIR_CORE2, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		break;
	case 3:
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_NOEDIT_REDIR_CORE3, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_EDIT_REDIR_CORE3, queue_id, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);
		break;
	default:
		ppe_drv_warn("%d Invalid core(%d)\n", queue_id, core);
		spin_unlock_bh(&p->lock);
		return;
	}

	/*
	 * Store core to queue mapping in ppe_drv structure.
	 */
	p->core2queue[core] = queue_id;
	spin_unlock_bh(&p->lock);

	return;

}
EXPORT_SYMBOL(ppe_drv_core2queue_mapping);

/*
 * ppe_drv_l3_route_ctrl_init()
 *	Initialize PPE global configuration
 */
static bool ppe_drv_l3_route_ctrl_init(struct ppe_drv *p)
{
	fal_ip_global_cfg_t cfg = { 0 };

	/*
	 * Global MTU and MRU cofiguration check; if flows MTU or MRU is not as
	 * expected by PPE then flow will be deaccelerated by PPE and packets will
	 * be redirected to CPU
	 */
	cfg.mru_fail_action = FAL_MAC_RDT_TO_CPU;
	cfg.mru_deacclr_en = flow_deacclr_dis ? A_FALSE : A_TRUE;
	cfg.mtu_fail_action = FAL_MAC_RDT_TO_CPU;
	cfg.mtu_deacclr_en = flow_deacclr_dis ? A_FALSE : A_TRUE;

	/*
	 * Don't deaccelerate flow based on DF bit.
	 */
	cfg.mtu_nonfrag_fail_action = FAL_MAC_RDT_TO_CPU;
	cfg.mtu_df_deacclr_en = A_FALSE;

	if (fal_ip_global_ctrl_set(PPE_DRV_SWITCH_ID, &cfg) != SW_OK) {
		ppe_drv_warn("%p: IP global control configuration failed\n", p);
		return false;
	}

	if (fal_ip_route_mismatch_action_set(PPE_DRV_SWITCH_ID, FAL_MAC_RDT_TO_CPU) != SW_OK) {
		ppe_drv_warn("%p: IP route mismatch action configuration failed\n", p);
		return false;
	}

	return true;
}

/*
 * ppe_drv_fse_ops_free()
 *	Function to release fse_ops related memory
 *
 * Should be called under ppe lock
 */
void ppe_drv_fse_ops_free(struct kref *kref)
{
	struct ppe_drv *p = container_of(kref, struct ppe_drv, fse_ops_ref);
	struct ppe_drv_fse_ops *ops_internal;

	if (!p->fse_ops) {
		ppe_drv_warn("%p: No FSE ops registered\n", p);
		return;
	}

	ops_internal = p->fse_ops;
	p->fse_ops = NULL;
	kfree(ops_internal);
}

/*
 * ppe_drv_fse_ops_unregister()
 *	Un-register FSE rule add/delete callbacks. This function will be called from Wi-Fi driver
 */
void ppe_drv_fse_ops_unregister(void)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	if (!p->fse_ops || !p->is_wifi_fse_up) {
		spin_unlock_bh(&p->lock);
		return;
	}

	p->is_wifi_fse_up = false;
	kref_put(&p->fse_ops_ref, ppe_drv_fse_ops_free);
	spin_unlock_bh(&p->lock);
}
EXPORT_SYMBOL(ppe_drv_fse_ops_unregister);

/**
 * ppe_drv_fse_ops_register()
 *	Register FSE rule add/delete callbacks. This function will be called from Wi-Fi driver
 */
bool ppe_drv_fse_ops_register(struct ppe_drv_fse_ops *ops)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_fse_ops *ops_internal;

	if (!ops->create_fse_rule || !ops->destroy_fse_rule) {
		ppe_drv_warn("%p: Invalid FSE ops passed from Wi-FI driver\n", p);
		return false;
	}

	ops_internal = (struct ppe_drv_fse_ops *)kzalloc(sizeof(struct ppe_drv_fse_ops), GFP_ATOMIC);
	if (!ops_internal) {
		ppe_drv_warn("%p: FSE ops registration failed\n", p);
		return false;
	}

	spin_lock_bh(&p->lock);
	if (p->fse_ops) {
		ppe_drv_warn("%p: FSE ops registration already done\n", p);
		spin_unlock_bh(&p->lock);
		kfree(ops_internal);
		return false;
	}

	ops_internal->create_fse_rule = ops->create_fse_rule;
	ops_internal->destroy_fse_rule = ops->destroy_fse_rule;
	p->fse_ops = ops_internal;
	p->is_wifi_fse_up = true;
	kref_init(&p->fse_ops_ref);
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("%p: FSE ops registration done successfully\n", p);
	return true;
}
EXPORT_SYMBOL(ppe_drv_fse_ops_register);

/*
 * ppe_drv_get_dentry()
 *	Get PPE driver debugfs dentry
 */
struct dentry *ppe_drv_get_dentry()
{
	struct ppe_drv *p = ppe_drv_gbl;
	return p->dentry;
}
EXPORT_SYMBOL(ppe_drv_get_dentry);

static const struct of_device_id ppe_drv_dt_ids[] = {
	{ .compatible =  "qcom,nss-ppe" },
	{},
};
MODULE_DEVICE_TABLE(of, ppe_drv_dt_ids);

/*
 * ppe_drv_confgiure_ucast_prio_map_tbl
 *	Configure unicast priority map table for RFS/DS flows
 */
bool ppe_drv_confgiure_ucast_prio_map_tbl(uint8_t profile_id, uint8_t *prio_map)
{
	struct ppe_drv *p = ppe_drv_gbl;
	uint8_t pri_class;
	uint8_t int_pri;
	sw_error_t ret;

	/*
	 * Set the priority class value for every possible priority.
	 */
	for (int_pri = 0; int_pri < PPE_DRV_MAX_PRIORITY; int_pri++) {
		pri_class = prio_map[int_pri];

		/*
		 * Configure priority class for Profile 9 used by RFS and DS.
		 */
		ret = fal_ucast_priority_class_set(PPE_DRV_SWITCH_ID, profile_id, int_pri, pri_class);
		if (ret != SW_OK) {
			ppe_drv_warn("%p Failed to configure ucast priority class for profile_id %d, int_pri: %d with err: %d\n",
					p, profile_id, int_pri, ret);
			return false;
		}

		ppe_drv_info("profile_id: %d, int_priority: %d, pri_class: %d\n", profile_id, int_pri, pri_class);
	}

	return true;
}
EXPORT_SYMBOL(ppe_drv_confgiure_ucast_prio_map_tbl);

/*
 * ppe_drv_port_mcast_priority_class_set()
 *	Set multicast priority to queue class mapping for a port.
 */
bool ppe_drv_port_mcast_priority_class_set(uint32_t port_id, uint8_t priority, uint8_t queue_class)
{
	struct ppe_drv *p = ppe_drv_gbl;
	sw_error_t ret;

	if (port_id >= PPE_DRV_PHYSICAL_MAX) {
		ppe_drv_warn("%p: Invalid port ID: %u\n", p, port_id);
		return false;
	}

	if (priority >= PPE_DRV_MAX_PRIORITY) {
		ppe_drv_warn("%p: Invalid priority: %u\n", p, priority);
		return false;
	}

	if (queue_class > PPE_DRV_MCAST_QUEUE_CLASS_MAX) {
		ppe_drv_warn("%p: Invalid queue class: %u (must be 0-%u)\n",
			     p, queue_class, PPE_DRV_MCAST_QUEUE_CLASS_MAX);
		return false;
	}

	ret = fal_port_mcast_priority_class_set(PPE_DRV_SWITCH_ID, port_id, priority, queue_class);
	if (ret != SW_OK) {
		ppe_drv_warn("%p: Failed to set mcast priority class for port %u, priority %u, error: %d\n",
			     p, port_id, priority, ret);
		return false;
	}

	ppe_drv_trace("%p: Successfully set mcast priority class for port %u, priority %u to class %u\n",
		      p, port_id, priority, queue_class);
	return true;
}
EXPORT_SYMBOL(ppe_drv_port_mcast_priority_class_set);

/*
 * ppe_drv_wlan_rfs_enable_set()
 *	Sets the value of the global variable to enable.
 */
void ppe_drv_wlan_rfs_enable_set(bool enable)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->lock);
	p->rfs.wlan_rfs_enable = enable;
	spin_unlock_bh(&p->lock);

	return;
}
EXPORT_SYMBOL(ppe_drv_wlan_rfs_enable_set);

/*
 * ppe_drv_cpu_port_init()
 *	Initialize the CPU port.
 */
ppe_drv_ret_t ppe_drv_cpu_port_init(void)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_iface *iface;
	struct ppe_drv_l3_if *l3_if;
	struct ppe_drv_port *port;
	uint16_t max_mtu = PPE_DRV_PORT_JUMBO_MAX;
	uint8_t status = PPE_DRV_RET_SUCCESS;

	/*
	 * Allocate and initialize the inline port
	 */
	iface = ppe_drv_iface_alloc(PPE_DRV_IFACE_TYPE_PHYSICAL, NULL);
	if (!iface) {
		ppe_drv_warn("%px: failed to allocate PPE interface for init", p);
		status = PPE_DRV_RET_IFACE_INVALID;
		goto iface_fail;
	}

	spin_lock_bh(&p->lock);
	p->rfs.cpu_iface = iface;

	port = ppe_drv_port_alloc(PPE_DRV_PORT_CPU_TYPE, NULL, false, false);
	if (!port) {
		ppe_drv_warn("%p: unable to get a valid port of type(%d), iface index: %u\n", iface, PPE_DRV_PORT_CPU_TYPE, iface->index);
		status = PPE_DRV_RET_PORT_ALLOC_FAIL;
		goto port_fail;
	}

	l3_if = ppe_drv_l3_if_alloc(PPE_DRV_L3_IF_TYPE_CPU_PORT);
	if (!l3_if) {
		ppe_drv_warn("%p: unable to get a valid l3_if of type(%d), iface index: %u\n", iface, PPE_DRV_L3_IF_TYPE_CPU_PORT, iface->index);
		status = PPE_DRV_RET_L3_IF_ALLOC_FAIL;
		goto l3_if_fail;
	}

	if (!ppe_drv_port_mtu_mru_set(port, max_mtu, max_mtu)) {
		ppe_drv_warn("%p: PORT MTU MRU failed, iface index: %u\n", iface, iface->index);
		status = PPE_DRV_RET_MTU_CFG_FAIL;
		goto fail;
	}

	if (!ppe_drv_l3_if_mtu_mru_set(l3_if, max_mtu, max_mtu)) {
		ppe_drv_warn("%p: L3_IF MTU MRU failed, iface index: %u\n", iface, iface->index);
		status = PPE_DRV_RET_MTU_CFG_FAIL;
		goto fail;
	}

	/*
	 * Attach l3_if to port
	 */
	if (!ppe_drv_port_l3_if_attach(port, l3_if)) {
		ppe_drv_warn("%p: unable to attach valid l3_if(%p) to port(%p), iface index(%u)\n", iface, l3_if, port, iface->index);
		status = PPE_DRV_RET_L3_IF_PORT_ATTACH_FAIL;
		goto fail;
	}

	port->port_l3_if = l3_if;
	ppe_drv_iface_port_set(iface, port);
	ppe_drv_iface_l3_if_set(iface, l3_if);

	spin_unlock_bh(&p->lock);

	return PPE_DRV_RET_SUCCESS;

fail:
	ppe_drv_l3_if_deref(l3_if);
l3_if_fail:
	ppe_drv_port_deref(port);
port_fail:
	spin_unlock_bh(&p->lock);
	ppe_drv_iface_deref(iface);
iface_fail:
	return status;
}

/*
 * ppe_drv_probe()
 *	probe the PPE driver
 */
static int ppe_drv_probe(struct platform_device *pdev)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct device_node *np;
	fal_ppe_tbl_caps_t cap;
	int i = 0;
	uint8_t status;

	np = of_node_get(pdev->dev.of_node);

	fal_ppe_capacity_get(PPE_DRV_SWITCH_ID, &cap);

	/*
	 * Fill number of table entries
	 */
	p->l3_if_num = cap.l3_if_caps;
	p->port_num = cap.port_caps;
	p->vsi_num = cap.vsi_caps;
	p->pub_ip_num = cap.pub_ip_caps;
	p->host_num = cap.host_caps;
	p->flow_num = cap.flow_caps;
	p->pppoe_session_max = cap.pppoe_session_caps;
	p->nexthop_num = cap.nexthop_caps;
	p->sc_num = cap.service_code_caps;
	p->iface_num = p->l3_if_num + p->port_num + p->pppoe_session_max;
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	p->prefix_num = cap.ipv6_prefix_caps;
	p->iid_num = cap.ipv6_iid_caps;
#endif

	/*
	 * Initialize locks
	 * Initialize locks before its first usage in "ppe_drv_tun_global_init".
	 */
	spin_lock_init(&p->lock);
	spin_lock_init(&p->stats_lock);
	spin_lock_init(&p->notifier_lock);


	if (!ppe_drv_hash_init()) {
		return -1;
	}

	if (!ppe_drv_l3_route_ctrl_init(p)) {
		return -1;
	}

#ifdef PPE_DRV_PKT_PADDING_STRIP
	if (!ppe_drv_pkt_edit_init()) {
		return -1;
	}
#endif

#ifdef PPE_TUNNEL_ENABLE
	if (!ppe_drv_tun_global_init(p)) {
		ppe_drv_warn("%p: failed to do global config init for tunnels", p);
		return -1;
	}
#endif

	p->pub_ip = ppe_drv_pub_ip_entries_alloc();
	if (!p->pub_ip) {
		ppe_drv_warn("%p: failed to allocate pub_ip entries", p);
		goto fail;
	}

	p->l3_if = ppe_drv_l3_if_entries_alloc();
	if (!p->l3_if) {
		ppe_drv_warn("%p: failed to allocate l3_if entries", p);
		goto fail;
	}

	p->vsi = ppe_drv_vsi_entries_alloc();
	if (!p->vsi) {
		ppe_drv_warn("%p: failed to allocate vsi entries", p);
		goto fail;
	}

	p->pppoe = ppe_drv_pppoe_entries_alloc();
	if (!p->pppoe) {
		ppe_drv_warn("%p: failed to allocate PPPoe entries", p);
		goto fail;
	}

	p->port = ppe_drv_port_entries_alloc();
	if (!p->port) {
		ppe_drv_warn("%p: failed to allocate Port entries", p);
		goto fail;
	}

	p->sc = ppe_drv_sc_entries_alloc();
	if (!p->sc) {
		ppe_drv_warn("%p: failed to allocate service code entries", p);
		goto fail;
	}

	p->iface = ppe_drv_iface_entries_alloc();
	if (!p->iface) {
		ppe_drv_warn("%p: failed to allocate iface entries", p);
		goto fail;
	}

	p->nexthop = ppe_drv_nexthop_entries_alloc();
	if (!p->nexthop) {
		ppe_drv_warn("%p: failed to allocate nexthop entries", p);
		goto fail;
	}

	p->host = ppe_drv_host_entries_alloc();
	if (!p->host) {
		ppe_drv_warn("%p: failed to allocate host entries", p);
		goto fail;
	}

	p->flow = ppe_drv_flow_entries_alloc();
	if (!p->flow) {
		ppe_drv_warn("%p: failed to allocate flow entries", p);
		goto fail;
	}

	p->cc = ppe_drv_cc_entries_alloc();
	if (!p->cc) {
		ppe_drv_warn("%p: failed to allocate cpu-code entries", p);
		goto fail;
	}

	ppe_drv_exception_init();
	status = ppe_drv_cpu_port_init();
	if (status) {
		ppe_drv_warn("%p: failed to initialize cpu port, error code = %u\n", p, status);
		goto fail;
	}

	if (!ppe_drv_phy_port_base_queue_init(p)) {
		ppe_drv_warn("%p: failed to initialize physical port base queue\n", p);
		goto fail;
	}

	if (!ppe_drv_confgiure_ucast_prio_map_tbl(PPE_DRV_REDIR_PROFILE_ID, ppe_drv_redir_prio_map)) {
		ppe_drv_warn("%p: failed to configure ucast priority class setting\n", p);
		goto fail;
	}

#ifdef NSS_PPE_DRV_HW_GRO
	if (!ppe_drv_confgiure_ucast_prio_map_tbl(PPE_DRV_GRO_PROFILE_ID, ppe_drv_gro_prio_map)) {
		ppe_drv_warn("%p: failed to configure ucast priority class setting\n", p);
		goto fail;
	}
#endif

	/*
	 *
	 * TODO: Check if we need to modify timer flag - specially irqsafe
	 * or pinned to a specific core.
	 */
	p->hw_flow_stats_ticks = msecs_to_jiffies(PPE_DRV_HW_FLOW_STATS_MS);
	timer_setup(&p->hw_flow_stats_timer, ppe_drv_hw_stats_sync, 0);

	/* Initialize list */
	INIT_LIST_HEAD(&p->conn_v4);
	INIT_LIST_HEAD(&p->conn_v6);
	INIT_LIST_HEAD(&p->conn_tun_v4);
	INIT_LIST_HEAD(&p->conn_tun_v6);
	INIT_LIST_HEAD(&p->notifier_list_head);

	p->toggled_v4 = false;
	p->toggled_v6 = false;
	p->tun_toggled_v4 = false;
	p->tun_toggled_v6 = false;
	p->disable_port_mtu_check = true;
	p->fse_ops = NULL;
	p->fse_enable = false;
        p->is_wifi_fse_up = false;

#ifdef NSS_PPE_DRV_HW_GRO
	atomic_set(&p->gro_ctx.num_hw_gro_flows, 0);
#endif

	p->tun_gbl.tun_l2tp.l2tp_dport = PPE_DRV_L2TP_DEFAULT_UDP_PORT;
	p->tun_gbl.tun_l2tp.l2tp_sport = PPE_DRV_L2TP_DEFAULT_UDP_PORT;
#ifdef PPE_TUNNEL_ENABLE
	/*
	 * Allocate tunnel specific entries
	 */

	p->ptun_ec = ppe_drv_tun_encap_entries_alloc(p);
	if (!p->ptun_ec) {
		ppe_drv_warn("%p: failed to allocate tunnel encap entries", p);
		goto fail;
	}

	p->ptun_dc = ppe_drv_tun_decap_entries_alloc(p);
	if (!p->ptun_dc) {
		ppe_drv_warn("%p: failed to allocate tunnel decap entries", p);
		goto fail;
	}

	p->ptun_l3_if = ppe_drv_tun_l3_if_entries_alloc(p);
	if (!p->ptun_l3_if) {
		ppe_drv_warn("%p: failed to allocate TL L3 interface entries", p);
		goto fail;
	}

	p->decap_map_entries = ppe_drv_tun_decap_map_entries_alloc(p);
	if (!p->decap_map_entries) {
		ppe_drv_warn("%p: failed to allocate TL MAP LPM table interface entries", p);
		goto fail;
	}

	p->encap_xlate_rules = ppe_drv_tun_encap_xlate_rule_entries_alloc(p);
	if (!p->encap_xlate_rules) {
		ppe_drv_warn("%p: failed to allocate EG edit rule entries", p);
		goto fail;
	}

	p->decap_xlate_rules = ppe_drv_tun_decap_xlate_rule_entries_alloc(p);
	if (!p->decap_xlate_rules) {
		ppe_drv_warn("%p: failed to allocate TL MAP LPM action interface entries", p);
		goto fail;
	}
#endif

	p->acl = ppe_drv_acl_entries_alloc();
	if (!p->acl) {
		ppe_drv_warn("%p: failed to allocate ACL entries", p);
		goto fail;
	}

#ifdef NSS_PPE_FEATURE_DOT1P
	p->dot1p = ppe_drv_dot1p_entries_alloc();
	if (!p->dot1p) {
		ppe_drv_warn("%p: failed to allocate DOT1P entries", p);
		goto fail;
	}

	p->dot1p_policer = ppe_drv_dot1p_policer_entries_alloc();
	if (!p->dot1p_policer) {
		ppe_drv_warn("%p: failed to allocate DOT1P policer entries", p);
		goto fail;
	}
#endif
#ifdef NSS_PPE_FEATURE_GEMPORT
	p->gemport = ppe_drv_gem_port_entries_alloc();
	if (!p->gemport) {
		ppe_drv_warn("%p: failed to allocate GEMPORT entries", p);
		goto fail;
	}
#endif

	p->pol_ctx = ppe_drv_policer_entries_alloc();
	if (!p->pol_ctx) {
		ppe_drv_warn("%p: failed to allocate global policer context", p);
		goto fail;
	}

#ifdef NSS_PPE_PM_COUNTER_FEATURE_SUPPORT
	/* Allocate PM counter management */
	p->pm = ppe_drv_pm_entries_alloc();
	if (!p->pm) {
		ppe_drv_warn("%p: Failed to allocate PM entries", p);
		goto fail;
	}

	/* Allocate PM counter gen management */
	p->pm_gen = ppe_drv_pm_gen_entries_alloc();
	if (!p->pm_gen) {
		ppe_drv_warn("%p: Failed to allocate PM gen entries", p);
		goto fail;
	}
#endif

#ifdef NSS_PPE_DRV_PORT_MGMT_SUPPORT
	p->port_mgmt = ppe_drv_port_mgmt_entries_alloc();
	if (!p->port_mgmt) {
		ppe_drv_warn("%p: Failed to allocate Port Mgmt entries", p);
		goto fail;
	}
#endif

	/* Allocate VLAN entries */
	p->vlan = ppe_drv_vlan_entries_alloc();
	if (!p->vlan) {
		ppe_drv_warn("%p: Failed to allocate VLAN entries", p);
		goto fail;
	}

#ifdef PPE_TUNNEL_ENABLE
	p->pgm = ppe_drv_tun_prgm_prsr_alloc(p);
	if (!p->pgm) {
		ppe_drv_warn("%p: failed to allocate program parser entries", p);
		goto fail;
	}

	p->pgm_udf = ppe_drv_tun_udf_alloc(p);
	if (!p->pgm_udf) {
		ppe_drv_warn("%p: failed to allocate tunnel udf entries", p);
		goto fail;
	}

#ifdef NSS_PPE_TUNNEL_TPR_ENABLE
	p->tun_tpr = ppe_drv_tun_tpr_alloc(p);
	if (!p->tun_tpr) {
		ppe_drv_warn("%p: failed to allocate tunnel tpr entries", p);
		goto fail;
	}
#endif
#endif
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	p->pfx = ppe_drv_nptv6_prefix_entries_alloc();
	if (!p->pfx) {
		ppe_drv_warn("%p: failed to allocate prefix table entries", p);
		goto fail;
	}

	p->iid = ppe_drv_nptv6_iid_entries_alloc();
	if (!p->iid) {
		ppe_drv_warn("%p: failed to allocate IID table entries", p);
		goto fail;
	}
#endif

	for (i = 0; i <  PPE_DRV_PORT_SRC_PROFILE_MAX; i++) {
		p->prof2portmap[i] = -1;
	}

#if defined(PPE_LOOPBACK_PORT_SUPPORT)
	ppe_drv_port_loopback_port_get_info();
#endif

	/*
	 * Take a reference
	 */
	kref_init(&p->ref);

	/*
	 * Start sync timer for hardware stats collection.
	 */
	mod_timer(&p->hw_flow_stats_timer, jiffies + p->hw_flow_stats_ticks);

	/*
	 * Non availability of debugfs directory is not a catastrophy
	 * We can still go ahead with other initialization
	 */
	ppe_drv_stats_debugfs_init();

	ppe_drv_flow_dump_init(p->dentry);
	ppe_drv_if_map_init(p->dentry);

	/*
	 * Initialize the enqueue vports.
	 */
	ppe_drv_port_enq_vp_init();

	nss_ppe_drv_minidump_log(p, sizeof(struct ppe_drv), "ppe_drv");

#ifdef NSS_PPE_FEATURE_DOT1P
	ppe_drv_dot1p_configure_default_rule();
#endif

	return of_platform_populate(np, NULL, NULL, &pdev->dev);

fail:

#ifdef PPE_TUNNEL_ENABLE
	if (p->decap_map_entries) {
		ppe_drv_tun_decap_entries_free(p->decap_map_entries);
		p->decap_map_entries = NULL;
	}

	if (p->encap_xlate_rules) {
		ppe_drv_tun_encap_xlate_rule_entries_free(p->encap_xlate_rules);
		p->encap_xlate_rules = NULL;
	}

	if (p->decap_xlate_rules) {
		ppe_drv_tun_decap_xlate_rule_entries_free(p->decap_xlate_rules);
		p->decap_xlate_rules = NULL;
	}

	if (p->ptun_ec) {
		ppe_drv_tun_encap_entries_free(p->ptun_ec);
		p->ptun_ec = NULL;
	}

	if (p->ptun_dc) {
		ppe_drv_tun_decap_entries_free(p->ptun_dc);
		p->ptun_dc = NULL;
	}

	if (p->ptun_l3_if) {
		ppe_drv_tun_l3_if_entries_free(p->ptun_l3_if);
		p->ptun_l3_if = NULL;
	}
#endif

	if (p->pol_ctx) {
		ppe_drv_policer_entries_free(p->pol_ctx);
		p->pol_ctx = NULL;
	}

#ifdef NSS_PPE_PM_COUNTER_FEATURE_SUPPORT
	if (p->pm) {
		ppe_drv_pm_entries_free(p->pm);
		p->pm = NULL;
	}

	if (p->pm_gen) {
		ppe_drv_pm_entries_free(p->pm_gen);
		p->pm_gen = NULL;
	}
#endif
#ifdef NSS_PPE_DRV_PORT_MGMT_SUPPORT
	if (p->port_mgmt) {
		ppe_drv_port_mgmt_entries_free(p->port_mgmt);
		p->port_mgmt = NULL;
	}
#endif
	if (p->vlan) {
		ppe_drv_vlan_entries_free(p->vlan);
		p->vlan = NULL;
	}

	if (p->acl) {
		ppe_drv_acl_entries_free(p->acl);
		p->acl = NULL;
	}

#ifdef NSS_PPE_FEATURE_DOT1P
	if (p->dot1p) {
		ppe_drv_dot1p_entries_free(p->dot1p);
		p->dot1p = NULL;
	}

	if (p->dot1p_policer) {
		ppe_drv_dot1p_policer_entries_free(p->dot1p_policer);
		p->dot1p_policer = NULL;
	}
#endif
#ifdef NSS_PPE_FEATURE_GEMPORT
	if (p->gemport) {
		ppe_drv_gem_port_entries_free(p->gemport);
		p->gemport = NULL;
	}
#endif

	if (p->pub_ip) {
		ppe_drv_pub_ip_entries_free(p->pub_ip);
		p->pub_ip = NULL;
	}

	if (p->l3_if) {
		ppe_drv_l3_if_entries_free(p->l3_if);
		p->l3_if = NULL;
	}

	if (p->vsi) {
		ppe_drv_vsi_entries_free(p->vsi);
		p->vsi = NULL;
	}

	if (p->pppoe) {
		ppe_drv_pppoe_entries_free(p->pppoe);
		p->pppoe = NULL;
	}

	if (p->port) {
		ppe_drv_port_entries_free(p->port);
		p->port = NULL;
	}

	if (p->sc) {
		ppe_drv_sc_entries_free(p->sc);
		p->sc = NULL;
	}

	if (p->iface) {
		ppe_drv_iface_entries_free(p->iface);
		p->iface = NULL;
	}

	if (p->nexthop) {
		ppe_drv_nexthop_entries_free(p->nexthop);
		p->nexthop = NULL;
	}

	if (p->host) {
		ppe_drv_host_entries_free(p->host);
		p->host = NULL;
	}

	if (p->flow) {
		ppe_drv_flow_entries_free(p->flow);
		p->flow = NULL;
	}

	if (p->cc) {
		ppe_drv_cc_entries_free(p->cc);
		p->cc = NULL;
	}

#ifdef PPE_TUNNEL_ENABLE
	if (p->pgm) {
		ppe_drv_tun_prgm_prsr_free(p->pgm);
		p->pgm = NULL;
	}

	if (p->pgm_udf) {
		ppe_drv_tun_udf_free(p->pgm_udf);
		p->pgm_udf = NULL;
	}

	if (p->ecap_hdr_ctrl) {
		ppe_drv_tun_encap_hdr_ctrl_free(p->ecap_hdr_ctrl);
		p->ecap_hdr_ctrl = NULL;
	}

#ifdef NSS_PPE_TUNNEL_TPR_ENABLE
	if (p->tun_tpr) {
		ppe_drv_tun_tpr_free(p->tun_tpr);
		p->tun_tpr = NULL;
	}
#endif
#endif
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	if (p->pfx) {
		ppe_drv_nptv6_prefix_entries_free(p->pfx);
		p->pfx = NULL;
	}

	if (p->iid) {
		ppe_drv_nptv6_iid_entries_free(p->iid);
		p->iid = NULL;
	}
#endif

	ppe_drv_flow_dump_exit();
	ppe_drv_if_map_exit();

	return -1;
}

/*
 * ppe_drv_remove()
 *	remove the ppe driver
 */
static int ppe_drv_remove(struct platform_device *pdev)
{
	struct ppe_drv *p = platform_get_drvdata(pdev);
	int i = 0;

	ppe_drv_stats_debugfs_exit();

	for (i = 0; i <  PPE_DRV_PORT_SRC_PROFILE_MAX; i++) {
		p->prof2portmap[i] = -1;
	}

	if (p->pub_ip) {
		ppe_drv_pub_ip_entries_free(p->pub_ip);
		p->pub_ip = NULL;
	}

	if (p->l3_if) {
		ppe_drv_l3_if_entries_free(p->l3_if);
		p->l3_if = NULL;
	}

	if (p->vsi) {
		ppe_drv_vsi_entries_free(p->vsi);
		p->vsi = NULL;
	}

	if (p->pppoe) {
		ppe_drv_pppoe_entries_free(p->pppoe);
		p->pppoe = NULL;
	}

	if (p->port) {
		ppe_drv_port_entries_free(p->port);
		p->port = NULL;
	}

	if (p->sc) {
		ppe_drv_sc_entries_free(p->sc);
		p->sc = NULL;
	}

	if (p->iface) {
		ppe_drv_iface_entries_free(p->iface);
		p->iface = NULL;
	}

	if (p->nexthop) {
		ppe_drv_nexthop_entries_free(p->nexthop);
		p->nexthop = NULL;
	}

	if (p->host) {
		ppe_drv_host_entries_free(p->host);
		p->host = NULL;
	}

	if (p->flow) {
		ppe_drv_flow_entries_free(p->flow);
		p->flow = NULL;
	}

	if (p->cc) {
		ppe_drv_cc_entries_free(p->cc);
		p->cc = NULL;
	}

#ifdef PPE_TUNNEL_ENABLE
	if (p->ptun_ec) {
		ppe_drv_tun_encap_entries_free(p->ptun_ec);
		p->ptun_ec = NULL;
	}

	if (p->ptun_dc) {
		ppe_drv_tun_decap_entries_free(p->ptun_dc);
		p->ptun_dc = NULL;
	}

	if (p->ptun_l3_if) {
		ppe_drv_tun_l3_if_entries_free(p->ptun_l3_if);
		p->ptun_l3_if = NULL;
	}

	if (p->decap_map_entries) {
		ppe_drv_tun_decap_entries_free(p->decap_map_entries);
		p->decap_map_entries = NULL;
	}

	if (p->encap_xlate_rules) {
		ppe_drv_tun_encap_xlate_rule_entries_free(p->encap_xlate_rules);
		p->encap_xlate_rules = NULL;
	}

	if (p->decap_xlate_rules) {
		ppe_drv_tun_decap_xlate_rule_entries_free(p->decap_xlate_rules);
		p->decap_xlate_rules = NULL;
	}
#endif

	if (p->acl) {
		ppe_drv_acl_entries_free(p->acl);
		p->acl = NULL;
	}

#ifdef NSS_PPE_FEATURE_DOT1P
	if (p->dot1p) {
		ppe_drv_dot1p_entries_free(p->dot1p);
		p->dot1p = NULL;
	}

	if (p->dot1p_policer) {
		ppe_drv_dot1p_policer_entries_free(p->dot1p_policer);
		p->dot1p_policer = NULL;
	}
#endif
#ifdef NSS_PPE_FEATURE_GEMPORT
	if (p->gemport) {
		ppe_drv_gem_port_entries_free(p->gemport);
		p->gemport = NULL;
	}
#endif

#ifdef PPE_TUNNEL_ENABLE
	ppe_drv_tun_vxlan_deconfigure(p);
#endif

	if (p->pol_ctx) {
		ppe_drv_policer_entries_free(p->pol_ctx);
		p->pol_ctx = NULL;
	}

#ifdef NSS_PPE_PM_COUNTER_FEATURE_SUPPORT
	if (p->pm) {
		ppe_drv_pm_entries_free(p->pm);
		p->pm = NULL;
	}

	if (p->pm_gen) {
		ppe_drv_pm_entries_free(p->pm_gen);
		p->pm_gen = NULL;
	}
#endif

	if (p->vlan) {
		ppe_drv_vlan_entries_free(p->vlan);
		p->vlan = NULL;
	}

	if (p->fse_ops) {
		ppe_drv_warn("FSE ops still registered while ppe module getting removed\n");
	}

#ifdef PPE_TUNNEL_ENABLE
	if (p->pgm) {
		ppe_drv_tun_prgm_prsr_free(p->pgm);
		p->pgm = NULL;
	}

	if (p->pgm_udf) {
		ppe_drv_tun_udf_free(p->pgm_udf);
		p->pgm_udf = NULL;
	}

	if (p->ecap_hdr_ctrl) {
		ppe_drv_tun_encap_hdr_ctrl_free(p->ecap_hdr_ctrl);
		p->ecap_hdr_ctrl = NULL;
	}

#ifdef NSS_PPE_TUNNEL_TPR_ENABLE
	if (p->tun_tpr) {
		ppe_drv_tun_tpr_free(p->tun_tpr);
		p->tun_tpr = NULL;
	}
#endif
#endif
#ifdef PPE_DRV_NPTV6_HW_SUPPORT
	if (p->pfx) {
		ppe_drv_nptv6_prefix_entries_free(p->pfx);
		p->pfx = NULL;
	}

	if (p->iid) {
		ppe_drv_nptv6_iid_entries_free(p->iid);
		p->iid = NULL;
	}
#endif

	ppe_drv_flow_dump_exit();
	ppe_drv_if_map_exit();

	return 0;
}

/*
 * ppe_drv_notify_change_upper_handler()
 *	Call registered notifiers with ppe drv
 *	To-do: Introduce new file for ppe-drv notifier functions
 */
static void ppe_drv_notify_change_upper_handler(void *ptr, int event)
{
	struct ppe_drv_notifier_ops *iterator;
	struct ppe_drv *p = ppe_drv_gbl;
	struct netdev_notifier_info *info = (struct netdev_notifier_info *)ptr;
	struct netdev_notifier_changeupper_info *cu_info = (struct netdev_notifier_changeupper_info *)info;

	spin_lock_bh(&p->notifier_lock);
	if (cu_info->linking) {
		ppe_drv_trace("%px: Linking ppe drv event: %d\n", ptr, event);
		list_for_each_entry(iterator, &p->notifier_list_head, entry) {
			if (iterator->notifier_call) {
				(iterator->notifier_call)(iterator, event, info);
			}
		}
		spin_unlock_bh(&p->notifier_lock);
		return;
	}

	/*
	 * Call notifier in reverse order during unlinking
	 * to clear the database in the correct order
	 */
	ppe_drv_trace("%px: Unlinking ppe drv event: %d\n", ptr, event);
	list_for_each_entry_reverse(iterator, &p->notifier_list_head, entry) {
		if (iterator->notifier_call) {
			(iterator->notifier_call)(iterator, event, ptr);
		}
	}
	spin_unlock_bh(&p->notifier_lock);
}

/*
 * ppe_drv_handle_netdev_event()
 *	Handle events received from network stack
 */
static int ppe_drv_handle_netdev_event(struct notifier_block *nb,
		unsigned long event, void *ptr)
{
	switch (event) {
	case NETDEV_CHANGEUPPER:
		ppe_drv_notify_change_upper_handler(ptr, PPE_DRV_EVENT_CHANGEUPPER);
		break;

	default:
		 ppe_drv_trace("Event not supported through ppe-drv: %ld\n", event);
	}

	return NOTIFY_DONE;
}

/* register netdev notifier callback */
static struct notifier_block nss_ppe_netdevice __read_mostly = {
	.notifier_call = ppe_drv_handle_netdev_event,
};

/*
 * ppe_drv_platform
 *	platform device instance
 */
static struct platform_driver ppe_drv_platform = {
	.probe		= ppe_drv_probe,
	.remove		= ppe_drv_remove,
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "nss-ppe",
		.of_match_table = of_match_ptr(ppe_drv_dt_ids),
	},
};

/*
 * ppe_drv_notifier_ops_register()
 *	Register notifier operations
 */
void ppe_drv_notifier_ops_register(struct ppe_drv_notifier_ops *notifier_ops)
{
	struct list_head *ptr;
	struct ppe_drv_notifier_ops *entry_pnb;
	struct ppe_drv *p = ppe_drv_gbl;

	ppe_drv_trace("%px: Add notifier callback with priority: %d\n", notifier_ops, notifier_ops->priority);

	spin_lock_bh(&p->notifier_lock);
	list_for_each(ptr, &p->notifier_list_head) {
		entry_pnb = list_entry(ptr, struct ppe_drv_notifier_ops, entry);
		if (entry_pnb->priority < notifier_ops->priority) {
			list_add(&notifier_ops->entry, &entry_pnb->entry);
			spin_unlock_bh(&p->notifier_lock);
			return;
		}
	}

	list_add(&notifier_ops->entry, &p->notifier_list_head);
	spin_unlock_bh(&p->notifier_lock);
}
EXPORT_SYMBOL(ppe_drv_notifier_ops_register);

/*
 * ppe_drv_notifier_ops_unregister()
 *	Unregister notifier operations
 */
void ppe_drv_notifier_ops_unregister(struct ppe_drv_notifier_ops *notifier_ops)
{
	struct ppe_drv *p = ppe_drv_gbl;

	spin_lock_bh(&p->notifier_lock);
	list_del(&notifier_ops->entry);
	spin_unlock_bh(&p->notifier_lock);
}
EXPORT_SYMBOL(ppe_drv_notifier_ops_unregister);

/*
 * ppe_drv_set_ppe_if_bm_to_port()
 *	API to set the OFFLOAD flag in the port from the interface bitmask
 */
static void ppe_drv_set_ppe_if_bm_to_port(void)
{
	struct ppe_drv_port *drv_port = NULL;
	struct ppe_drv *p = ppe_drv_gbl;
	unsigned int if_bit_count = __builtin_popcount(PPE_DRV_PORT_OFFLOAD_MAX_VAL);
	uint32_t i;

	spin_lock_bh(&p->lock);
	for (i = 1; i <= if_bit_count; i++) {
		drv_port = ppe_drv_port_from_port_num(i);
		if (!drv_port) {
			ppe_drv_warn("failed in getting drv port from %d port\n", i);
			continue;
		}

		if (if_bm_to_offload & (1 << (i - 1))) {
			drv_port->flags |= PPE_DRV_PORT_FLAG_OFFLOAD_ENABLED;
		} else {
			drv_port->flags &= ~PPE_DRV_PORT_FLAG_OFFLOAD_ENABLED;
		}
	}
	spin_unlock_bh(&p->lock);
}

/*
 * ppe_drv_if_bm_to_offload_handler()
 *	API to configure the interface bitmask where the offload is enabled
 */
static int ppe_drv_if_bm_to_offload_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	uint32_t current_value;

	/*
	 * Take the current value
	 */
	current_value = if_bm_to_offload;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!write) {
		return ret;
	}

	if (if_bm_to_offload > PPE_DRV_PORT_OFFLOAD_MAX_VAL) {
		ppe_drv_warn("Incorrect value of offload bitmask. Setting it to"
				" default value. Value: %0x, if_bm_to_offload: %0x\n",
				if_bm_to_offload, PPE_DRV_PORT_OFFLOAD_MAX_VAL);
		if_bm_to_offload = current_value;
	}

	ppe_drv_set_ppe_if_bm_to_port();

	ppe_drv_warn("PPE DRV interface bitmask to offload is %0x\n", if_bm_to_offload);
	return ret;
}

/*
 * ppe_drv_disable_port_mtu_check_handler()
 * 	Set disable port mtu config
 */
static int ppe_drv_disable_port_mtu_check_handler(struct ctl_table *table,
						int write, void __user *buffer,
						size_t *lenp, loff_t *ppos)
{
	int ret;
	struct ppe_drv *p = ppe_drv_gbl;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!write) {
		return ret;
	}

	p->disable_port_mtu_check = disable_port_mtu_check;

	ppe_drv_info("Updating disable_port_mtu_check flag as %d\n", p->disable_port_mtu_check);
	return ret;
}

/*
 * ppe_drv_l4_checksum_exception_enable()
 *	API to enable/disable the exception for l4 checksum.
 */
int ppe_drv_l4_checksum_exception_enable(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	uint32_t current_value;

	/*
	 * Take the current value
	 */
	current_value = l4_checksum_exception_enable;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write) {
		return ret;
	}

	/*
	 * Return if you are trying to set the same value.
	 */
	if (current_value == l4_checksum_exception_enable) {
		return 0;
	}

	if(ppe_drv_exception_l4_checksum_enable()) {
		ppe_drv_trace("PPE DRV l4 exception is:%d ret:%d\n", l4_checksum_exception_enable, ret);
		return -1;
	}

	ppe_drv_trace("PPE DRV l4 exception is %d\n", l4_checksum_exception_enable);

	return 0;
}

/*
 * ppe_drv_mac_lrn_exception_en()
 *	API to enable/disable the exception for MAC address learn exception.
 */
int ppe_drv_mac_lrn_exception_en(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write) {
		return ret;
	}

	ppe_drv_trace("PPE DRV MAC address learn exception %d\n", mac_lrn_exception_en);

	return 0;
}

#if defined(PPE_LOOPBACK_PORT_SUPPORT)
/*
 * ppe_drv_loopback_port_feature_write_handler()
 *	sysctl handler for loopback port write operation.
 */
static int ppe_drv_loopback_port_feature_write_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int ret;

	ret = proc_douintvec(table, write, buffer, lenp, ppos);

	if (!write) {
		return ret;
	}

	if (loopback_port_ft_type == PPE_DRV_LOOPBACK_PORT_FT_TYPE_NONE) {
		/*
		 * If needed add the code to disable the previously
		 * enabled feature.
		 */

		p->loopback_port_info.ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_NONE;
		memset(p->loopback_port_info.ctx, 0, sizeof(p->loopback_port_info.ctx));
		return ret;
	}

	if (p->loopback_port_info.enabled == false) {
		ppe_drv_warn("Loopback port is not enabled\n");
		return ret;
	}

	switch (loopback_port_ft_type) {
	/*
	 * configure the direction speific information.
	 * configure the service code.
	 * configure the queue to service code mapping.
	 * set p->loopback_port_info.ft_type.
	 */
	default:
		ppe_drv_warn("invalid feature type %u\n", loopback_port_ft_type);
		break;
	}

	return ret;
}
#endif

#ifdef PPE_DRV_ESP_SPI_PASSTH_ENABLE
/*
 * ppe_drv_ipsec_pass_through_en_handler()
 *	Handler for ppe_drv_ipsec_passth_en sysctl
 *
 *	This handler configures IPsec passthrough by managing the flow_key_en_bitmap
 *	in the PPE hardware. The bitmap controls which protocol fields are used for
 *	flow key generation.
 *
 *	Flow key bitmap bits:
 *		- BIT(0): FAL_FLOW_KEY_ROUTING
 *		- BIT(1): FAL_FLOW_KEY_IPSEC_AH  (IPsec Authentication Header)
 *		- BIT(2): FAL_FLOW_KEY_IPSEC_ESP (IPsec Encapsulating Security Payload)
 *		- BIT(3): FAL_FLOW_KEY_IPSEC_NATT (IPsec NAT Traversal)
 *
 *	When ppe_drv_ipsec_passth_en is set to 1:
 *		- Reads the current flow_key_en_bitmap configuration
 *		- Sets BIT(1) and BIT(2) to enable IPsec AH and ESP flow keys
 *		- Writes the updated configuration back to hardware
 *
 *	When ppe_drv_ipsec_passth_en is set to 0:
 *		- Reads the current flow_key_en_bitmap configuration
 *		- Clears BIT(1) and BIT(2) to disable IPsec AH and ESP flow keys
 *		- Writes the updated configuration back to hardware
 */
static int ppe_drv_ipsec_pass_through_en_handler(struct ctl_table *table, int write,
		void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int ret;
	fal_flow_global_cfg_t cfg = {0};
	sw_error_t sw_ret;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);
	if (!write) {
		return ret;
	}

	/*
	 * Validate the input value (should be 0 or 1)
	 */
	if (ppe_drv_ipsec_passth_en != 0 && ppe_drv_ipsec_passth_en != 1) {
		ppe_drv_warn("Invalid value for ppe_drv_ipsec_passth_en. Must be 0 or 1\n");
		ppe_drv_ipsec_passth_en = 0;
		return -EINVAL;
	}

	/*
	 * Read the existing flow_key_en_bitmap value from hardware.
	 * This ensures we preserve other bits that may be set.
	 */
	sw_ret = fal_flow_global_cfg_get(0, &cfg);
	if (sw_ret != SW_OK) {
		ppe_drv_warn("Failed to get flow global config, error: %d\n", sw_ret);
		return -EIO;
	}

	if (ppe_drv_ipsec_passth_en == 1) {
		/*
		 * Enable IPsec passthrough by setting BIT(1) and BIT(2).
		 * Use bitwise OR to set the bits while preserving other bits.
		 * FAL_FLOW_KEY_IPSEC_AH  = BIT(1) - IPsec Authentication Header
		 * FAL_FLOW_KEY_IPSEC_ESP = BIT(2) - IPsec Encapsulating Security Payload
		 */
		cfg.flow_key_en_bitmap |= FAL_FLOW_KEY_IPSEC_AH;
		cfg.flow_key_en_bitmap |= FAL_FLOW_KEY_IPSEC_ESP;
		ppe_drv_info("Enabling IPsec passthrough, flow_key_en_bitmap: 0x%x\n", cfg.flow_key_en_bitmap);
	} else {
		/*
		 * Disable IPsec passthrough by clearing BIT(1) and BIT(2).
		 * Use bitwise AND with complement to clear the bits while preserving other bits.
		 */
		cfg.flow_key_en_bitmap &= ~FAL_FLOW_KEY_IPSEC_AH;
		cfg.flow_key_en_bitmap &= ~FAL_FLOW_KEY_IPSEC_ESP;
		ppe_drv_info("Disabling IPsec passthrough, flow_key_en_bitmap: 0x%x\n", cfg.flow_key_en_bitmap);
	}

	/*
	 * Write the updated configuration back to hardware.
	 * This applies the IPsec passthrough enable/disable setting.
	 */
	sw_ret = fal_flow_global_cfg_set(0, &cfg);
	if (sw_ret != SW_OK) {
		ppe_drv_warn("Failed to set flow global config, error: %d\n", sw_ret);
		return -EIO;
	}

	ppe_drv_info("PPE IPsec passthrough enable set to %d\n", ppe_drv_ipsec_passth_en);

	return 0;
}
#endif

/*
 * ppe_drv_eth2eth_offload_if_bitmap_handler()
 * 	Set eth to eth offload with if bitmap config
 */
static int ppe_drv_eth2eth_offload_if_bitmap_handler(struct ctl_table *table,
						int write, void __user *buffer,
						size_t *lenp, loff_t *ppos)
{
	int ret;
	struct ppe_drv *p = ppe_drv_gbl;

	ret = proc_dointvec(table, write, buffer, lenp, ppos);

	if (!write) {
		return ret;
	}

	p->eth2eth_offload_if_bitmap = eth2eth_offload_if_bitmap;

	ppe_drv_info("Updating eth2eth_offload_if_bitmap flag as %d\n", p->eth2eth_offload_if_bitmap);
	return ret;
}

/*
 * ppe_drv_static_dbg_level_handler()
 *	Set static debug level for ppe-driver.
 */
static int ppe_drv_static_dbg_level_handler(struct ctl_table *table,
					int write, void __user *buffer,
					size_t *lenp, loff_t *ppos)
{
	int ret;
	char *level_str;
	enum ppe_drv_static_dbg_level dbg_level;

	/*
	 * Find the string, return an error if not found
	 */
	ret = proc_dostring(table, write, buffer, lenp, ppos);
	if (ret || !write) {
		return ret;
	}

	level_str = static_dbg_level_str;
	printk("dbg_level: %s", level_str);

	if (!strcmp(level_str, "warn")) {
		dbg_level = PPE_DRV_STATIC_DBG_LEVEL_WARN;
	} else if (!strcmp(level_str, "info")) {
		dbg_level = PPE_DRV_STATIC_DBG_LEVEL_INFO;
	} else if (!strcmp(level_str, "trace")) {
		dbg_level = PPE_DRV_STATIC_DBG_LEVEL_TRACE;
	} else if (!strcmp(level_str, "none")) {
		dbg_level = PPE_DRV_STATIC_DBG_LEVEL_NONE;
	} else {
		printk("Usage: echo '[warn|info|trace|none]' > /proc/sys/ppe/ppe_drv/static_dbg_level\n");
		return -EINVAL;
	}

	if (dbg_level >= PPE_DRV_DEBUG_LEVEL) {
		printk("debug level: %d not compiled in: %d\n", dbg_level, PPE_DRV_DEBUG_LEVEL);
		return -EINVAL;
	}

	static_dbg_level = dbg_level;
	return ret;
}

#ifdef PPE_DRV_PKT_PADDING_STRIP
/*
 * ppe_drv_pkt_padding_handler
 * 	sysctl to enable/disable stripping of packet padding
 */
static int ppe_drv_pkt_padding_handler(struct ctl_table *table,  int write, void __user *buffer, size_t *lenp, loff_t *ppos)
{
	fal_pktedit_padding_t padding = { 0 };
	char *start_ch_ptr = NULL;
	char *end_ch_ptr = NULL;
	char config[32];
	int len, i, ret;
	char *map_name;

	/*
	 * Find the string, return an error if not found
	 */
	ret = proc_dostring(table, write, buffer, lenp, ppos);
	if (ret || !write) {
		return ret;
	}

	map_name = packet_padding;

	/*
	 * echo "route_pad_strip_en=1 br_pad_strip_en=0 tun_inner_pad_strip_en=0" > /proc/sys/ppe/ppe_drv/packet_padding
	 */
	start_ch_ptr = map_name + strspn(map_name, NSS_PPE_DRV_WHITESPACE);
	for (i = 0; *start_ch_ptr; start_ch_ptr = end_ch_ptr + strspn(end_ch_ptr, NSS_PPE_DRV_WHITESPACE), i++) {
		char *config_str, *pad_strip_config;
		int pad_strip_en;

		end_ch_ptr = start_ch_ptr + strcspn(start_ch_ptr, NSS_PPE_DRV_WHITESPACE);
		if (end_ch_ptr != start_ch_ptr) {
			len = end_ch_ptr - start_ch_ptr;
		} else {
			len = strlen(start_ch_ptr);
		}

		if (len <= 32) {
			memcpy(config, start_ch_ptr, len);
		}

		config[len] = '\0';

		/*
		 * Obtain the config, value pair
		 */
		config_str = config;
		pad_strip_config = strsep(&config_str, "=");
		if (pad_strip_config != NULL) {
			pad_strip_en = *config_str - '0';
		} else {
			return -1;
		}

		if ((pad_strip_en != 0) && (pad_strip_en != 1)) {
			pr_err("Invalid config value\n");
			return -1;
		}

		if (!strcmp(pad_strip_config, "route_pad_strip_en")) {
			padding.strip_padding_route_en = pad_strip_en;
			if (padding.strip_padding_route_en) {
				padding.strip_padding_en = 1;
			}
		} else if (!strcmp(pad_strip_config, "br_pad_strip_en")) {
			padding.strip_padding_bridge_en = pad_strip_en;
			if (padding.strip_padding_bridge_en) {
				padding.strip_padding_en = 1;
			}
		} else if (!strcmp(pad_strip_config, "tun_inner_pad_strip_en")) {
			padding.strip_tunnel_inner_padding_en = pad_strip_en;
			if (padding.strip_tunnel_inner_padding_en) {
				padding.strip_padding_en = 1;
			}
		} else {
			pr_err("Invalid config, usage example: echo route_pad_strip_en=1"
					" br_pad_strip_en=0 tun_inner_pad_strip_en=0 > /proc/sys/ppe/ppe_drv/packet_padding/n");
			return -1;
		}
	}

	if (fal_pktedit_padding_set(PPE_DRV_SWITCH_ID, &padding) != SW_OK) {
		ppe_drv_warn("Failed to update strip_padding_en config \n");
		return -1;
	}

	ppe_drv_info("Updating strip_padding_en config to %d\n", padding.strip_padding_en);

	return ret;
}
#endif

/*
 * ppe_drv_upstream_dev_handler()
 *      Set upstream device.
 */
static int ppe_drv_upstream_dev_handler(struct ctl_table *table,
		int write, void __user *buffer,
		size_t *lenp, loff_t *ppos)
{
	int ret;
	char *dev_name;
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp = NULL;
	struct net_device *dev;

	/*
	 * Find the string, return an error if not found
	 */
	ret = proc_dostring(table, write, buffer, lenp, ppos);
	if (ret || !write) {
		return ret;
	}

#if defined(PPE_LOOPBACK_RING_SUPPORT)
	/*
	 * Check if loopback ring is enabled
	 */
	if (!p->loopback_ring_info.enabled) {
		ppe_drv_warn("Loopback ring is not enabled\n");
		return -1;
	}
#else
	/*
	 * TO-DO: enable this support with loopback feature
	 */
	ppe_drv_warn("loopback feature support not enabled for upstream port\n");
	return -1;
#endif

	dev_name = upstream_dev_str;

	spin_lock_bh(&p->lock);

	if (p->upstream_dev) {
		ppe_drv_warn("%p: Upstream port already set\n", p);
		spin_unlock_bh(&p->lock);
		return -1;
	}

	dev = dev_get_by_name(&init_net, dev_name);
	if (!dev) {
		spin_unlock_bh(&p->lock);
		return -1;
	}

	pp = ppe_drv_port_from_dev(dev);
	if (!pp) {
		ppe_drv_warn("%p: No PPE port for given netdevice %s\n", p, dev->name);
		dev_put(dev);
		spin_unlock_bh(&p->lock);
		return -1;
	}

	/*
	 * Configure L2_VP port table for upstream port with LOOPBACK service code
	 */
	if (!ppe_drv_port_l2_vp_sc_config(pp, PPE_DRV_SC_LOOPBACK_RING, 0)) {
		ppe_drv_warn("%p: Error in configuring L2 VP table  %s\n", p, dev->name);
		dev_put(dev);
		spin_unlock_bh(&p->lock);
		return -1;
	}

	/*
	 * For default source profile, we need to set the corresponding loopback ring to map to upstream port for host originated packet
	 */
	ppe_drv_sc_ucast_queue_set(PPE_DRV_SC_LOOPBACK_RING, ppe_drv_port_ucast_queue_get_by_port(pp->port), PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);

	/*
	 * Re-configure L2_SERVICE_TBL for upstream port
	 */
	ret = ppe_drv_sc_in_service_tbl_dest_port(PPE_DRV_SC_LOOPBACK_RING_NEXT, pp->port);
	if (ret != SW_OK) {
		spin_unlock_bh(&p->lock);
		ppe_drv_warn("%p: service code configuration failed for sc", p);
		return false;
        }

	p->upstream_dev = dev;
	dev_put(dev);

	spin_unlock_bh(&p->lock);

	ppe_drv_trace("%p: upstream_dev configured %s\n", p, dev->name);

	return ret;
}

#ifdef PPE_LOOPBACK_PORT_SUPPORT
/*
 * ppe_drv_lpbk_port_info_ctx_fill()
 *	Fill Loopback info in global structure.
 */
bool ppe_drv_lpbk_port_info_ctx_fill(enum ppe_drv_loopback_port_feature_type ft_type)
{
	struct ppe_drv *p = ppe_drv_gbl;
#ifdef PPE_DRV_VEIP_FEATURE_SUPPORT
	int32_t host_queue = -1;
#endif

	if (!ppe_drv_port_loopback_port_get_info()) {
		ppe_drv_warn("%p: loopback port get info failed.", p);
		return false;
	}
	switch(ft_type) {
		case PPE_DRV_LOOPBACK_PORT_FT_TYPE_ACL_PON:
			/*
			 * configure the pon pass information.
			 */
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_ACL_PON;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_SC;

			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC_NEXT].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_ACL_PON;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC_NEXT].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_SC_NEXT;

			p->loopback_port_info.ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_ACL_PON;
			ppe_drv_info("port_id: %d base queue: %d\n", p->loopback_port_info.port_id, p->loopback_port_info.ucastq_start);

			/*
			 * configure the service code.
			 */
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC].sc, p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC_NEXT].sc,
					p->loopback_port_info.port_id);
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC_NEXT].sc, PPE_DRV_SC_NONE, PPE_DRV_PORT_CPU);

			/*
			 * configure the queue to service code mapping.
			 */
			ppe_drv_sc_ucast_queue_set(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_ACL_SC].sc, p->loopback_port_info.ucastq_start,
					PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_REDIR_PROFILE_ID);

			break;
#ifdef PPE_DRV_VEIP_FEATURE_SUPPORT
		case PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_US:
			/*
			 * configure the pon pass information.
			 */
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_US;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_HGU_US_SC;

			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC_NEXT].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_US;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC_NEXT].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_HGU_US_SC_NEXT;

			p->loopback_port_info.ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_US;
			ppe_drv_info("port_id: %d base queue: %d\n", p->loopback_port_info.port_id, p->loopback_port_info.ucastq_start);

			/*
			 * configure the service code.
			 */
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC].sc, p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC_NEXT].sc,
					p->loopback_port_info.port_id);
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_US_SC_NEXT].sc, PPE_DRV_SC_NONE, PPE_DRV_PORT_CPU);

			break;

		case PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_DS:
			/*
			 * Get the queue base for host queues (CPU port 0).
			 */
			host_queue = ppe_drv_port_ucast_queue_get_by_port(PPE_DRV_PORT_CPU);
			if (host_queue < 0) {
				ppe_drv_warn("Failed to get host queue for CPU port\n");
				return false;
			}

			/*
			 * configure the pon pass information.
			 */
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_DS;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_HGU_DS_SC;

			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC_NEXT].dir_ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_DS;
			p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC_NEXT].sc = PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_HGU_DS_SC_NEXT;

			p->loopback_port_info.ft_type = PPE_DRV_LOOPBACK_PORT_FT_TYPE_PON_HGU_DS;
			ppe_drv_info("port_id: %d base queue: %d\n", p->loopback_port_info.port_id, p->loopback_port_info.ucastq_start);

			/*
			 * configure the service code.
			 */
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC].sc, p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC_NEXT].sc,
					p->loopback_port_info.port_id);
			ppe_drv_sc_config(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC_NEXT].sc, PPE_DRV_SC_NONE, PPE_DRV_PORT_CPU);

			/*
			 * configure the queue to service code mapping.
			 */
			ppe_drv_sc_ucast_queue_set(p->loopback_port_info.ctx[PPE_DRV_LOOPBACK_PORT_CTX_PON_HGU_DS_SC_NEXT].sc, host_queue, PPE_DRV_PORT_SRC_PROFILE, PPE_DRV_PORT_SRC_PROFILE);
			break;
#endif

		default:
			ppe_drv_warn("invalid feature type %u\n", ft_type);
			break;
	}
	return true;
}

/*
 * ppe_drv_l2vp_sc_add()
 *	Add the sc configs in L2_VP port tbl.
 */
bool ppe_drv_l2vp_sc_add(uint32_t dest_port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp = NULL;

	/*
	 * Configure lpbk_port.
	 */
	if (!ppe_drv_lpbk_port_info_ctx_fill(PPE_DRV_LOOPBACK_PORT_FT_TYPE_ACL_PON)) {
		ppe_drv_warn("%p: Error in configuring loopback port info.\n", p);
		return false;
	}

	pp = ppe_drv_port_from_port_num(dest_port_id);
	if (!pp) {
		ppe_drv_warn("%p: No PPE port for given netdevice %d\n", p, dest_port_id);
		return false;
	}

	/*
	 * Configure L2_VP port table for UNI port with LOOPBACK service code
	 */
	if (!ppe_drv_port_l2_vp_sc_config(pp, PPE_DRV_SC_LOOPBACK_PORT_FEATURE_PON_SC, p->loopback_port_info.port_id)) {
		ppe_drv_warn("%p: Error in configuring L2 VP table  %d\n", p, dest_port_id);
		return false;
	}

	ppe_drv_trace("the L2 service table is configured\n");
	return true;
}

/*
 * ppe_drv_l2vp_sc_rmv()
 *	Remove the sc configs in L2_VP port tbl.
 */
bool ppe_drv_l2vp_sc_rmv(uint32_t dest_port_id)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp = NULL;

	pp = ppe_drv_port_from_port_num(dest_port_id);
	if (!pp) {
		ppe_drv_warn("%p: No PPE port for given netdevice %d\n", p, dest_port_id);
		return false;
	}

	/*
	 * Reset L2_VP port table for UNI port with LOOPBACK service code
	 */
	if (!ppe_drv_port_l2_vp_sc_reset(pp)) {
		ppe_drv_warn("%p: Error in reset L2 VP table  %d\n", p, dest_port_id);
		return false;
	}

	return true;
}
#endif

/*
 * ppe_drv_src2uni_handler()
 *      Set Source Profile to UNI port mapping.
 */
static int ppe_drv_src2uni_handler(struct ctl_table *table,
		int write, void __user *buffer,
		size_t *lenp, loff_t *ppos)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *pp_local;
	fal_ucast_queue_dest_t q_dst = {0};
	struct ppe_drv_port *pp;
	struct net_device *net_dev;
	struct ppe_drv_port *up;
	int ret, profile;
	char *map_name;
	fal_port_t src_port;
	a_uint32_t src_profile = 0;
	char dev[IFNAMSIZ];
	char *start_ch_ptr = NULL;
	char *end_ch_ptr = NULL;
	sw_error_t sw_err = SW_OK;
	int len, i, port_id = 0;
	uint32_t bitmap = 0;

	/*
	 * Find the string, return an error if not found
	 */
	ret = proc_dostring(table, write, buffer, lenp, ppos);
	if (ret || !write) {
		return ret;
	}

#if defined(PPE_LOOPBACK_RING_SUPPORT)
	/*
	 * Check if loopback ring is enabled
	 */
	if (!p->loopback_ring_info.enabled) {
		ppe_drv_warn("Loopback ring is not enabled\n");
		return -1;
	}
#else
	/*
	 * TO-DO: enable this support with loopback feature
	 */
	ppe_drv_warn("loopback feature support not enabled for upstream port\n");
	return -1;
#endif

	/*
	 * This call expect upstream device to be set
	 */
	if (!p->upstream_dev) {
		ppe_drv_warn("Unable to find source upstream dev\n");
		return -1;
	}

	spin_lock_bh(&p->lock);
	up = ppe_drv_port_from_dev(p->upstream_dev);
	if (!up) {
		ppe_drv_warn("Unable to find source upstream port\n");
		spin_unlock_bh(&p->lock);
		return -1;
	}

	map_name = src2uni_map;

	/*
	 * Parse the input here; input can be of two types:
	 *
	 * Case1: Setup src_profile to device mapping
	 *        echo '<src_profile> <net_device>' > /proc/sys/ppe/ppe_drv/src2uni_map
	 *
	 * Case2: Clear source profile to UNI mapping
	 *        echo '<src_profile> update' > /proc/sys/ppe/ppe_drv/src2uni_map
	 */
	start_ch_ptr = map_name + strspn(map_name, NSS_PPE_DRV_WHITESPACE);
	for (i = 0; *start_ch_ptr; start_ch_ptr = end_ch_ptr + strspn(end_ch_ptr, NSS_PPE_DRV_WHITESPACE), i++) {
		if (i == 0) {
			/*
			 * Process src profile number
			 */
			end_ch_ptr = start_ch_ptr + strcspn(start_ch_ptr, NSS_PPE_DRV_WHITESPACE);
			if (end_ch_ptr != start_ch_ptr) {
				len = end_ch_ptr - start_ch_ptr;
			} else {
				len = strlen(start_ch_ptr);
			}

			src_profile = *start_ch_ptr - '0';
			continue;
		}

		if (src_profile >= PPE_DRV_PORT_SRC_PROFILE_MAX) {
			ppe_drv_warn("Invalid src_profile number given from omci\n");
			spin_unlock_bh(&p->lock);
			return -1;
		}

		end_ch_ptr = start_ch_ptr + strcspn(start_ch_ptr, NSS_PPE_DRV_WHITESPACE);
		if (end_ch_ptr != start_ch_ptr) {
			len = end_ch_ptr - start_ch_ptr;
		} else {
			len = strlen(start_ch_ptr);
		}

		if (len <= IFNAMSIZ) {
			memcpy(dev, start_ch_ptr, len);
		}

		dev[len] = '\0';

		if (!strcmp(dev, "flush")) {
			for (port_id = 1; port_id <= PPE_DRV_PHY_ETH_PORT_MAX; port_id++) {
				pp_local = &p->port[port_id];
				if (pp_local->src_profile == src_profile) {
					sw_err = fal_qm_port_source_profile_set(PPE_DRV_SWITCH_ID, port_id, PPE_DRV_PORT_SRC_PROFILE);
					if (sw_err != SW_OK) {
						ppe_drv_warn("Unable to reset source profile for port\n");
						spin_unlock_bh(&p->lock);
						return -1;
					}

					pp_local->src_profile = PPE_DRV_PORT_SRC_PROFILE;
				}
			}

			/*
			 * Profile 0 is reserved for all default use cases like packet from host/WLAN and downlink traffic.
			 * Hence, running the below loop from source profile 1
			 */
			for (profile = 1; profile <  PPE_DRV_PORT_SRC_PROFILE_MAX; profile++) {
				uint32_t queue_id;
				uint8_t profile = 0;
				uint8_t port_bitmask =  p->prof2portmap[profile];
				if (!port_bitmask)
					continue;

				/*
				 * Run the loop for Physical ports except for port0 and port7
				 */
				for (i = 1; i < (PPE_DRV_PHYSICAL_MAX - 1); i++) {
					pp_local = &p->port[i];

					if (!kref_read(&pp_local->ref_cnt))
						continue;

					if (pp_local->port == up->port)
						continue;

					q_dst.src_profile = src_profile;
					q_dst.dst_port = pp_local->port;

					queue_id = ppe_drv_port_ucast_queue_get_by_port(pp_local->port);
					sw_err = fal_ucast_queue_base_profile_set(PPE_DRV_SWITCH_ID, &q_dst, queue_id, pp_local->profile_id);
					if (sw_err != SW_OK) {
						ppe_drv_warn("%p unable to set port queue base ID", p);
						continue;
					}

				}

				p->prof2portmap[profile] = -1;
			}

			spin_unlock_bh(&p->lock);
			return ret;
		}

		net_dev = dev_get_by_name(&init_net, dev);
		if (!net_dev) {
			ppe_drv_warn("%p: No valid netdevice found for dev: %s\n", net_dev, dev);
			spin_unlock_bh(&p->lock);
			return -1;
		}

		pp = ppe_drv_port_from_dev(net_dev);
		if (!pp) {
			ppe_drv_warn("%p: No PPE port for given netdevice\n", dev);
			dev_put(net_dev);
			spin_unlock_bh(&p->lock);
			return -1;
		}

		src_port = pp->port;
		pp->src_profile = src_profile;
		sw_err = fal_qm_port_source_profile_set(PPE_DRV_SWITCH_ID, src_port, src_profile);
		if (sw_err != SW_OK) {
			ppe_drv_warn("Unable to configure source profile\n");
			dev_put(net_dev);
			spin_unlock_bh(&p->lock);
			return -1;
		}

		/*
		 * Bitmap of grouped ports
		 */
		bitmap |= (1 << pp->port);
		p->prof2portmap[src_profile] |= (1 << pp->port);

		dev_put(net_dev);
	}

	ppe_drv_loopback_sc2queue_mapping(p, src_profile);

	/*
	 * For port isolation, other physical port not mapped to current source profile need to be mapped to upstream port queue
	 */
	for (i = 1; i < (PPE_DRV_PHYSICAL_MAX - 1); i++) {
		pp_local = &p->port[i];

		if (!kref_read(&pp_local->ref_cnt))
			continue;

		if (bitmap & (1 << pp_local->port))
			continue;

		if (pp_local->port == up->port)
			continue;

		q_dst.src_profile = src_profile;
		q_dst.dst_port = pp_local->port;

		sw_err = fal_ucast_queue_base_profile_set(PPE_DRV_SWITCH_ID, &q_dst, ppe_drv_port_ucast_queue_get_by_port(up->port), pp_local->profile_id);
		if (sw_err != SW_OK) {
			ppe_drv_warn("Unable to configure destination source profile\n");
		}
	}

	spin_unlock_bh(&p->lock);

	return ret;
}

/*
 * ppe_drv_get_and_hold_qdisc_netdev()
 * 	Get qdisc netdev from flow
 */
uint32_t ppe_drv_get_qos_tag(int32_t flow_index) {
	return ppe_drv_flow_get_qos_tag(flow_index);
}
EXPORT_SYMBOL(ppe_drv_get_qos_tag);

/*
 * ppe_drv_get_and_hold_qdisc_netdev()
 * 	Get qdisc netdev from flow
 */
struct net_device *ppe_drv_get_and_hold_qdisc_netdev(int32_t flow_index) {
	return ppe_drv_flow_get_and_hold_qdisc_netdev(flow_index);
}
EXPORT_SYMBOL(ppe_drv_get_and_hold_qdisc_netdev);

/*
 * ppe_drv_get_qdisc_rule_flag()
 * 	Get qdisc flags from flow
 */
int8_t ppe_drv_get_qdisc_rule_flag(int32_t flow_index) {
	return ppe_drv_flow_get_qdisc_rule_flag(flow_index);
}
EXPORT_SYMBOL(ppe_drv_get_qdisc_rule_flag);

/*
 * ppe_drv_sub
 *	PPE DRV sub directory
 */
static struct ctl_table ppe_drv_sub[] = {
	{
		.procname	=	"if_bm_to_offload",
		.data		=	&if_bm_to_offload,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	ppe_drv_if_bm_to_offload_handler
	},
	{
		.procname	=	"static_dbg_level",
		.data		=	&static_dbg_level_str,
		.maxlen		=	sizeof(char) * PPE_DRV_STATIC_DBG_LEVEL_STR_LEN,
		.mode		=	0644,
		.proc_handler	=	ppe_drv_static_dbg_level_handler
	},
	{
		.procname       =       "disable_port_mtu_check",
		.data           =       &disable_port_mtu_check,
		.maxlen         =       sizeof(int),
		.mode           =       0644,
		.proc_handler   =       ppe_drv_disable_port_mtu_check_handler
	},
	{
		.procname       =       "eth2eth_offload_if_bitmap",
		.data           =       &eth2eth_offload_if_bitmap,
		.maxlen         =       sizeof(int),
		.mode           =       0644,
		.proc_handler   =       ppe_drv_eth2eth_offload_if_bitmap_handler
	},
	{
		.procname       =       "upstream_dev",
		.data           =       &upstream_dev_str,
		.maxlen         =       sizeof(char) * PPE_DRV_UPSTREAM_DEV_LEVEL_STR_LEN,
		.mode           =       0644,
		.proc_handler   =       ppe_drv_upstream_dev_handler
	},
	{

		.procname       =       "src2uni_map",
		.data           =       &src2uni_map,
		.maxlen         =       sizeof(char) * PPE_DRV_SRC2UNI_LEVEL_STR_LEN,
		.mode           =       0644,
		.proc_handler   =       ppe_drv_src2uni_handler
	},
#ifdef PPE_DRV_PKT_PADDING_STRIP
	{
		.procname       =       "packet_padding",
		.data           =       &packet_padding,
		.maxlen         =       sizeof(char) * PPE_DRV_PACKET_PADDING_STR_LEN,
		.mode           =       0644,
		.proc_handler   =       ppe_drv_pkt_padding_handler
	},
#endif
	{
		.procname	=	"l4_checksum_exception_enable",
		.data		=	&l4_checksum_exception_enable,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	ppe_drv_l4_checksum_exception_enable
	},
	{
		.procname	=	"mac_lrn_exception_en",
		.data		=	&mac_lrn_exception_en,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	ppe_drv_mac_lrn_exception_en
	},

#if defined(PPE_LOOPBACK_PORT_SUPPORT)
	{
		.procname	=	"loopback_port_ft_type",
		.data		=	&loopback_port_ft_type,
		.maxlen		=	sizeof(uint32_t),
		.mode		=	0644,
		.proc_handler   =	ppe_drv_loopback_port_feature_write_handler,
	},
#endif

#ifdef PPE_DRV_ESP_SPI_PASSTH_ENABLE
	{
		.procname	=	"ppe_drv_ipsec_passth_en",
		.data		=	&ppe_drv_ipsec_passth_en,
		.maxlen		=	sizeof(int),
		.mode		=	0644,
		.proc_handler	=	ppe_drv_ipsec_pass_through_en_handler
	},
#endif
	{}
};

/*
 * ppe_drv_mht_port_from_fdb()
 *	Get the port id corresponding to the destination
 *	mac address and vid
 */
int32_t ppe_drv_mht_port_from_fdb(uint8_t *dmac, uint16_t vid)
{
	fal_fdb_entry_t entry = {0};
	sw_error_t err = SW_OK;
	entry.fid = vid;
	entry.port.id = 0;
	entry.type = SW_ENTRY;

	memcpy(&entry.addr, dmac, ETH_ALEN);

	err = fal_fdb_entry_search(PPE_DRV_MHT_SWITCH_ID, &entry);
	if (err != SW_OK) {
		return -1;
	}

	if (entry.port.id > 0) {
		return ffs(entry.port.id) - 1;
	}

	return -1;
}
EXPORT_SYMBOL(ppe_drv_mht_port_from_fdb);

/*
 * ppe_drv_passive_vp_enable_handler()
 *	Handler function to set value of passive_vp_enable.
 */
static int ppe_drv_passive_vp_enable_handler(const char *val, const struct kernel_param *kp)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int res = param_set_bool(val, kp);

	spin_lock_bh(&p->lock);
	p->rfs.passive_vp_enable = passive_vp_enable;
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("Passive VP is %s.\n", (passive_vp_enable ? "enabled" : "disabled"));

	return res;
}

static const struct kernel_param_ops passive_vp_enable_ops = {
	.set = ppe_drv_passive_vp_enable_handler,
	.get = param_get_bool,
};

module_param_cb(passive_vp_enable, &passive_vp_enable_ops, &passive_vp_enable, 0644);

/*
 * ppe_drv_eth_coremask_set_handler()
 *	Handler function to set value of eth_coremask.
 */
static int ppe_drv_eth_coremask_set_handler(const char *val, const struct kernel_param *kp)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int res = param_set_uint(val, kp);

	if ((eth_coremask < PPE_DRV_RFS_COREMASK_MIN) || (eth_coremask > PPE_DRV_RFS_COREMASK_MAX)) {
		printk("Invalid coremask value, should be between %u to %u. Hence setting to default value : %u \n",
						PPE_DRV_RFS_COREMASK_MIN, PPE_DRV_RFS_COREMASK_MAX, PPE_DRV_RFS_COREMASK_DEFAULT);
		eth_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
		res = 0;
	}

	spin_lock_bh(&p->lock);
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_PHYSICAL] = eth_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_PHYSICAL] = eth_coremask;
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("Ethernet coremask value is set to : %u\n", eth_coremask);

	return res;
}

static const struct kernel_param_ops eth_coremask_ops = {
    .set = ppe_drv_eth_coremask_set_handler,
    .get = param_get_uint,
};

module_param_cb(eth_coremask, &eth_coremask_ops, &eth_coremask, 0644);

#ifdef NSS_PPE_DRV_HW_GRO
bool ppe_drv_hw_gro_feature_set(struct net_device *dev, bool enable)
{
	struct ppe_drv *p = ppe_drv_gbl;
	struct ppe_drv_port *port;

	spin_lock_bh(&p->lock);
	port = ppe_drv_port_from_dev(dev);
	if (!port) {
		ppe_drv_warn("PPE port invalid for HW GRO setting for dev:%s", dev->name);
		spin_unlock_bh(&p->lock);
		return false;
	}

	port->hw_gro_en = enable;
	spin_unlock_bh(&p->lock);
	return true;
}
EXPORT_SYMBOL(ppe_drv_hw_gro_feature_set);

/*
 * ppe_drv_eth_gro_coremask_set_handler()
 *	Handler function to set value of eth_gro_coremask.
 */
static int ppe_drv_eth_gro_coremask_set_handler(const char *val, const struct kernel_param *kp)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int res = param_set_uint(val, kp);

	if (!p) {
		ppe_drv_warn("PPE driver not initialized\n");
		return -EINVAL;
	}

	if ((eth_gro_coremask < PPE_DRV_GRO_COREMASK_MIN) || (eth_gro_coremask > PPE_DRV_GRO_COREMASK_MAX)) {
		ppe_drv_warn("Invalid coremask value, should be between %u to %u. Hence setting to default value : %u \n",
						PPE_DRV_GRO_COREMASK_MIN, PPE_DRV_GRO_COREMASK_MAX, PPE_DRV_GRO_COREMASK_DEFAULT);
		eth_gro_coremask = PPE_DRV_GRO_COREMASK_DEFAULT;
		res = 0;
	}

	spin_lock_bh(&p->lock);
	p->gro_ctx.gro_info.coremask = eth_gro_coremask;
	p->gro_ctx.gro_info.shadow_coremask = eth_gro_coremask;
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("Ethernet gro coremask value is set to : %u\n", eth_gro_coremask);

	return res;
}

static const struct kernel_param_ops eth_gro_coremask_ops = {
    .set = ppe_drv_eth_gro_coremask_set_handler,
    .get = param_get_uint,
};

module_param_cb(eth_gro_coremask, &eth_gro_coremask_ops, &eth_gro_coremask, 0644);
#endif

/*
 * ppe_drv_wlan_coremask_set_handler()
 *	Handler function to set value of wlan_coremask.
 */
static int ppe_drv_wlan_coremask_set_handler(const char *val, const struct kernel_param *kp)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int res = param_set_uint(val, kp);

	if ((wlan_coremask < PPE_DRV_RFS_COREMASK_MIN) || (wlan_coremask > PPE_DRV_RFS_COREMASK_MAX)) {
		printk("Invalid coremask value, should be between %u to %u. Hence setting to default value : %u \n",
						PPE_DRV_RFS_COREMASK_MIN, PPE_DRV_RFS_COREMASK_MAX, PPE_DRV_RFS_COREMASK_DEFAULT);
		wlan_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
		res = 0;
	}

	spin_lock_bh(&p->lock);
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_WLAN] = wlan_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_WLAN] = wlan_coremask;
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("WLAN coremask value is set to : %u\n", wlan_coremask);

	return res;
}

static const struct kernel_param_ops wlan_coremask_ops = {
    .set = ppe_drv_wlan_coremask_set_handler,
    .get = param_get_uint,
};

module_param_cb(wlan_coremask, &wlan_coremask_ops, &wlan_coremask, 0644);

/*
 * ppe_drv_tun_coremask_set_handler()
 *	Handler function to set value of tun_coremask.
 */
static int ppe_drv_tun_coremask_set_handler(const char *val, const struct kernel_param *kp)
{
	struct ppe_drv *p = ppe_drv_gbl;
	int res = param_set_uint(val, kp);

	if ((tun_coremask < PPE_DRV_RFS_COREMASK_MIN) || (tun_coremask > PPE_DRV_RFS_COREMASK_MAX)) {
		printk("Invalid coremask value, should be between %u to %u. Hence setting to default value : %u \n",
						PPE_DRV_RFS_COREMASK_MIN, PPE_DRV_RFS_COREMASK_MAX, PPE_DRV_RFS_COREMASK_DEFAULT);
		tun_coremask = PPE_DRV_RFS_COREMASK_DEFAULT;
		res = 0;
	}

	spin_lock_bh(&p->lock);
	p->rfs.coremask[PPE_DRV_RFS_INTERFACE_TYPE_TUNNEL] = tun_coremask;
	p->rfs.shadow_coremask[PPE_DRV_RFS_INTERFACE_TYPE_TUNNEL] = tun_coremask;
	spin_unlock_bh(&p->lock);

	ppe_drv_trace("Tunnel coremask value is set to : %u\n", tun_coremask);

	return res;
}

static const struct kernel_param_ops tun_coremask_ops = {
    .set = ppe_drv_tun_coremask_set_handler,
    .get = param_get_uint,
};

module_param_cb(tun_coremask, &tun_coremask_ops, &tun_coremask, 0644);

/*
 * ppe_drv_module_init()
 *	module init for ppe driver
 */
static int __init ppe_drv_module_init(void)
{
	int ret;

	if (!of_find_compatible_node(NULL, NULL, "qcom,nss-ppe")) {
		ppe_drv_info("PPE device tree node not found\n");
		return -EINVAL;
	}

	/*
	* Allocate ppe_drv_gbl
	*/
	ppe_drv_gbl = kzalloc(sizeof(struct ppe_drv), GFP_KERNEL);
	if(!ppe_drv_gbl) {
		ppe_drv_warn("Failed to allocate global ppe_drv structure\n");
		return -EINVAL;
	}

	if (platform_driver_register(&ppe_drv_platform)) {
		ppe_drv_warn("unable to register the driver\n");
		kfree(ppe_drv_gbl);
		return -EIO;
	}

	ret = register_netdevice_notifier(&nss_ppe_netdevice);
	if (ret) {
		ppe_drv_warn("Failed to register NETDEV notifier, error=%d\n", ret);
		platform_driver_unregister(&ppe_drv_platform);
		kfree(ppe_drv_gbl);
		return -EINVAL;
	}

	ppe_drv_trace("Ethernet coremask value is set to : %u\n", eth_coremask);

	/*
	 * Register sysctl framework for PPE DRV
	 */
	ppe_drv_gbl->ppe_drv_header = register_sysctl("ppe/ppe_drv", ppe_drv_sub);
	if (!ppe_drv_gbl->ppe_drv_header) {
		ppe_drv_warn("sysctl table configuration failed");
		unregister_netdevice_notifier(&nss_ppe_netdevice);
		platform_driver_unregister(&ppe_drv_platform);
		kfree(ppe_drv_gbl);
		return -EINVAL;
	}

	return 0;
}
module_init(ppe_drv_module_init);

/*
 * ppe_drv_module_exit()
 *	module exit for ppe driver
 */
static void __exit ppe_drv_module_exit(void)
{
	unregister_sysctl_table(ppe_drv_gbl->ppe_drv_header);
	ppe_drv_gbl->ppe_drv_header = NULL;
	unregister_netdevice_notifier(&nss_ppe_netdevice);
	platform_driver_unregister(&ppe_drv_platform);
	nss_ppe_drv_minidump_free(ppe_drv_gbl, "ppe_drv");
	kfree(ppe_drv_gbl);
}
module_exit(ppe_drv_module_exit);
MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("NSS PPE driver");
