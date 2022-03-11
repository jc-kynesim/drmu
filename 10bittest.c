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

static inline uint64_t
p16val(unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3)
{
    return
        ((uint64_t)(v0 & 0xffff) << 48) |
        ((uint64_t)(v1 & 0xffff) << 32) |
        ((uint64_t)(v2 & 0xffff) << 16) |
        ((uint64_t)(v3 & 0xffff) << 0);
}

// v0 -> A(2), v1 -> R(10), v2 -> G(10), v3 -> B(10)
static void
plane16_to_argb2101010(uint8_t * const dst_data, const unsigned int dst_stride,
                       const uint8_t * const src_data, const unsigned int src_stride,
                       const unsigned int w, const unsigned int h)
{
    unsigned int i, j;
    for (i = 0; i != h; ++i) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint32_t * d = (uint32_t *)(dst_data + i * dst_stride);
        for (j = 0; j != w; ++j, ++d, ++s) {
            *d =
                (((*s >> (48 + 14)) &     3) << 30) |
                (((*s >> (32 +  6)) & 0x3ff) << 20) |
                (((*s >> (16 +  6)) & 0x3ff) << 10) |
                (((*s >> (0  +  6)) & 0x3ff) << 0);
        }
    }
}

// v1 -> Y(10)
static void
plane16_to_sand30_y(uint8_t * const dst_data, const unsigned int dst_stride2,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    unsigned int i, j, k;
    const unsigned int dst_stride1 = 128;
    const unsigned int cw = dst_stride1 / 4 * 3;
    for (i = 0; i != h; ++i) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint32_t * d = (uint32_t *)(dst_data + i * dst_stride1);
        for (j = 0; j < w; j += cw) {
            for (k = j; k != j + cw; k += 3, s += 3, d += 1) {
                uint32_t a = (k + 0 >= w) ? 0x200 : (uint32_t)((s[0] >> (32 + 6)) & 0x3ff);
                uint32_t b = (k + 1 >= w) ? 0x200 : (uint32_t)((s[1] >> (32 + 6)) & 0x3ff);
                uint32_t c = (k + 2 >= w) ? 0x200 : (uint32_t)((s[2] >> (32 + 6)) & 0x3ff);
                *d = a | (b << 10) | (c << 20);
            }
            d += (dst_stride2 - 1) * dst_stride1 / sizeof(*d);
        }
    }
}

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(10), v3 -> V(10)
// w, h are src dimensions
static void
plane16_to_sand30_c(uint8_t * const dst_data, const unsigned int dst_stride2,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    unsigned int i, j, k;
    const unsigned int dst_stride1 = 128;
    const unsigned int cw = dst_stride1 / 4 * 3;
    const uint64_t grey = 0x200 | (0x200 << 10);

    for (i = 0; i < h; i += 2) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint64_t * d = (uint64_t *)(dst_data + i / 2 * dst_stride1);
        for (j = 0; j < w; j += cw) {
            for (k = j; k < j + cw; k += 6, s += 6, d += 1) {
                uint64_t a = (k + 0 >= w) ? grey :
                    (uint64_t)(((s[0] >> (16 + 6)) & 0x3ff) | (((s[0] >> (6)) & 0x3ff) << 10));
                uint64_t b = (k + 2 >= w) ? grey :
                    (uint64_t)(((s[2] >> (16 + 6)) & 0x3ff) | (((s[2] >> (6)) & 0x3ff) << 10));
                uint64_t c = (k + 4 >= w) ? grey :
                    (uint64_t)(((s[4] >> (16 + 6)) & 0x3ff) | (((s[4] >> (6)) & 0x3ff) << 10));
                *d = a | ((b & 0x3ff) << 20) | ((b & 0xffc00) << 22) | (c << 42);
            }
            d += (dst_stride2 - 1) * dst_stride1 / sizeof(*d);
        }
    }
}

static void
plane16_to_sand30(uint8_t * const dst_data_y, const unsigned int dst_stride2_y,
                  uint8_t * const dst_data_c, const unsigned int dst_stride2_c,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    plane16_to_sand30_y(dst_data_y, dst_stride2_y, src_data, src_stride, w, h);
    plane16_to_sand30_c(dst_data_c, dst_stride2_c, src_data, src_stride, w, h);
}

static void
fillstripe16(uint8_t * const p,
           const unsigned int w, const unsigned int h, const unsigned int stride,
           const unsigned int r,
           const uint64_t val0, const uint64_t add_val)
{
    unsigned int i, j, k;
    for (i = 0; i != h; ++i) {
        uint64_t x = val0;
        uint64_t * p2 = (uint64_t *)( p + i * stride);
        for (j = 0; j != w; ++j) {
            for (k = 0; k != r; ++k)
                *p2++ = x;
            x += add_val;
        }
    }
}

static void
fillpin16(uint8_t * const p,
           const unsigned int w, const unsigned int h, const unsigned int stride,
           const unsigned int r,
           const uint64_t val0, const uint64_t val1)
{
    unsigned int i, j;
    for (i = 0; i != h; ++i) {
        uint64_t * p2 = (uint64_t *)( p + i * stride);
        for (j = 0; j != w; ++j) {
            *p2++ = (j % r == 0) ? val0 : val1;
        }
    }
}


