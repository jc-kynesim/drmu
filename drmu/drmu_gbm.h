#ifndef _DRMU_DRMU_GBM_H
#define _DRMU_DRMU_GBM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct gbm_bo;
struct drmu_env_s;
struct drmu_fb_s;

uint32_t drmu_gbm_fmt_to_drm(const uint32_t f);
uint32_t drmu_gbm_fmt_from_drm(const uint32_t f);

struct drmu_fb_s * drmu_fb_gbm_attach(struct drmu_env_s * const du, struct gbm_bo * const bo);

#ifdef __cplusplus
}
#endif

#endif
