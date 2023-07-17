#ifndef _DRMU_DRMU_VLC_FMTS_H
#define _DRMU_DRMU_VLC_FMTS_H

#include "config.h"

#ifndef HAS_ZC_CMA
#define HAS_ZC_CMA   0
#endif
#define HAS_DRMPRIME 1

#include <stdint.h>

#include <vlc_common.h>
#include <vlc_picture.h>

#ifdef __cplusplus
extern "C" {
#endif

// Convert chroma to drm - can't cope with RGB32 or RGB16 as they require
// more info. returns 0 if unknown.
uint32_t drmu_format_vlc_chroma_to_drm(const vlc_fourcc_t chroma);
// Convert format to drm fourcc - does cope with RGB32 & RGB16
uint32_t drmu_format_vlc_to_drm(const video_frame_format_t * const vf_vlc);
vlc_fourcc_t drmu_format_vlc_to_vlc(const uint32_t vf_drm);

#if HAS_DRMPRIME
// pmod may be NULL
uint32_t drmu_format_vlc_to_drm_prime(const vlc_fourcc_t chroma_in, uint64_t * const pmod);
#endif
#if HAS_ZC_CMA
uint32_t drmu_format_vlc_to_drm_cma(const vlc_fourcc_t chroma_in);
#endif

#ifdef __cplusplus
}
#endif
#endif
