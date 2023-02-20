#include "drmu_vlc.h"
#include "drmu_fmts.h"
#include "drmu_log.h"

#if HAS_ZC_CMA
#include "../../hw/mmal/mmal_cma_pic.h"
#endif
#if HAS_DRMPRIME
#include "../../codec/avcodec/drm_pic.h"
#endif

#include <errno.h>

#include <libavutil/buffer.h>
#include <libavutil/hwcontext_drm.h>

#include <libdrm/drm.h>
#include <libdrm/drm_mode.h>
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

typedef struct fb_aux_pic_s {
    picture_context_t * pic_ctx;
} fb_aux_pic_t;

static void
pic_fb_delete_cb(void * v)
{
    fb_aux_pic_t * const aux = v;

    aux->pic_ctx->destroy(aux->pic_ctx);
    free(aux);
}

static int
pic_hdr_metadata(struct hdr_output_metadata * const m, const struct video_format_t * const fmt)
{
    struct hdr_metadata_infoframe * const inf = &m->hdmi_metadata_type1;
    unsigned int i;

    memset(m, 0, sizeof(*m));
    m->metadata_type = HDMI_STATIC_METADATA_TYPE1;
    inf->metadata_type = HDMI_STATIC_METADATA_TYPE1;

    switch (fmt->transfer) {
        case TRANSFER_FUNC_SMPTE_ST2084:
            inf->eotf = HDMI_EOTF_SMPTE_ST2084;
            break;
        case TRANSFER_FUNC_ARIB_B67:
            inf->eotf = HDMI_EOTF_BT_2100_HLG;
            break;
        default:
            // HDMI_EOTF_TRADITIONAL_GAMMA_HDR for 10bit?
            inf->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
            return -ENOENT;
    }

    // VLC & HDMI use the same scales for everything but max_luma
    for (i = 0; i != 3; ++i) {
        inf->display_primaries[i].x = fmt->mastering.primaries[i * 2 + 0];
        inf->display_primaries[i].y = fmt->mastering.primaries[i * 2 + 1];
    }
    inf->white_point.x = fmt->mastering.white_point[0];
    inf->white_point.y = fmt->mastering.white_point[1];
    inf->max_display_mastering_luminance = (uint16_t)(fmt->mastering.max_luminance / 10000);
    inf->min_display_mastering_luminance = (uint16_t)fmt->mastering.min_luminance;

    inf->max_cll = fmt->lighting.MaxCLL;
    inf->max_fall = fmt->lighting.MaxFALL;

    return 0;
}

static drmu_color_encoding_t
fb_vlc_color_encoding(const video_format_t * const fmt)
{
    switch (fmt->space)
    {
        case COLOR_SPACE_BT2020:
            return DRMU_COLOR_ENCODING_BT2020;
        case COLOR_SPACE_BT601:
            return DRMU_COLOR_ENCODING_BT601;
        case COLOR_SPACE_BT709:
            return DRMU_COLOR_ENCODING_BT709;
        case COLOR_SPACE_UNDEF:
        default:
            break;
    }

    return (fmt->i_visible_width > 1024 || fmt->i_visible_height > 600) ?
        DRMU_COLOR_ENCODING_BT709 :
        DRMU_COLOR_ENCODING_BT601;
}

static drmu_color_range_t
fb_vlc_color_range(const video_format_t * const fmt)
{
#if HAS_VLC4
    switch (fmt->color_range)
    {
        case COLOR_RANGE_FULL:
            return DRMU_COLOR_RANGE_YCBCR_FULL_RANGE;
        case COLOR_RANGE_UNDEF:
        case COLOR_RANGE_LIMITED:
        default:
            break;
    }
#else
    if (fmt->b_color_range_full)
        return DRMU_COLOR_RANGE_YCBCR_FULL_RANGE;
#endif
    return DRMU_COLOR_RANGE_YCBCR_LIMITED_RANGE;
}


static const char *
fb_vlc_colorspace(const video_format_t * const fmt)
{
    switch (fmt->space) {
        case COLOR_SPACE_BT2020:
            return DRMU_COLORSPACE_BT2020_RGB;
        default:
            break;
    }
    return DRMU_COLORSPACE_DEFAULT;
}

