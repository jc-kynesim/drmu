#include "drmu_vlc_fmts.h"

#include <vlc_picture.h>
#include <libdrm/drm_fourcc.h>

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

#define DRMU_VLC_FMTS_FLAG_DRMP         1
#define DRMU_VLC_FMTS_FLAG_ZC           2
#define DRMU_VLC_FMTS_FLAG_FULL_RANGE   4

struct drmu_vlc_fmt_info_ss {
    vlc_fourcc_t vlc_chroma;
    uint32_t drm_pixelformat;
    uint32_t rmask;
    uint32_t gmask;
    uint32_t bmask;
    uint64_t drm_modifier;
    unsigned int flags;
};

// N.B. DRM seems to order its format descriptor names the opposite way round to VLC
// DRM is hi->lo within a little-endian word, VLC is byte order

#define I2(vlc, drm) {(vlc), (drm), 0, 0, 0, DRM_FORMAT_MOD_LINEAR, 0 }
#define RM(vlc, drm, r, g, b) {(vlc), (drm), (r), (g), (b), DRM_FORMAT_MOD_LINEAR, 0 }
#define R2(vlc, drm) RM((vlc), (drm), 0, 0, 0)

static const drmu_vlc_fmt_info_t fmt_table[] = {
    R2(VLC_CODEC_RGBA, DRM_FORMAT_ABGR8888),
    R2(VLC_CODEC_BGRA, DRM_FORMAT_ARGB8888),
    R2(VLC_CODEC_ARGB, DRM_FORMAT_BGRA8888),
    // VLC_CODEC_ABGR does not exist in VLC
    // AYUV appears to be the only DRM YUVA-like format
    I2(VLC_CODEC_VUYA, DRM_FORMAT_AYUV),
    I2(VLC_CODEC_VYUY, DRM_FORMAT_YUYV),
    I2(VLC_CODEC_UYVY, DRM_FORMAT_YVYU),
    I2(VLC_CODEC_YUYV, DRM_FORMAT_VYUY),
    I2(VLC_CODEC_YVYU, DRM_FORMAT_UYVY),
    I2(VLC_CODEC_NV12, DRM_FORMAT_NV12),
    I2(VLC_CODEC_NV21, DRM_FORMAT_NV21),
    I2(VLC_CODEC_NV16, DRM_FORMAT_NV16),
    I2(VLC_CODEC_NV61, DRM_FORMAT_NV61),
    I2(VLC_CODEC_NV24, DRM_FORMAT_NV24),
    I2(VLC_CODEC_NV42, DRM_FORMAT_NV42),
    I2(VLC_CODEC_P010, DRM_FORMAT_P010),
    I2(VLC_CODEC_I420, DRM_FORMAT_YUV420),
    { VLC_CODEC_J420, DRM_FORMAT_YUV420, 0, 0, 0, DRM_FORMAT_MOD_LINEAR, DRMU_VLC_FMTS_FLAG_FULL_RANGE },
    I2(VLC_CODEC_YV12, DRM_FORMAT_YVU420),
    I2(VLC_CODEC_I422, DRM_FORMAT_YUV422),
    { VLC_CODEC_J422, DRM_FORMAT_YUV422, 0, 0, 0, DRM_FORMAT_MOD_LINEAR, DRMU_VLC_FMTS_FLAG_FULL_RANGE },
    I2(VLC_CODEC_I444, DRM_FORMAT_YUV444),
    { VLC_CODEC_J444, DRM_FORMAT_YUV444, 0, 0, 0, DRM_FORMAT_MOD_LINEAR, DRMU_VLC_FMTS_FLAG_FULL_RANGE },
#if HAS_DRMPRIME
    { VLC_CODEC_DRM_PRIME_I420,   DRM_FORMAT_YUV420,   0, 0, 0, DRM_FORMAT_MOD_LINEAR,           DRMU_VLC_FMTS_FLAG_DRMP },
    { VLC_CODEC_DRM_PRIME_NV12,   DRM_FORMAT_NV12,     0, 0, 0, DRM_FORMAT_MOD_LINEAR,           DRMU_VLC_FMTS_FLAG_DRMP },
    { VLC_CODEC_DRM_PRIME_SAND8,  DRM_FORMAT_NV12,     0, 0, 0, DRM_FORMAT_MOD_BROADCOM_SAND128, DRMU_VLC_FMTS_FLAG_DRMP },
    { VLC_CODEC_DRM_PRIME_SAND30, DRM_FORMAT_P030,     0, 0, 0, DRM_FORMAT_MOD_BROADCOM_SAND128, DRMU_VLC_FMTS_FLAG_DRMP },
    { VLC_CODEC_DRM_PRIME_RGB32,  DRM_FORMAT_XRGB8888, 0, 0, 0, DRM_FORMAT_MOD_LINEAR,           DRMU_VLC_FMTS_FLAG_DRMP },
#endif
#if HAS_ZC_CMA
    { VLC_CODEC_MMAL_ZC_I420,     DRM_FORMAT_YUV420,   0, 0, 0, DRM_FORMAT_MOD_LINEAR,           DRMU_VLC_FMTS_FLAG_ZC },
    { VLC_CODEC_MMAL_ZC_SAND8,    DRM_FORMAT_NV12,     0, 0, 0, DRM_FORMAT_MOD_BROADCOM_SAND128, DRMU_VLC_FMTS_FLAG_ZC },
    { VLC_CODEC_MMAL_ZC_SAND30,   DRM_FORMAT_P030,     0, 0, 0, DRM_FORMAT_MOD_BROADCOM_SAND128, DRMU_VLC_FMTS_FLAG_ZC },
    { VLC_CODEC_MMAL_ZC_RGB32,    DRM_FORMAT_RGBX8888, 0, 0, 0, DRM_FORMAT_MOD_LINEAR,           DRMU_VLC_FMTS_FLAG_ZC },
#endif

    RM(VLC_CODEC_RGB32, DRM_FORMAT_XRGB8888, 0xff0000, 0xff00, 0xff),
    RM(VLC_CODEC_RGB32, DRM_FORMAT_XBGR8888, 0xff, 0xff00, 0xff0000),
    RM(VLC_CODEC_RGB32, DRM_FORMAT_RGBX8888, 0xff000000, 0xff0000, 0xff00),
    RM(VLC_CODEC_RGB32, DRM_FORMAT_BGRX8888, 0xff00, 0xff0000, 0xff000000),
    RM(VLC_CODEC_RGB24, DRM_FORMAT_RGB888,   0xff0000, 0xff00, 0xff),
    RM(VLC_CODEC_RGB24, DRM_FORMAT_BGR888,   0xff, 0xff00, 0xff0000),
    RM(VLC_CODEC_RGB16, DRM_FORMAT_RGB565,   0xf800, 0x7e0, 0x1f),
    RM(VLC_CODEC_RGB16, DRM_FORMAT_BGR565,   0x1f, 0x7e0, 0xf800),

    I2(0, 0)
};
#undef I2
#undef RM
#undef R2

