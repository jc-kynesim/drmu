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
#include "drmu_output.h"
#include <drm_fourcc.h>

#define TRACE_ALL 0

#define DRM_MODULE "vc4"

static void
fillpatch(uint8_t * patch, const uint8_t a, const uint8_t b, const uint8_t c, const uint8_t d)
{
    unsigned int i, j;
    for (i = 0; i != 32; ++i, patch += 128*4) {
        for (j = 0; j != 32*4; j += 4) {
            patch[j + 0] = a;
            patch[j + 1] = b;
            patch[j + 2] = c;
            patch[j + 3] = d;
        }
    }
}

static void
fillgrid(uint8_t * const grid)
{
    unsigned int i, j;
    for (i = 0; i != 4; ++i) {
        for (j = 0; j != 4; ++j) {
            fillpatch(grid + j*32*4 + i*32*128*4,
                      (j & 1) ? 255 : 0,
                      (j & 2) ? 255 : 0,
                      (i & 1) ? 255 : 0,
                      (i & 2) ? 255 : 0);
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
    drmu_output_t * dout = NULL;
    drmu_plane_t * p0 = NULL;
    drmu_plane_t * psub[4] = {NULL};
    drmu_fb_t * fb0 = NULL;
    drmu_fb_t * fbsub[4] = {NULL};
    drmu_atomic_t * da = NULL;
    const drmu_mode_simple_params_t * sp = NULL;

    static const uint32_t fmts[4] = {
        DRM_FORMAT_ARGB8888,
        DRM_FORMAT_ABGR8888,
        DRM_FORMAT_RGBA8888,
        DRM_FORMAT_BGRA8888,
    };

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

    (void)argc;
    (void)argv;

    if ((dout = drmu_output_new(du)) == NULL)
        goto fail;
    if (drmu_output_add_output(dout, NULL) != 0)
        goto fail;
    sp = drmu_output_mode_simple_params(dout);

    // **** Plane selection needs noticable improvement
    // This wants to be the primary
    if ((p0 = drmu_output_plane_ref_primary(dout)) == NULL)
        goto fail;
    for (i = 0; i!= 4; ++i) {
        if ((psub[i] = drmu_output_plane_ref_other(dout)) == NULL)
            fprintf(stderr, "Cannot find plane for %s\n", drmu_log_fourcc(fmts[i]));
    }

    {
        unsigned int n = 0;
        const uint32_t * p = drmu_plane_formats(p0, &n);
        for (i = 0; i != n; ++i, ++p) {
            printf("Format[%d]: %.4s\n", i, (const char *)p);
        }
    }

    fb0 = drmu_fb_new_dumb(du, 128, 128, DRM_FORMAT_ARGB8888);
    memset(drmu_fb_data(fb0, 0), 128, 128*128*4);

    for (i = 0; i!= 4; ++i) {
        if ((fbsub[i] = drmu_fb_new_dumb(du, 128, 128, fmts[i])) == NULL)
            fprintf(stderr, "Cannot find make dumb for %s\n", drmu_log_fourcc(fmts[i]));
        else
            fillgrid(drmu_fb_data(fbsub[i], 0));
    }

    da = drmu_atomic_new(du);

    drmu_atomic_plane_fb_set(da, p0, fb0, drmu_rect_wh(sp->width, sp->height));
    for (i = 0; i!= 4; ++i) {
        if (fbsub[i] && psub[i]) {
            fprintf(stderr, "Set patch %d to %s\n", i, drmu_log_fourcc(fmts[i]));
            drmu_atomic_plane_fb_set(da, psub[i], fbsub[i], (drmu_rect_t){i * (128 * 5/4) + 32, 32, 128, 128});
        }
    }
    drmu_atomic_queue(&da);

    sleep(3000);

fail:
    drmu_fb_unref(&fb0);
    drmu_plane_unref(&p0);
    drmu_output_unref(&dout);
    drmu_env_unref(&du);
    return 0;
}


