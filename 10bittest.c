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
#include <memory.h>

#include "drmu.h"
#include "drmu_log.h"
#include <drm_fourcc.h>

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

#define STRIPES (7 * 4 * 2)
#define SWIDTH 1024

static void
fillstripe(uint8_t * const p,
           const unsigned int w, const unsigned int h, const unsigned int stride,
           const unsigned int r,
           const uint32_t val0, const uint32_t add_val)
{
    unsigned int i, j, k;
    for (i = 0; i != h; ++i) {
        uint32_t x = val0;
        uint32_t * p2 = (uint32_t *)( p + i * stride);
        for (j = 0; j != w; ++j) {
            for (k = 0; k != r; ++k)
                *p2++ = x;
            x += add_val;
        }
    }
}

static void
fillgraduated10(uint8_t * const p, const unsigned int h, const unsigned int k)
{
    unsigned int i, j;
    const unsigned int w = SWIDTH;
    const unsigned int stripestride = h * w * k * 4;
    for (i = 1; i != 8; ++i) {
        uint8_t * const p1 = p + (i - 1) * 8 * stripestride;
        for (j = 0; j != 4; ++j) {
            uint8_t * const p2 = p1  + j * 2 * stripestride;
            fillstripe(p2,
                       w, h, w * 4 * k, 4 * k,
                       (j << 30), ((i & 1) << 2) | ((i & 2) << 11) | ((i & 4) << 20));
            fillstripe(p2 + stripestride,
                       w, h, w * 4 * k, k,
                       (j << 30), ((i & 1) << 0) | ((i & 2) << 9) | ((i & 4) << 18));
        }
    }
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

int main(int argc, char *argv[])
{
    unsigned int i;

    drmu_env_t * du = NULL;
    drmu_crtc_t * dc = NULL;
    drmu_plane_t * p0 = NULL;
    drmu_plane_t * p1 = NULL;
    drmu_fb_t * fb0 = NULL;
    drmu_fb_t * fb1 = NULL;
    drmu_atomic_t * da = NULL;
    unsigned int total_h;
    unsigned int total_w;
    unsigned int stripe_h, width_k;
    uint32_t p1fmt = DRM_FORMAT_ARGB2101010;
    unsigned int dw, dh;

    (void)argc;
    (void)argv;

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if ((du = drmu_env_new_xlease(&log)) == NULL &&
            (du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    drmu_env_restore_enable(du);
    drmu_env_modeset_allow(du, true);

    if ((dc = drmu_crtc_new_find(du)) == NULL)
        goto fail;

    drmu_crtc_max_bpc_allow(dc, 1);
    dw = drmu_crtc_width(dc);
    dh = drmu_crtc_height(dc);

    stripe_h = dh / STRIPES;
    total_h = stripe_h * STRIPES;
    width_k = dw / SWIDTH;
    total_w = width_k * SWIDTH;

    // **** Plane selection needs noticable improvement
    // This wants to be the primary
    if ((p0 = drmu_plane_new_find(dc, DRM_FORMAT_ARGB8888)) == NULL)
        goto fail;

    {
        unsigned int n = 0;
        const uint32_t * p = drmu_plane_formats(p0, &n);
        for (i = 0; i != n; ++i, ++p) {
            printf("Format[%d]: %.4s\n", i, (const char *)p);
        }
    }


    if ((p1 = drmu_plane_new_find(dc, p1fmt)) == NULL)
        fprintf(stderr, "Cannot find plane for %s\n", drmu_log_fourcc(p1fmt));

    fb0 = drmu_fb_new_dumb(du, 128, 128, DRM_FORMAT_ARGB8888);
    memset(drmu_fb_data(fb0, 0), 128, 128*128*4);

    if ((fb1 = drmu_fb_new_dumb(du, total_w, total_h, p1fmt)) == NULL)
        fprintf(stderr, "Cannot find make dumb for %s\n", drmu_log_fourcc(p1fmt));
    else
        fillgraduated10(drmu_fb_data(fb1, 0), stripe_h, width_k);

    da = drmu_atomic_new(du);

    drmu_atomic_plane_fb_set(da, p0, fb0, drmu_rect_wh(dw, dh));
    if (total_h > dh || total_w > dw)
        drmu_atomic_plane_fb_set(da, p1, fb1, drmu_rect_wh(dw, dh));
    else
        drmu_atomic_plane_fb_set(da, p1, fb1, drmu_rect_wh(total_w, total_h));

    static const struct hdr_output_metadata meta = {
        .metadata_type = HDMI_STATIC_METADATA_TYPE1,
        .hdmi_metadata_type1 = {
            .eotf = HDMI_EOTF_SMPTE_ST2084,
            .metadata_type = HDMI_STATIC_METADATA_TYPE1,
            .display_primaries = {{34000,16000},{13250,34500},{7500,3000}},
            .white_point = {15635,16450},
            .max_display_mastering_luminance = 1000,
            .min_display_mastering_luminance = 5,
            .max_cll = 1000,
            .max_fall = 400
        }
    };
    if (drmu_atomic_crtc_hdr_metadata_set(da, dc, &meta) != 0)
        fprintf(stderr, "Failed metadata set");
    if (drmu_atomic_crtc_colorspace_set(da, dc, "BT2020_RGB") != 0)
        fprintf(stderr, "Failed colorspace set\n");
    if (drmu_atomic_crtc_hi_bpc_set(da, dc, true) != 0)
        fprintf(stderr, "Failed hi bpc set\n");

    drmu_atomic_queue(&da);

    sleep(10);

fail:
    drmu_atomic_unref(&da);
    drmu_fb_unref(&fb1);
    drmu_plane_delete(&p1);
    drmu_fb_unref(&fb0);
    drmu_plane_delete(&p0);
    drmu_crtc_delete(&dc);
    drmu_env_delete(&du);
    return 0;
}


