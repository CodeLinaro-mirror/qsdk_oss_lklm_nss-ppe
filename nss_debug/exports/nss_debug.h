/*
 * NSS Debug Public API (renamed from qca_nss_debug)
 * Sections are grouped for clarity; no ABI changes.
 */
#ifndef NSS_DEBUG_H
#define NSS_DEBUG_H

#include <linux/types.h>

/*
 * Log levels (default: WARN)
 * Threshold semantics: ERROR ⊆ WARN ⊆ INFO ⊆ TRACE
 */
enum nss_log_level {
    NSS_LOG_LEVEL_ERROR = 0,
    NSS_LOG_LEVEL_WARN  = 1,
    NSS_LOG_LEVEL_INFO  = 2,
    NSS_LOG_LEVEL_TRACE = 3,
};

#define NSS_DEFAULT_LOG_LEVEL NSS_LOG_LEVEL_WARN

/*
 * Log categories as bit flags (allow multi-category and "ALL")
 */
enum nss_log_category_bit {
    NSS_LOG_CAT_ACCEL_OFFLOAD_FAILURE_BIT = 0,
    NSS_LOG_CAT_TRAFFIC_STALL_BIT,
    NSS_LOG_CAT_PING_FAILURE_BIT,
    NSS_LOG_CAT_HIGH_CPU_USAGE_BIT,
    NSS_LOG_CAT_LOW_THROUGHPUT_BIT,
    NSS_LOG_CAT_PRIORITIZATION_BIT,
    NSS_LOG_CAT_REFCOUNT_LEAK_BIT,
    NSS_LOG_CAT_RCU_STALL_BIT,
    NSS_LOG_CAT_OOM_BIT,
    NSS_LOG_CAT_MEMORY_BLOAT_BIT,
    NSS_LOG_CAT_OLT_CONNECTIVITY_BIT,
    NSS_LOG_CAT_STC_FAILURE_BIT,
    NSS_LOG_CAT_GENERIC_BIT,
    NSS_LOG_CAT_MAX_BIT,
};

/* Bitmask values for categories */
typedef u32 nss_cat_mask_t;

#define NSS_LOG_CAT_ACCEL_OFFLOAD_FAILURE (1U << NSS_LOG_CAT_ACCEL_OFFLOAD_FAILURE_BIT)
#define NSS_LOG_CAT_TRAFFIC_STALL        (1U << NSS_LOG_CAT_TRAFFIC_STALL_BIT)
#define NSS_LOG_CAT_PING_FAILURE         (1U << NSS_LOG_CAT_PING_FAILURE_BIT)
#define NSS_LOG_CAT_HIGH_CPU_USAGE       (1U << NSS_LOG_CAT_HIGH_CPU_USAGE_BIT)
#define NSS_LOG_CAT_LOW_THROUGHPUT       (1U << NSS_LOG_CAT_LOW_THROUGHPUT_BIT)
#define NSS_LOG_CAT_PRIORITIZATION       (1U << NSS_LOG_CAT_PRIORITIZATION_BIT)
#define NSS_LOG_CAT_REFCOUNT_LEAK        (1U << NSS_LOG_CAT_REFCOUNT_LEAK_BIT)
#define NSS_LOG_CAT_RCU_STALL            (1U << NSS_LOG_CAT_RCU_STALL_BIT)
#define NSS_LOG_CAT_OOM                  (1U << NSS_LOG_CAT_OOM_BIT)
#define NSS_LOG_CAT_MEMORY_BLOAT         (1U << NSS_LOG_CAT_MEMORY_BLOAT_BIT)
#define NSS_LOG_CAT_OLT_CONNECTIVITY     (1U << NSS_LOG_CAT_OLT_CONNECTIVITY_BIT)
#define NSS_LOG_CAT_STC_FAILURE          (1U << NSS_LOG_CAT_STC_FAILURE_BIT)
#define NSS_LOG_CAT_GENERIC              (1U << NSS_LOG_CAT_GENERIC_BIT)

#define NSS_LOG_CAT_ALL           ((nss_cat_mask_t)~0U)

#define NSS_LOG_CAT_MAX           (NSS_LOG_CAT_MAX_BIT)

/*
 * UIO control protocol (shared with userspace)
 */
#define NSS_DBG_TAG_MAX 32

