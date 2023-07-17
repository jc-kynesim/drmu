#include "drmu_vlc_fmts.h"

#include <libdrm/drm_fourcc.h>

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

// N.B. DRM seems to order its format descriptor names the opposite way round to VLC
// DRM is hi->lo within a little-endian word, VLC is byte order

#if HAS_ZC_CMA
uint32_t
drmu_format_vlc_to_drm_cma(const vlc_fourcc_t chroma_in)
{
    switch (chroma_in) {
        case VLC_CODEC_MMAL_ZC_I420:
            return DRM_FORMAT_YUV420;
        case VLC_CODEC_MMAL_ZC_SAND8:
            return DRM_FORMAT_NV12;
        case VLC_CODEC_MMAL_ZC_SAND30:
            return DRM_FORMAT_P030;
        case VLC_CODEC_MMAL_ZC_RGB32:
            return DRM_FORMAT_RGBX8888;
    }
    return 0;
}
#endif

#if HAS_DRMPRIME
uint32_t
drmu_format_vlc_to_drm_prime(const vlc_fourcc_t chroma_in, uint64_t * const pmod)
{
    uint32_t fmt = 0;
    uint64_t mod = DRM_FORMAT_MOD_LINEAR;

    switch (chroma_in) {
        case VLC_CODEC_DRM_PRIME_I420:
            fmt = DRM_FORMAT_YUV420;
            break;
        case VLC_CODEC_DRM_PRIME_NV12:
            fmt = DRM_FORMAT_NV12;
            break;
        case VLC_CODEC_DRM_PRIME_SAND8:
            fmt = DRM_FORMAT_NV12;
            mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0);
            break;
        case VLC_CODEC_DRM_PRIME_SAND30:
            fmt = DRM_FORMAT_P030;
            mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0);
            break;
    }
    if (pmod)
        *pmod = !fmt ? DRM_FORMAT_MOD_INVALID : mod;
    return fmt;
}
#endif

// Convert chroma to drm - can't cope with RGB32 or RGB16 as they require
// more info
uint32_t
drmu_format_vlc_chroma_to_drm(const vlc_fourcc_t chroma)
{
    switch (chroma) {
        case VLC_CODEC_RGBA:
            return DRM_FORMAT_ABGR8888;
        case VLC_CODEC_BGRA:
            return DRM_FORMAT_ARGB8888;
        case VLC_CODEC_ARGB:
            return DRM_FORMAT_BGRA8888;
        // VLC_CODEC_ABGR does not exist in VLC
        case VLC_CODEC_VUYA:
            return DRM_FORMAT_AYUV;
        // AYUV appears to be the only DRM YUVA-like format
        case VLC_CODEC_VYUY:
            return DRM_FORMAT_YUYV;
        case VLC_CODEC_UYVY:
            return DRM_FORMAT_YVYU;
        case VLC_CODEC_YUYV:
            return DRM_FORMAT_VYUY;
        case VLC_CODEC_YVYU:
            return DRM_FORMAT_UYVY;
        case VLC_CODEC_NV12:
            return DRM_FORMAT_NV12;
        case VLC_CODEC_NV21:
            return DRM_FORMAT_NV21;
        case VLC_CODEC_NV16:
            return DRM_FORMAT_NV16;
        case VLC_CODEC_NV61:
            return DRM_FORMAT_NV61;
        case VLC_CODEC_NV24:
            return DRM_FORMAT_NV24;
        case VLC_CODEC_NV42:
            return DRM_FORMAT_NV42;
        case VLC_CODEC_P010:
            return DRM_FORMAT_P010;
        case VLC_CODEC_J420:
        case VLC_CODEC_I420:
            return DRM_FORMAT_YUV420;
        case VLC_CODEC_YV12:
            return DRM_FORMAT_YVU420;
        case VLC_CODEC_J422:
        case VLC_CODEC_I422:
            return DRM_FORMAT_YUV422;
        case VLC_CODEC_J444:
        case VLC_CODEC_I444:
            return DRM_FORMAT_YUV444;
        default:
            break;
    }

#if HAS_ZC_CMA
    return drmu_format_vlc_to_drm_cma(chroma);
#else
    return 0;
#endif
}

