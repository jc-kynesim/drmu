#ifndef _DRMU_DRMU_VLC_FMTS_H
#define _DRMU_DRMU_VLC_FMTS_H

#include "config.h"

#ifndef HAS_ZC_CMA
#define HAS_ZC_CMA   0
#endif
#define HAS_DRMPRIME 1

#include <stdbool.h>
#include <stdint.h>

#include <vlc_common.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_vlc_fmt_info_ss;
typedef struct drmu_vlc_fmt_info_ss drmu_vlc_fmt_info_t;

const drmu_vlc_fmt_info_t * drmu_vlc_fmt_info_find_vlc(const video_frame_format_t * const vf_vlc);
// f == NULL => start at the beginning
const drmu_vlc_fmt_info_t * drmu_vlc_fmt_info_find_vlc_next(const video_frame_format_t * const vf_vlc, const drmu_vlc_fmt_info_t * f);
const drmu_vlc_fmt_info_t * drmu_vlc_fmt_info_find_drm(const uint32_t pixelformat, const uint64_t modifier);
// f == NULL => start at the beginning
const drmu_vlc_fmt_info_t * drmu_vlc_fmt_info_find_drm_next(const uint32_t pixelformat, const uint64_t modifier, const drmu_vlc_fmt_info_t * f);

vlc_fourcc_t drmu_vlc_fmt_info_vlc_chroma(const drmu_vlc_fmt_info_t * const f);
void drmu_vlc_fmt_info_vlc_rgb_masks(const drmu_vlc_fmt_info_t * const f, uint32_t * r, uint32_t * g, uint32_t * b);
uint32_t drmu_vlc_fmt_info_drm_pixelformat(const drmu_vlc_fmt_info_t * const f);
uint64_t drmu_vlc_fmt_info_drm_modifier(const drmu_vlc_fmt_info_t * const f);

bool drmu_vlc_fmt_info_is_zc_cma(const drmu_vlc_fmt_info_t * const f);
bool drmu_vlc_fmt_info_is_drmprime(const drmu_vlc_fmt_info_t * const f);


// Convert chroma to drm - can't cope with RGB32 or RGB16 as they require
// more info. returns 0 if unknown.
uint32_t drmu_format_vlc_chroma_to_drm(const vlc_fourcc_t chroma);
// Convert format to drm fourcc - does cope with RGB32 & RGB16
// pMod receives modifier - may be null
uint32_t drmu_format_vlc_to_drm(const video_frame_format_t * const vf_vlc, uint64_t * const pMod);

#if HAS_DRMPRIME
// pmod may be NULL
uint32_t drmu_format_vlc_to_drm_prime(const video_frame_format_t * const vf_vlc, uint64_t * const pmod);
#endif
#if HAS_ZC_CMA
uint32_t drmu_format_vlc_to_drm_cma(const video_frame_format_t * const vf_vlc, uint64_t * const pMod);
#endif

#ifdef __cplusplus
}
#endif
#endif
