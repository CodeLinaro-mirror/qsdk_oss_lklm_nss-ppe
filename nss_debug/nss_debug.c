// - Runtime knobs: Print controls
// - Module init/exit

#include <linux/module.h>
#include <linux/init.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/ktime.h>
#include <linux/printk.h>
#include <linux/stdarg.h>

#include "exports/nss_debug.h"

/*
 * Global state and per-module overrides (used by print path)
 */
static enum nss_log_level g_level = NSS_DEFAULT_LOG_LEVEL;
/* Default categories: enable all by default */
static nss_cat_mask_t g_cat_mask = NSS_LOG_CAT_ALL;
static bool g_print_enable = true;

struct nss_module_cfg {
    char *tag;
    enum nss_log_level level;
    bool level_set;
    nss_cat_mask_t cat_mask;
    bool mask_set;
    struct list_head node;
};

static LIST_HEAD(cfg_list);
static DEFINE_SPINLOCK(cfg_lock);


static struct nss_module_cfg *nss_find_cfg(const char *module_tag)
{
    struct nss_module_cfg *it;
    list_for_each_entry(it, &cfg_list, node) {
        if (strcmp(it->tag, module_tag) == 0)
            return it;
    }
    return NULL;
}

static enum nss_log_level nss_effective_level(const char *module_tag)
{
    unsigned long flags;
    enum nss_log_level lvl = g_level;
    struct nss_module_cfg *cfg;

    spin_lock_irqsave(&cfg_lock, flags);
    cfg = nss_find_cfg(module_tag);
    if (cfg && cfg->level_set)
        lvl = cfg->level;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return lvl;
}

static nss_cat_mask_t nss_effective_mask(const char *module_tag)
{
    unsigned long flags;
    nss_cat_mask_t mask = g_cat_mask;
    struct nss_module_cfg *cfg;

    spin_lock_irqsave(&cfg_lock, flags);
    cfg = nss_find_cfg(module_tag);
    if (cfg && cfg->mask_set)
        mask = cfg->cat_mask;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return mask;
}

static const char *nss_level_str(enum nss_log_level level)
{
    switch (level) {
    case NSS_LOG_LEVEL_ERROR: return "ERROR";
    case NSS_LOG_LEVEL_WARN:  return "WARN";
    case NSS_LOG_LEVEL_INFO:  return "INFO";
    case NSS_LOG_LEVEL_TRACE: return "TRACE";
    }
    return "?";
}

static const char *nss_level_prefix(enum nss_log_level level)
{
    switch (level) {
    case NSS_LOG_LEVEL_ERROR: return KERN_ERR;
    case NSS_LOG_LEVEL_WARN:  return KERN_WARNING;
    case NSS_LOG_LEVEL_INFO:  return KERN_INFO;
    case NSS_LOG_LEVEL_TRACE: return KERN_DEBUG;
    }
    return KERN_DEBUG;
}

/*
 * Core logging
 */
int nss_log(enum nss_log_level level, const char *module_tag,
            nss_cat_mask_t categories, const char *fmt, ...)
{
    enum nss_log_level thr = nss_effective_level(module_tag ?: "?");
    nss_cat_mask_t mask = nss_effective_mask(module_tag ?: "?");
    va_list args;
    struct timespec64 ts;
    char msgbuf[512];
    int n;

    /* Print path: apply level + category filters only if enabled */
    if (g_print_enable) {
        if (level > thr)
            goto capture_path;
        if ((categories & mask) == 0)
            goto capture_path;
    }

    /* Format message */
    va_start(args, fmt);
    n = vsnprintf(msgbuf, sizeof(msgbuf), fmt, args);
    va_end(args);
    if (n < 0)
        return n;

    ktime_get_real_ts64(&ts);

    /* Emit printk if print is enabled and filters passed */
    if (g_print_enable) {
        printk("%s[%lld.%09ld][%s][%s][cats=0x%08x] %s\n",
               nss_level_prefix(level),
               (long long)ts.tv_sec, ts.tv_nsec,
               nss_level_str(level), module_tag ?: "?",
               (unsigned int)categories,
               msgbuf);
    }

capture_path:
    /* Capture removed */
    return 0;
}
EXPORT_SYMBOL(nss_log);

int nss_set_level_global(enum nss_log_level level)
{
    unsigned long flags;
    if (level < NSS_LOG_LEVEL_ERROR || level > NSS_LOG_LEVEL_TRACE)
        return -EINVAL;
    spin_lock_irqsave(&cfg_lock, flags);
    g_level = level;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_set_level_global);

enum nss_log_level nss_get_level_global(void)
{
    return g_level;
}
EXPORT_SYMBOL(nss_get_level_global);

/* Expose current global mask for procfs status */
nss_cat_mask_t nss_get_global_mask(void)
{
    return g_cat_mask;
}
EXPORT_SYMBOL(nss_get_global_mask);