static drmu_chroma_siting_t
fb_vlc_chroma_siting(const video_format_t * const fmt)
{
    switch (fmt->chroma_location) {
        case CHROMA_LOCATION_LEFT:
            return DRMU_CHROMA_SITING_LEFT;
        case CHROMA_LOCATION_CENTER:
            return DRMU_CHROMA_SITING_CENTER;
        case CHROMA_LOCATION_TOP_LEFT:
            return DRMU_CHROMA_SITING_TOP_LEFT;
        case CHROMA_LOCATION_TOP_CENTER:
            return DRMU_CHROMA_SITING_TOP;
        case CHROMA_LOCATION_BOTTOM_LEFT:
            return DRMU_CHROMA_SITING_BOTTOM_LEFT;
        case CHROMA_LOCATION_BOTTOM_CENTER:
            return DRMU_CHROMA_SITING_BOTTOM;
        default:
        case CHROMA_LOCATION_UNDEF:
            break;
    }
    return DRMU_CHROMA_SITING_UNSPECIFIED;
}

void
drmu_fb_vlc_pic_set_metadata(drmu_fb_t * const dfb, const picture_t * const pic)
{
    struct hdr_output_metadata meta;

    drmu_fb_int_color_set(dfb,
                          fb_vlc_color_encoding(&pic->format),
                          fb_vlc_color_range(&pic->format),
                          fb_vlc_colorspace(&pic->format));

    drmu_fb_int_chroma_siting_set(dfb, fb_vlc_chroma_siting(&pic->format));

    drmu_fb_hdr_metadata_set(dfb, pic_hdr_metadata(&meta, &pic->format) == 0 ? &meta : NULL);
}

#if HAS_DRMPRIME
// Create a new fb from a VLC DRM_PRIME picture.
// Picture is held reffed by the fb until the fb is deleted
drmu_fb_t *
drmu_fb_vlc_new_pic_attach(drmu_env_t * const du, picture_t * const pic)
{
    int i, j, n;
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    const AVDRMFrameDescriptor * const desc = drm_prime_get_desc(pic);
    fb_aux_pic_t * aux = NULL;

    if (dfb == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }

    if (desc == NULL) {
        drmu_err(du, "%s: Missing descriptor", __func__);
        goto fail;
    }
    if (desc->nb_objects > 4) {
        drmu_err(du, "%s: Bad descriptor", __func__);
        goto fail;
    }

    drmu_fb_int_fmt_size_set(dfb,
                             desc->layers[0].format,
                             pic->format.i_width,
                             pic->format.i_height,
                             drmu_rect_vlc_pic_crop(pic));


    // Set delete callback & hold this pic
    // Aux attached to dfb immediately so no fail cleanup required
    if ((aux = calloc(1, sizeof(*aux))) == NULL) {
        drmu_err(du, "%s: Aux alloc failure", __func__);
        goto fail;
    }
    aux->pic_ctx = pic->context->copy(pic->context);
    drmu_fb_int_on_delete_set(dfb, pic_fb_delete_cb, aux);

    for (i = 0; i < desc->nb_objects; ++i)
    {
        drmu_bo_t * bo = drmu_bo_new_fd(du, desc->objects[i].fd);
        if (bo == NULL)
            goto fail;
        drmu_fb_int_bo_set(dfb, i, bo);
    }

    n = 0;
    for (i = 0; i < desc->nb_layers; ++i)
    {
        for (j = 0; j < desc->layers[i].nb_planes; ++j)
        {
            const AVDRMPlaneDescriptor *const p = desc->layers[i].planes + j;
            const AVDRMObjectDescriptor *const obj = desc->objects + p->object_index;

            drmu_fb_int_layer_mod_set(dfb, n++, p->object_index, p->pitch, p->offset, obj->format_modifier);
        }
    }

    drmu_fb_vlc_pic_set_metadata(dfb, pic);

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}
#endif

