#include <errno.h>
#include <stdlib.h>

#include <stdio.h>
#include <memory.h>

#include "plane16.h"
#include "drmu_fmts.h"

void
plane16_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const drmu_fmt_info_t * const px,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h)
{
    unsigned int y;
    unsigned int x;
    unsigned int plane;
    for (plane = 0; plane != 4 && px->planes[plane].bpg != 0; ++plane) {
        const struct drmu_fmt_plane_info_s * const pi = px->planes + plane;
        unsigned int ty[4] = {0};

        for (y = 0; y != h / pi->ydiv; ++y) {
            uint8_t * d = dst_datas[plane] + y * dst_strides[plane];
            unsigned int tx[4] = {0};

            for (x = 0; x != w / pi->xdiv; ++x) {
                uint64_t a = 0;

                for (const struct drmu_fmt_pel_info_s *p = pi->pels; p->bits != 0; ++p) {
                    unsigned int c = p->chan;

                    if (c < 4) {
                        unsigned int v = (tx[c] >= w) ? 0x8000 :
                            *(uint16_t *)(src_data + 8 * tx[c] + src_stride * ty[c] + c * 2);
                        a |= (v >> (16 - p->bits)) << p->off;
                        tx[c] += px->chans[c].sx;
                    }
                    else if (c == 4) {
                        a |= (0xffff >> (16 - p->bits)) << p->off;
                    }
                }

                for (unsigned int i = 0; i != pi->bpg; ++i) {
                    *d++ = a & 0xff;
                    a >>= 8;
                }
            }

            for (unsigned int i = 0; i != 4; ++i) {
                ty[i] += px->chans[i].sy;
            }
        }
    }
}

int
plane16_fmt_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const uint32_t fmt,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h)
{
    const drmu_fmt_info_t * const px = drmu_fmt_info_find_fmt(fmt);
    if (px == NULL)
        return -ENOENT;
    plane16_to_generic(dst_datas, dst_strides, px, src_data, src_stride, w, h);
    return 0;
}

// v0 -> A(2), v1 -> R(10), v2 -> G(10), v3 -> B(10)
void
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

// v0 -> A(2), v3 -> B(10), v2 -> G(10), v1 -> R(10)
void
plane16_to_abgr2101010(uint8_t * const dst_data, const unsigned int dst_stride,
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
                (((*s >> (32 +  6)) & 0x3ff) <<  0) |
                (((*s >> (16 +  6)) & 0x3ff) << 10) |
                (((*s >> (0  +  6)) & 0x3ff) << 20);
        }
    }
}

// v0 -> A(8), v3 -> B(8), v2 -> G(8), v1 -> R(8)
void
plane16_to_abgr8888(uint8_t * const dst_data, const unsigned int dst_stride,
                    const uint8_t * const src_data, const unsigned int src_stride,
                    const unsigned int w, const unsigned int h)
{
    unsigned int i, j;
    for (i = 0; i != h; ++i) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint32_t * d = (uint32_t *)(dst_data + i * dst_stride);
        for (j = 0; j != w; ++j, ++d, ++s) {
            *d =
                (((*s >> (48 + 8)) & 0xff) << 24) |
                (((*s >> (32 + 8)) & 0xff) <<  0) |
                (((*s >> (16 + 8)) & 0xff) <<  8) |
                (((*s >> (0  + 8)) & 0xff) << 16);
        }
    }
}


// v1 -> Y(10)
void
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
                uint32_t a = (k + 0 >= w) ? 0x200 : (uint32_t)((s[0] >> (0 + 6)) & 0x3ff);
                uint32_t b = (k + 1 >= w) ? 0x200 : (uint32_t)((s[1] >> (0 + 6)) & 0x3ff);
                uint32_t c = (k + 2 >= w) ? 0x200 : (uint32_t)((s[2] >> (0 + 6)) & 0x3ff);
                *d = a | (b << 10) | (c << 20);
            }
            d += (dst_stride2 - 1) * dst_stride1 / sizeof(*d);
        }
    }
}

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(10), v3 -> V(10)
// w, h are src dimensions
void
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
                    (uint64_t)(((s[0] >> (16 + 6)) & 0x3ff) | (((s[0] >> (32 + 6)) & 0x3ff) << 10));
                uint64_t b = (k + 2 >= w) ? grey :
                    (uint64_t)(((s[2] >> (16 + 6)) & 0x3ff) | (((s[2] >> (32 + 6)) & 0x3ff) << 10));
                uint64_t c = (k + 4 >= w) ? grey :
                    (uint64_t)(((s[4] >> (16 + 6)) & 0x3ff) | (((s[4] >> (32 + 6)) & 0x3ff) << 10));
                *d = a | ((b & 0x3ff) << 20) | ((b & 0xffc00) << 22) | (c << 42);
            }
            d += (dst_stride2 - 1) * dst_stride1 / sizeof(*d);
        }
    }
}

