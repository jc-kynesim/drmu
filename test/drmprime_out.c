/*
 * Copyright (c) 2020 John Cox for Raspberry Pi Trading
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */


// *** This module is a work in progress and its utility is strictly
//     limited to testing.

#include "drmprime_out.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include <sys/eventfd.h>

#include "libavutil/frame.h"
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"

#include "config.h"
#include "drmu.h"
#include "drmu_av.h"
#include "drmu_dmabuf.h"
#include "drmu_fmts.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_pool.h"
#include "drmu_util.h"
#include "drmu_writeback.h"
#include <drm_fourcc.h>

#include "cube/runcube.h"

#include "freetype/runticker.h"

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

struct drmprime_out_env_s {
    drmu_env_t * du;
    drmu_output_t * dout;

    // Writeback stuff
    drmu_writeback_env_t * wbe;
    drmu_plane_t * dxp;
    uint32_t xfmt;

    runticker_env_t * rte;
    runcube_env_t * rce;
};

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);

    (void)v;
    (void)level;

    if (n >= 255)
        n = 255;
    buf[n] = '\n';
    fwrite(buf, n + 1, 1, stderr);
}

drmu_output_t * drmprime_out_drmu_output(drmprime_out_env_t* const dpo)
{
    return dpo->dout;
}

void
drmprime_out_size(drmprime_out_env_t * const dpo, unsigned int *pW, unsigned int *pH)
{
    const drmu_mode_simple_params_t * sp = drmu_output_mode_simple_params(dpo->dout);
    *pW = sp == NULL ? 0 : sp->width;
    *pH = sp == NULL ? 0 : sp->height;
}

void drmprime_out_delete(drmprime_out_env_t *dpo)
{
    drmprime_out_runticker_stop(dpo);
    drmprime_out_runcube_stop(dpo);

    drmu_plane_unref(&dpo->dxp);
    drmu_writeback_env_unref(&dpo->wbe);

    drmu_output_unref(&dpo->dout);
    drmu_env_kill(&dpo->du);
    free(dpo);
}

drmprime_out_env_t* drmprime_out_new()
{
    drmprime_out_env_t* const dpo = calloc(1, sizeof(*dpo));
    if (dpo == NULL)
        return NULL;

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if (
#if HAS_XLEASE
            (dpo->du = drmu_env_new_xlease(&log)) == NULL &&
#endif
            (dpo->du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }
    drmu_env_restore_enable(dpo->du);

    if ((dpo->dout = drmu_output_new(dpo->du)) == NULL)
        goto fail;

    if (drmu_output_add_output(dpo->dout, NULL) != 0)
        goto fail;

    drmu_output_max_bpc_allow(dpo->dout, true);

    return dpo;

fail:
    drmprime_out_delete(dpo);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}

drmprime_out_env_t* drmprime_out_new_fd(int fd)
{
    drmprime_out_env_t* const dpo = calloc(1, sizeof(*dpo));
    if (dpo == NULL)
        return NULL;

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if ((dpo->du = drmu_env_new_fd(fd, &log)) == NULL)
            goto fail;
    }
    drmu_env_restore_enable(dpo->du);

    if ((dpo->dout = drmu_output_new(dpo->du)) == NULL)
        goto fail;

    if (drmu_output_add_output(dpo->dout, NULL) != 0)
        goto fail;

    drmu_output_max_bpc_allow(dpo->dout, true);

    return dpo;

fail:
    drmprime_out_delete(dpo);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}



void drmprime_out_runticker_start(drmprime_out_env_t * const dpo, const char * const ticker_text)
{
#if HAS_RUNTICKER
    const drmu_mode_simple_params_t * mode = drmu_output_mode_simple_params(dpo->dout);
    static const char fontfile[] = "/usr/share/fonts/truetype/freefont/FreeSerif.ttf";

    if ((dpo->rte = runticker_start(dpo->dout,
                               mode->width / 10, mode->height * 8/10, mode->width * 8/10, mode->height / 10,
                               ticker_text, fontfile)) == NULL) {
        fprintf(stderr, "Failed to create ticker\n");
    }
#else
    (void)dpo;
    (void)ticker_text;
    fprintf(stderr, "Ticker support not compiled\n");
#endif
}

void drmprime_out_runticker_stop(drmprime_out_env_t * const dpo)
{
#if HAS_RUNTICKER
    runticker_stop(&dpo->rte);
#else
    (void)dpo;
#endif
}

