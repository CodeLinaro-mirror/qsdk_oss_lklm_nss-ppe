/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: ISC
 */

#include <linux/version.h>
#include <linux/debug_mem_usage.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <ppe_drv.h>
#include "ppe_vp_base.h"

#define PPE_VP_FLOW_IDX_FOR_NO_QDISC	-1
extern struct ppe_vp_base vp_base;

/*
 * ppe_vp_rx_process_cb
 * 	Standard Rx handler for packets with Rx VP
 */
bool ppe_vp_rx_process_cb(struct ppe_vp_cb_info *info, void *cb_data)
{
	struct sk_buff *skb = info->skb;
	struct net_device *dev = skb->dev;

	skb->protocol = eth_type_trans(skb, dev);
	skb->fast_xmit = 0;

	/*
	 * Reset the below flags in case any of DS flow is exceptioned.
	 */
	skb->fast_recycled = 0;
	skb->recycled_for_ds = 0;
	mem_debug_update_skb(skb);
	netif_receive_skb(skb);
	return true;
}

/*
 * ppe_vp_rx_dp_list_cb
 *	Forward packets received from nss-dp.
 */
void ppe_vp_rx_dp_list_cb(struct sk_buff_head *head, struct nss_dp_vp_rx_info *rx_info)
{
	struct ppe_vp **vpa = &vp_base.vp_table.vp_allocator[0];
	ppe_vp_list_callback_t dst_list_cb;
	struct ppe_vp_rx_stats *rx_stats;
	struct ppe_vp *dest_vp;
	void *app_data;

	/*
	 * Forward to destination VP
	 */
	if (unlikely(rx_info->dvp < PPE_DRV_VIRTUAL_START) || unlikely(rx_info->dvp > PPE_DRV_VIRTUAL_MAX)) {
		atomic64_add(skb_queue_len(head), &vp_base.base_stats.rx_dvp_invalid);
		goto drop;
	}

	rcu_read_lock();
	dest_vp = rcu_dereference(vpa[PPE_VP_BASE_PORT_TO_IDX(rx_info->dvp)]);
	if (unlikely(!dest_vp || !(dest_vp->flags & PPE_VP_FLAG_VP_ACTIVE))) {
		atomic64_add(skb_queue_len(head), &vp_base.base_stats.rx_dvp_inactive);
		rcu_read_unlock();
		goto drop;
	}

	rx_stats = this_cpu_ptr(dest_vp->vp_stats.rx_stats);
	u64_stats_update_begin(&rx_stats->syncp);
	rx_stats->rx_pkts += skb_queue_len(head);
	rx_stats->rx_bytes += rx_info->batch_bytes;
	u64_stats_update_end(&rx_stats->syncp);

	dst_list_cb = dest_vp->dst_list_cb;
	app_data = dest_vp->dst_cb_data;
	rcu_read_unlock();

	/*
	 * Destination VP user would consume the skb.
	 * User's responsibility to update skb->dev.
	 */
	if (unlikely(!dst_list_cb)) {
		atomic64_add(skb_queue_len(head), &vp_base.base_stats.rx_dvp_no_listcb);
		goto drop;
	}

	dst_list_cb(dest_vp->netdev, head, app_data);
	return;
drop:
	skb_queue_purge(head);
	return;
}

/*
 * ppe_vp_rx_dp_cb
 *	Process packet received from nss-dp.
 */
