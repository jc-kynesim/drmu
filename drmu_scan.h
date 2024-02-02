#ifndef _DRMU_DRMU_SCAN_H
#define _DRMU_DRMU_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_log_env_s;
struct drmu_env_s;
struct drmu_output_s;

int
drmu_scan_output(const char * const cname, const struct drmu_log_env_s * const dlog,
                 struct drmu_env_s ** const pDu, struct drmu_output_s ** const pDoutput);

#ifdef __cplusplus
}
#endif
#endif

