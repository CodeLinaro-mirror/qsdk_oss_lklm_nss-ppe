/*
 * Copyright (c) 2022, 2024 Qualcomm Innovation Center, Inc. All rights reserved.
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
 */

#define PPE_VP_TX_MACID_TAG		0xBB
#define PPE_VP_TX_MACID_TAG_SHIFT	24
#define PPE_VP_TX_MACID_MASK		0x00FFFFFF
#define PPE_VP_TX_INVALID_MACID		0

/*
 * ppe_vp_tx_set_macid_tag()
 *      API to set the valid macid tag in skb->mark for active VP WLAN connection.
 *
 * @param[IN] mark mark of the skb
 *
 * @return
 * updated mark value with macid tag.
 */
static inline uint32_t ppe_vp_tx_set_macid_tag(uint32_t mark) {
	return ((PPE_VP_TX_MACID_TAG << PPE_VP_TX_MACID_TAG_SHIFT) | mark);
}

/*
 * ppe_vp_tx_get_macid()
 *      API to get macid from skb->mark if it has valid macid tag.
 *
 * @param[IN] mark   mark of the skb.
 *
 * @return
 * macid if tag is valid otherwise return PPE_VP_TX_INVALID_MACID.
 */
static inline uint8_t ppe_vp_tx_get_macid(uint32_t mark) {
	uint8_t mark_tag = mark >> PPE_VP_TX_MACID_TAG_SHIFT;

	if (likely(mark_tag == PPE_VP_TX_MACID_TAG)) {
		return (mark & PPE_VP_TX_MACID_MASK);
	}

	return PPE_VP_TX_INVALID_MACID;
}

/*
 * ppe_vp_tx_to_ppe()
 *      API for PPE VP user to enqueue packet for PPE processing
 *
 * @param[IN] vp_num   VP num corresponding to its interface.
 * @param[IN] skb  Socket buffer to be enqueued.
 *
 * @return
 * true if packet is consumed by the API or false if the packet is not consumed.
 */
bool ppe_vp_tx_to_ppe(int32_t vp_num, struct sk_buff *skb);

/*
 * ppe_vp_tx_to_ppe_by_dev()
 *      API for PPE VP user to enqueue packet for PPE processing on a particular dev.
 *
 * @param[IN] dev	net device on which the packet has to xmit.
 * @param[IN] skb	Socket buffer to be enqueued.
 *
 * @return
 * true if packet is consumed by the API or false if the packet is not consumed.
 */
bool ppe_vp_tx_to_ppe_by_dev(struct net_device *dev, struct sk_buff *skb);

/*
 * ppe_vp_tx_to_vp()
 *      API for transmitting the packets to VP in PPE.
 *
 * @param[IN] vp_num   VP num corresponding to its interface.
 * @param[IN] skb  Socket buffer to be enqueued.
 *
 * @return
 * true if packet is successfully forwarded to VP in PPE, false if its dropped.
 */
bool ppe_vp_tx_to_vp(int32_t vp_num, struct sk_buff *skb);


