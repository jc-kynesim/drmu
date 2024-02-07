#ifndef _DRMU_DRMU_LOG_H
#define _DRMU_DRMU_LOG_H

#include <stdarg.h>

struct drmu_env_s;

enum drmu_log_level_e {
        DRMU_LOG_LEVEL_NONE = -1,     // Max level specifier for nothing (not a real level)
        DRMU_LOG_LEVEL_MESSAGE = 0,   // (Nearly) always printed info
        DRMU_LOG_LEVEL_ERROR,         // Error
        DRMU_LOG_LEVEL_WARNING,
        DRMU_LOG_LEVEL_INFO,          // Interesting but not critical info
        DRMU_LOG_LEVEL_DEBUG,         // Info only useful for debug
        DRMU_LOG_LEVEL_ALL,           // Max level specifier for everything (not a real level)
};

typedef void drmu_log_fn(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl);

typedef struct drmu_log_env_s {
        drmu_log_fn * fn;
        void * v;
        enum drmu_log_level_e max_level;
} drmu_log_env_t;

void drmu_log_generic(const struct drmu_log_env_s * const log, const enum drmu_log_level_e level,
                      const char * const fmt, ...);

#define drmu_log_macro(_log, _level, _fmt, ...) do {\
        const drmu_log_env_t * const _log2 = (_log);\
        if ((_level) <= _log2->max_level)\
                drmu_log_generic(_log2, (_level), "%s:%u:%s: " _fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);\
} while (0)

// Char offset if file, line extracted - func still in format
#define DRMU_LOG_FMT_OFFSET_FUNC        6
// Char offset if file, line & fn extracted
#define DRMU_LOG_FMT_OFFSET_FMT         10

#define drmu_err_log(_log, ...)      drmu_log_macro((_log), DRMU_LOG_LEVEL_ERROR,   __VA_ARGS__)
#define drmu_warn_log(_log, ...)     drmu_log_macro((_log), DRMU_LOG_LEVEL_WARNING, __VA_ARGS__)
#define drmu_info_log(_log, ...)     drmu_log_macro((_log), DRMU_LOG_LEVEL_INFO,    __VA_ARGS__)
#define drmu_debug_log(_log, ...)    drmu_log_macro((_log), DRMU_LOG_LEVEL_DEBUG,   __VA_ARGS__)

#define drmu_err(_du, ...)      drmu_err_log(drmu_env_log(_du), __VA_ARGS__)
#define drmu_warn(_du, ...)     drmu_warn_log(drmu_env_log(_du), __VA_ARGS__)
#define drmu_info(_du, ...)     drmu_info_log(drmu_env_log(_du), __VA_ARGS__)
#define drmu_debug(_du, ...)    drmu_debug_log(drmu_env_log(_du), __VA_ARGS__)

static inline char drmu_log_safechar(int c)
{
    return (c < ' ' || c >=0x7f) ? '?' : c;
}

static inline const char * drmu_log_fourcc_to_str(char buf[5], uint32_t fcc)
{
    if (fcc == 0)
        return "----";
    buf[0] = drmu_log_safechar((fcc >> 0) & 0xff);
    buf[1] = drmu_log_safechar((fcc >> 8) & 0xff);
    buf[2] = drmu_log_safechar((fcc >> 16) & 0xff);
    buf[3] = drmu_log_safechar((fcc >> 24) & 0xff);
    buf[4] = 0;
    return buf;
}

#define drmu_log_fourcc(fcc) drmu_log_fourcc_to_str((char[5]){0}, fcc)


#endif

