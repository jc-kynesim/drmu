#include "drmu_fmts.h"

#include <stddef.h>

#include <libdrm/drm_fourcc.h>

// Format properties

typedef struct drmu_fmt_info_s {
    uint32_t fourcc;
    uint8_t  bpp;  // For dumb BO alloc
    uint8_t  bit_depth;  // For display
    uint8_t  plane_count;
    struct {
        uint8_t wdiv;
        uint8_t hdiv;
    } planes[4];
    drmu_chroma_siting_t chroma_siting;  // Default for this format (YUV420 = (0.0, 0.5), otherwise (0, 0)
} drmu_fmt_info_t;

#define P_ONE       {{.wdiv = 1, .hdiv = 1}}
#define P_YC420     {{.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 2}}
#define P_YC422     {{.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 1}}
#define P_YC444     {{.wdiv = 2, .hdiv = 1}, {.wdiv = 1, .hdiv = 1}}  // Assumes doubled .bpp
#define P_YUV420    {{.wdiv = 1, .hdiv = 1}, {.wdiv = 2, .hdiv = 2}, {.wdiv = 2, .hdiv = 2}}
#define P_YUV422    {{.wdiv = 1, .hdiv = 1}, {.wdiv = 2, .hdiv = 1}, {.wdiv = 2, .hdiv = 1}}
#define P_YUV444    {{.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 1}, {.wdiv = 1, .hdiv = 1}}

static const drmu_fmt_info_t format_info[] = {
    { .fourcc = DRM_FORMAT_XRGB1555, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XBGR1555, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBX5551, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRX5551, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ARGB1555, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ABGR1555, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBA5551, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRA5551, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGR565, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGB565, .bpp = 16, .bit_depth = 5, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_RGB888, .bpp = 24, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGR888, .bpp = 24, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_XRGB8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XBGR8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBX8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRX8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ARGB8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ABGR8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBA8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRA8888, .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_XRGB2101010, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XBGR2101010, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBX1010102, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRX1010102, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ARGB2101010, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_ABGR2101010, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_RGBA1010102, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_BGRA1010102, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_AYUV,        .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XYUV8888,    .bpp = 32, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_VUY888,      .bpp = 24, .bit_depth = 8, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XVYU2101010, .bpp = 32, .bit_depth = 10, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_XVYU12_16161616, .bpp = 64, .bit_depth = 12, .plane_count = 1, .planes = P_ONE},
    { .fourcc = DRM_FORMAT_XVYU16161616, .bpp = 64, .bit_depth = 16, .plane_count = 1, .planes = P_ONE},

    { .fourcc = DRM_FORMAT_YUYV, .bpp = 16, .bit_depth = 8, .plane_count = 1, .planes = P_ONE,
        .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_YVYU, .bpp = 16, .bit_depth = 8, .plane_count = 1, .planes = P_ONE,
        .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_VYUY, .bpp = 16, .bit_depth = 8, .plane_count = 1, .planes = P_ONE,
        .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_UYVY, .bpp = 16, .bit_depth = 8, .plane_count = 1, .planes = P_ONE,
        .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },

    { .fourcc = DRM_FORMAT_NV12,   .bpp = 8,  .bit_depth = 8,  .plane_count = 2, .planes = P_YC420,
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },
    { .fourcc = DRM_FORMAT_NV21,   .bpp = 8,  .bit_depth = 8,  .plane_count = 2, .planes = P_YC420,
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },
    { .fourcc = DRM_FORMAT_P010,   .bpp = 16, .bit_depth = 10, .plane_count = 2, .planes = P_YC420,
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },
    { .fourcc = DRM_FORMAT_NV16,   .bpp = 8,  .bit_depth = 8,  .plane_count = 2, .planes = P_YC422,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_NV61,   .bpp = 8,  .bit_depth = 8,  .plane_count = 2, .planes = P_YC422,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_NV24,   .bpp = 16, .bit_depth = 8,  .plane_count = 2, .planes = P_YC444,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_NV42,   .bpp = 16, .bit_depth = 8,  .plane_count = 2, .planes = P_YC444,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },

    { .fourcc = DRM_FORMAT_YUV420, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV420,
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },
    { .fourcc = DRM_FORMAT_YVU420, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV420,
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },
    { .fourcc = DRM_FORMAT_YUV422, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV422,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_YUV422, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV422,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_YUV444, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV444,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },
    { .fourcc = DRM_FORMAT_YUV444, .bpp = 8, .bit_depth = 8, .plane_count = 3, .planes = P_YUV444,
      .chroma_siting = DRMU_CHROMA_SITING_TOP_LEFT_I },

    // 3 pel in 32 bits. So code as 32bpp with wdiv 3.
    { .fourcc = DRM_FORMAT_P030,   .bpp = 32, .bit_depth = 10, .plane_count = 2,
      .planes = {{.wdiv = 3, .hdiv = 1}, {.wdiv = 3, .hdiv = 2}},
      .chroma_siting = DRMU_CHROMA_SITING_LEFT_I },

    { .fourcc = 0 }
};

const drmu_fmt_info_t *
drmu_fmt_info_find_fmt(const uint32_t fourcc)
{
    for (const drmu_fmt_info_t * p = format_info; p->fourcc; ++p) {
        if (p->fourcc == fourcc)
            return p;
    }
    return NULL;
}

unsigned int
drmu_fmt_info_bit_depth(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? 0 : fmt_info->bit_depth;
}
uint32_t drmu_fmt_info_fourcc(const drmu_fmt_info_t * const fmt_info)
{
    return fmt_info->fourcc;
}
unsigned int drmu_fmt_info_pixel_bits(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? 0 : fmt_info->bpp;
}
unsigned int drmu_fmt_info_plane_count(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? 0 : fmt_info->plane_count;
}
unsigned int drmu_fmt_info_wdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n)
{
    return fmt_info->planes[plane_n].wdiv;
}
unsigned int drmu_fmt_info_hdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n)
{
    return fmt_info->planes[plane_n].hdiv;
}
drmu_chroma_siting_t drmu_fmt_info_chroma_siting(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? DRMU_CHROMA_SITING_TOP_LEFT : fmt_info->chroma_siting;
}


