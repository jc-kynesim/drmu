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
#include "libavutil/hwcontext.h"

#include "drmu.h"
#include "drmu_av.h"
#include <drm_fourcc.h>

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

typedef struct drmprime_out_env_s
{
    drmu_env_t * du;
    drmu_crtc_t * dc;
    drmu_plane_t * dp;
    drmu_pool_t * pic_pool;
    drmu_atomic_t * display_set;

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

    {
        drmu_atomic_t * da = drmu_atomic_new(de->du);
        drmu_fb_t * dfb = drmu_fb_av_new_frame_attach(de->du, src_frame);
        drmu_rect_t r = drmu_rect_wh(drmu_crtc_width(de->dc), drmu_crtc_height(de->dc));
        drmu_atomic_plane_set(da, de->dp, dfb, r);
        drmu_fb_unref(&dfb);
        drmu_atomic_queue(&da);
    }

    av_frame_free(&frame);

    return 0;
}

void drmprime_out_delete(drmprime_out_env_t *de)
{
    drmu_plane_delete(&de->dp);
    drmu_crtc_delete(&de->dc);
    drmu_env_delete(&de->du);
    free(de);
}

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);
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

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if ((de->du = drmu_env_new_xlease(&log)) == NULL &&
            (de->du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    drmu_env_modeset_allow(de->du, false);

    if ((de->dc = drmu_crtc_new_find(de->du)) == NULL)
        goto fail;

    drmu_crtc_max_bpc_allow(de->dc, true);

    if ((de->pic_pool = drmu_pool_new(de->du, 5)) == NULL)
        goto fail;

    // **** Plane selection needs noticable improvement
    // This wants to be the primary
    if ((de->dp = drmu_plane_new_find(de->dc, DRM_FORMAT_NV12)) == NULL)
        goto fail;

    // ** Could pick mode here

    return de;

fail:
    drmprime_out_delete(de);
    fprintf(stderr, ">>> %s: FAIL\n", __func__);
    return NULL;
}

