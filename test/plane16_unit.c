#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plane16.h"
#include "drmu_fmts.h"

#include <libdrm/drm_fourcc.h>

#ifndef DRM_FORMAT_P030
#define DRM_FORMAT_P030 fourcc_code('P', '0', '3', '0')
#endif
#ifndef DRM_FORMAT_P210
#define DRM_FORMAT_P210 fourcc_code('P', '2', '1', '0')
#endif
#ifndef DRM_FORMAT_P016
#define DRM_FORMAT_P016 fourcc_code('P', '0', '1', '6')
#endif
#ifndef DRM_FORMAT_P010
#define DRM_FORMAT_P010 fourcc_code('P', '0', '1', '0')
#endif

/* w/h divisible by 1,2,3,6 — covers every xdiv/ydiv in the format table */
#define TEST_W 12
#define TEST_H  4

static unsigned int rand_state = 0x12345678u;

static uint8_t
next_rand(void)
{
    rand_state = rand_state * 1664525u + 1013904223u;
    return (rand_state >> 16) & 0xff;
}

/*
 * How many groups per row does plane16_to_generic fill from real plane16 data
 * (as opposed to the out-of-bounds grey substitute value)?
 *
 * For most formats this equals the total group count.  It differs for packed
 * 4:2:2 formats such as YUYV where xdiv=1 but each group contains two Y
 * samples, so only the first ceil(w/2) groups carry real data.
 *
 * For each channel c we compute:
 *   advance[c] = chans[c].sx * (number of pels of channel c in the group)
 *   last[c]    = (count_c - 1) * chans[c].sx   (offset of the last pel of c)
 * Group x is fully in-bounds for c when  x*advance[c] + last[c] < w, so the
 * valid group count for c is  ceil((w - last[c]) / advance[c]).
 * We take the minimum across all real channels.
 */
static unsigned int
real_group_count(const drmu_fmt_info_t * const px,
                 const struct drmu_fmt_plane_info_s * const pi,
                 const unsigned int w)
{
    unsigned int advance[4] = {0};
    unsigned int count_c[4] = {0};
    unsigned int vg = (w + pi->xdiv - 1) / pi->xdiv;

    for (const struct drmu_fmt_pel_info_s *p = pi->pels; p->bits != 0; ++p) {
        if (p->chan < 4) {
            advance[p->chan] += px->chans[p->chan].sx;
            count_c[p->chan]++;
        }
    }
    for (unsigned int c = 0; c < 4; ++c) {
        if (advance[c] > 0) {
            const unsigned int last = (count_c[c] - 1) * px->chans[c].sx;
            const unsigned int v = last >= w ? 0
                : (w - last + advance[c] - 1) / advance[c];
            if (v < vg)
                vg = v;
        }
    }
    return vg;
}