static void
fillgraduated10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride, const bool is_yuv)
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
            uint64_t inc10 = p16val(0, (i & 4) << 4, (i & 2) << 5, (i & 1) << 6);
            uint64_t inc8  = inc10 << 2;
            const uint64_t base10 = is_yuv ?
                p16val(~0U, (i & 4) ? 0 : 0x8000, (i & 2) ? 0 : 0x8000, (i & 1) ? 0 : 0x8000) :
                p16val(~0U, 0, 0, 0);
            uint64_t val0 =  base10 | (inc10 * w * j);
            fillstripe16(p2,
                       w / 4, h, stride, 4 * k,
                       val0,
                       inc8);
            fillstripe16(p2 + stripestride,
                       w, h, stride, k,
                       val0, inc10);
        }
    }
}

static void
fillgradgrey10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride, const bool is_yuv)
{
    unsigned int j;
    const unsigned int vstripes = 16;
    const unsigned int w = (1024 / vstripes);
    const unsigned int k = dw / w;
    const unsigned int h = dh / (vstripes * 2);
    const unsigned int stripestride = h * stride;
    const uint64_t base10 = is_yuv ? p16val(~0U, 0, 0x8000, 0x8000) : p16val(~0U, 0, 0, 0);
    const uint64_t inc10 = is_yuv ? p16val(0, 1 << 6, 0, 0) : p16val(0, 1 << 6, 1 << 6, 1 << 6);
    const uint64_t inc8  = inc10 << 2;

    for (j = 0; j != vstripes; ++j) {
        uint8_t * const p2 = p  + j * 2 * stripestride;
        uint64_t val0 =  base10 | (inc10 * w * j);
        fillstripe16(p2,
                   w / 4, h, stride, 4 * k,
                   val0,
                   inc8);
        fillstripe16(p2 + stripestride,
                   w, h, stride, k,
                   val0, inc10);
    }
}

static void
fillpin10(uint8_t * const p, unsigned int dw, unsigned int dh, unsigned int stride, const bool is_yuv)
{
    unsigned int i, j;
    const unsigned int vstripes = 8;
    const unsigned int h = dh / (vstripes * 7);
    const unsigned int stripestride = h * stride;
    const uint64_t grey = is_yuv ? p16val(~0U, 16, 0x8000, 0x8000) : p16val(~0U, 0, 0, 0);
    unsigned int v0a = is_yuv ? (16 << 8) : 0;
    unsigned int v1a = 0x8000;
    unsigned int v0b = is_yuv ? 0x8000 : 0;
    unsigned int v1b = is_yuv ? (235 << 8) : 0x8000;

    for (i = 0; i != vstripes; ++i) {
        uint8_t * const p1 = p + i * 7 * stripestride;
        for (j = 1; j != 8; ++j) {
            uint8_t * const p2 = p1 + (j - 1) * stripestride;
            uint64_t val0 = p16val(~0U, (j & 4) ? v1a : v0a, (j & 2) ? v1b : v0b, (j & 1) ? v1b : v0b);
            fillpin16(p2, dw, h, stride, is_yuv ? (i + 1) * 2 : i + 2, val0, grey);
        }
    }
}

#if 0
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
#endif