// *** Sorted lookups?

const drmu_vlc_fmt_info_t *
drmu_vlc_fmt_info_find_vlc_next(const video_frame_format_t * const vf_vlc, const drmu_vlc_fmt_info_t * f)
{
    f = (f == NULL) ? fmt_table : f + 1;

    for (; f->vlc_chroma != 0; ++f)
    {
        if (f->vlc_chroma != vf_vlc->i_chroma)
            continue;
        if (f->rmask != 0 && vf_vlc->i_rmask != 0 &&
            (f->rmask != vf_vlc->i_rmask || f->gmask != vf_vlc->i_gmask || f->bmask != vf_vlc->i_bmask))
            continue;
        return f;
    }
    return NULL;
}

const drmu_vlc_fmt_info_t *
drmu_vlc_fmt_info_find_vlc(const video_frame_format_t * const vf_vlc)
{
    return drmu_vlc_fmt_info_find_vlc_next(vf_vlc, NULL);
}

// Remove any params from a modifier
static inline uint64_t canon_mod(const uint64_t m)
{
    return fourcc_mod_is_vendor(m, BROADCOM) ? fourcc_mod_broadcom_mod(m) : m;
}

const drmu_vlc_fmt_info_t *
drmu_vlc_fmt_info_find_drm_next(const uint32_t pixelformat, const uint64_t modifier, const drmu_vlc_fmt_info_t * f)
{
    const uint64_t cmod = canon_mod(modifier);

    f = (f == NULL) ? fmt_table : f + 1;

    for (; f->vlc_chroma != 0; ++f)
    {
        if (f->drm_pixelformat != pixelformat || f->drm_modifier != cmod)
            continue;
        // Only return the "base" version
        if (f->flags != 0)
            continue;
        return f;
    }
    return NULL;
}