void drmprime_out_runcube_start(drmprime_out_env_t * const dpo)
{
#if HAS_RUNCUBE
    dpo->rce = runcube_drmu_start(dpo->dout);
#else
    (void)dpo;
    fprintf(stderr, "Cube support not compiled\n");
#endif
}

void drmprime_out_runcube_stop(drmprime_out_env_t * const dpo)
{
#if HAS_RUNCUBE
    runcube_drmu_stop(&dpo->rce);
#else
    (void)dpo;
#endif
}



//-----------------------------------------------------------------------------

struct drmprime_video_env_s
{
    drmprime_out_env_t * dpo;
    drmu_env_t * du;
    drmu_output_t * dout;
    drmu_plane_t * dp;
    drmu_pool_t * pic_pool;
    drmu_atomic_t * display_set;

    drmu_writeback_fb_t * wbq;

    int mode_id;
    drmu_mode_simple_params_t picked;
    drmu_rect_t win_rect;
    drmu_rect_t vid_rect;
    unsigned int zpos;
    unsigned int rotation;

    bool wants_prod;
    bool prod_wait;
    int prod_fd;
};

typedef struct gb2_dmabuf_s
{
    drmu_fb_t * fb;
} gb2_dmabuf_t;

static void
do_prod(void *v)
{
    static const uint64_t one = 1;
    drmprime_video_env_t *const dpo = v;
    int rv;
    while ((rv = write(dpo->prod_fd, &one, sizeof(one))) != sizeof(one)) {
        if (!(rv == -1 && errno == EINTR))
            break;
    }
}

static void gb2_free(void * v, uint8_t * data)
{
    gb2_dmabuf_t * const gb2 = v;
    (void)data;

    drmu_fb_unref(&gb2->fb);
    free(gb2);
}

// Assumes drmprime_out_env in s->opaque
int drmprime_video_get_buffer2(drmprime_video_env_t * const dpo, struct AVCodecContext *s, AVFrame *frame, int flags)
{
    int align[AV_NUM_DATA_POINTERS];
    int w = frame->width;
    int h = frame->height;
    uint64_t mod;
    const uint32_t fmt = drmu_av_fmt_to_drm(frame->format, &mod);
    unsigned int i;
    unsigned int layers;
    const drmu_fmt_info_t * fmti;
    gb2_dmabuf_t * gb2;
    (void)flags;

    assert((s->codec->capabilities & AV_CODEC_CAP_DR1) != 0);
    assert(fmt != 0);

    // Alignment logic taken directly from avcodec_default_get_buffer2
    avcodec_align_dimensions2(s, &w, &h, align);

    gb2 = calloc(1, sizeof(*gb2));
    if ((gb2->fb = drmu_pool_fb_new(dpo->pic_pool, w, h, fmt, mod)) == NULL) {
        free(gb2);
        return AVERROR(ENOMEM);
    }
    drmu_fb_crop_frac_set(gb2->fb, drmu_rect_shl16((drmu_rect_t){
        .x = frame->crop_left,
        .y = frame->crop_top,
        .w = w - (frame->crop_left + frame->crop_right),
        .h = h - (frame->crop_top + frame->crop_bottom)}));

    frame->buf[0] = av_buffer_create((uint8_t*)gb2, sizeof(*gb2), gb2_free, gb2, 0);

    fmti = drmu_fb_format_info_get(gb2->fb);
    layers = drmu_fmt_info_plane_count(fmti);

    for (i = 0; i != layers; ++i) {
        frame->data[i] = drmu_fb_data(gb2->fb, i);
        frame->linesize[i] = drmu_fb_pitch(gb2->fb, i);
    }

    drmu_fb_write_start(gb2->fb);

    frame->opaque = dpo;
    return 0;
}

static drmu_rect_t
frame_output_rect(drmprime_video_env_t * const de, drmu_fb_t * const dfb, const AVFrame * const src_frame)
{
    const drmu_mode_simple_params_t *const sp = drmu_output_mode_simple_params(de->dout);
    drmu_rect_t crop = drmu_rect_shr16(drmu_fb_crop_frac(dfb));
    drmu_ufrac_t ppar = {.num = src_frame->sample_aspect_ratio.num * crop.w, .den = src_frame->sample_aspect_ratio.den * crop.h};
    drmu_ufrac_t mpar = drmu_util_guess_simple_mode_par(sp);
    drmu_rect_t r = de->win_rect.w != 0 ? de->win_rect : drmu_rect_wh(sp->width, sp->height);

    if (de->win_rect.w != 0) {
        mpar.num *= r.w * sp->height;
        mpar.den *= r.h * sp->width;
        mpar = drmu_ufrac_reduce(mpar);
    }

    ppar = ppar.den == 0 || ppar.num == 0 ? drmu_util_guess_par(crop.w, crop.h) : drmu_ufrac_reduce(ppar);
    if (drmu_rotation_is_transposed(de->rotation))
        ppar = drmu_ufrac_invert(ppar);

    if (ppar.num * mpar.den < ppar.den * mpar.num) {
        // Pillarbox
        const uint32_t w = r.w;
        r.w = r.h * ppar.num / ppar.den;
        r.x += (w - r.w) / 2;
    }
    else {
        // Letterbox
        const uint32_t h = r.h;
        r.h = r.w * ppar.den / ppar.num;
        r.y += (h - r.h) / 2;
    }
    return r;
}

