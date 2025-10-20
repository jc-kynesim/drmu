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

#include <libdrm/drm_fourcc.h>

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

static unsigned int
fb_vlc_orientation(const video_format_t * const fmt)
{
    switch (fmt->orientation) {
        case ORIENT_NORMAL:
            return DRMU_ROTATION_0;
        case ORIENT_HFLIPPED:
            return DRMU_ROTATION_H_FLIP;
        case ORIENT_VFLIPPED:
            return DRMU_ROTATION_V_FLIP;
        case ORIENT_ROTATED_180:
            return DRMU_ROTATION_180;
        case ORIENT_TRANSPOSED:
            return DRMU_ROTATION_TRANSPOSE;
        case ORIENT_ROTATED_270:
            return DRMU_ROTATION_270;
        case ORIENT_ROTATED_90:
            return DRMU_ROTATION_90;
        case ORIENT_ANTI_TRANSPOSED:
            return DRMU_ROTATION_180_TRANSPOSE;
        default:
            break;
    }
    return DRMU_ROTATION_INVALID;
}

void
drmu_fb_vlc_pic_set_metadata(drmu_fb_t * const dfb, const picture_t * const pic)
{
    struct hdr_output_metadata meta;

    drmu_fb_color_set(dfb,
                          fb_vlc_color_encoding(&pic->format),
                          fb_vlc_color_range(&pic->format),
                          fb_vlc_colorspace(&pic->format));

    drmu_fb_chroma_siting_set(dfb, fb_vlc_chroma_siting(&pic->format));

    drmu_fb_hdr_metadata_set(dfb, pic_hdr_metadata(&meta, &pic->format) == 0 ? &meta : NULL);

    drmu_fb_orientation_set(dfb, fb_vlc_orientation(&pic->format));
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
    uint64_t mod;
    uint32_t fmt = drmu_format_vlc_to_drm_cma(&pic->format, &mod);
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
                drmu_fb_int_layer_mod_set(dfb, i, 0, pic->p[i].i_pitch, pic->p[i].p_pixels - base_addr, mod);
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
    const unsigned int bypp = (drmu_fmt_info_pixel_bits(f) + 7) / 8;
    const uint32_t pitch_n = drmu_fb_pitch(dfb, plane_n);
    const drmu_rect_t crop = drmu_rect_shr16_rnd(drmu_fb_crop_frac(dfb));

    if (pitch_n == 0) {
        return (plane_t) {.p_pixels = NULL };
    }

    return (plane_t){
        .p_pixels = (uint8_t *)drmu_fb_data(dfb, plane_n) +
            pitch_n * (crop.y / hdiv) + (crop.x / wdiv) * bypp,
        .i_lines = drmu_fb_height(dfb) / hdiv,
        .i_pitch = pitch_n,
        .i_pixel_pitch = bypp,
        .i_visible_lines = crop.h / hdiv,
        .i_visible_pitch = (crop.w / wdiv) * bypp
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

