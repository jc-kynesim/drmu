#include "drmu.h"
#include "drmu_av.h"
#include "drmu_log.h"

#include <libavutil/frame.h>
#include <libavutil/hwcontext_drm.h>

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

//    drmu_fb_int_color_set(dfb,
//                          fb_vlc_color_encoding(&pic->format),
//                          fb_vlc_color_range(&pic->format),
//                          fb_vlc_colorspace(&pic->format));

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

#if-0
    if (pic->format.mastering.max_luminance == 0) {
        drmu_fb_int_hdr_metadata_set(dfb, NULL);
    }
    else {
        const struct hdr_output_metadata meta = pic_hdr_metadata(&pic->format);
        drmu_fb_int_hdr_metadata_set(dfb, &meta);
    }
#endif

    if (drmu_fb_int_make(dfb) != 0)
        goto fail;
    return dfb;

fail:
    drmu_fb_int_free(dfb);
    return NULL;
}


