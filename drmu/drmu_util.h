#ifndef _DRMU_DRMU_UTIL_H
#define _DRMU_DRMU_UTIL_H

#include <stddef.h>

#include "drmu_math.h"

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
char * drmu_util_simple_param_to_mode_str(char * buf, size_t buflen, const struct drmu_mode_simple_params_s * const p);

#define drmu_util_simple_mode(p) drmu_util_simple_param_to_mode_str((char[64]){0}, 64, (p))

// Take a string and return a drmu rotation value (DRMU_ROTATION_xxx)
// Returns pointer to char after parsed string in *peos (c.f. strtol)
// peos may be NULL if not required. Some rotations have multiple valid
// strings.
// N.B. There is no invalid return, rubbish will return ROTATION_0, *peos = s
unsigned int drmu_util_str_to_rotation(const char * s, char ** peos);

// Rotation to string - guaranteed to return a string that str_to_rotation can
// ingest
const char * drmu_util_rotation_to_str(const unsigned int rot);

// Given width & height guess par. Spots Likely SD and returns 4:3 otherwise reduced w:h
drmu_ufrac_t drmu_util_guess_par(const unsigned int w, const unsigned int h);
// Get a par from simple_params. par can be zero & if so then guess
drmu_ufrac_t drmu_util_guess_simple_mode_par(const struct drmu_mode_simple_params_s * const p);

// Misc memcpy util

// Simple 2d memcpy
void drmu_memcpy_2d(void * const dst_p, const size_t dst_stride,
                    const void * const src_p, const size_t src_stride,
                    const size_t width, const size_t height);
// 'FB' copy
static inline void
drmu_memcpy_rect(void * const dst_p, const size_t dst_stride, const drmu_rect_t dst_rect,
                 const void * const src_p, const size_t src_stride, const drmu_rect_t src_rect,
                 const unsigned int pixel_stride)
{
    drmu_memcpy_2d((char *)dst_p + dst_rect.x * pixel_stride + dst_rect.y * dst_stride, dst_stride,
                   (char *)src_p + src_rect.x * pixel_stride + src_rect.y * src_stride, src_stride,
                   (src_rect.w < dst_rect.w ? src_rect.w : dst_rect.w) * pixel_stride,
                   src_rect.h < dst_rect.h ? src_rect.h : dst_rect.h);
}


#ifdef __cplusplus
}
#endif

#endif

