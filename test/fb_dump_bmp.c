/*
 * Copyright (c) 2026 John Cox for Raspberry Pi Trading
 *
 * Dump a drmu framebuffer to a Windows .BMP file
 */

#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "drmu.h"
#include "drmu_fmts.h"
#include "drmu_fourcc.h"

#include "fb_dump_bmp.h"
#include "plane16.h"

#define BMP_FILE_HDR_SIZE 14
#define BMP_INFO_HDR_SIZE 40
#define BMP_HDR_SIZE      (BMP_FILE_HDR_SIZE + BMP_INFO_HDR_SIZE)

static uint8_t *
p_u16le(uint8_t * p, const unsigned int v)
{
    *p++ = (uint8_t)(v >> 0);
    *p++ = (uint8_t)(v >> 8);
    return p;
}

static uint8_t *
p_u32le(uint8_t * p, const uint32_t v)
{
    *p++ = (uint8_t)(v >> 0);
    *p++ = (uint8_t)(v >> 8);
    *p++ = (uint8_t)(v >> 16);
    *p++ = (uint8_t)(v >> 24);
    return p;
}

// True if the format is something we can turn into 8-bit BGR
// plane16_from_generic will cope with anything linear but BMP has no
// useful way of expressing YUV or subsampled data
static bool
fmt_is_bmpable(const drmu_fmt_info_t * const fi)
{
    unsigned int i;

    if (fi == NULL)
        return false;
    if (drmu_fmt_info_is_yuv(fi))
        return false;
    if (drmu_fmt_info_plane_count(fi) != 1)
        return false;
    if (fi->planes[0].xdiv != 1 || fi->planes[0].ydiv != 1)
        return false;

    // R, G & B must all be present and unsubsampled
    for (i = 0; i != 3; ++i) {
        if (fi->chans[i].sx != 1 || fi->chans[i].sy != 1)
            return false;
    }
    return true;
}

int
fb_dump_to_bmp(const char * fname, drmu_fb_t * const fb)
{
    const drmu_fmt_info_t * const fi = drmu_fb_fmt_info(fb);
    const drmu_rect_t a = drmu_fb_active(fb);
    const uint64_t mod = drmu_fb_modifier(fb, 0);
    const unsigned int line_len = a.w * 3;
    const unsigned int stride = (line_len + 3) & ~3;
    const unsigned int p16_stride = a.w * 8;
    uint8_t hdr[BMP_HDR_SIZE] = {0};
    uint8_t * hp = hdr;
    uint8_t * p16 = NULL;
    uint8_t * line = NULL;
    const uint8_t * src_datas[4] = {NULL};
    unsigned int src_strides[4] = {0};
    unsigned int y;
    int rv = 0;
    FILE * ff = NULL;

    if (!fmt_is_bmpable(fi)) {
        fprintf(stderr, "Format %s cannot be written as BMP\n",
                fi == NULL ? "<unknown>" : drmu_fmt_info_name(fi));
        return -EINVAL;
    }
    if (mod != DRM_FORMAT_MOD_LINEAR && mod != DRM_FORMAT_MOD_INVALID) {
        fprintf(stderr, "Modifier %#" PRIx64 " cannot be written as BMP\n", mod);
        return -EINVAL;
    }
    if (a.w == 0 || a.h == 0)
        return -EINVAL;

    if ((p16 = malloc((size_t)p16_stride * a.h)) == NULL ||
        (line = calloc(1, stride)) == NULL) {
        rv = -ENOMEM;
        goto fail;
    }

    if ((ff = fopen(fname, "wb")) == NULL) {
        rv = -errno;
        goto fail;
    }

    hp = p_u16le(hp, 0x4d42);                               // "BM"
    hp = p_u32le(hp, BMP_HDR_SIZE + stride * a.h);          // File size
    hp = p_u32le(hp, 0);                                    // Reserved
    hp = p_u32le(hp, BMP_HDR_SIZE);                         // Offset to pixels
    hp = p_u32le(hp, BMP_INFO_HDR_SIZE);                    // Info header size
    hp = p_u32le(hp, a.w);                                  // Width
    hp = p_u32le(hp, a.h);                                  // Height (+ve => bottom up)
    hp = p_u16le(hp, 1);                                    // Planes
    hp = p_u16le(hp, 24);                                   // Bits per pixel
    hp = p_u32le(hp, 0);                                    // BI_RGB (no compression)
    hp = p_u32le(hp, stride * a.h);                         // Image size
    hp = p_u32le(hp, 2835);                                 // X pixels/metre (72dpi)
    hp = p_u32le(hp, 2835);                                 // Y pixels/metre (72dpi)
    hp = p_u32le(hp, 0);                                    // Palette entries used
    hp = p_u32le(hp, 0);                                    // Important palette entries

    if (fwrite(hdr, BMP_HDR_SIZE, 1, ff) != 1) {
        rv = -EIO;
        goto fail;
    }

    // Going via plane16 is very inefficient but very simple
    // * Rework if speed or memory usage is ever important
    drmu_fb_read_start(fb);
    src_datas[0] = drmu_fb_data(fb, 0);
    src_strides[0] = drmu_fb_pitch(fb, 0);
    plane16_from_generic(p16, p16_stride, fi, src_datas, src_strides, a.w, a.h);
    drmu_fb_read_end(fb);

    // BMP scanlines run bottom to top; plane16 is BGRA in memory order so
    // the colour components are already in BMP order
    for (y = 0; y != a.h; ++y) {
        const uint16_t * s = (const uint16_t *)(p16 + (size_t)p16_stride * (a.h - 1 - y));
        uint8_t * d = line;
        unsigned int x;

        for (x = 0; x != a.w; ++x, s += 4) {
            *d++ = (uint8_t)(s[0] >> 8);    // B
            *d++ = (uint8_t)(s[1] >> 8);    // G
            *d++ = (uint8_t)(s[2] >> 8);    // R
        }

        if (fwrite(line, stride, 1, ff) != 1) {
            rv = -EIO;
            goto fail;
        }
    }

fail:
    if (ff != NULL && fclose(ff) != 0 && rv == 0)
        rv = -EIO;
    free(line);
    free(p16);
    return rv;
}
