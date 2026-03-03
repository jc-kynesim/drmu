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

// Avoid using this structure directly - it is subject to change without notice
// and will probably disappear once I have a usable full API for plane layout
typedef struct drmu_fmt_info_s {
    uint32_t fourcc;
    uint8_t bpp;  // For dumb BO alloc
    uint8_t bit_depth;  // For display
    uint8_t plane_count;
    uint8_t is_yuv;

    struct {
        uint8_t sx;
        uint8_t sy;
    } chans[4];

    struct drmu_fmt_plane_info_s {
        uint8_t bpg;    // Bytes per group
        uint8_t xdiv;   // w / xdiv = groups
        uint8_t ydiv;
        struct drmu_fmt_pel_info_s {
            uint8_t chan;
            uint8_t bits;
            uint8_t off;
        } pels[7];      // Finish with 0,0
    } planes[4];

    drmu_chroma_siting_t chroma_siting;  // Default for this format (YUV420 = (0.0, 0.5), otherwise (0, 0)
    const char * name;
} drmu_fmt_info_t;

const drmu_fmt_info_t * drmu_fmt_info_find_fmt(const uint32_t fourcc);
// Look up format info by "name" (intended for user input)
// 1st checks for a match against 4cc (case sensitive) then checks against
// the name associated with the format, where "name" is the text after
// DRM_FORMAT_
const drmu_fmt_info_t * drmu_fmt_info_find_name(const char * const name);

unsigned int drmu_fmt_info_bit_depth(const drmu_fmt_info_t * const fmt_info);
uint32_t drmu_fmt_info_fourcc(const drmu_fmt_info_t * const fmt_info);
const char * drmu_fmt_info_name(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_pixel_bits(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_plane_count(const drmu_fmt_info_t * const fmt_info);
bool drmu_fmt_info_is_yuv(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_wdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
unsigned int drmu_fmt_info_hdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
drmu_chroma_siting_t drmu_fmt_info_chroma_siting(const drmu_fmt_info_t * const fmt_info);

#ifdef __cplusplus
}
#endif

#endif

