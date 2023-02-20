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
buf_fb_delete_cb(void * v)
{
    fb_aux_buf_t * const aux = v;

    av_buffer_unref(&aux->buf);
    free(aux);
}

// Take a paranoid approach to the rational
static inline uint_fast32_t
hdr_rat_x50000(const AVRational x, const uint_fast32_t maxval)
{
    int64_t t;
    if (x.den == 0)
        return 0;

    t = ((int64_t)x.num * 50000) / x.den;
    return (t < 0 || (uint64_t)t > maxval) ? 0 : (uint_fast32_t)t;
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
        case AVCOL_TRC_SMPTE2084:
            info->eotf = HDMI_EOTF_SMPTE_ST2084;
            break;
        case AVCOL_TRC_ARIB_STD_B67:
            info->eotf = HDMI_EOTF_BT_2100_HLG;
            break;
        case AVCOL_TRC_BT709:
        case AVCOL_TRC_BT2020_10:
        case AVCOL_TRC_BT2020_12:
            info->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_HDR;
            return -ENOENT;
        default:
            info->eotf = HDMI_EOTF_TRADITIONAL_GAMMA_SDR;
            return -ENOENT;
    }

    // It is legit to have partial info with the remainder full of zeros

    if (av_disp && av_disp->has_primaries) {
        // CEA-861-G says the order of these doesn't matter
        // RGB are determined by relative values
        for (i = 0; i != 3; ++i) {
            info->display_primaries[i].x = hdr_rat_x50000(av_disp->display_primaries[i][0], UINT16_MAX);
            info->display_primaries[i].y = hdr_rat_x50000(av_disp->display_primaries[i][1], UINT16_MAX);
        }
        info->white_point.x = hdr_rat_x50000(av_disp->white_point[0], UINT16_MAX);
        info->white_point.y = hdr_rat_x50000(av_disp->white_point[1], UINT16_MAX);
    }
    if (av_disp && av_disp->has_luminance) {
        info->min_display_mastering_luminance = hdr_rat_x50000(av_disp->min_luminance, UINT16_MAX * 5) / 5;
        info->max_display_mastering_luminance = hdr_rat_x50000(av_disp->max_luminance, UINT16_MAX * 50000U) / 50000U;
    }

    if (av_light) {
        info->max_cll = av_light->MaxCLL;
        info->max_fall = av_light->MaxFALL;
    }

    return 0;
}

static drmu_color_encoding_t
fb_av_color_encoding(const AVFrame * const frame)
{
    switch (frame->colorspace)
    {
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:
        case AVCOL_SPC_ICTCP:
            return DRMU_COLOR_ENCODING_BT2020;

        case AVCOL_SPC_BT470BG:
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_SMPTE240M:
            return DRMU_COLOR_ENCODING_BT601;

        case AVCOL_SPC_BT709:
            return DRMU_COLOR_ENCODING_BT709;

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
        DRMU_COLOR_ENCODING_BT709 :
        DRMU_COLOR_ENCODING_BT601;
}

static drmu_color_range_t
fb_av_color_range(const AVFrame * const frame)
{
    switch (frame->color_range)
    {
        case AVCOL_RANGE_MPEG:
            return DRMU_COLOR_RANGE_YCBCR_LIMITED_RANGE;

        case AVCOL_RANGE_UNSPECIFIED:
        case AVCOL_RANGE_JPEG:
        default:
            break;
    }
    return DRMU_COLOR_RANGE_YCBCR_FULL_RANGE;
}


// Best guess at this mapping
static const char *
fb_av_colorspace(const AVFrame * const frame)
{
    switch (frame->color_primaries) {
        case AVCOL_PRI_BT709:  // = 1, also ITU-R BT1361 / IEC 61966-2-4 / SMPTE RP177 Annex B
            switch (frame->color_trc) {
                case AVCOL_TRC_IEC61966_2_4:
                    return DRMU_COLORSPACE_XVYCC_709;
                default:
                    return DRMU_COLORSPACE_BT709_YCC;
            }

        case AVCOL_PRI_BT470BG:   // 5, also ITU-R BT601-6 625 / ITU-R BT1358 625 / ITU-R BT1700 625 PAL & SECAM
        case AVCOL_PRI_SMPTE170M: // 6, also ITU-R BT601-6 525 / ITU-R BT1358 525 / ITU-R BT1700 NTSC
        case AVCOL_PRI_SMPTE240M: // 7  functionally identical to above
            switch (frame->color_trc) {
                case AVCOL_TRC_IEC61966_2_1:
                    return DRMU_COLORSPACE_SYCC_601;
                case AVCOL_TRC_IEC61966_2_4:
                    return DRMU_COLORSPACE_XVYCC_601;
                default:
                    return DRMU_COLORSPACE_SMPTE_170M_YCC;
            }

        case AVCOL_PRI_BT2020:    // ITU-R BT2020
            switch (frame->colorspace) {
                case AVCOL_SPC_BT2020_CL:
                    return DRMU_COLORSPACE_BT2020_CYCC;
                default:
                    return DRMU_COLORSPACE_BT2020_YCC;
            }

        case AVCOL_PRI_SMPTE432:  // 12, SMPTE ST 432-1 (2010) / P3 D65 / Display P3
            return DRMU_COLORSPACE_DCI_P3_RGB_D65;

        case AVCOL_PRI_SMPTE431:  // 11, SMPTE ST 431-2 (2011) / DCI P3
            return DRMU_COLORSPACE_DCI_P3_RGB_THEATER;
        case AVCOL_PRI_BT470M:    // also FCC Title 47 Code of Federal Regulations 73.682 (a)(20)
        case AVCOL_PRI_FILM:      // 8  colour filters using Illuminant C
        case AVCOL_PRI_SMPTE428:  // 10, SMPTE ST 428-1 (CIE 1931 XYZ)
        case AVCOL_PRI_EBU3213:   // 22, EBU Tech. 3213-E / JEDEC P22 phosphors
        default:
            break;
    }
    return DRMU_COLORSPACE_DEFAULT;
}

static drmu_chroma_siting_t
fb_av_chroma_siting(const enum AVChromaLocation loc)
{
    switch (loc) {
        case AVCHROMA_LOC_LEFT:
            return DRMU_CHROMA_SITING_LEFT;
        case AVCHROMA_LOC_CENTER:
            return DRMU_CHROMA_SITING_CENTER;
        case AVCHROMA_LOC_TOPLEFT:
            return DRMU_CHROMA_SITING_TOP_LEFT;
        case AVCHROMA_LOC_TOP:
            return DRMU_CHROMA_SITING_TOP;
        case AVCHROMA_LOC_BOTTOMLEFT:
            return DRMU_CHROMA_SITING_BOTTOM_LEFT;
        case AVCHROMA_LOC_BOTTOM:
            return DRMU_CHROMA_SITING_BOTTOM;
        case AVCHROMA_LOC_UNSPECIFIED:
        default:
            break;
    }
    return DRMU_CHROMA_SITING_UNSPECIFIED;
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

    drmu_fb_int_chroma_siting_set(dfb, fb_av_chroma_siting(frame->chroma_location));

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
            drmu_fb_hdr_metadata_set(dfb, &meta);
    }

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}