static struct nss_module_cfg *nss_get_or_create_cfg(const char *module_tag)
{
    struct nss_module_cfg *cfg;
    cfg = nss_find_cfg(module_tag);
    if (cfg)
        return cfg;

    cfg = kzalloc(sizeof(*cfg), GFP_ATOMIC);
    if (!cfg)
        return NULL;
    cfg->tag = kstrdup(module_tag, GFP_ATOMIC);
    if (!cfg->tag) {
        kfree(cfg);
        return NULL;
    }
    INIT_LIST_HEAD(&cfg->node);
    cfg->level_set = false;
    cfg->mask_set = false;
    cfg->level = g_level;
    cfg->cat_mask = g_cat_mask;
    list_add_tail(&cfg->node, &cfg_list);
    return cfg;
}

int nss_set_level_module(const char *module_tag, enum nss_log_level level)
{
    unsigned long flags;
    struct nss_module_cfg *cfg;
    if (!module_tag || level < NSS_LOG_LEVEL_ERROR || level > NSS_LOG_LEVEL_TRACE)
        return -EINVAL;
    spin_lock_irqsave(&cfg_lock, flags);
    cfg = nss_get_or_create_cfg(module_tag);
    if (!cfg) {
        spin_unlock_irqrestore(&cfg_lock, flags);
        return -ENOMEM;
    }
    cfg->level = level;
    cfg->level_set = true;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_set_level_module);

int nss_enable_category(nss_cat_mask_t categories)
{
    unsigned long flags;
    spin_lock_irqsave(&cfg_lock, flags);
    g_cat_mask |= categories;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_enable_category);

int nss_disable_category(nss_cat_mask_t categories)
{
    unsigned long flags;
    spin_lock_irqsave(&cfg_lock, flags);
    g_cat_mask &= ~categories;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_disable_category);

int nss_enable_category_module(const char *module_tag, nss_cat_mask_t categories)
{
    unsigned long flags;
    struct nss_module_cfg *cfg;
    if (!module_tag)
        return -EINVAL;
    spin_lock_irqsave(&cfg_lock, flags);
    cfg = nss_get_or_create_cfg(module_tag);
    if (!cfg) {
        spin_unlock_irqrestore(&cfg_lock, flags);
        return -ENOMEM;
    }
    cfg->cat_mask |= categories;
    cfg->mask_set = true;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_enable_category_module);

int nss_disable_category_module(const char *module_tag, nss_cat_mask_t categories)
{
    unsigned long flags;
    struct nss_module_cfg *cfg;
    if (!module_tag)
        return -EINVAL;
    spin_lock_irqsave(&cfg_lock, flags);
    cfg = nss_get_or_create_cfg(module_tag);
    if (!cfg) {
        spin_unlock_irqrestore(&cfg_lock, flags);
        return -ENOMEM;
    }
    cfg->cat_mask &= ~categories;
    cfg->mask_set = true;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_disable_category_module);

int nss_get_stats(const char *module_tag, struct nss_stats *out)
{
    (void)module_tag;
    if (out) {
        memset(out, 0, sizeof(*out));
    }
    return 0;
}
EXPORT_SYMBOL(nss_get_stats);

void nss_dump_state(const char *module_tag)
{
    (void)module_tag;
}
EXPORT_SYMBOL(nss_dump_state);


static int __init nss_debug_init(void)
{
    int ret;
    pr_info("nss_debug: init (basic logging)\n");

    /* Create procfs entries for global controls */
    ret = nss_debug_procfs_init();
    if (ret) {
        pr_warn("nss_debug: procfs init failed: %d\n", ret);
    }

    return 0;
}

static void __exit nss_debug_exit(void)
{
    struct nss_module_cfg *it, *tmp;
    unsigned long flags;
    spin_lock_irqsave(&cfg_lock, flags);
    list_for_each_entry_safe(it, tmp, &cfg_list, node) {
        list_del(&it->node);
        kfree(it->tag);
        kfree(it);
    }
    spin_unlock_irqrestore(&cfg_lock, flags);
    /* Remove procfs entries */
    nss_debug_procfs_exit();
    pr_info("nss_debug: exit\n");
}

module_init(nss_debug_init);
module_exit(nss_debug_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NSS Debug module (basic logging)");
MODULE_AUTHOR("QCA");

/* Runtime knob implementations */
int nss_set_print_enable(bool enable)
{
    unsigned long flags;
    spin_lock_irqsave(&cfg_lock, flags);
    g_print_enable = enable;
    spin_unlock_irqrestore(&cfg_lock, flags);
    return 0;
}
EXPORT_SYMBOL(nss_set_print_enable);

bool nss_get_print_enable(void)
{
    return g_print_enable;
}
EXPORT_SYMBOL(nss_get_print_enable);

/* Capture knobs removed */
