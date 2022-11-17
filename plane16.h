#include <stdint.h>

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

// v1 -> Y(10)
void plane16_to_sand30_y(uint8_t * const dst_data, const unsigned int dst_stride2,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);

// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(10), v3 -> V(10)
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


// v1 -> Y(8)
void plane16_to_y8(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);
// Only copies (sx % 2) == 0 && (sy % 2) == 0
// v2 -> U(8), v3 -> V(8)
// w, h are src dimensions
void plane16_to_uv8_420(uint8_t * const dst_data, const unsigned int dst_stride,
                  const uint8_t * const src_data, const unsigned int src_stride,
                  const unsigned int w, const unsigned int h);


int plane16_parse_val(const char * s, char ** const ps, uint64_t * const pval);

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

