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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <pthread.h>
#include <semaphore.h>

#include "libavutil/frame.h"
#include "libavcodec/avcodec.h"
#include "libavutil/hwcontext.h"

#include "config.h"
#include "drmu.h"
#include "drmu_av.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include <drm_fourcc.h>

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

typedef struct drmprime_out_env_s
{
    drmu_env_t * du;
    drmu_output_t * dout;
    drmu_plane_t * dp;
    drmu_pool_t * pic_pool;
    drmu_atomic_t * display_set;

    int mode_id;
    drmu_mode_simple_params_t picked;
} drmprime_out_env_t;

int drmprime_out_display(drmprime_out_env_t *de, struct AVFrame *src_frame)
{
    AVFrame *frame;

    if ((src_frame->flags & AV_FRAME_FLAG_CORRUPT) != 0) {
        fprintf(stderr, "Discard corrupt frame: fmt=%d, ts=%" PRId64 "\n", src_frame->format, src_frame->pts);
        return 0;
    }

    if (src_frame->format == AV_PIX_FMT_DRM_PRIME) {
        frame = av_frame_alloc();
        av_frame_ref(frame, src_frame);
    } else if (src_frame->format == AV_PIX_FMT_VAAPI) {
        frame = av_frame_alloc();
        frame->format = AV_PIX_FMT_DRM_PRIME;
        if (av_hwframe_map(frame, src_frame, 0) != 0) {
            fprintf(stderr, "Failed to map frame (format=%d) to DRM_PRiME\n", src_frame->format);
            av_frame_free(&frame);
            return AVERROR(EINVAL);
        }
    } else {
        fprintf(stderr, "Frame (format=%d) not DRM_PRiME\n", src_frame->format);
        return AVERROR(EINVAL);
    }

    drmu_env_queue_wait(de->du);
    {
        drmu_atomic_t * da = drmu_atomic_new(de->du);
        drmu_fb_t * dfb = drmu_fb_av_new_frame_attach(de->du, src_frame);
        const drmu_mode_simple_params_t *const sp = drmu_output_mode_simple_params(de->dout);
        drmu_rect_t r = drmu_rect_wh(sp->width, sp->height);

        if (de->dp == NULL) {
            de->dp = drmu_output_plane_ref_format(de->dout, 0, drmu_fb_pixel_format(dfb), drmu_fb_modifier(dfb, 0));
            if (!de->dp) {
                fprintf(stderr, "Failed to find plane for pixel format %s mod %#" PRIx64 "\n", drmu_log_fourcc(drmu_fb_pixel_format(dfb)), drmu_fb_modifier(dfb, 0));
                drmu_atomic_unref(&da);
                av_frame_free(&frame);
                return AVERROR(EINVAL);
            }
        }

        drmu_output_fb_info_set(de->dout, dfb);
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
        drmu_atomic_output_add_props(da, de->dout);
        drmu_atomic_plane_add_fb(da, de->dp, dfb, r);

        drmu_fb_unref(&dfb);
        drmu_atomic_queue(&da);
    }

    av_frame_free(&frame);

    return 0;
}

int drmprime_out_modeset(drmprime_out_env_t * de, int w, int h, const AVRational rate)
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

void drmprime_out_delete(drmprime_out_env_t *de)
{
    drmu_pool_delete(&de->pic_pool);
    drmu_plane_unref(&de->dp);
    drmu_output_unref(&de->dout);
    drmu_env_unref(&de->du);
    free(de);
}

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

drmprime_out_env_t* drmprime_out_new()
{
    drmprime_out_env_t* const de = calloc(1, sizeof(*de));
    if (de == NULL)
        return NULL;

    de->mode_id = -1;

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if (
#if HAS_XLEASE
            (de->du = drmu_env_new_xlease(&log)) == NULL &&
#endif
            (de->du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    if ((de->dout = drmu_output_new(de->du)) == NULL)
        goto fail;

    if (drmu_output_add_output(de->dout, NULL) != 0)
        goto fail;

    drmu_output_max_bpc_allow(de->dout, true);

    if ((de->pic_pool = drmu_pool_new(de->du, 5)) == NULL)
        goto fail;

    // Plane allocation delayed till we have a format - not all planes are idempotent

    return de;

fail:
    drmprime_out_delete(de);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}

