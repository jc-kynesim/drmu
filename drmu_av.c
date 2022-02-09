#include "drmu.h"
#include "drmu_av.h"
#include "drmu_log.h"

#include <limits.h>
#include <libdrm/drm_mode.h>
#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/pixfmt.h>

typedef struct fb_aux_buf_s {
    AVBufferRef * buf;
} fb_aux_buf_t;

static void
buf_fb_delete_cb(drmu_fb_t * dfb, void * v)
{
    fb_aux_buf_t * const aux = v;

    av_buffer_unref(&aux->buf);
    free(aux);
}

// Take a paranoid approach to the rational
static uint_fast32_t
hdr_rat_x50000(const AVRational x)
{
    int64_t t;
    if (x.den == 0)
        return 0;

    t = ((int64_t)x.num * 50000) / x.den;
    return (t < 0 || t > UINT_MAX) ? 0 : (uint_fast32_t)t;
}

// See CEA-861_G 6.9 (Pg 84)
// Returns:
//  -ENOENT   av data doesn't appear to contain hdr_metadata
//  -EINVAL   av data contains bad (or unrecognised) data
static int
drmu_crtc_av_hdr_metadata_from_av(struct hdr_output_metadata * const out_meta,
                                  const enum AVColorTransferCharacteristic av_trans,
                                  const AVMasteringDisplayMetadata * const av_disp,
                                  const AVContentLightMetadata * const av_light)
{
    unsigned int i;
    struct hdr_metadata_infoframe *const info = &out_meta->hdmi_metadata_type1;

    memset(out_meta, 0, sizeof(*out_meta));

    out_meta->metadata_type = HDMI_STATIC_METADATA_TYPE1;

    info->metadata_type = HDMI_STATIC_METADATA_TYPE1;
    switch (av_trans) {
        case AVCOL_TRC_BT2020_10:
        case AVCOL_TRC_BT2020_12:
        case AVCOL_TRC_SMPTE2084:
            info->eotf = HDMI_EOTF_SMPTE_ST2084;
            break;
        case AVCOL_TRC_ARIB_STD_B67:
            info->eotf = HDMI_EOTF_BT_2100_HLG;
            break;
        default:
            info->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
            return -ENOENT;
    }

    // It is legit to have partial info with the remainder full of zeros

    if (av_disp && av_disp->has_primaries) {
        // CEA-861-G says the order of these doesn't matter
        // RGB are determined by relative values
        for (i = 0; i != 3; ++i) {
            info->display_primaries[i].x = hdr_rat_x50000(av_disp->display_primaries[i][0]);
            info->display_primaries[i].y = hdr_rat_x50000(av_disp->display_primaries[i][1]);
        }
        info->white_point.x = hdr_rat_x50000(av_disp->white_point[0]);
        info->white_point.y = hdr_rat_x50000(av_disp->white_point[1]);
    }
    if (av_disp && av_disp->has_luminance) {
        info->min_display_mastering_luminance = hdr_rat_x50000(av_disp->min_luminance) / 5;
        info->max_display_mastering_luminance = hdr_rat_x50000(av_disp->max_luminance) / 50000U;
    }

    if (av_light) {
        info->max_cll = av_light->MaxCLL;
        info->max_fall = av_light->MaxFALL;
    }

    return 0;
}

static const char *
fb_av_color_encoding(const AVFrame * const frame)
{
    switch (frame->colorspace)
    {
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
        case AVCOL_SPC_ICTCP:
            return "ITU-R BT.2020 YCbCr";

        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_SMPTE240M:
            return "ITU-R BT.601 YCbCr";

        case AVCOL_SPC_BT709:
            return "ITU-R BT.709 YCbCr";

        case AVCOL_SPC_RGB:
        case AVCOL_SPC_UNSPECIFIED:
        case AVCOL_SPC_FCC:
        case AVCOL_SPC_YCGCO:
        case AVCOL_SPC_SMPTE2085:
        case AVCOL_SPC_CHROMA_DERIVED_NCL:
        case AVCOL_SPC_CHROMA_DERIVED_CL:
        default:
            break;
    }

    return (frame->width > 1024 || frame->height > 600) ?
        "ITU-R BT.709 YCbCr" :
        "ITU-R BT.601 YCbCr";
}

static const char *
fb_av_color_range(const AVFrame * const frame)
{
    switch (frame->color_range)
    {
        case AVCOL_RANGE_MPEG:
            return "YCbCr limited range";

        case AVCOL_RANGE_UNSPECIFIED:
        case AVCOL_RANGE_JPEG:
        default:
            break;
    }
    return "YCbCr full range";
}

static const char *
fb_av_colorspace(const AVFrame * const frame)
{
    switch (frame->colorspace) {
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
        case AVCOL_SPC_ICTCP:
            return "BT2020_RGB";
        default:
            break;
    }
    return "Default";
}

// Create a new fb from a VLC DRM_PRIME picture.
// Buf is held reffed by the fb until the fb is deleted
drmu_fb_t *
drmu_fb_av_new_frame_attach(drmu_env_t * const du, AVFrame * const frame)
{
    int i, j, n;
    drmu_fb_t * const dfb = drmu_fb_int_alloc(du);
    const AVDRMFrameDescriptor * const desc = (const AVDRMFrameDescriptor *)frame->data[0];
    fb_aux_buf_t * aux = NULL;

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
                             frame->width,
                             frame->height,
                             (drmu_rect_t){
                                 .x = frame->crop_left,
                                 .y = frame->crop_top,
                                 .w = frame->width - (frame->crop_left + frame->crop_right),
                                 .h = frame->height - (frame->crop_top + frame->crop_bottom)});

    drmu_fb_int_color_set(dfb,
                          fb_av_color_encoding(frame),
                          fb_av_color_range(frame),
                          fb_av_colorspace(frame));

    // Set delete callback & hold this pic
    // Aux attached to dfb immediately so no fail cleanup required
    if ((aux = calloc(1, sizeof(*aux))) == NULL) {
        drmu_err(du, "%s: Aux alloc failure", __func__);
        goto fail;
    }
    aux->buf = av_buffer_ref(frame->buf[0]);
    drmu_fb_int_on_delete_set(dfb, buf_fb_delete_cb, aux);

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

    // * Metadata can turn up in container but not ES but I don't have an example of that yet

    {
        struct hdr_output_metadata meta;
        const AVFrameSideData * const side_disp = av_frame_get_side_data(frame, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA);
        const AVFrameSideData * const side_light = av_frame_get_side_data(frame, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        int rv = drmu_crtc_av_hdr_metadata_from_av(&meta,
            frame->color_trc,
            !side_disp ? NULL : (const AVMasteringDisplayMetadata *)side_disp->data,
            !side_light ? NULL : (const AVContentLightMetadata *)side_light->data);
        if (rv == 0)
            drmu_fb_int_hdr_metadata_set(dfb, &meta);
    }

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}