typedef struct frame_env_s {
    drmu_output_t * dout;
    drmu_plane_t * dp;
    uint32_t zpos;
    drmu_rect_t dest_rect;
} frame_env_t;

static void
frame_env_free(frame_env_t * const fe)
{
    drmu_plane_unref(&fe->dp);
    drmu_output_unref(&fe->dout);
    free(fe);
}

static frame_env_t *
frame_env_new(drmprime_video_env_t * const de, const drmu_rect_t dest_rect)
{
    frame_env_t * const fe = calloc(1, sizeof(*fe));
    if (fe == NULL)
        return NULL;

    fe->dout = drmu_output_ref(de->dout);
    fe->dp = drmu_plane_ref(de->dp);
    fe->zpos = de->zpos;
    fe->dest_rect = dest_rect;
    return fe;
}

static void
writeback_fb_done_cb(void * v, struct drmu_fb_s * dfb)
{
    frame_env_t * const fe = v;

    if (dfb != NULL) {
        drmu_atomic_t * da = drmu_atomic_new(drmu_output_env(fe->dout));
        drmu_atomic_output_add_props(da, fe->dout);
        drmu_atomic_plane_add_fb(da, fe->dp, dfb, fe->dest_rect);
        drmu_atomic_plane_add_zpos(da, fe->dp, fe->zpos);
        drmu_atomic_queue(&da);
    }

    frame_env_free(fe);
}

