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
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_util.h"
#include <drm_fourcc.h>

#include "plane16.h"

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

#define STRIPES (7 * 4 * 2)
#define SWIDTH 256

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

#define BT2020_RGB_Y(r, g, b) ((double)(r)*0.2627+(double)(g)*0.6780+(double)(b)*0.0593)
#define BT2020_RGB_Cb(r, g, b) (((double)(b)-BT2020_RGB_Y((r),(g),(b)))/1.8814)
#define BT2020_RGB_Cr(r, g, b) (((double)(r)-BT2020_RGB_Y((r),(g),(b)))/1.4746)
#define BT2020_RGB_P16(r, g, b) p16val(~0U, BT2020_RGB_Y((r),(g),(b)), BT2020_RGB_Cb((r),(g),(b))+0x8000+0.5, BT2020_RGB_Cr((r),(g),(b))+0x8000+0.5)

static int
color_siting(drmu_atomic_t * const da, drmu_output_t * const dout,
             uint8_t * const p16, unsigned int dw, unsigned int dh, unsigned int p16_stride,
             const bool dofrac)
{
    drmu_env_t * du = drmu_atomic_env(da);
    drmu_plane_t * planes[7] = {NULL};
    drmu_fb_t * fb = NULL;
    unsigned int i;
    int rv = 0;
    const uint32_t fmt = DRM_FORMAT_P030;
    const uint64_t mod = DRM_FORMAT_MOD_BROADCOM_SAND128_COL_HEIGHT(0);
    const unsigned int w = 18; // 16 puts the TL on an odd number so don't do that
    const unsigned int h = 18;
    const uint64_t bk = p16val(~0, 0x6000, 0x8000, 0x8000);
    const uint64_t fg = BT2020_RGB_P16(0, 0, 230*256);
    const unsigned int s16_stride = w * sizeof(uint64_t);
    uint8_t * s16 = malloc(h * s16_stride);
    const unsigned int patch_wh = dh / 4;
    const unsigned int patch_gap = (dh - patch_wh * 3) / 4;

    static const struct {
        drmu_chroma_siting_t color_siting;
        unsigned int patch_x, patch_y;
    } sitings[7] = {
        {DRMU_CHROMA_SITING_BOTTOM_I,      1, 2},
        {DRMU_CHROMA_SITING_BOTTOM_LEFT_I, 0, 2},
        {DRMU_CHROMA_SITING_CENTER_I,      1, 1},
        {DRMU_CHROMA_SITING_LEFT_I,        0, 1},
        {DRMU_CHROMA_SITING_TOP_I,         1, 0},
        {DRMU_CHROMA_SITING_TOP_LEFT_I,    0, 0},
        {DRMU_CHROMA_SITING_UNSPECIFIED_I, 2, 2},
    };

    (void)dw;

    if (s16 == NULL) {
        fprintf(stderr, "Failed malloc for plane16 fb\n");
        rv = -ENOMEM;
        goto fail;
    }

    plane16_fill(s16, w, h, s16_stride, bk);
    plane16_fill(p16pos(s16, s16_stride, 0, (h / 2 - 1)), w, 2, s16_stride, fg);
    plane16_fill(p16pos(s16, s16_stride, (w / 2 - 1), 0), 2, h, s16_stride, fg);

    for (i = 0; i != 7; ++i) {
        if ((planes[i] = drmu_output_plane_ref_other(dout)) == NULL) {
            fprintf(stderr, "Color siting test needs 8 planes, only got %d\nMaybe don't run from X?\n", i + 1);
            rv = -ENOENT;
            goto fail;
        }
    }

    if ((fb = drmu_fb_new_dumb_mod(du, w, h, fmt, mod)) == NULL) {
        fprintf(stderr, "Failed to create siting fb\n");
        rv = -ENOMEM;
        goto fail;
    }
    drmu_fb_int_color_set(fb, "ITU-R BT.2020 YCbCr", DRMU_PLANE_RANGE_LIMITED, "BT2020_RGB");

    if (dofrac)
        drmu_fb_crop_frac_set(fb, (drmu_rect_t){.x = 0x8000, .y = 0x8000, .w = (w << 16) - 0x8000, .h = (h << 16) - 0x8000});

    plane16_to_sand30(drmu_fb_data(fb, 0), drmu_fb_pitch2(fb, 0),
                      drmu_fb_data(fb, 1), drmu_fb_pitch2(fb, 1),
                      s16, s16_stride, w, h);

    for (i = 0; i != 7; ++i) {
        const unsigned int x = patch_gap + sitings[i].patch_x * (patch_wh + patch_gap);
        const unsigned int y = patch_gap + sitings[i].patch_y * (patch_wh + patch_gap);
        plane16_fill(p16pos(p16, p16_stride, x + patch_wh / 2 - 1, y - patch_gap / 2),
                     2, patch_wh + patch_gap,
                     p16_stride, p16val(~0U, 235 << 8, 235 << 8, 235 << 8));
        plane16_fill(p16pos(p16, p16_stride, x - patch_gap / 2, y + patch_wh / 2 - 1),
                     patch_wh + patch_gap, 2,
                     p16_stride, p16val(~0U, 235 << 8, 235 << 8, 235 << 8));

        drmu_atomic_plane_fb_set(da, planes[i], fb, (drmu_rect_t){
                        .x = x,
                        .y = y,
                        .w = patch_wh,
                        .h = patch_wh});
        drmu_atomic_plane_add_chroma_siting(da, planes[i], sitings[i].color_siting);
    }


fail:
    // Would be "better" to delete planes at the end, but if we never alloc
    // another then this is, in fact, safe (nasty though)
    for (i = 0; i != 7; ++i)
        drmu_plane_unref(planes + i);
    drmu_fb_unref(&fb);
    free(s16);
    return rv;
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
           "-s  colour siting\n"
           "-F  make siting patch .5 pixel smaller\n"
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
    drmu_output_t * dout = NULL;
    drmu_crtc_t * dc = NULL;
    drmu_conn_t * dn = NULL;
    drmu_plane_t * p1 = NULL;
    drmu_fb_t * fb1 = NULL;
    drmu_fb_t * fb_out = NULL;
    drmu_atomic_t * da = NULL;
    uint32_t p1fmt = DRM_FORMAT_ARGB2101010;
    uint64_t p1mod = DRM_FORMAT_MOD_INVALID;
    drmu_mode_simple_params_t mp;
    const char * colorspace = "BT2020_RGB";
    const char * encoding = "ITU-R BT.2020 YCbCr";
    const char * range = NULL;
    const char * default_range = DRMU_PLANE_RANGE_FULL;
    const char * broadcast_rgb = NULL;
    bool grey_only = false;
    bool fill_pin = false;
    bool fill_solid = false;
    bool test_siting = false;
    bool is_yuv = false;
    bool mode_req = false;
    bool hi_bpc = true;
    bool dofrac = false;
    bool try_writeback = false;
    int verbose = 0;
    int c;
    uint64_t fillval = p16val(~0U, 0x8000, 0x8000, 0x8000);
    uint8_t *p16 = NULL;
    unsigned int p16_stride = 0;
    int rv;

    while ((c = getopt(argc, argv, "8c:e:f:Fgpr:R:svwy")) != -1) {
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
                    range = DRMU_BROADCAST_RGB_FULL;
                else if (strcmp(s, "limited") == 0)
                    range = DRMU_BROADCAST_RGB_LIMITED_16_235;
                else if (strcmp(s, "auto") == 0)
                    range = DRMU_BROADCAST_RGB_AUTOMATIC;
                else {
                    printf("Unrecognised broadcast range - valid values are auto, limited, full\n");
                    exit(1);
                }
                break;
            }
            case 's':
                test_siting = true;
                break;
            case 'F':
                dofrac = true;
                break;
            case 'f': {
                const char * s = optarg;
                if (plane16_parse_val(s, (char**)&s, &fillval) != 0 || *s != '\0')
                    usage();
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
            case 'w':
                try_writeback = true;
                break;
            case 'v':
                ++verbose;
                break;
            default:
                usage();
        }
    }

    if (optind < argc) {
        if (*drmu_util_parse_mode_simple_params(argv[optind], &mp) == '\0') {
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

    da = drmu_atomic_new(du);

    if ((dout = drmu_output_new(du)) == NULL)
        goto fail;

    drmu_output_max_bpc_allow(dout, true);
    drmu_output_modeset_allow(dout, true);

    if (try_writeback) {
        if (drmu_output_add_writeback(dout) != 0) {
            drmu_err(du, "Failed to add writeback");
            goto fail;
        }
    }
    else {
        if (drmu_output_add_output(dout, NULL) != 0)
            goto fail;
    }
    dc = drmu_output_crtc(dout);
    dn = drmu_output_conn(dout, 0);

    drmu_output_max_bpc_allow(dout, hi_bpc);

    if (try_writeback) {
        if (!mp.width || !mp.height) {
            mp.width = 1920;
            mp.height = 1080;
        }
        printf("Try writeback %dx%d", mp.width, mp.height);

        if ((fb_out = drmu_fb_new_dumb(du, mp.width, mp.height, DRM_FORMAT_ARGB8888)) == NULL) {
            printf("Failed to create fb-out");
            goto fail;
        }
        if (drmu_atomic_output_add_writeback_fb(da, dout, fb_out) != 0) {
            printf("Failed to add writeback fb");
            goto fail;
        }
    }
    else if (!mode_req) {
        mp = *drmu_output_mode_simple_params(dout);
        printf("Mode %s\n",
               drmu_util_simple_mode(&mp));
    }
    else
    {
        drmu_mode_simple_params_t pickparam = *drmu_output_mode_simple_params(dout);
        int mode;

        if (mp.width || mp.height) {
            pickparam.width = mp.width;
            pickparam.height = mp.height;
        }
        pickparam.hz_x_1000 = mp.hz_x_1000;  // 0 is legit -pick something
        pickparam.flags = mp.flags;

        mode = drmu_output_mode_pick_simple(dout, drmu_mode_pick_simple_interlace_cb, &pickparam);

        if (mode != -1) {
            mp = drmu_conn_mode_simple_params(dn, mode);
            printf("Mode requested %s; found %s\n",
                   drmu_util_simple_mode(&pickparam), drmu_util_simple_mode(&mp));

            if (mp.width != pickparam.width || mp.height != pickparam.height ||
                !(pickparam.hz_x_1000 == 0 ||
                  (pickparam.hz_x_1000 < mp.hz_x_1000 + 100 && pickparam.hz_x_1000 + 100 > mp.hz_x_1000))) {
                fprintf(stderr, "Mode not close enough\n");
                goto fail;
            }

            drmu_atomic_crtc_add_modeinfo(da, dc, drmu_conn_modeinfo(dn, mode));
        }
        else {
            fprintf(stderr, "No mode that matches request found\n");
            goto fail;
        }
    }
    printf("Use hi bits per channel: %s\n", hi_bpc ? "yes" : "no");
    printf("Colorspace: %s, Broadcast RGB: %s\n", colorspace, broadcast_rgb);

    if ((p16 = malloc(mp.width * mp.height * 8)) == NULL) {
        printf("Failed to alloc P16 plane\n");
        goto fail;
    }
    p16_stride = mp.width * 8;

    if ((p1 = drmu_output_plane_ref_primary(dout)) == NULL) {
        fprintf(stderr, "Cannot find plane for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    if ((fb1 = drmu_fb_new_dumb_mod(du, mp.width, mp.height, p1fmt, p1mod)) == NULL) {
        fprintf(stderr, "Cannot make dumb for %s\n", drmu_log_fourcc(p1fmt));
        goto fail;
    }

    drmu_fb_int_color_set(fb1, encoding, range, colorspace);
    printf("%s encoding: %s, range %s\n", is_yuv ? "YUV" : "RGB", encoding, range);

    // Start with grey fill
    plane16_fill(p16, mp.width, mp.height, p16_stride, fillval);

    if (fill_pin)
        fillpin10(p16, mp.width, mp.height, p16_stride, is_yuv);
    else if (grey_only)
        fillgradgrey10(p16, mp.width, mp.height, p16_stride, is_yuv);
    else if (test_siting) {
        if (color_siting(da, dout, p16, mp.width, mp.height, p16_stride, dofrac))
            goto fail;
    } else if (!fill_solid)
        fillgraduated10(p16, mp.width, mp.height, p16_stride, is_yuv);

    if (is_yuv)
        plane16_to_sand30(drmu_fb_data(fb1, 0), drmu_fb_pitch2(fb1, 0),
                          drmu_fb_data(fb1, 1), drmu_fb_pitch2(fb1, 1),
                          p16, p16_stride, mp.width, mp.height);
    else
        plane16_to_argb2101010(drmu_fb_data(fb1, 0), drmu_fb_pitch(fb1, 0),
                               p16, p16_stride, mp.width, mp.height);

    drmu_atomic_plane_fb_set(da, p1, fb1, drmu_rect_wh(mp.width, mp.height));

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
    if (drmu_atomic_conn_hdr_metadata_set(da, dn, &meta) != 0) {
        fprintf(stderr, "Failed metadata set");
        goto fail;
    }
    if (drmu_atomic_conn_colorspace_set(da, dn, colorspace) != 0) {
        fprintf(stderr, "Failed to set colorspace to '%s'\n", colorspace);
        goto fail;
    }
    if (drmu_atomic_conn_broadcast_rgb_set(da, dn, broadcast_rgb) != 0) {
        fprintf(stderr, "Failed to set broadcast_rgb to '%s'\n", broadcast_rgb);
        goto fail;
    }
    if (drmu_atomic_conn_hi_bpc_set(da, dn, hi_bpc) != 0)
        fprintf(stderr, "Failed hi bpc set\n");

    if (try_writeback) {
        if (drmu_atomic_commit(da, DRM_MODE_ATOMIC_ALLOW_MODESET) != 0) {
            fprintf(stderr, "Failed to commit writeback\n");
            goto fail;
        }
        rv = drmu_fb_out_fence_wait(fb_out, 1000);
        if (rv == 1) {
            printf("Waited OK for writeback\n");
        }
        else if (rv == 0) {
            printf("Timeout for writeback\n");
        }
        else {
            fprintf(stderr, "Failed to wait for writeback: %s\n", strerror(-rv));
            goto fail;
        }

        {
            FILE * f = fopen("wb.rgb", "wb");
            fwrite(drmu_fb_data(fb_out, 0), drmu_fb_pitch(fb_out, 0), drmu_fb_height(fb_out), f);
            fclose(f);
        }
        // *** Test fb contents
    }
    else {
        drmu_atomic_queue(&da);
        getchar();
    }

fail:
    drmu_atomic_unref(&da);
    drmu_fb_unref(&fb_out);
    drmu_fb_unref(&fb1);
    drmu_plane_unref(&p1);
    drmu_output_unref(&dout);
    drmu_env_unref(&du);
    return 0;
}


