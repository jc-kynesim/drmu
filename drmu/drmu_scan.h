#ifndef _DRMU_DRMU_SCAN_H
#define _DRMU_DRMU_SCAN_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_log_env_s;
struct drmu_env_s;
struct drmu_output_s;

struct drum_scan_s;
typedef struct drmu_scan_s drmu_scan_t;

// Iterator over possible cards
// Constructs the card list in this call
// dlog may be NULL if no logging wanted
// Returns NULL on error
drmu_scan_t * drmu_scan_new(const struct drmu_log_env_s * const dlog);
// Path returned is backed by the drmu_scan_t, it must be copied if required
// to persist past the next invocation of _next or _unref
// Returns NULL if dscan == NULL or no more paths
const char * drmu_scan_path(drmu_scan_t * const dscan);
// Get a du
// If the current path cannot be opened by as a drmu_env_t then will call _next
// until a usable path is found.
// Returns NULL if dscan == NULL or no more usable paths
struct drmu_env_s * drmu_scan_du(drmu_scan_t * const dscan);
// Move to next path
// Returns 0 if found, -ENOENT if no more
int drmu_scan_next(drmu_scan_t * const dscan);

void drmu_scan_unref(drmu_scan_t ** const ppdscan);

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