int drmprime_video_display(drmprime_video_env_t *de, struct AVFrame *src_frame)
{
    bool is_prime;
    drmprime_out_env_t * const dpo = de->dpo;

    if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
        fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
        return 0;
    }

    if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
        is_prime = true;
    } else if (src_frame->opaque == de) {
        is_prime = false;
    }
    else {
        fprintf(stderr, "Frame (format=%d) not DRM_PRiME & frame->opaque not ours\n", src_frame->format);
        return AVERROR(EINVAL);
    }

    if (de->prod_wait) {
        uint64_t buf[1];
        int rv;
        de->prod_wait = false;
        while ((rv = read(de->prod_fd, buf, 8)) != 8) {
            if (rv == -1 && errno == EINTR)
                continue;
            fprintf(stderr, "Unexpected return value from reading prod: rv=%d, err=%d", rv, errno);
            break;
        }
    }

    {
        drmu_env_t * const du = de->du;
        drmu_atomic_t * da = drmu_atomic_new(du);
        drmu_fb_t * dfb = is_prime ?
            drmu_fb_av_new_frame_attach(du, src_frame) :
            drmu_fb_ref(((gb2_dmabuf_t *)src_frame->buf[0]->data)->fb);
//        const drmu_mode_simple_params_t *const sp = drmu_output_mode_simple_params(de->dout);

        drmu_fb_write_end(dfb); // Needed for mapped dmabufs, noop otherwise

        de->vid_rect = frame_output_rect(de, dfb, src_frame);

        if (!is_prime)
            drmu_av_fb_frame_metadata_set(dfb, src_frame);


#if 0
        const struct hdr_output_metadata * const meta = drmu_fb_hdr_metadata_get(dfb);
        const struct hdr_metadata_infoframe *const info = &meta->hdmi_metadata_type1;

        if (meta) {
            printf("Meta found: type=%d, eotf=%d, type=%d, primaries: %d,%d:%d,%d:%d,%d white: %d,%d maxluma=%d minluma=%d maxcll=%d maxfall=%d\n",
                   meta->metadata_type,
                   info->eotf, info->metadata_type,
                   info->display_primaries[0].x, info->display_primaries[0].y,
                   info->display_primaries[1].x, info->display_primaries[1].y,
                   info->display_primaries[2].x, info->display_primaries[2].y,
                   info->white_point.x, info->white_point.y,
                   info->max_display_mastering_luminance,
                   info->min_display_mastering_luminance,
                   info->max_cll,
                   info->max_fall);
        }
#endif

        if (drmu_rotation_is_transposed(de->rotation)) {
            drmu_rect_t rs = drmu_rect_shr16(drmu_fb_crop_frac(dfb));
            frame_env_t * fe = NULL;
            unsigned int rot;
            int rv;

            if (rs.h * rs.w > de->vid_rect.h * de->vid_rect.w)
                rs = de->vid_rect;

            // This should be global
            if (dpo->wbe == NULL) {
                if ((dpo->wbe = drmu_writeback_env_new(du)) == NULL) {
                    fprintf(stderr, "Failed to create writeback env\n");
                    return -1;
                }
                // dxp is writeback plane
                if (dpo->dxp == NULL) {
                    drmu_output_t * dxout = drmu_writeback_env_output(dpo->wbe);
                    dpo->dxp = drmu_output_plane_ref_format(dxout, DRMU_PLANE_TYPE_PRIMARY, drmu_fb_pixel_format(dfb), drmu_fb_modifier(dfb, 0));
                    if (!dpo->dxp) {
                        fprintf(stderr, "Failed to find plane for pixel format %s mod %#" PRIx64 "\n", drmu_log_fourcc(drmu_fb_pixel_format(dfb)), drmu_fb_modifier(dfb, 0));
                        drmu_atomic_unref(&da);
                        return AVERROR(EINVAL);
                    }
                }
            }

            if (de->wbq == NULL) {
                unsigned int plane_type = de->zpos == 0 ? DRMU_PLANE_TYPE_PRIMARY : DRMU_PLANE_TYPE_OVERLAY;
                if ((de->wbq = drmu_writeback_fb_new(dpo->wbe, de->pic_pool)) == NULL) {
                    fprintf(stderr, "Failed to get queue for writeback\n");
                    return -1;
                }
                // dp is display plane
                if ((de->dp = drmu_writeback_env_fmt_plane(dpo->wbe, de->dout, plane_type, &dpo->xfmt)) == NULL) {
                    fprintf(stderr, "Failed to get plane for writeback\n");
                    return -1;
                }
            }

            if ((fe = frame_env_new(de, de->vid_rect)) == NULL) {
                fprintf(stderr, "Failed to alloc frame_env\n");
                return -ENOMEM;
            }

            rs = drmu_writeback_fb_queue_rect(de->wbq, rs);
            rot = drmu_writeback_fb_queue_rotation(de->wbq, de->rotation);

            drmu_atomic_plane_add_fb(da, dpo->dxp, dfb, drmu_rect_transpose(rs));
            drmu_fb_unref(&dfb);

            drmu_atomic_plane_add_rotation(da, dpo->dxp, drmu_rotation_suba(de->rotation, rot));

            if ((rv = drmu_writeback_fb_queue(de->wbq, rs, rot, dpo->xfmt, writeback_fb_done_cb, fe, &da)) != 0) {
                fprintf(stderr, "Writeback FB Q fail\n");
                return rv;
            }
        }
        else {
            if (de->dp == NULL) {
                unsigned int types = DRMU_PLANE_TYPE_OVERLAY;
                if (de->zpos == 0)
                    types |= DRMU_PLANE_TYPE_PRIMARY;
                de->dp = drmu_output_plane_ref_format(de->dout, types, drmu_fb_pixel_format(dfb), drmu_fb_modifier(dfb, 0));
                if (!de->dp) {
                    fprintf(stderr, "Failed to find plane for pixel format %s mod %#" PRIx64 "\n", drmu_log_fourcc(drmu_fb_pixel_format(dfb)), drmu_fb_modifier(dfb, 0));
                    drmu_atomic_unref(&da);
                    return AVERROR(EINVAL);
                }
            }

            drmu_output_fb_info_set(de->dout, dfb);
            drmu_atomic_output_add_props(da, de->dout);
            drmu_atomic_plane_add_fb(da, de->dp, dfb, de->vid_rect);
            drmu_atomic_plane_add_zpos(da, de->dp, de->zpos);
            drmu_atomic_plane_add_rotation(da, de->dp, de->rotation);
            if (de->wants_prod) {
                drmu_atomic_add_commit_callback(da, do_prod, de);
                de->prod_wait = true;
            }

            drmu_fb_unref(&dfb);
            drmu_atomic_queue(&da);
        }
    }

    return 0;
}