enum nss_dbg_msg_type {
    NSS_DBG_MSG_TYPE_INVALID = 0,
    NSS_DBG_MSG_TYPE_SET_PRINT_ENABLE = 1,
    NSS_DBG_MSG_TYPE_SET_GLOBAL_LEVEL = 4,
    NSS_DBG_MSG_TYPE_ENABLE_GLOBAL_MASK = 5,
    NSS_DBG_MSG_TYPE_DISABLE_GLOBAL_MASK = 6,
    NSS_DBG_MSG_TYPE_SET_MODULE_LEVEL = 8,
    NSS_DBG_MSG_TYPE_ENABLE_MODULE_MASK = 9,
    NSS_DBG_MSG_TYPE_DISABLE_MODULE_MASK = 10,
    NSS_DBG_MSG_TYPE_RESET = 11,
};

struct nss_dbg_req_set_enable {
    __u8 enable; /* 0 or 1 */
};

struct nss_dbg_req_set_level {
    __u32 level; /* enum nss_log_level */
};

struct nss_dbg_req_set_mask {
    __u32 mask; /* nss_cat_mask_t */
};


struct nss_dbg_req_module_level {
    char tag[NSS_DBG_TAG_MAX];
    __u32 level; /* enum nss_log_level */
};

struct nss_dbg_req_module_mask {
    char tag[NSS_DBG_TAG_MAX];
    __u32 mask; /* nss_cat_mask_t */
};

struct nss_dbg_request {
    __u32 msg_type; /* enum nss_dbg_msg_type */
    union {
        struct nss_dbg_req_set_enable set_enable;
        struct nss_dbg_req_set_level set_level;
        struct nss_dbg_req_set_mask set_mask;
        struct nss_dbg_req_module_level module_level;
        struct nss_dbg_req_module_mask module_mask;
    } u;
};

/*
 * Core logging API
 */
int nss_log(enum nss_log_level level,
            const char *module_tag,
            nss_cat_mask_t categories,
            const char *fmt, ...) __attribute__((format(printf, 4, 5)));

/*
 * Level controls (global and per-module)
 */
int nss_set_level_global(enum nss_log_level level);
enum nss_log_level nss_get_level_global(void);
int nss_set_level_module(const char *module_tag, enum nss_log_level level);

/*
 * Category filters (global and per-module)
 */
int nss_enable_category(nss_cat_mask_t categories);
int nss_disable_category(nss_cat_mask_t categories);
/* Getter for current global category mask */
nss_cat_mask_t nss_get_global_mask(void);
int nss_enable_category_module(const char *module_tag, nss_cat_mask_t categories);
int nss_disable_category_module(const char *module_tag, nss_cat_mask_t categories);

/*
 * Stats and state dump (placeholders)
 * Note: retained for future implementation.
 */
struct nss_stats {
    u64 error_count;
    u64 warn_count;
    u64 info_count;
    u64 trace_count;
    u64 category_counts[NSS_LOG_CAT_MAX_BIT];
};

int nss_get_stats(const char *module_tag, struct nss_stats *out);
void nss_dump_state(const char *module_tag);

/*
 * UIO interaction (placeholder)
 */
int nss_uio_write(const void *record, size_t len);

/*
 * Convenience macros (used by kernel logging sites)
 */
#define nss_err(module_tag, category, fmt, ...) \
    nss_log(NSS_LOG_LEVEL_ERROR, (module_tag), (category), (fmt), ##__VA_ARGS__)

#define nss_warn(module_tag, category, fmt, ...) \
    nss_log(NSS_LOG_LEVEL_WARN, (module_tag), (category), (fmt), ##__VA_ARGS__)

#define nss_info(module_tag, category, fmt, ...) \
    nss_log(NSS_LOG_LEVEL_INFO, (module_tag), (category), (fmt), ##__VA_ARGS__)

#define nss_trace(module_tag, category, fmt, ...) \
    nss_log(NSS_LOG_LEVEL_TRACE, (module_tag), (category), (fmt), ##__VA_ARGS__)

/*
 * Runtime knobs for print path
 */
int nss_set_print_enable(bool enable);
bool nss_get_print_enable(void);

/* Procfs hooks (internal to module) */
int nss_debug_procfs_init(void);
void nss_debug_procfs_exit(void);

#endif /* NSS_DEBUG_H */

