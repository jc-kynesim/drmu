#ifndef _DRMU_DRMU_FMTS_H
#define _DRMU_DRMU_FMTS_H

#include <stdbool.h>
#include <stdint.h>

#include "drmu_chroma.h"

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_fmt_info_s;
typedef struct drmu_fmt_info_s drmu_fmt_info_t;

typedef struct pel_info_s {
    uint8_t chan;
    uint8_t bits;
    uint8_t off;
} pel_info_t;

typedef struct drmu_fmt_info_s {
    uint32_t fourcc;
    uint8_t  bpp;  // For dumb BO alloc
    uint8_t  bit_depth;  // For display
    uint8_t  plane_count;

    struct {
        uint8_t sx;
        uint8_t sy;
    } chans[4];

    struct drmu_fmt_plane_info_s {
        uint8_t bpg; // Bytes per group
        uint8_t xdiv, ydiv; // w / xdiv = groups
        pel_info_t pels[9]; // Finish with 0,0
    } planes[4];

    drmu_chroma_siting_t chroma_siting;  // Default for this format (YUV420 = (0.0, 0.5), otherwise (0, 0)
    const char * name;
} drmu_fmt_info_t;

const drmu_fmt_info_t * drmu_fmt_info_find_fmt(const uint32_t fourcc);
const drmu_fmt_info_t * drmu_fmt_info_find_name(const char * const name);

unsigned int drmu_fmt_info_bit_depth(const drmu_fmt_info_t * const fmt_info);
uint32_t drmu_fmt_info_fourcc(const drmu_fmt_info_t * const fmt_info);
const char * drmu_fmt_info_name(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_pixel_bits(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_plane_count(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_wdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
unsigned int drmu_fmt_info_hdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
drmu_chroma_siting_t drmu_fmt_info_chroma_siting(const drmu_fmt_info_t * const fmt_info);

void plane16_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const drmu_fmt_info_t * const px,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h);

int plane16_fmt_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const uint32_t fmt,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h);

#ifdef __cplusplus
}
#endif

#endif