int drmprime_video_modeset(drmprime_video_env_t * de, int w, int h, const AVRational rate)
{
    drmu_mode_simple_params_t pick = {
        .width = w,
        .height = h,
        .hz_x_1000 = rate.den <= 0 ? 0 : rate.num * 1000 / rate.den
    };

    if (pick.width == de->picked.width &&
        pick.height == de->picked.height &&
        pick.hz_x_1000 == de->picked.hz_x_1000)
    {
        return 0;
    }

    drmu_output_modeset_allow(de->dout, true);

    de->mode_id = drmu_output_mode_pick_simple(de->dout, drmu_mode_pick_simple_cb, &pick);

    // This will set the mode on the crtc var but won't actually change the output
    if (de->mode_id >= 0) {
        const drmu_mode_simple_params_t * sp;
        drmu_output_mode_id_set(de->dout, de->mode_id);
        sp = drmu_output_mode_simple_params(de->dout);
        fprintf(stderr, "Req %dx%d Hz %d.%03d got %dx%d\n", pick.width, pick.height, pick.hz_x_1000 / 1000, pick.hz_x_1000%1000,
                sp->width, sp->height);
    }
    else {
        fprintf(stderr, "Req %dx%d Hz %d.%03d got nothing\n", pick.width, pick.height, pick.hz_x_1000 / 1000, pick.hz_x_1000%1000);
    }

    de->picked = pick;

    return 0;
}

void
drmprime_video_set_window_size(drmprime_video_env_t *de, const unsigned int w, const unsigned int h)
{
    de->win_rect.w = w;
    de->win_rect.h = h;
}

void
drmprime_video_set_window_pos(drmprime_video_env_t *de, const unsigned int x, const unsigned int y)
{
    de->win_rect.x = x;
    de->win_rect.y = y;
}

void
drmprime_video_set_window_zpos(drmprime_video_env_t *de, const unsigned int z)
{
    de->zpos = z;
}

int
drmprime_video_set_window_rotation(drmprime_video_env_t *de, const unsigned int rot)
{
    de->rotation = rot;
    return 0;
}


void
drmprime_video_set_sync(drmprime_video_env_t *de, const bool wants_prod)
{
    de->wants_prod = wants_prod;
}

void drmprime_video_delete(drmprime_video_env_t *de)
{
    drmu_pool_kill(&de->pic_pool);

    drmu_writeback_fb_unref(&de->wbq);
    drmu_plane_unref(&de->dp);
    drmu_output_unref(&de->dout);
    if (de->prod_fd != -1)
        close(de->prod_fd);
    free(de);
}

drmprime_video_env_t* drmprime_video_new(drmprime_out_env_t * const dpo)
{
    drmprime_video_env_t* const de = calloc(1, sizeof(*de));
    if (de == NULL)
        return NULL;

    de->dpo = dpo;
    de->mode_id = -1;
    de->prod_fd = -1;
    de->dout = drmu_output_ref(drmprime_out_drmu_output(dpo));
    de->du = drmu_output_env(de->dout);

    if ((de->prod_fd = eventfd(0, 0)) == -1) {
        fprintf(stderr, "Failed to get event fd\n");
        goto fail;
    }

    if ((de->pic_pool = drmu_pool_new_dmabuf_video(de->du, 32)) == NULL)
        goto fail;

    // Plane allocation delayed till we have a format - not all planes are idempotent

    return de;

fail:
    drmprime_video_delete(de);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}

int
drmprime_str_to_rotation(const char * s, const char ** peos)
{
    static const struct {
        const char * str;
        unsigned int rot;
    } str_to_rot[] = {
        {"0", DRMU_ROTATION_0},
        {"X", DRMU_ROTATION_X_FLIP},
        {"Y", DRMU_ROTATION_Y_FLIP},
        {"180T", DRMU_ROTATION_180_TRANSPOSE},
        {"180", DRMU_ROTATION_180},
        {"TRANSPOSE", DRMU_ROTATION_TRANSPOSE},
        {"T", DRMU_ROTATION_TRANSPOSE},
        {"90", DRMU_ROTATION_90},
        {"270", DRMU_ROTATION_270},
        {NULL, 0},
    };
    unsigned int i;

    for (i = 0; str_to_rot[i].str != NULL; ++i) {
        size_t n = strlen(str_to_rot[i].str);
        if (strncasecmp(s, str_to_rot[i].str, n) == 0) {
            if (peos != NULL)
                *peos = s + n;
            return str_to_rot[i].rot;
        }
    }
    if (peos != NULL)
        *peos = s;
    return -1;
}

