#include "drmu_gbm.h"

#include <stdlib.h>

#include <gbm.h>
#include <stdbool.h>

#include <libdrm/drm_fourcc.h>

#include "drmu.h"
#include "drmu_log.h"

uint32_t
drmu_gbm_fmt_to_drm(const uint32_t f)
{
    switch (f) {
        case GBM_BO_FORMAT_XRGB8888:
            return DRM_FORMAT_XRGB8888;
        case GBM_BO_FORMAT_ARGB8888:
            return DRM_FORMAT_ARGB8888;
        default:
            return f;
    }
}

uint32_t
drmu_gbm_fmt_from_drm(const uint32_t f)
{
    switch (f) {
        case DRM_FORMAT_XRGB8888:
            return GBM_BO_FORMAT_XRGB8888;
        case DRM_FORMAT_ARGB8888:
            return GBM_BO_FORMAT_ARGB8888;
        default:
            return f;
    }
}

drmu_fb_t *
drmu_fb_gbm_attach(drmu_env_t * const du, struct gbm_bo * const bo)
{
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    const uint32_t fmt = drmu_gbm_fmt_to_drm(gbm_bo_get_format(bo));
    const unsigned int width = gbm_bo_get_width(bo);
    const unsigned int height = gbm_bo_get_height(bo);
    const unsigned int planes = gbm_bo_get_plane_count(bo);
    const uint64_t mod = gbm_bo_get_modifier(bo);
    unsigned int i;
    int n;
    uint32_t last_handle = 0;

    if (dfb == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }

    drmu_fb_int_fmt_size_set(dfb, fmt, width, height,
                             drmu_rect_wh(width, height));

    for (i = 0, n = -1; i != planes; ++i) {
        const uint32_t handle = gbm_bo_get_handle_for_plane(bo, i).u32;

        if (handle != last_handle) {
            drmu_bo_t * const dbo = drmu_bo_new_external(du, handle);
            if (dbo == NULL)
                goto fail;
            ++n;
            last_handle = handle;
            drmu_fb_int_bo_set(dfb, n, dbo);
        }

        drmu_fb_int_layer_mod_set(dfb, i, n,
                                  gbm_bo_get_stride_for_plane(bo, i),
                                  gbm_bo_get_offset(bo, i),
                                  mod);
    }

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;

    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}

