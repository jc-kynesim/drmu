#ifndef _DRMU_DRMU_LOG_H
#define _DRMU_DRMU_LOG_H

#include <stdarg.h>

struct drmu_env_s;

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

#endif

