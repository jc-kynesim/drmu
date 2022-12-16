#ifndef _DRMU_DRMU_VLC_H
#define _DRMU_DRMU_VLC_H

#include "config.h"

#ifndef HAS_VLC4
#define HAS_VLC4     0
#endif
#ifndef HAS_ZC_CMA
#define HAS_ZC_CMA   0
#endif
#define HAS_DRMPRIME 1

#include <stdint.h>

#include <vlc_common.h>
#include <vlc_picture.h>
#include <vlc_vout_display.h>

#include "drmu.h"

#ifdef __cplusplus
extern "C" {
#endif

// Get cropping rectangle from a vlc format
static inline drmu_rect_t
drmu_rect_vlc_format_crop(const video_frame_format_t * const format)
{
    return (drmu_rect_t){
        .x = format->i_x_offset,
        .y = format->i_y_offset,
        .w = format->i_visible_width,
        .h = format->i_visible_height};
}

// Get cropping rectangle from a vlc pic
static inline drmu_rect_t
drmu_rect_vlc_pic_crop(const picture_t * const pic)
{
    return drmu_rect_vlc_format_crop(&pic->format);
}

// Get rect from vlc place
static inline drmu_rect_t
drmu_rect_vlc_place(const vout_display_place_t * const place)
{
    return (drmu_rect_t){
        .x = place->x,
        .y = place->y,
        .w = place->width,
        .h = place->height
    };
}

static inline vlc_rational_t
drmu_ufrac_vlc_to_rational(const drmu_ufrac_t x)
{
    return (vlc_rational_t) {.num = x.num, .den = x.den};
}


// Convert chroma to drm - can't cope with RGB32 or RGB16 as they require
// more info. returns 0 if unknown.
uint32_t drmu_format_vlc_chroma_to_drm(const vlc_fourcc_t chroma);
// Convert format to drm fourcc - does cope with RGB32 & RGB16
uint32_t drmu_format_vlc_to_drm(const video_frame_format_t * const vf_vlc);
vlc_fourcc_t drmu_format_vlc_to_vlc(const uint32_t vf_drm);

drmu_fb_t * drmu_fb_vlc_new_pic_attach(drmu_env_t * const du, picture_t * const pic);
plane_t drmu_fb_vlc_plane(drmu_fb_t * const dfb, const unsigned int plane_n);

#if HAS_DRMPRIME
// pmod may be NULL
uint32_t drmu_format_vlc_to_drm_prime(const vlc_fourcc_t chroma_in, uint64_t * const pmod);
#endif
#if HAS_ZC_CMA
uint32_t drmu_format_vlc_to_drm_cma(const vlc_fourcc_t chroma_in);
drmu_fb_t * drmu_fb_vlc_new_pic_cma_attach(drmu_env_t * const du, picture_t * const pic);
#endif

// Copy properties like colour_space, hdr_metadata into the fb
void drmu_fb_vlc_pic_set_metadata(drmu_fb_t * const dfb, const picture_t * const pic);

// Logging function callback for VLC
enum drmu_log_level_e;
void drmu_log_vlc_cb(void * v, enum drmu_log_level_e level_drmu, const char * fmt, va_list vl);

#ifdef __cplusplus
}
#endif
#endif

