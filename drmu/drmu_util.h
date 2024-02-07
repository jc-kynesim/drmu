#ifndef _DRMU_DRMU_UTIL_H
#define _DRMU_DRMU_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_mode_simple_params_s;

// Parse a string of the form [<w>x<h>][i][@<hz>[.<mHz>]]
// Returns pointer to terminating char
// Missing fields are zero
char * drmu_util_parse_mode(const char * s, unsigned int * pw, unsigned int * ph, unsigned int * pHzx1000);

// As above but place results into a simple params structure
// (Unused fields zeroed)
// Also copes with interlace
char * drmu_util_parse_mode_simple_params(const char * s, struct drmu_mode_simple_params_s * const p);

// Simple params to mode string
char * drmu_util_simple_param_to_mode_str(char * buf, size_t buflen, const drmu_mode_simple_params_t * const p);

#define drmu_util_simple_mode(p) drmu_util_simple_param_to_mode_str((char[64]){0}, 64, (p))

#ifdef __cplusplus
}
#endif

#endif

