#ifndef _DRMU_DRMU_CHROMA_H
#define _DRMU_DRMU_CHROMA_H

#include <stdbool.h>
#include <stdint.h>

#include "drmu_chroma.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drmu_chroma_siting_s {
    int32_t x, y;
} drmu_chroma_siting_t;

// Init constants - C winges if the struct is specified in a const init (which seems like a silly error)
#define drmu_chroma_siting_float_i(_x, _y) {.x = (int32_t)((double)(_x) * 65536 + .5), .y = (int32_t)((double)(_y) * 65536 + .5)}
#define DRMU_CHROMA_SITING_BOTTOM_I             drmu_chroma_siting_float_i(0.5, 1.0)
#define DRMU_CHROMA_SITING_BOTTOM_LEFT_I        drmu_chroma_siting_float_i(0.0, 1.0)
#define DRMU_CHROMA_SITING_CENTER_I             drmu_chroma_siting_float_i(0.5, 0.5)
#define DRMU_CHROMA_SITING_LEFT_I               drmu_chroma_siting_float_i(0.0, 0.5)
#define DRMU_CHROMA_SITING_TOP_I                drmu_chroma_siting_float_i(0.5, 0.0)
#define DRMU_CHROMA_SITING_TOP_LEFT_I           drmu_chroma_siting_float_i(0.0, 0.0)
#define DRMU_CHROMA_SITING_UNSPECIFIED_I        {INT32_MIN, INT32_MIN}
// Inline constants
#define drmu_chroma_siting_float(_x, _y) (drmu_chroma_siting_t)drmu_chroma_siting_float_i(_x, _y)
#define DRMU_CHROMA_SITING_BOTTOM               drmu_chroma_siting_float(0.5, 1.0)
#define DRMU_CHROMA_SITING_BOTTOM_LEFT          drmu_chroma_siting_float(0.0, 1.0)
#define DRMU_CHROMA_SITING_CENTER               drmu_chroma_siting_float(0.5, 0.5)
#define DRMU_CHROMA_SITING_LEFT                 drmu_chroma_siting_float(0.0, 0.5)
#define DRMU_CHROMA_SITING_TOP                  drmu_chroma_siting_float(0.5, 0.0)
#define DRMU_CHROMA_SITING_TOP_LEFT             drmu_chroma_siting_float(0.0, 0.0)
#define DRMU_CHROMA_SITING_UNSPECIFIED          (drmu_chroma_siting_t){INT32_MIN, INT32_MIN}

static inline bool
drmu_chroma_siting_eq(const drmu_chroma_siting_t a, const drmu_chroma_siting_t b)
{
    return a.x == b.x && a.y == b.y;
}

#ifdef __cplusplus
}
#endif

#endif

