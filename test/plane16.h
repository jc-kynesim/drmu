#include <stdbool.h>
#include <stdint.h>

struct drmu_fmt_info_s;

// v0 -> A(2), v1 -> R(10), v2 -> G(10), v3 -> B(10)
void plane16_to_argb2101010(uint8_t * const dst_data, const unsigned int dst_stride,
                       const uint8_t * const src_data, const unsigned int src_stride,
                       const unsigned int w, const unsigned int h);

// v0 -> A(2), v3 -> B(10), v2 -> G(10), v1 -> R(10)
void
plane16_to_abgr2101010(uint8_t * const dst_data, const unsigned int dst_stride,
                       const uint8_t * const src_data, const unsigned int src_stride,
                       const unsigned int w, const unsigned int h);

// v0 -> A(8), v3 -> B(8), v2 -> G(8), v1 -> R(8)
void
plane16_to_abgr8888(uint8_t * const dst_data, const unsigned int dst_stride,
                    const uint8_t * const src_data, const unsigned int src_stride,
                    const unsigned int w, const unsigned int h);

// v3 -> Y(10)
void plane16_to_sand30_y(uint8_t * const dst_data, const unsigned int dst_stride2,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(10), v1 -> V(10)
// w, h are src dimensions
void plane16_to_sand30_c(uint8_t * const dst_data, const unsigned int dst_stride2,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);

// Do both plane16_to_sand30_c/y
void plane16_to_sand30(uint8_t * const dst_data_y, const unsigned int dst_stride2_y,
                  uint8_t * const dst_data_c, const unsigned int dst_stride2_c,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);

void plane16_fill(uint8_t * const data, unsigned int dw, unsigned int dh, unsigned int stride,
             const uint64_t grey);

// Use format info to do the conversion. Works on whole frame.
// Should work (slowly) for all linear formats.
// Inverse of plane16_to_generic: for subsampled channels (sx/sy > 1) the
// source value is replicated to all covered plane16 pixels.
void generic_to_plane16(
        uint8_t * const dst_data, const unsigned int dst_stride,
        const struct drmu_fmt_info_s * const px,
        const uint8_t * const src_datas[4], const unsigned int src_strides[4],
        const unsigned int w, const unsigned int h);

int fmt_generic_to_plane16(
        uint8_t * const dst_data, const unsigned int dst_stride,
        const uint32_t fmt,
        const uint8_t * const src_datas[4], const unsigned int src_strides[4],
        const unsigned int w, const unsigned int h);

void plane16_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const struct drmu_fmt_info_s * const px,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h);

int plane16_fmt_to_generic(
        uint8_t * const dst_datas[4], const unsigned int dst_strides[4],
        const uint32_t fmt,
        const uint8_t * const src_data, const unsigned int src_stride,
        const unsigned int w, const unsigned int h);

// vN -> Y(8)
// decimate by wdiv, hdiv
void plane16_to_8(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h,
                  const unsigned int n, const unsigned int wdiv, const unsigned int hdiv);
// v3 -> Y(8)
static inline void plane16_to_y8(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h)
{
    plane16_to_8(dst_data, dst_stride, src_data, src_stride, w, h, 3, 1, 1);
}

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(8), v1 -> V(8)
// w, h are src dimensions
void plane16_to_uv8_420(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);


int plane16_parse_val(const char * s, char ** const ps, uint64_t * const pval);


enum plane16_cenc {
    PLANE16_BT_601 = 1,
    PLANE16_BT_709 = 2,
    PLANE16_BT_2020 = 3,
};

void plane16_rgb_to_yuv(uint8_t * data, unsigned int const stride, const unsigned int w, const unsigned int h,
                        enum plane16_cenc cenc, bool full_rgb, bool full_yuv);

// Typically v0=A, v1=R, v2=G, v3=B or v0=A, v1=V, v2=U, v3=Y
// Giving BGRA and YUVA in memory order
static inline uint64_t
p16val(unsigned int v0, unsigned int v1, unsigned int v2, unsigned int v3)
{
    return
        ((uint64_t)(v0 & 0xffff) << 48) |
        ((uint64_t)(v1 & 0xffff) << 32) |
        ((uint64_t)(v2 & 0xffff) << 16) |
        ((uint64_t)(v3 & 0xffff) << 0);
}

static inline uint8_t *
p16pos(uint8_t * p, unsigned int stride, unsigned int x, unsigned int y)
{
    return p + stride * y + sizeof(uint64_t) * x;
}