const drmu_vlc_fmt_info_t *
drmu_vlc_fmt_info_find_drm(const uint32_t pixelformat, const uint64_t modifier)
{
    return drmu_vlc_fmt_info_find_drm_next(pixelformat, modifier, NULL);
}

vlc_fourcc_t
drmu_vlc_fmt_info_vlc_chroma(const drmu_vlc_fmt_info_t * const f)
{
    return f == NULL ? 0 : f->vlc_chroma;
}

void
drmu_vlc_fmt_info_vlc_rgb_masks(const drmu_vlc_fmt_info_t * const f, uint32_t * r, uint32_t * g, uint32_t * b)
{
    if (f == NULL)
    {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }
    *r = f->rmask;
    *g = f->gmask;
    *b = f->bmask;
}

uint32_t
drmu_vlc_fmt_info_drm_pixelformat(const drmu_vlc_fmt_info_t * const f)
{
    return f == NULL ? 0 : f->drm_pixelformat;
}

uint64_t
drmu_vlc_fmt_info_drm_modifier(const drmu_vlc_fmt_info_t * const f)
{
    return f == NULL ? DRM_FORMAT_MOD_INVALID : f->drm_modifier;
}

bool
drmu_vlc_fmt_info_is_drmprime(const drmu_vlc_fmt_info_t * const f)
{
#if HAS_DRMPRIME
    return f != NULL && (f->flags & DRMU_VLC_FMTS_FLAG_DRMP);
#else
    (void)f;
    return false;
#endif
}

bool
drmu_vlc_fmt_info_is_zc_cma(const drmu_vlc_fmt_info_t * const f)
{
#if HAS_ZC_CMA
    return f != NULL && (f->flags & DRMU_VLC_FMTS_FLAG_ZC);
#else
    (void)f;
    return false;
#endif
}


uint32_t
drmu_format_vlc_to_drm(const video_frame_format_t * const vf_vlc, uint64_t * const pMod)
{
    const drmu_vlc_fmt_info_t * const f = drmu_vlc_fmt_info_find_vlc(vf_vlc);

    if (pMod)
        *pMod = drmu_vlc_fmt_info_drm_modifier(f);
    return drmu_vlc_fmt_info_drm_pixelformat(f);
}

#if HAS_ZC_CMA
uint32_t
drmu_format_vlc_to_drm_cma(const video_frame_format_t * const vf_vlc, uint64_t * const pMod)
{
    const drmu_vlc_fmt_info_t * f = drmu_vlc_fmt_info_find_vlc(vf_vlc);

    if (!drmu_vlc_fmt_info_is_zc_cma(f))
        f = NULL;

    if (pMod)
        *pMod = drmu_vlc_fmt_info_drm_modifier(f);
    return drmu_vlc_fmt_info_drm_pixelformat(f);
}
#endif

#if HAS_DRMPRIME
uint32_t
drmu_format_vlc_to_drm_prime(const video_frame_format_t * const vf_vlc, uint64_t * const pMod)
{
    const drmu_vlc_fmt_info_t * f = drmu_vlc_fmt_info_find_vlc(vf_vlc);

    if (!drmu_vlc_fmt_info_is_drmprime(f))
        f = NULL;

    if (pMod)
        *pMod = drmu_vlc_fmt_info_drm_modifier(f);
    return drmu_vlc_fmt_info_drm_pixelformat(f);
}
#endif

// Convert chroma to drm - can't cope with RGB32 or RGB16 as they require
// more info
uint32_t
drmu_format_vlc_chroma_to_drm(const vlc_fourcc_t chroma)
{
    const drmu_vlc_fmt_info_t * f = drmu_vlc_fmt_info_find_vlc(&(const video_frame_format_t){.i_chroma = chroma});
    return drmu_vlc_fmt_info_drm_pixelformat(f);
}

