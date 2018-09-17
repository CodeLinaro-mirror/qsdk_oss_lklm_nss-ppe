/*
 **************************************************************************
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
 * nss_nlcranipc.h
 *	NSS Netlink CRANIPC API definitions
 */
#ifndef __NSS_NLCRANIPC_H
#define __NSS_NLCRANIPC_H

bool nss_nlcranipc_init(void);
bool nss_nlcranipc_exit(void);

#if defined(CONFIG_NSS_NLCRANIPC)
#define NSS_NLCRANIPC_INIT nss_nlcranipc_init
#define NSS_NLCRANIPC_EXIT nss_nlcranipc_exit
#else
#define NSS_NLCRANIPC_INIT 0
#define NSS_NLCRANIPC_EXIT 0
#endif /* !CONFIG_NSS_NLCRANIPC */

#endif /* __NSS_NLCRANIPC_H */
