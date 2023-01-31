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

const drmu_fmt_info_t * drmu_fmt_info_find_fmt(const uint32_t fourcc);

unsigned int drmu_fmt_info_bit_depth(const drmu_fmt_info_t * const fmt_info);
uint32_t drmu_fmt_info_fourcc(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_pixel_bits(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_plane_count(const drmu_fmt_info_t * const fmt_info);
unsigned int drmu_fmt_info_wdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
unsigned int drmu_fmt_info_hdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n);
drmu_chroma_siting_t drmu_fmt_info_chroma_siting(const drmu_fmt_info_t * const fmt_info);

#ifdef __cplusplus
}
#endif

#endif