void
plane16_to_sand30(uint8_t * const dst_data_y, const unsigned int dst_stride2_y,
                  uint8_t * const dst_data_c, const unsigned int dst_stride2_c,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    plane16_to_sand30_y(dst_data_y, dst_stride2_y, src_data, src_stride, w, h);
    plane16_to_sand30_c(dst_data_c, dst_stride2_c, src_data, src_stride, w, h);
}

// vN -> Y(8)
void
plane16_to_8(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h,
                  const unsigned int n, const unsigned int wdiv, const unsigned int hdiv)
{
    unsigned int i, j;
    const unsigned int shift = 56 - n * 16;
    uint8_t * d2 = dst_data;

    for (i = 0; i < h; i += hdiv, d2 += dst_stride) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint8_t * d = d2;
        for (j = 0; j < w; j += wdiv, ++d)
            *d = ((s[j] >> shift) & 0xff);
    }
}

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(8), v1 -> V(8)
// w, h are src dimensions
void
plane16_to_uv8_420(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    unsigned int i, j;
    for (i = 0; i < h; i += 2) {
        const uint64_t * s = (const uint64_t *)(src_data + i * src_stride);
        uint8_t * d = dst_data + (i / 2) * dst_stride;
        for (j = 0; j < w; j += 2) {
            *d++ = ((*s >> (16 + 8)) & 0xff);
            *d++ = ((*s >> (32  + 8)) & 0xff);
            s += 2;
        }
    }
}

void
plane16_fill(uint8_t * const data, unsigned int dw, unsigned int dh, unsigned int stride,
             const uint64_t grey)
{
    unsigned int i;
    for (i = 0; i != dh; ++i) {
        unsigned int j;
        uint64_t * p = (uint64_t *)(data + i * stride);
        for (j = 0; j != dw; ++j)
            *p++ = grey;
    }
}

int
plane16_parse_val(const char * s, char ** const ps, uint64_t * const pval)
{
    unsigned long v[5] = {~0UL};
    unsigned long *p = v;

    p[1] = strtoul(s, (char**)&s, 0) << 6;
    if (*s != ',')
        return -EINVAL;
    p[2] = strtoul(s + 1, (char**)&s, 0) << 6;
    if (*s != ',')
        return -EINVAL;
    p[3] = strtoul(s + 1, (char**)&s, 0) << 6;

    if (*s == ',') {
        p[4] = strtoul(s + 1, (char**)&s, 0) << 6;
        ++p;
    }

    if (ps)
        *ps = (char *)s;
    *pval = p16val(p[0], p[1], p[2], p[3]);
    return 0;
}

#define VA 3
#define VR 2
#define VG 1
#define VB 0

#define VY 0
#define VU 1
#define VV 2

static inline uint16_t p16_clampd(double x)
{
    int i = x * 0x10000;
    return i < 0 ? 0 : i > 0xffff ? 0xffff : i;
}

void
plane16_rgb_to_yuv(uint8_t * data, unsigned int const stride, const unsigned int w, const unsigned int h,
                   enum plane16_cenc cenc, bool full_rgb, bool full_yuv)
{
    unsigned int x, y_pos;

    static const double mbt601[3]  = { 0.299,  0.587,  0.114  };
    static const double mbt709[3]  = { 0.2126, 0.7152, 0.0722 };
    static const double mbt2020[3] = { 0.2627, 0.6780, 0.0593 };

    const double *m;

    switch (cenc) {
    case PLANE16_BT_601:
        m = mbt601;
        break;
    case PLANE16_BT_709:
        m = mbt709;
        break;
    case PLANE16_BT_2020:
        m = mbt2020;
        break;
    default:
        return;
    }

    for (y_pos = 0; y_pos != h; ++y_pos) {
        uint16_t * p = (uint16_t *)(data + y_pos * stride);
        for (x = 0; x != w; ++x, p += 4) {
            double r = p[VR] / (double)0x10000;
            double g = p[VG] / (double)0x10000;
            double b = p[VB] / (double)0x10000;
            double y, u, v;
            if (!full_rgb) {
                r = (r - 16.0 / 256.0) * 256.0 / 219.0;
                g = (g - 16.0 / 256.0) * 256.0 / 219.0;
                b = (b - 16.0 / 256.0) * 256.0 / 219.0;
            }
            y = r * m[0] + g * m[1] + b * m[2];
            v = (r - y) / ((1.0 - m[0]) * 2.0);
            u = (b - y) / ((1.0 - m[2]) * 2.0);
            if (!full_yuv) {
                y = (y * 219.0 / 256.0) + 16.0 / 256.0;
                u = (u * 224.0 / 256.0);
                v = (v * 224.0 / 256.0);
            }
            p[VY] = p16_clampd(y);
            p[VU] = p16_clampd(u + 0.5);
            p[VV] = p16_clampd(v + 0.5);
        }
    }
}

