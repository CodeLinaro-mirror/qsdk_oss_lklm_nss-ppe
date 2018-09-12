/*
 ***************************************************************************
 * Copyright (c) 2018, The Linux Foundation. All rights reserved.
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
 * nss_cranipc_def.h
 */

#ifndef _NSS_CRANIPC_DEF_H_
#define _NSS_CRANIPC_DEF_H_
/*
 * Compile messages for dynamic enable/disable
 */
#if defined(CONFIG_DYNAMIC_DEBUG)
#define nss_cran_warn(s, ...) pr_warn("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#define nss_cran_err(s, ...) pr_err("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#define nss_cran_info(s, ...) pr_notice("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#else /* CONFIG_DYNAMIC_DEBUG */
/*
 * Statically compile messages at different levels
 */
#if (NSS_CRAN_DEBUG_LEVEL < 2)
#define nss_cran_warn(s, ...)
#else
#define nss_cran_warn(s, ...) pr_warn("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#endif

#if (NSS_CRAN_DEBUG_LEVEL < 3)
#define nss_cran_info(s, ...)
#else
#define nss_cran_info(s, ...) pr_notice("%s[%d]:" s, __func__, __LINE__, ##__VA_ARGS__)
#endif
#endif /* CONFIG_DYNAMIC_DEBUG */

#define NSS_CRANIPC_MSG_LEN (sizeof(struct nss_cranipc_msg) - sizeof(struct nss_cmn_msg))
#define NSS_CRANIPC_MTU 65535

struct nss_cranipc_priv {
        struct rtnl_link_stats64 stats;
	struct nss_ctx_instance *nss_ctx;
	int is_first_init_done;
};

static struct net_device *nss_cranipc_register_netdev(void);

#endif