#if HAS_ZC_CMA
drmu_fb_t *
drmu_fb_vlc_new_pic_cma_attach(drmu_env_t * const du, picture_t * const pic)
{
    int i;
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    fb_aux_pic_t * aux = NULL;
    uint32_t fmt = drmu_format_vlc_to_drm_cma(pic->format.i_chroma);
    const bool is_sand = (pic->format.i_chroma == VLC_CODEC_MMAL_ZC_SAND8 ||
                          pic->format.i_chroma == VLC_CODEC_MMAL_ZC_SAND30);
    cma_buf_t * const cb = cma_buf_pic_get(pic);

    if (dfb == NULL) {
        drmu_err(du, "%s: Alloc failure", __func__);
        return NULL;
    }

    if (fmt == 0) {
        drmu_err(du, "Pic bad format for cma");
        goto fail;
    }

    if (cb == NULL) {
        drmu_err(du, "Pic missing cma block");
        goto fail;
    }

    drmu_fb_int_fmt_size_set(dfb,
                             fmt,
                             pic->format.i_width,
                             pic->format.i_height,
                             drmu_rect_vlc_pic_crop(pic));

    // Set delete callback & hold this pic
    // Aux attached to dfb immediately so no fail cleanup required
    if ((aux = calloc(1, sizeof(*aux))) == NULL) {
        drmu_err(du, "%s: Aux alloc failure", __func__);
        goto fail;
    }
    aux->pic_ctx = pic->context->copy(pic->context);
    drmu_fb_int_on_delete_set(dfb, pic_fb_delete_cb, aux);

    {
        drmu_bo_t * bo = drmu_bo_new_fd(du, cma_buf_fd(cb));
        if (bo == NULL)
            goto fail;
        drmu_fb_int_bo_set(dfb, 0, bo);
    }

    {
        uint8_t * const base_addr = cma_buf_addr(cb);
        for (i = 0; i < pic->i_planes; ++i) {
            if (is_sand)
                drmu_fb_int_layer_mod_set(dfb, i, 0, pic->format.i_width, pic->p[i].p_pixels - base_addr,
                                          DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(pic->p[i].i_pitch));
            else
                drmu_fb_int_layer_mod_set(dfb, i, 0, pic->p[i].i_pitch, pic->p[i].p_pixels - base_addr, 0);
        }
    }

    drmu_fb_vlc_pic_set_metadata(dfb, pic);

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}
#endif

plane_t
drmu_fb_vlc_plane(drmu_fb_t * const dfb, const unsigned int plane_n)
{
    const drmu_fmt_info_t *const f = drmu_fb_format_info_get(dfb);
    unsigned int hdiv = drmu_fmt_info_hdiv(f, plane_n);
    unsigned int wdiv = drmu_fmt_info_wdiv(f, plane_n);
    const unsigned int bpp = drmu_fmt_info_pixel_bits(f);
    const uint32_t pitch_n = drmu_fb_pitch(dfb, plane_n);
    const drmu_rect_t crop = drmu_fb_crop_frac(dfb);

    if (pitch_n == 0) {
        return (plane_t) {.p_pixels = NULL };
    }

    return (plane_t){
        .p_pixels = drmu_fb_data(dfb, plane_n),
        .i_lines = drmu_fb_height(dfb) / hdiv,
        .i_pitch = pitch_n,
        .i_pixel_pitch = bpp / 8,
        .i_visible_lines = (crop.h >> 16) / hdiv,
        .i_visible_pitch = ((crop.w >> 16) * bpp / 8) / wdiv
    };
}

#if !HAS_VLC4
#define vlc_object_vaLog vlc_vaLog
#endif

void
drmu_log_vlc_cb(void * v, enum drmu_log_level_e level_drmu, const char * fmt, va_list vl)
{
    const char * const file_name = va_arg(vl, const char *);
    const unsigned int line_no = va_arg(vl, unsigned int);
    const char * const function_name = va_arg(vl, const char *);
    const int level_vlc =
        level_drmu <= DRMU_LOG_LEVEL_MESSAGE ? VLC_MSG_INFO :
        level_drmu <= DRMU_LOG_LEVEL_ERROR   ? VLC_MSG_ERR :
        level_drmu <= DRMU_LOG_LEVEL_WARNING ? VLC_MSG_WARN :
            VLC_MSG_DBG;

    vlc_object_vaLog((vlc_object_t *)v, level_vlc, vlc_module_name, file_name, line_no,
                     function_name, fmt + DRMU_LOG_FMT_OFFSET_FMT, vl);
}

