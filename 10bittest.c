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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_util.h"
#include <drm_fourcc.h>

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

#define STRIPES (7 * 4 * 2)
#define SWIDTH 256

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
fillpin(uint8_t * const p,
           const unsigned int w, const unsigned int h, const unsigned int stride,
           const unsigned int r,
           const uint32_t val0)
{
    unsigned int i, j;
    for (i = 0; i != h; ++i) {
        uint32_t * p2 = (uint32_t *)( p + i * stride);
        for (j = 0; j != w; ++j) {
            *p2++ = (j % r == 0) ? val0 : 0;
        }
    }
}


static void
fillgraduated10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride)
{
    unsigned int i, j;
    const unsigned int vstripes = 4;
    const unsigned int w = (1024 / vstripes);
    const unsigned int k = dw / w;
    const unsigned int h = dh / (vstripes * 2 * 8);
    const unsigned int stripestride = h * stride;

    for (i = 1; i != 8; ++i) {
        uint8_t * const p1 = p + (i - 1) * 8 * stripestride;
        for (j = 0; j != 4; ++j) {
            uint8_t * const p2 = p1  + j * 2 * stripestride;
            uint32_t inc8  = ((i & 1) << 2) | ((i & 2) << 11) | ((i & 4) << 20);
            uint32_t inc10 = ((i & 1) << 0) | ((i & 2) << 9)  | ((i & 4) << 18);
            uint32_t val0 = (3U << 30) | (inc10 * w * j);
            fillstripe(p2,
                       w / 4, h, stride, 4 * k,
                       val0,
                       inc8);
            fillstripe(p2 + stripestride,
                       w, h, stride, k,
                       val0, inc10);
        }
    }
}

static void
fillgradgrey10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride)
{
    unsigned int j;
    const unsigned int vstripes = 16;
    const unsigned int w = (1024 / vstripes);
    const unsigned int k = dw / w;
    const unsigned int h = dh / (vstripes * 2);
    const unsigned int stripestride = h * stride;

    for (j = 0; j != vstripes; ++j) {
        uint8_t * const p2 = p  + j * 2 * stripestride;
        uint32_t inc8  = (1 << 2) | (2 << 11) | (4 << 20);
        uint32_t inc10 = (1 << 0) | (2 << 9)  | (4 << 18);
        uint32_t val0 = (3U << 30) | (inc10 * w * j);
        fillstripe(p2,
                   w / 4, h, stride, 4 * k,
                   val0,
                   inc8);
        fillstripe(p2 + stripestride,
                   w, h, stride, k,
                   val0, inc10);
    }
}

static void
fillpin10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride)
{
    unsigned int i, j;
    const unsigned int vstripes = 8;
    const unsigned int h = dh / (vstripes * 7);
    const unsigned int stripestride = h * stride;

    for (i = 0; i != vstripes; ++i) {
        uint8_t * const p1 = p + i * 7 * stripestride;
        for (j = 1; j != 8; ++j) {
            uint8_t * const p2 = p1 + (j - 1) * stripestride;
            uint32_t inc10 = ((j & 1) << 0) | ((j & 2) << 9)  | ((j & 4) << 18);
            uint32_t val0 = (3U << 30) | (inc10 << 9);
            fillpin(p2 + stripestride, dw, h, stride, i + 2, val0);
        }
    }
}

static void
fillyuv_sand30(uint8_t * const py, uint8_t * const pc, unsigned int dw, unsigned int dh, unsigned int stride2,
               const unsigned long vals[3])
{
    unsigned int c;
    const unsigned int colw = (128 / 4) * 3;
    const unsigned int stride1 = 128;
    const uint32_t y   = vals[0] | (vals[0] << 10) | (vals[0] << 20);
    const uint32_t uv0 = vals[1] | (vals[2] << 10) | (vals[1] << 20);
    const uint32_t uv1 = vals[2] | (vals[1] << 10) | (vals[2] << 20);

    for (c = 0; c < (dw + colw - 1) / colw; ++c) {
        unsigned int i;
        uint32_t * py2 = (uint32_t *)(py + c * stride2 * stride1);
        uint32_t * pc2 = (uint32_t *)(pc + c * stride2 * stride1);
        for (i = 0; i != dh * stride1 / 4; ++i) {
            *py2++ = y;
        }
        for (i = 0; i != dh/2 * stride1 / 8; ++i) {
            *pc2++ = uv0;
            *pc2++ = uv1;
        }
    }
}

