#ifndef _DRMU_DRMU_UTIL_H
#define _DRMU_DRMU_UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

// Parse a string of the form [<w>x<h>][@<hz>[.<mHz>]]
// Returns pointer to terminating char
// Missing fields are zero
char * drmu_util_parse_mode(const char * s, unsigned int * pw, unsigned int * ph, unsigned int * pHzx1000);


#ifdef __cplusplus
}
#endif

#endif