void ppe_vp_rx_dp_cb(struct sk_buff *skb, struct nss_dp_vp_rx_info *rxi)
{

	struct ppe_vp **vpa = &vp_base.vp_table.vp_allocator[0];
#ifdef NSS_PPE_DRV_HW_GRO
	struct nss_vp_rx_custom_mdata *vp_rx_mdata = &rxi->vp_rx_mdata;
	struct nss_vp_rx_custom_gro_mdata *gro_mdata = NULL;
#endif
	struct ppe_vp_cb_info client_cb_info = {0};
	struct ppe_vp *svp, *dvp;
	int32_t flow_idx = rxi->flow_idx;
	int8_t flags;
	struct net_device *qdisc_dev;

	/*
	 * Forward to destination VP
	 */
	if (likely(rxi->dvp >= PPE_DRV_VIRTUAL_START)) {
		struct ppe_vp_rx_stats *rx_stats;
		struct net_device *dev;

		rcu_read_lock();
		dvp = rcu_dereference(vpa[PPE_VP_BASE_PORT_TO_IDX(rxi->dvp)]);
		if (unlikely(!dvp || !(dvp->flags & PPE_VP_FLAG_VP_ACTIVE))) {
			/*
			 * Drop this packet as destination VP is not active anymore.
			 */
			atomic64_inc(&vp_base.base_stats.rx_dvp_inactive);
			rcu_read_unlock();
			mem_debug_update_skb(skb);
			dev_kfree_skb_any(skb);
			ppe_vp_info("%px: Destination VP:%d is not active anymore, dropping skb:%p\n", dvp, rxi->dvp, skb);
			return;
		}

		dev = dvp->netdev;

		rx_stats = this_cpu_ptr(dvp->vp_stats.rx_stats);

		/*
		 * Make sure device is UP before handling the packets.
		 */
		if (unlikely(!(dev->flags & IFF_UP))) {
			rcu_read_unlock();

			u64_stats_update_begin(&rx_stats->syncp);
			rx_stats->rx_dev_not_up++;
			u64_stats_update_end(&rx_stats->syncp);

			mem_debug_update_skb(skb);
			dev_kfree_skb_any(skb);

			return;
		}

		/*
		 * Pull any fake MAC added by PPE for L3 interfaces.
		 */
		if (dvp->vp_type == PPE_VP_TYPE_SW_L3) {
			struct ethhdr *ethh;
			if (unlikely(!pskb_may_pull(skb, (sizeof(struct ethhdr))))) {
				rcu_read_unlock();

				u64_stats_update_begin(&rx_stats->syncp);
				rx_stats->rx_drops++;
				u64_stats_update_end(&rx_stats->syncp);

				mem_debug_update_skb(skb);
				dev_kfree_skb_any(skb);
				ppe_vp_info("%px: Tx VP:%d skb pull failed dropping skb:%p\n", dvp, rxi->dvp, skb);
				return;
			}

			ethh = (struct ethhdr *)skb->data;
			skb->protocol = ethh->h_proto;
			skb_pull(skb, (sizeof(struct ethhdr)));
		}
		if (rxi->svp >= PPE_DRV_VIRTUAL_START) {
			svp = rcu_dereference(vpa[PPE_VP_BASE_PORT_TO_IDX(rxi->svp)]);
			if (likely(svp && svp->flags & PPE_VP_FLAG_VP_ACTIVE)) {
				skb->skb_iif = svp->netdev_if_num;
				ppe_vp_info("%px: Destination VP:%d skb:%p received from an inactive Rx VP:%d\n", dvp, rxi->dvp, skb, rxi->svp);
			}
		}

		skb->dev = dev;

		u64_stats_update_begin(&rx_stats->syncp);
		rx_stats->rx_pkts++;
		rx_stats->rx_bytes +=skb->len;
		u64_stats_update_end(&rx_stats->syncp);

		/*
		 * If it can be, try forwarding through fast_xmit.
		 */
		if (likely(dvp->flags & PPE_VP_FLAG_VP_FAST_XMIT)) {
			struct ethhdr *ethh;

			/*
			 * Check if valid flow index and valid Qdisc info is received
			 */
			if (likely((flow_idx == PPE_VP_FLOW_IDX_FOR_NO_QDISC))
					|| !(rxi->qdisc_valid)) {
				mem_debug_update_skb(skb);
				if (unlikely(!dev_fast_xmit_vp(skb, dev))) {
					atomic64_inc(&vp_base.base_stats.rx_fastxmit_fails);

					/*
					 * Update the skb protocol field
					 */
					ethh = (struct ethhdr *)skb->data;
					skb->protocol = ethh->h_proto;
					skb_reset_mac_header(skb);
					skb_set_network_header(skb, rxi->l3offset);

					mem_debug_update_skb(skb);
					dev_queue_xmit(skb);
				}

				rcu_read_unlock();
				return;
			}

			flags = ppe_drv_get_qdisc_rule_flag(flow_idx);

			/*
			 * This is the case of Qdisc on any one interface other than bottom
			 */
			if (likely(flags & PPE_DRV_HOST_QDISC_DEV_FAST_XMIT_QDISC)) {

				/*
				 * Update the skb protocol field, since host qdisc is present
				 */
				ethh = (struct ethhdr *)skb->data;
				skb->protocol = ethh->h_proto;
				skb_reset_mac_header(skb);
				skb_set_network_header(skb, rxi->l3offset);

				qdisc_dev = ppe_drv_get_and_hold_qdisc_netdev(flow_idx);
				if (likely(qdisc_dev)) {
					skb->priority = ppe_drv_get_qos_tag(flow_idx);
					mem_debug_update_skb(skb);
					if (likely(dev_fast_xmit_qdisc(skb, qdisc_dev, dev))) {
						dev_put(qdisc_dev);
						rcu_read_unlock();
						return;
					}
				}

				atomic64_inc(&vp_base.base_stats.rx_qdisc_fastxmit_fails);
				mem_debug_update_skb(skb);
				dev_queue_xmit(skb);
				if (unlikely(qdisc_dev)) {
					dev_put(qdisc_dev);
				}

				rcu_read_unlock();
				return;
			}

			/*
			 * When qdisc is on bottom interface, send dev_queue_xmit(bottom_dev)
			 */
			if (likely(flags & PPE_DRV_HOST_QDISC_DEV_QUEUE_XMIT)) {

				/*
				 * Update the skb protocol field, since host qdisc is present
				 */
				ethh = (struct ethhdr *)skb->data;
				skb->protocol = ethh->h_proto;
				skb_reset_mac_header(skb);
				skb_set_network_header(skb, rxi->l3offset);

				qdisc_dev = ppe_drv_get_and_hold_qdisc_netdev(flow_idx);
				if (likely(qdisc_dev)) {
					skb->dev = qdisc_dev;
					skb->priority = ppe_drv_get_qos_tag(flow_idx);
				}

				mem_debug_update_skb(skb);
				dev_queue_xmit(skb);
				if (likely(qdisc_dev)) {
					dev_put(qdisc_dev);
				}

				rcu_read_unlock();
				return;
			}
		}

		/*
		 * Destination VP user would consume the skb.
		 */

		if (unlikely(dvp->dst_cb)) {
			client_cb_info.skb = skb;
			client_cb_info.ip_summed = rxi->ip_summed;
			client_cb_info.napi = rxi->napi;
			client_cb_info.fake_mac_present = rxi->fake_mac;
			client_cb_info.flow_idx = rxi->flow_idx;

			/* Initialize metadata to NONE by default */
			client_cb_info.mdata_info.mdata_type = PPE_VP_CB_MDATA_TYPE_NONE;
#ifdef NSS_PPE_DRV_HW_GRO
			gro_mdata = &vp_rx_mdata->rx_mdata.gro_mdata;
			if (unlikely(gro_mdata->hw_gro_en)) {
				client_cb_info.mdata_info.mdata_type = PPE_VP_CB_MDATA_TYPE_HW_GRO;
				client_cb_info.mdata_info.minfo.gro_info.hw_gro_en = gro_mdata->hw_gro_en;
				client_cb_info.mdata_info.minfo.gro_info.hw_gro_more = gro_mdata->hw_gro_more;
				client_cb_info.mdata_info.minfo.gro_info.hw_gro_psh = gro_mdata->hw_gro_psh;
				client_cb_info.mdata_info.minfo.gro_info.hw_gro_fin = gro_mdata->hw_gro_fin;
			}
#endif
			if (unlikely(!dvp->dst_cb(&client_cb_info, dvp->dst_cb_data))) {
				ppe_vp_info("%px: Destination VP:%d  Tx dev:%s skb:%p \
						dropped by user\n", dvp, rxi->dvp, dev->name, skb);
			}
		} else {
			/*
			 * No registered callback with vp, forward through kernel.
			 */
			struct ethhdr *ethh;
			ethh = (struct ethhdr *)skb->data;
			skb->protocol = ethh->h_proto;
			skb_set_network_header(skb, rxi->l3offset);
			mem_debug_update_skb(skb);
			dev_queue_xmit(skb);
		}

		rcu_read_unlock();
		return;
	}

	/*
	 * VP less than 64 is invalid
	 */
	if (unlikely(rxi->dvp > 0)) {
		atomic64_inc(&vp_base.base_stats.rx_dvp_invalid);
		mem_debug_update_skb(skb);
		dev_kfree_skb_any(skb);
		return;
	}

	/*
	 * It's exceptioned packet, give it to its source to be given
	 * to stack for Rx handling.
	 */
	if (rxi->svp >= PPE_DRV_VIRTUAL_START) {
		struct ppe_vp_rx_stats *rx_stats;
		struct net_device *dev;

		rcu_read_lock();
		svp = rcu_dereference(vpa[PPE_VP_BASE_PORT_TO_IDX(rxi->svp)]);
		if (unlikely(!svp || !(svp->flags & PPE_VP_FLAG_VP_ACTIVE))) {
			/*
			 * Drop this packet as source VP is not valid anymore.
			 */
			atomic64_inc(&vp_base.base_stats.rx_svp_inactive);
			rcu_read_unlock();
			mem_debug_update_skb(skb);
			dev_kfree_skb_any(skb);
			ppe_vp_info("%px: Rx VP:%d is not active anymore, dropping skb:%p\n", svp, rxi->svp, skb);
			return;
		}

		dev = svp->netdev;

		rx_stats = this_cpu_ptr(svp->vp_stats.rx_stats);

		if (svp->vp_type == PPE_VP_TYPE_SW_L3) {
			struct ethhdr *ethh;
			ppe_vp_trace("%px: Rx VP#%d, Rx dev:%p with name: %s: L3 VP \n", svp, rxi->svp, dev, dev->name);

			if (unlikely(!pskb_may_pull(skb, (sizeof(struct ethhdr))))) {
				rcu_read_unlock();

				u64_stats_update_begin(&rx_stats->syncp);
				rx_stats->rx_drops++;
				u64_stats_update_end(&rx_stats->syncp);

				mem_debug_update_skb(skb);
				dev_kfree_skb_any(skb);
				ppe_vp_info("%px: Rx VP:%d skb pull failed dropping skb:%p\n", svp, rxi->svp, skb);
				return;
			}

			ethh = (struct ethhdr *)skb->data;
			skb->protocol = ethh->h_proto;
			skb_pull(skb, (sizeof(struct ethhdr)));
		}

		u64_stats_update_begin(&rx_stats->syncp);
		rx_stats->rx_excp_pkts++;
		rx_stats->rx_excp_bytes += skb->len;
		u64_stats_update_end(&rx_stats->syncp);

		skb_reset_mac_header(skb);
		skb->dev = dev;
		skb->skb_iif = svp->netdev_if_num;

		client_cb_info.skb = skb;
		client_cb_info.ip_summed = rxi->ip_summed;
		client_cb_info.napi = rxi->napi;
		client_cb_info.fake_mac_present = rxi->fake_mac;

		/*
		 * If not processed successfully VP receive handler would free the skb
		 */
		mem_debug_update_skb(skb);
		if (unlikely(!svp->src_cb(&client_cb_info, svp->src_cb_data))) {
			rcu_read_unlock();
			ppe_vp_info("%px: Rx VP:%d Rx dev:%s skb:%p dropped by user\n", svp, rxi->svp, dev->name, skb);
			return;
		}

		/*
		 * skb successfully given to Source VP user.
		 */
		rcu_read_unlock();
		return;
	}

	/*
	 * Packet has neither source or destination VP set
	 */
	atomic64_inc(&vp_base.base_stats.rx_svp_invalid);
	mem_debug_update_skb(skb);
	dev_kfree_skb_any(skb);
	return;
}