uint32_t
drmu_format_vlc_to_drm(const video_frame_format_t * const vf_vlc)
{
    switch (vf_vlc->i_chroma) {
        case VLC_CODEC_RGB32:
        {
            // VLC RGB32 aka RV32 means we have to look at the mask values
            const uint32_t r = vf_vlc->i_rmask;
            const uint32_t g = vf_vlc->i_gmask;
            const uint32_t b = vf_vlc->i_bmask;
            if (r == 0xff0000 && g == 0xff00 && b == 0xff)
                return DRM_FORMAT_XRGB8888;
            if (r == 0xff && g == 0xff00 && b == 0xff0000)
                return DRM_FORMAT_XBGR8888;
            if (r == 0xff000000 && g == 0xff0000 && b == 0xff00)
                return DRM_FORMAT_RGBX8888;
            if (r == 0xff00 && g == 0xff0000 && b == 0xff000000)
                return DRM_FORMAT_BGRX8888;
            break;
        }
        case VLC_CODEC_RGB24:
        {
            // VLC RGB24 aka RV24 means we have to look at the mask values
            const uint32_t r = vf_vlc->i_rmask;
            const uint32_t g = vf_vlc->i_gmask;
            const uint32_t b = vf_vlc->i_bmask;
            if (r == 0xff0000 && g == 0xff00 && b == 0xff)
                return DRM_FORMAT_RGB888;
            if (r == 0xff && g == 0xff00 && b == 0xff0000)
                return DRM_FORMAT_BGR888;
            break;
        }
        case VLC_CODEC_RGB16:
        {
            // VLC RGB16 aka RV16 means we have to look at the mask values
            const uint32_t r = vf_vlc->i_rmask;
            const uint32_t g = vf_vlc->i_gmask;
            const uint32_t b = vf_vlc->i_bmask;
            if (r == 0xf800 && g == 0x7e0 && b == 0x1f)
                return DRM_FORMAT_RGB565;
            if (r == 0x1f && g == 0x7e0 && b == 0xf800)
                return DRM_FORMAT_BGR565;
            break;
        }
        default:
            break;
    }

    return drmu_format_vlc_chroma_to_drm(vf_vlc->i_chroma);
}

vlc_fourcc_t
drmu_format_vlc_to_vlc(const uint32_t vf_drm)
{
    switch (vf_drm) {
        case DRM_FORMAT_XRGB8888:
        case DRM_FORMAT_XBGR8888:
        case DRM_FORMAT_RGBX8888:
        case DRM_FORMAT_BGRX8888:
            return VLC_CODEC_RGB32;
        case DRM_FORMAT_RGB888:
        case DRM_FORMAT_BGR888:
            return VLC_CODEC_RGB24;
        case DRM_FORMAT_BGR565:
        case DRM_FORMAT_RGB565:
            return VLC_CODEC_RGB16;
        case DRM_FORMAT_ABGR8888:
            return VLC_CODEC_RGBA;
        case DRM_FORMAT_ARGB8888:
            return VLC_CODEC_BGRA;
        case DRM_FORMAT_BGRA8888:
            return VLC_CODEC_ARGB;
        // VLC_CODEC_ABGR does not exist in VLC
        case DRM_FORMAT_AYUV:
            return VLC_CODEC_VUYA;
        case DRM_FORMAT_YUYV:
            return VLC_CODEC_VYUY;
        case DRM_FORMAT_YVYU:
            return VLC_CODEC_UYVY;
        case DRM_FORMAT_VYUY:
            return VLC_CODEC_YUYV;
        case DRM_FORMAT_UYVY:
            return VLC_CODEC_YVYU;
        case DRM_FORMAT_NV12:
            return VLC_CODEC_NV12;
        case DRM_FORMAT_NV21:
            return VLC_CODEC_NV21;
        case DRM_FORMAT_NV16:
            return VLC_CODEC_NV16;
        case DRM_FORMAT_NV61:
            return VLC_CODEC_NV61;
        case DRM_FORMAT_NV24:
            return VLC_CODEC_NV24;
        case DRM_FORMAT_NV42:
            return VLC_CODEC_NV42;
        case DRM_FORMAT_P010:
            return VLC_CODEC_P010;
        case DRM_FORMAT_YUV420:
            return VLC_CODEC_I420;
        case DRM_FORMAT_YVU420:
            return VLC_CODEC_YV12;
        case DRM_FORMAT_YUV422:
            return VLC_CODEC_I422;
        case DRM_FORMAT_YUV444:
            return VLC_CODEC_I444;
        default:
            break;
    }
    return 0;
}

