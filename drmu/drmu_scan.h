#ifndef _DRMU_DRMU_SCAN_H
#define _DRMU_DRMU_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_log_env_s;
struct drmu_env_s;
struct drmu_output_s;

// Scan /dev/drv/card* in numeric order looking for a device with the given conn_name
// as a connected connector. conn_name is prefix matched only so "DP" will likely match
// the first connected display port and "" will find the first connected anything.
int
drmu_scan_output(const char * const conn_name, const struct drmu_log_env_s * const dlog,
                 struct drmu_env_s ** const pDu, struct drmu_output_s ** const pDoutput);

#ifdef __cplusplus
}
#endif
#endif