static void
fillsolid16(uint8_t * const data, unsigned int dw, unsigned int dh, unsigned int stride,
               const unsigned long vals[3])
{
    unsigned int i;
    const uint64_t grey = p16val(~0U, vals[0], vals[1], vals[2]);
    for (i = 0; i != dh; ++i) {
        unsigned int j;
        uint64_t * p = (uint64_t *)(data + i * stride);
        for (j = 0; j != dw; ++j)
            *p++ = grey;
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
    printf("Usage: 10bittest [-g|-p|-f <y>,<u>,<v>] [-y] [-8] [-c <colourspace>] [-v] [<w>x<h>][@<hz>]\n\n"
           "-g  grey blocks only, otherwise colour stripes\n"
           "-p  pinstripes\n"
           "-f  solid a, b, c 10-bit values\n"
           "-y  Use YUV plane (same vals as for RGB - no conv)\n"
           "-e  YUV encoding (only for -y) 609, 709, 2020 (default)\n"
           "-r  YUV range full, limited (default)\n"
           "-R  Broadcast RGB: auto, full (default), limited\n"
           "    if -r set then defaults to that\n"
           "-c  set con colorspace to (string) <colourspace>\n"
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
    const char * colorspace = "BT2020_RGB";
    const char * encoding = "ITU-R BT.2020 YCbCr";
    const char * range = NULL;
    const char * default_range = DRMU_PLANE_RANGE_FULL;
    const char * broadcast_rgb = NULL;
    bool grey_only = false;
    bool fill_pin = false;
    bool fill_solid = false;
    bool is_yuv = false;
    bool mode_req = false;
    bool hi_bpc = true;
    int verbose = 0;
    int c;
    unsigned long fillvals[3] = {0x8000, 0x8000, 0x8000};
    uint8_t *p16 = NULL;
    unsigned int p16_stride = 0;

    while ((c = getopt(argc, argv, "8c:e:f:gpr:R:vy")) != -1) {
        switch (c) {
            case 'c':
                colorspace = optarg;
                break;
            case 'e': {
                const char * s = optarg;
                if (strcmp(s, "601") == 0)
                    encoding = "ITU-R BT.601 YCbCr";
                else if (strcmp(s, "709") == 0)
                    encoding = "ITU-R BT.709 YCbCr";
                else if (strcmp(s, "2020") == 0)
                    encoding = "ITU-R BT.2020 YCbCr";
                else {
                    printf("Unrecognised encoding - valid values are 601, 709, 2020\n");
                    exit(1);
                }
                break;
            }
            case 'g':
                grey_only = true;
                break;
            case 'p':
                fill_pin = true;
                break;
            case 'r': {
                const char * s = optarg;
                if (strcmp(s, "full") == 0)
                    range = DRMU_PLANE_RANGE_FULL;
                else if (strcmp(s, "limited") == 0)
                    range = DRMU_PLANE_RANGE_LIMITED;
                else {
                    printf("Unrecognised range - valid values are limited, full\n");
                    exit(1);
                }
                break;
            }
            case 'R': {
                const char * s = optarg;
                if (strcmp(s, "full") == 0)
                    range = DRMU_CRTC_BROADCAST_RGB_FULL;
                else if (strcmp(s, "limited") == 0)
                    range = DRMU_CRTC_BROADCAST_RGB_LIMITED_16_235;
                else if (strcmp(s, "auto") == 0)
                    range = DRMU_CRTC_BROADCAST_RGB_AUTOMATIC;
                else {
                    printf("Unrecognised broadcast range - valid values are auto, limited, full\n");
                    exit(1);
                }
                break;
            }
            case 'f': {
                const char * s = optarg;
                fillvals[0] = strtoul(s, (char**)&s, 0) << 6;
                if (*s != ',')
                    usage();
                fillvals[1] = strtoul(s + 1, (char**)&s, 0) << 6;
                if (*s != ',')
                    usage();
                fillvals[2] = strtoul(s + 1, (char**)&s, 0) << 6;
                if (*s != '\0')
                    usage();
                fill_solid = true;
                break;
            }
            case 'y':
                p1fmt = DRM_FORMAT_P030;
                p1mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0);
                default_range = DRMU_PLANE_RANGE_LIMITED;
                is_yuv = true;
                break;
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

    if (range == NULL)
        range = default_range;

    if (broadcast_rgb == NULL)
        broadcast_rgb = drmu_color_range_to_broadcast_rgb(range);

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
    printf("Colorspace: %s, Broadcast RGB: %s\n", colorspace, broadcast_rgb);

    if ((p16 = malloc(dw * dh * 8)) == NULL) {
        printf("Failed to alloc P16 plane\n");
        goto fail;
    }
    p16_stride = dw * 8;

    if ((p1 = drmu_plane_new_find(dc, p1fmt)) == NULL) {
        fprintf(stderr, "Cannot find plane for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    if ((fb1 = drmu_fb_new_dumb_mod(du, dw, dh, p1fmt, p1mod)) == NULL) {
        fprintf(stderr, "Cannot make dumb for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    drmu_fb_int_color_set(fb1, encoding, range, colorspace);
    printf("%s encoding: %s, range %s\n", is_yuv ? "YUV" : "RGB", encoding, range);

    stride = drmu_fb_pitch(fb1, 0);
    data = drmu_fb_data(fb1, 0);

    // Start with grey fill
    fillsolid16(p16, dw, dh, p16_stride, fillvals);

    if (fill_pin)
        fillpin10(p16, dw, dh, p16_stride, is_yuv);
    else if (grey_only)
        fillgradgrey10(p16, dw, dh, p16_stride, is_yuv);
    else if (!fill_solid)
        fillgraduated10(p16, dw, dh, p16_stride, is_yuv);

    if (is_yuv)
        plane16_to_sand30(data, drmu_fb_pitch2(fb1, 0),
                          drmu_fb_data(fb1, 1), drmu_fb_pitch2(fb1, 1),
                          p16, p16_stride, dw, dh);
    else
        plane16_to_argb2101010(data, stride, p16, p16_stride, dw, dh);

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
    if (drmu_atomic_crtc_hdr_metadata_set(da, dc, &meta) != 0) {
        fprintf(stderr, "Failed metadata set");
        goto fail;
    }
    if (drmu_atomic_crtc_colorspace_set(da, dc, colorspace) != 0) {
        fprintf(stderr, "Failed to set colorspace to '%s'\n", colorspace);
        goto fail;
    }
    if (drmu_atomic_crtc_broadcast_rgb_set(da, dc, broadcast_rgb) != 0) {
        fprintf(stderr, "Failed to set broadcast_rgb to '%s'\n", broadcast_rgb);
        goto fail;
    }
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