static void
allgrey(uint8_t * const data, unsigned int dw, unsigned int dh, unsigned int stride)
{
    unsigned int i;
    for (i = 0; i != dh; ++i) {
        unsigned int j;
        uint32_t * p = (uint32_t *)(data + i * stride);
        for (j = 0; j != dw; ++j)
            *p++ = ((3U << 30) | (1 << 29) | (1 << 19) | (1 << 9));
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

static void
usage()
{
    printf("Usage: 10bittest [-g|-p|-y <y>,<u>,<v>] [-8] [-v] [<w>x<h>][@<hz>]\n\n"
           "-g  grey blocks only, otherwise colour stripes\n"
           "-p  pinstripes\n"
           "-y  solid y,u,v 10-bit values"
           "-8  keep max_bpc 8\n"
           "-v  verbose\n"
           "\n"
           "Hit return to exit\n"
           "\n"
           "Stripes have values incrementing as for 8-bit data at the top and\n"
           "incrementing for 10-bit at the bottom\n"
           "Pinstripes iterate through the 7 easy colours and then get 1 pixel\n"
           "wider on repeat\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    drmu_env_t * du = NULL;
    drmu_crtc_t * dc = NULL;
    drmu_plane_t * p1 = NULL;
    drmu_fb_t * fb1 = NULL;
    drmu_atomic_t * da = NULL;
    uint32_t p1fmt = DRM_FORMAT_ARGB2101010;
    uint64_t p1mod = DRM_FORMAT_MOD_INVALID;
    unsigned int dw = 0, dh = 0;
    unsigned int hz = 0;
    unsigned int stride;
    uint8_t * data;
    bool grey_only = false;
    bool fill_pin = false;
    bool fill_yuv = false;
    bool mode_req = false;
    bool hi_bpc = true;
    int verbose = 0;
    int c;
    unsigned long fillvals[3] = {0};

    while ((c = getopt(argc, argv, "8gpvy:")) != -1) {
        switch (c) {
            case 'g':
                grey_only = true;
                break;
            case 'p':
                fill_pin = true;
                break;
            case 'y': {
                const char * s = optarg;
                fillvals[0] = strtoul(s, (char**)&s, 0);
                if (*s != ',')
                    usage();
                fillvals[1] = strtoul(s + 1, (char**)&s, 0);
                if (*s != ',')
                    usage();
                fillvals[2] = strtoul(s + 1, (char**)&s, 0);
                if (*s != '\0')
                    usage();
                fill_yuv = true;
                p1fmt = DRM_FORMAT_P030;
                p1mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0);
                break;
            }
            case '8':
                hi_bpc = false;
                break;
            case 'v':
                ++verbose;
                break;
            default:
                usage();
        }
    }

    if (optind < argc) {
        if (*drmu_util_parse_mode(argv[optind], &dw, &dh, &hz) == '\0') {
            mode_req = true;
            ++optind;
        }
    }

    if (optind != argc)
        usage();

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = verbose ? DRMU_LOG_LEVEL_ALL : DRMU_LOG_LEVEL_INFO
        };
        if ((du = drmu_env_new_xlease(&log)) == NULL &&
            (du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    drmu_env_restore_enable(du);
    drmu_env_modeset_allow(du, true);

    da = drmu_atomic_new(du);

    if ((dc = drmu_crtc_new_find(du)) == NULL)
        goto fail;

    drmu_crtc_max_bpc_allow(dc, hi_bpc);

    if (!mode_req) {
        drmu_mode_pick_simple_params_t pickparam = drmu_crtc_mode_simple_params(dc, -1);
        dw = pickparam.width;
        dh = pickparam.height;
        printf("Mode %dx%d@%d.%03d\n",
               pickparam.width, pickparam.height, pickparam.hz_x_1000 / 1000, pickparam.hz_x_1000 % 1000);
    }
    else
    {
        drmu_mode_pick_simple_params_t pickparam = drmu_crtc_mode_simple_params(dc, -1);
        int mode;

        if (dw || dh) {
            pickparam.width = dw;
            pickparam.height = dh;
        }
        pickparam.hz_x_1000 = hz;  // 0 is legit -pick something

        mode = drmu_crtc_mode_pick(dc, drmu_mode_pick_simple_cb, &pickparam);

        if (mode != -1) {
            const drmu_mode_pick_simple_params_t m = drmu_crtc_mode_simple_params(dc, mode);
            printf("Mode requested %dx%d@%d.%03d; found %dx%d@%d.%03d\n",
                   pickparam.width, pickparam.height, pickparam.hz_x_1000 / 1000, pickparam.hz_x_1000 % 1000,
                   m.width, m.height, m.hz_x_1000 / 1000, m.hz_x_1000 % 1000);

            if (m.width != pickparam.width || m.height != pickparam.height ||
                !(pickparam.hz_x_1000 == 0 ||
                  (pickparam.hz_x_1000 < m.hz_x_1000 + 100 && pickparam.hz_x_1000 + 100 > m.hz_x_1000))) {
                fprintf(stderr, "Mode not close enough\n");
                goto fail;
            }

            drmu_atomic_crtc_mode_id_set(da, dc, mode);

            dw = m.width;
            dh = m.height;
        }
        else {
            fprintf(stderr, "No mode that matches request found\n");
            goto fail;
        }


    }
    printf("Use hi bits per channel: %s\n", hi_bpc ? "yes" : "no");


    if ((p1 = drmu_plane_new_find(dc, p1fmt)) == NULL) {
        fprintf(stderr, "Cannot find plane for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    if ((fb1 = drmu_fb_new_dumb_mod(du, dw, dh, p1fmt, p1mod)) == NULL) {
        fprintf(stderr, "Cannot make dumb for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    stride = drmu_fb_pitch(fb1, 0);
    data = drmu_fb_data(fb1, 0);

    // Start with grey fill
    if (!fill_yuv)
        allgrey(data, dw, dh, stride);

    if (fill_pin)
        fillpin10(data, dw, dh, stride);
    else if (grey_only)
        fillgradgrey10(data, dw, dh, stride);
    else if (fill_yuv)
        fillyuv_sand30(data, drmu_fb_data(fb1, 1), dw, dh, drmu_fb_pitch2(fb1, 0), fillvals);
    else
        fillgraduated10(data, dw, dh, stride);

    drmu_atomic_plane_fb_set(da, p1, fb1, drmu_rect_wh(dw, dh));

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
    if (drmu_atomic_crtc_hi_bpc_set(da, dc, hi_bpc) != 0)
        fprintf(stderr, "Failed hi bpc set\n");

    drmu_atomic_queue(&da);

    getchar();

fail:
    drmu_atomic_unref(&da);
    drmu_fb_unref(&fb1);
    drmu_plane_delete(&p1);
    drmu_crtc_delete(&dc);
    drmu_env_delete(&du);
    return 0;
}


