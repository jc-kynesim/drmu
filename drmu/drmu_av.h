#ifndef _DRMU_DRMU_AV_H
#define _DRMU_DRMU_AV_H

#include <stdint.h>
#include <libavutil/pixfmt.h>

#ifdef __cplusplus
extern "C" {
#endif

struct AVFrame;
struct drmu_env_s;
struct drmu_fb_s;

uint32_t drmu_av_fmt_to_drm(enum AVPixelFormat pixfmt, uint64_t * pMod);

struct drmu_fb_s * drmu_fb_av_new_frame_attach(struct drmu_env_s * const du, struct AVFrame * const frame);

#ifdef __cplusplus
}
#endif
#endif