static int
test_fmt(const drmu_fmt_info_t * const px, const unsigned int w, const unsigned int h)
{
    const unsigned int p16_stride = w * 8;
    uint8_t * p16 = NULL;
    uint8_t * src_bufs[4] = {NULL, NULL, NULL, NULL};
    uint8_t * dst_bufs[4] = {NULL, NULL, NULL, NULL};
    unsigned int src_strides[4] = {0};
    unsigned int dst_strides[4] = {0};
    int rv = 0;
    unsigned int plane;

    p16 = malloc(p16_stride * h);
    if (!p16)
        return -1;

    for (plane = 0; plane != 4 && px->planes[plane].bpg != 0; ++plane) {
        const struct drmu_fmt_plane_info_s * const pi = px->planes + plane;
        const unsigned int pw = (w + pi->xdiv - 1) / pi->xdiv;
        const unsigned int ph = (h + pi->ydiv - 1) / pi->ydiv;
        const unsigned int stride = pw * pi->bpg;

        src_strides[plane] = dst_strides[plane] = stride;
        src_bufs[plane] = malloc(stride * ph);
        dst_bufs[plane] = malloc(stride * ph);
        if (!src_bufs[plane] || !dst_bufs[plane]) {
            rv = -1;
            goto done;
        }
        for (unsigned int i = 0; i < stride * ph; ++i)
            src_bufs[plane][i] = next_rand();
        memset(dst_bufs[plane], 0, stride * ph);
    }

    /* packed → plane16 → packed */
    generic_to_plane16(p16, p16_stride, px,
                       (const uint8_t * const *)src_bufs, src_strides, w, h);
    {
        uint8_t * dst_ptrs[4] = {dst_bufs[0], dst_bufs[1], dst_bufs[2], dst_bufs[3]};
        plane16_to_generic(dst_ptrs, dst_strides, px, p16, p16_stride, w, h);
    }

    for (plane = 0; plane != 4 && px->planes[plane].bpg != 0 && rv == 0; ++plane) {
        const struct drmu_fmt_plane_info_s * const pi = px->planes + plane;
        const unsigned int ph = (h + pi->ydiv - 1) / pi->ydiv;
        const unsigned int pw = (w + pi->xdiv - 1) / pi->xdiv;
        const unsigned int stride = pw * pi->bpg;
        const unsigned int vg = real_group_count(px, pi, w);

        /*
         * Build a per-byte comparison mask.  Bits belonging to CHAN_X or
         * padding (not covered by any real channel pel) are masked to zero:
         * generic_to_plane16 ignores them on write, and plane16_to_generic
         * fills them with a constant (all-ones for CHAN_X), so they will
         * differ from the random source bytes.
         */
        uint64_t real_bits = 0;
        uint8_t mask[8];
        for (const struct drmu_fmt_pel_info_s *p = pi->pels; p->bits != 0; ++p) {
            if (p->chan < 4)
                real_bits |= ((1ULL << p->bits) - 1) << p->off;
        }
        for (unsigned int i = 0; i < pi->bpg; ++i)
            mask[i] = (real_bits >> (i * 8)) & 0xff;

        for (unsigned int by = 0; by < ph && rv == 0; ++by) {
            for (unsigned int bx = 0; bx < vg && rv == 0; ++bx) {
                for (unsigned int i = 0; i < pi->bpg; ++i) {
                    const unsigned int off = by * stride + bx * pi->bpg + i;
                    const uint8_t sv = src_bufs[plane][off] & mask[i];
                    const uint8_t dv = dst_bufs[plane][off] & mask[i];
                    if (sv != dv) {
                        printf("  plane=%u group=(%u,%u) byte=%u: "
                               "src=0x%02x dst=0x%02x mask=0x%02x\n",
                               plane, bx, by, i,
                               src_bufs[plane][off], dst_bufs[plane][off],
                               mask[i]);
                        rv = -1;
                    }
                }
            }
        }
    }

done:
    free(p16);
    for (plane = 0; plane != 4; ++plane) {
        free(src_bufs[plane]);
        free(dst_bufs[plane]);
    }
    return rv;
}

static const uint32_t test_fmts[] = {
    /* packed RGBA, various bit depths */
    DRM_FORMAT_ARGB8888,
    DRM_FORMAT_ABGR8888,
    DRM_FORMAT_XRGB8888,        /* CHAN_X: those bits become all-ones */
    DRM_FORMAT_ARGB1555,
    DRM_FORMAT_RGB565,
    DRM_FORMAT_ARGB2101010,
    DRM_FORMAT_ABGR2101010,
    DRM_FORMAT_XRGB2101010,     /* CHAN_X 2-bit */
    /* packed YUV */
    DRM_FORMAT_AYUV,
    DRM_FORMAT_YUYV,            /* packed 4:2:2: only first w/2 groups are real */
    DRM_FORMAT_UYVY,
    /* semi-planar */
    DRM_FORMAT_NV12,            /* 4:2:0 */
    DRM_FORMAT_NV16,            /* 4:2:2 */
    DRM_FORMAT_NV24,            /* 4:4:4 */
    /* planar */
    DRM_FORMAT_YUV420,
    DRM_FORMAT_YUV444,
    /* semi-planar high bit-depth (P010 has 6 padding bits per sample) */
    DRM_FORMAT_P010,
    DRM_FORMAT_P016,
    /* P030: xdiv=3 (Y) / xdiv=6 (UV), tests multi-pel-per-group path */
    DRM_FORMAT_P030,
};

int
main(int argc, char *argv[])
{
    unsigned int pass = 0, fail = 0;
    (void)argc;
    (void)argv;

    for (unsigned int i = 0; i != sizeof(test_fmts) / sizeof(test_fmts[0]); ++i) {
        const uint32_t fourcc = test_fmts[i];
        const drmu_fmt_info_t * const px = drmu_fmt_info_find_fmt(fourcc);

        if (px == NULL) {
            printf("SKIP %c%c%c%c\n",
                   (fourcc >>  0) & 0xff, (fourcc >>  8) & 0xff,
                   (fourcc >> 16) & 0xff, (fourcc >> 24) & 0xff);
            continue;
        }

        const int rv = test_fmt(px, TEST_W, TEST_H);
        if (rv != 0) {
            printf("FAIL %s\n", drmu_fmt_info_name(px));
            ++fail;
        } else {
            printf("PASS %s\n", drmu_fmt_info_name(px));
            ++pass;
        }
    }

    printf("\n%u passed, %u failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
