#ifndef _DRMU_DRMU_MATH_H
#define _DRMU_DRMU_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct drmu_rect_s {
    int32_t x, y;
    uint32_t w, h;
} drmu_rect_t;

typedef struct drmu_ufrac_s {
    unsigned int num;
    unsigned int den;
} drmu_ufrac_t;

drmu_ufrac_t drmu_ufrac_reduce(drmu_ufrac_t x);

static inline drmu_ufrac_t
drmu_ufrac_invert(const drmu_ufrac_t x)
{
    return (drmu_ufrac_t){
        .num = x.den,
        .den = x.num
    };
}

static inline int32_t
drmu_rect_rescale_1s(int_fast32_t x, uint_fast32_t mul, uint_fast32_t div)
{
    const int_fast64_t m = x * (int_fast64_t)mul;
    const uint_fast32_t d2 = div/2;
    return div == 0 ? (int32_t)m :
        m >= 0 ? (int32_t)(((uint_fast64_t)m + d2) / div) :
            -(int32_t)(((uint_fast64_t)(-m) + d2) / div);
}

static inline uint32_t
drmu_rect_rescale_1u(uint_fast32_t x, uint_fast32_t mul, uint_fast32_t div)
{
    const uint_fast64_t m = x * (uint_fast64_t)mul;
    return (uint32_t)(div == 0 ? m : (m + div/2) / div);
}

static inline drmu_rect_t
drmu_rect_rescale(const drmu_rect_t s, const drmu_rect_t mul, const drmu_rect_t div)
{
    return (drmu_rect_t){
        .x = drmu_rect_rescale_1s(s.x - div.x, mul.w, div.w) + mul.x,
        .y = drmu_rect_rescale_1s(s.y - div.y, mul.h, div.h) + mul.y,
        .w = drmu_rect_rescale_1u(s.w,         mul.w, div.w),
        .h = drmu_rect_rescale_1u(s.h,         mul.h, div.h)
    };
}

static inline drmu_rect_t
drmu_rect_add_xy(const drmu_rect_t a, const drmu_rect_t b)
{
    return (drmu_rect_t){
        .x = a.x + b.x,
        .y = a.y + b.y,
        .w = a.w,
        .h = a.h
    };
}

static inline drmu_rect_t
drmu_rect_wh(const unsigned int w, const unsigned int h)
{
    return (drmu_rect_t){
        .x = 0,
        .y = 0,
        .w = w,
        .h = h
    };
}

static inline drmu_rect_t
drmu_rect_shl16(const drmu_rect_t a)
{
    return (drmu_rect_t){
        .x = a.x << 16,
        .y = a.y << 16,
        .w = a.w << 16,
        .h = a.h << 16
    };
}

static inline drmu_rect_t
drmu_rect_shr16(const drmu_rect_t a)
{
    return (drmu_rect_t){
        .x = a.x >> 16,
        .y = a.y >> 16,
        .w = a.w >> 16,
        .h = a.h >> 16
    };
}

static inline drmu_rect_t
drmu_rect_shr_rnd(const drmu_rect_t a, unsigned int n)
{
    if (n == 0)
        return a;
    --n;
    return (drmu_rect_t) {
        .x = ((a.x >> n) + 1) >> 1,
        .y = ((a.y >> n) + 1) >> 1,
        .w = ((a.w >> n) + 1) >> 1,
        .h = ((a.h >> n) + 1) >> 1
    };
}

static inline drmu_rect_t
drmu_rect_shr16_rnd(const drmu_rect_t a)
{
    return drmu_rect_shr_rnd(a, 16);
}

static inline drmu_rect_t
drmu_rect_div_xy(const drmu_rect_t a, const unsigned int dx, const unsigned int dy)
{
    return (drmu_rect_t) {
        .x = a.x / (int)dx,
        .y = a.y / (int)dy,
        .w = a.w / dx,
        .h = a.h / dy
    };
}

static inline  drmu_rect_t
drmu_rect_transpose(const drmu_rect_t a)
{
    return (drmu_rect_t){
        .x = a.y,
        .y = a.x,
        .w = a.h,
        .h = a.w
    };
}

#ifdef __cplusplus
}
#endif

#endif
