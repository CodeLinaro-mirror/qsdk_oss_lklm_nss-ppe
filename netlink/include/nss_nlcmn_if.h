/*
 **************************************************************************
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 * Permission to use, copy, modify, and/or distribute this software for
 * any purpose with or without fee is hereby granted, provided that the
 * above copyright notice and this permission notice appear in all copies.
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 **************************************************************************
 */

/*
 * @file nss_nlcmn_if.h
 *	NSS Netlink common headers
 */
#ifndef __NSS_NLCMN_IF_H
#define __NSS_NLCMN_IF_H

/**
 * @brief Common message header for each NSS netlink message
 */
struct nss_nlcmn {
	uint32_t version;	/**< message version */

	uint32_t pid;		/**< process ID for the message */
	uint32_t sock_data;	/**< socket specific info, used by kernel */
	uint32_t user_data;	/**< user specific data */
	uint32_t user_cb;	/**< user specific callback */

	uint16_t cmd_len;	/**< command len */
	uint16_t cmd_type;	/**< command type */
};

/**
 * @brief messages senders must use this to initialize command
 *
 * @param cm[IN] common message
 * @param cmd[IN] command for the family
 * @param len[IN] command length
 */
static inline void nss_nlcmn_init_cmd(struct nss_nlcmn *cm, uint16_t cmd, uint16_t len)
{
	cm->cmd_type = cmd;
	cm->cmd_len = len;
}

/**
 * @brief messages senders must use this to initialize the user fields
 *
 * @param cm[IN] common message
 * @param user_data[IN] user specific data stored per command
 * @param user_cb[IN] user specific callback per command
 */
static inline void nss_nlcmn_init_user(struct nss_nlcmn *cm, uint32_t user_data, uint32_t user_cb)
{
	cm->user_data = user_data;
	cm->user_cb = user_cb;
}
/**
 * @brief check the version number of the incoming message
 *
 * @param cm[IN] common message header
 *
 * @return true on version match
 */
static inline bool nss_nlcmn_chk_ver(struct nss_nlcmn *cm, uint32_t ver)
{
	return (cm->version == ver);
}

/**
 * @brief set the version number for common message header
 *
 * @param cm[IN] common message header
 * @param ver[IN] version number to apply
 */
static inline void nss_nlcmn_set_ver(struct nss_nlcmn *cm, uint32_t ver)
{
	cm->version = ver;
}

/**
 * @brief get the NSS Family command type
 *
 * @param cm[IN] common message
 *
 * @return command type
 */
static inline uint8_t nss_nlcmn_get_cmd(struct nss_nlcmn *cm)
{
	return cm->cmd_type;
}

/**
 * @brief get the NSS Family command len
 *
 * @param cm[IN] command message
 *
 * @return command type
 */
static inline uint16_t nss_nlcmn_get_len(struct nss_nlcmn *cm)
{
	return cm->cmd_len;
}

/**
 * @brief get the user data for the command
 *
 * @param cm[IN] command message
 *
 * @return user data
 */
static inline uint32_t nss_nlcmn_get_user_data(struct nss_nlcmn *cm)
{
	return cm->user_data;
}

/**
 * @brief get the user callback for the command
 *
 * @param cm[IN] command message
 *
 * @return user callback
 */
static inline uint32_t nss_nlcmn_get_user_cb(struct nss_nlcmn *cm)
{
	return cm->user_cb;
}
#endif /* __NSS_NLCMN_IF_H */


