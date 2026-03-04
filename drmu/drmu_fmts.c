#include "drmu_fmts.h"

#include <stddef.h>
#include <ctype.h>
#include <string.h>

#include "drmu_fourcc.h"

#ifndef HAS_SORTED_FMTS
#define HAS_SORTED_FMTS 0
#endif
#ifndef BUILD_MK_SORTED_FMTS_H
#define BUILD_MK_SORTED_FMTS_H 0
#endif

// Format properties

#if BUILD_MK_SORTED_FMTS_H || !HAS_SORTED_FMTS

#define C_XY(_sx,_sy)   {{.sx = 1, .sy = 1}, {.sx = (_sx), .sy = (_sy)}, {.sx = (_sx), .sy = (_sy)}, {.sx = 1, .sy = 1}}
#define C_444           C_XY(1,1)
#define C_422           C_XY(2,1)
#define C_420           C_XY(2,2)

#define CHAN_B         0
#define CHAN_Y         0
#define CHAN_G         1
#define CHAN_U         1
#define CHAN_R         2
#define CHAN_V         2
#define CHAN_A         3
#define CHAN_X         4

#define CTST_B         0
#define CTST_Y         1
#define CTST_G         0
#define CTST_U         1
#define CTST_R         0
#define CTST_V         1
#define CTST_A         0
#define CTST_X         0

#define SITING_SY_1     DRMU_CHROMA_SITING_TOP_LEFT_I
#define SITING_SY_2     DRMU_CHROMA_SITING_LEFT_I

#define PEL(_C, _bits, _off) {.chan = CHAN_##_C, .bits = _bits, .off = _off}

#define FMT_ONE_422_NAMED_PKLO(_name, CA, CB, CC, CD, bits, pk)\
    { .fourcc = DRM_FORMAT_##_name,\
      .bpp = (pk) * 2,\
      .bit_depth = (bits),\
      .plane_count = 1,\
      .is_yuv = 1,\
      .chans = C_422,\
      .planes = {{\
            .bpg = ((pk) / 8) * 4,\
            .xdiv = 1, .ydiv = 1,\
            .pels = {PEL(CA, bits, 0), PEL(CB, bits, (pk)), PEL(CC, bits, (pk) * 2), PEL(CD, bits, (pk) * 3)}}},\
      .chroma_siting = SITING_SY_1,\
      .name=#_name,\
    }
#define FMT_ONE_422(CA, CB, CC, CD, bits) FMT_ONE_422_NAMED_PKLO(CA##CB##CC##CD, CA, CB, CC, CD, bits, ((bits + 7) & ~7))

#define  FMT_ONE_4_NAMED(_name, CA, CB, CC, CD, NA, NB, NC, ND)\
    { .fourcc = DRM_FORMAT_##_name,\
      .bpp = (NA + NB + NC + ND),\
      .bit_depth = (NA > ND ? NA : ND),\
      .plane_count = 1,\
      .is_yuv = CTST_##CA || CTST_##CB || CTST_##CC || CTST_##CD,\
      .chans = C_444,\
      .planes = {{\
            .bpg = (NA + NB + NC + ND) / 8,\
            .xdiv = 1, .ydiv = 1,\
            .pels = {PEL(CD, ND, 0), PEL(CC, NC, ND), PEL(CB, NB, ND + NC), PEL(CA, NA, ND + NC + NB)}}},\
      .chroma_siting = SITING_SY_1,\
      .name=#_name,\
    }
#define  FMT_ONE_4(CA, CB, CC, CD, NA, NB, NC, ND) FMT_ONE_4_NAMED(CA##CB##CC##CD##NA##NB##NC##ND, CA, CB, CC, CD, NA, NB, NC, ND)

#define  FMT_ONE_3_NAMED(_name, CA, CB, CC, NA, NB, NC)\
    { .fourcc = DRM_FORMAT_##_name,\
      .bpp = (NA + NB + NC),\
      .bit_depth = NA,\
      .plane_count = 1,\
      .is_yuv = CTST_##CA || CTST_##CB || CTST_##CC,\
      .chans = C_444,\
      .planes = {{\
            .bpg = (NA + NB + NC) / 8,\
            .xdiv = 1, .ydiv = 1,\
            .pels = {PEL(CC, NC, 0), PEL(CB, NB, NC), PEL(CA, NA, NC + NB)}}},\
      .chroma_siting = SITING_SY_1,\
      .name=#_name,\
    }
#define  FMT_ONE_3(CA, CB, CC, NA, NB, NC) FMT_ONE_3_NAMED(CA##CB##CC##NA##NB##NC, CA, CB, CC, NA, NB, NC)

#define  FMT_NV_PKHI(_name, CA, CB, CC, bits, _sx, _sy, pk)\
    { .fourcc = DRM_FORMAT_##_name,\
      .bpp = (pk),\
      .bit_depth = (bits),\
      .plane_count = 2,\
      .chans = C_XY((_sx), (_sy)),\
      .is_yuv = CTST_##CA || CTST_##CB || CTST_##CC,\
      .planes = {{\
          .bpg = (pk) / 8,\
          .xdiv = 1, .ydiv = 1,\
          .pels = {PEL(CA, (bits), (pk) - (bits))}},\
      {\
          .bpg = ((pk) / 8) * 2,\
          .xdiv = (_sx), .ydiv = (_sy),\
          .pels = {PEL(CB, (bits), (pk) - (bits)), PEL(CC, (bits), (pk) * 2 - (bits))}}\
      },\
      .chroma_siting = SITING_SY_##_sy,\
      .name=#_name,\
    }
#define  FMT_NV(name, CB, CC, bits, SX, SY) FMT_NV_PKHI(name, Y, CB, CC, (bits), SX, SY, (((bits) + 7) & ~7))

#define  FMT_TRI_PKLO(_name, CA, CB, CC, bits, _sx, _sy, pk)\
    { .fourcc = DRM_FORMAT_##_name,\
      .bpp = (pk),\
      .bit_depth = (bits),\
      .plane_count = 3,\
      .is_yuv = CTST_##CA || CTST_##CB || CTST_##CC,\
      .chans = C_XY((_sx), (_sy)),\
      .planes = {{\
          .bpg = (pk) / 8,\
          .xdiv = 1, .ydiv = 1,\
          .pels = {PEL(CA, (bits), 0)}},\
      {\
          .bpg = (pk) / 8,\
          .xdiv = (_sx), .ydiv = (_sy),\
          .pels = {PEL(CB, (bits), 0)}},\
      {\
          .bpg = (pk) / 8,\
          .xdiv = (_sx), .ydiv = (_sy),\
          .pels = {PEL(CC, (bits), 0)}}\
      },\
      .chroma_siting = SITING_SY_##_sy,\
      .name=#_name,\
    }
#define  FMT_TRI(name, CA, CB, CC, bits, SX, SY) FMT_TRI_PKLO(name, CA, CB, CC, (bits), SX, SY, (((bits) + 7) & ~7))

static
// Not const when creating the sorted version 'cos we sort in place
#if !BUILD_MK_SORTED_FMTS_H
const
#endif
drmu_fmt_info_t format_info[] = {
    FMT_ONE_4(X, R, G, B, 1, 5, 5, 5),
    FMT_ONE_4(X, B, G, R, 1, 5, 5, 5),
    FMT_ONE_4(R, G, B, X, 5, 5, 5, 1),
    FMT_ONE_4(B, G, R, X, 5, 5, 5, 1),

    FMT_ONE_4(A, R, G, B, 1, 5, 5, 5),
    FMT_ONE_4(A, B, G, R, 1, 5, 5, 5),
    FMT_ONE_4(R, G, B, A, 5, 5, 5, 1),
    FMT_ONE_4(B, G, R, A, 5, 5, 5, 1),

    FMT_ONE_3(B, G, R, 5, 6, 5),
    FMT_ONE_3(R, G, B, 5, 6, 5),

    FMT_ONE_3(B, G, R, 8, 8, 8),
    FMT_ONE_3(R, G, B, 8, 8, 8),

    FMT_ONE_3(B, G, R, 2, 3, 3),
    FMT_ONE_3(R, G, B, 3, 3, 2),

    FMT_ONE_4(X, R, G, B, 8, 8, 8, 8),
    FMT_ONE_4(X, B, G, R, 8, 8, 8, 8),
    FMT_ONE_4(R, G, B, X, 8, 8, 8, 8),
    FMT_ONE_4(B, G, R, X, 8, 8, 8, 8),

    FMT_ONE_4(A, R, G, B, 8, 8, 8, 8),
    FMT_ONE_4(A, B, G, R, 8, 8, 8, 8),
    FMT_ONE_4(R, G, B, A, 8, 8, 8, 8),
    FMT_ONE_4(B, G, R, A, 8, 8, 8, 8),

    FMT_ONE_4(A, R, G, B, 4, 4, 4, 4),
    FMT_ONE_4(A, B, G, R, 4, 4, 4, 4),
    FMT_ONE_4(R, G, B, A, 4, 4, 4, 4),
    FMT_ONE_4(B, G, R, A, 4, 4, 4, 4),

    FMT_ONE_4(X, R, G, B, 4, 4, 4, 4),
    FMT_ONE_4(X, B, G, R, 4, 4, 4, 4),
    FMT_ONE_4(R, G, B, X, 4, 4, 4, 4),
    FMT_ONE_4(B, G, R, X, 4, 4, 4, 4),

    FMT_ONE_4(X, R, G, B, 2, 10, 10, 10),
    FMT_ONE_4(X, B, G, R, 2, 10, 10, 10),
    FMT_ONE_4(R, G, B, X, 10, 10, 10, 2),
    FMT_ONE_4(B, G, R, X, 10, 10, 10, 2),

    FMT_ONE_4(A, R, G, B, 2, 10, 10, 10),
    FMT_ONE_4(A, B, G, R, 2, 10, 10, 10),
    FMT_ONE_4(R, G, B, A, 10, 10, 10, 2),
    FMT_ONE_4(B, G, R, A, 10, 10, 10, 2),

    FMT_NV(NV12, U, V, 8, 2, 2),
    FMT_NV(NV21, V, U, 8, 2, 2),
    FMT_NV(NV16, U, V, 8, 2, 1),
    FMT_NV(NV61, V, U, 8, 2, 1),
    FMT_NV(NV24, U, V, 8, 1, 1),
    FMT_NV(NV42, V, U, 8, 1, 1),

    FMT_NV(P010, U, V, 10, 2, 2),
    FMT_NV(P012, U, V, 12, 2, 2),
    FMT_NV(P016, U, V, 16, 2, 2),
    FMT_NV(P210, U, V, 10, 2, 1),
    FMT_NV(P212, U, V, 12, 2, 1),
    FMT_NV(P410, U, V, 10, 1, 1),
    FMT_NV(P412, U, V, 12, 1, 1),

    FMT_TRI(YUV420, Y, U, V, 8, 2, 2),
    FMT_TRI(YVU420, Y, V, U, 8, 2, 2),
    FMT_TRI(YUV422, Y, U, V, 8, 2, 1),
    FMT_TRI(YVU422, Y, V, U, 8, 2, 1),
    FMT_TRI(YUV444, Y, U, V, 8, 1, 1),
    FMT_TRI(YVU444, Y, V, U, 8, 1, 1),

    FMT_TRI(S010, Y, U, V, 10, 2, 2),
    FMT_TRI(S012, Y, U, V, 12, 2, 2),
    FMT_TRI(S016, Y, U, V, 12, 2, 2),

    FMT_ONE_4_NAMED(AYUV, A, Y, U, V, 8, 8, 8, 8),
    FMT_ONE_4(X, Y, U, V, 8, 8, 8, 8),
    FMT_ONE_3(V, U, Y, 8, 8, 8),
    FMT_ONE_4(X, V, Y, U, 2, 10, 10, 10),
    FMT_ONE_4(X, V, Y, U, 16, 16, 16, 16),

    FMT_ONE_422(Y, U, Y, V, 8),
    FMT_ONE_422(Y, V, Y, U, 8),
    FMT_ONE_422(V, Y, U, Y, 8),
    FMT_ONE_422(U, Y, V, Y, 8),

    { .fourcc = DRM_FORMAT_P030,\
      .bpp = 32,\
      .bit_depth = 10,\
      .plane_count = 2,\
      .is_yuv = 1,\
      .chans = C_420,\
      .planes = {{\
          .bpg = 4,\
          .xdiv = 3, .ydiv = 1,\
          .pels = {PEL(Y, 10, 0), PEL(Y, 10, 10), PEL(Y, 10, 20)}},\
      {\
          .bpg = 8,\
          .xdiv = 6, .ydiv = 2,\
          .pels = {PEL(U, 10, 0), PEL(V, 10, 10), PEL(U, 10, 20), PEL(V, 10, 32), PEL(U, 10, 42), PEL(V, 10, 52)}}\
      },\
      .chroma_siting = SITING_SY_2,\
      .name = "P030",\
    },

    { .fourcc = 0 }
};
#endif

#if BUILD_MK_SORTED_FMTS_H
// ---------------------------------------------------------------------------
//
// Sort & emit format table (not part of the lib)

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

static const unsigned int format_count = sizeof(format_info)/sizeof(format_info[0]) - 1;  // Ignore null term in count

static int sort_fn(const void * va, const void * vb)
{
    const drmu_fmt_info_t * a = va;
    const drmu_fmt_info_t * b = vb;
    return a->fourcc < b->fourcc ? -1 : a->fourcc == b->fourcc ? 0 : 1;
}

int
main(int argc, char * argv[])
{
    FILE * f;
    unsigned int i;

    if (argc != 2) {
        fprintf(stderr, "Needs output file only\n");
        return 1;
    }
    if ((f = fopen(argv[1], "wt")) == NULL) {
        fprintf(stderr, "Failed to open'%s'\n", argv[1]);
        return 1;
    }
    qsort(format_info, format_count, sizeof(format_info[0]), sort_fn);

    fprintf(f, "static const drmu_fmt_info_t format_info[] = {\n");
    for (i = 0; i != format_count; ++i) {
        const drmu_fmt_info_t * x = format_info + i;
        unsigned int j;
        fprintf(f, "{.fourcc=%#"PRIx32",.bpp=%d,.bit_depth=%d,.plane_count=%d,.is_yuv=%d,.chans={", x->fourcc, x->bpp, x->bit_depth, x->plane_count, x->is_yuv);
        for (j = 0; j != sizeof(x->planes)/sizeof(x->planes[0]); ++j) {
            fprintf(f, "{%d,%d},", x->chans[j].sx, x->chans[j].sy);
        }
        fprintf(f, "},.planes={");
        for (j = 0; j != sizeof(x->planes)/sizeof(x->planes[0]); ++j) {
            unsigned int k;
            fprintf(f, "{.bpg=%d,.xdiv=%d,.ydiv=%d,.pels={", x->planes[j].bpg, x->planes[j].xdiv, x->planes[j].ydiv);
            for (k = 0; k != sizeof(x->planes[0].pels)/sizeof(x->planes[0].pels[0]); ++k) {
                fprintf(f, "{%d,%d,%d},", x->planes[j].pels[k].chan, x->planes[j].pels[k].bits, x->planes[j].pels[k].off);
            }
            fprintf(f, "}},");
        }
        fprintf(f, "},");
        fprintf(f, ".chroma_siting={%d,%d},", x->chroma_siting.x, x->chroma_siting.y);
        fprintf(f, ".name=\"%s\",", x->name);
        fprintf(f, "},\n");
    }
    fprintf(f, "{0}\n};\n");
    fprintf(f, "static const unsigned int format_count = %d;\n", format_count);

    fclose(f);
    return 0;
}

#else
// ---------------------------------------------------------------------------
//
// Include sorted format table
#if HAS_SORTED_FMTS
#include "sorted_fmts.h"
#endif

const drmu_fmt_info_t *
drmu_fmt_info_find_fmt(const uint32_t fourcc)
{
    if (!fourcc)
        return NULL;
#if HAS_SORTED_FMTS
    unsigned int lo = 0;
    unsigned int hi = format_count;

    while (lo < hi) {
        unsigned int x = (hi + lo) / 2;
        if (format_info[x].fourcc == fourcc)
            return &format_info[x];
        if (format_info[x].fourcc < fourcc)
            lo = x + 1;
        else
            hi = x;
    }
#else
    for (const drmu_fmt_info_t * p = format_info; p->fourcc; ++p) {
        if (p->fourcc == fourcc)
            return p;
    }
#endif
    return NULL;
}

const drmu_fmt_info_t *
drmu_fmt_info_find_name(const char * const name)
{
    char buf[32];
    const drmu_fmt_info_t * fi;
    unsigned int i;

    if (name == NULL || name[0] == 0)
        return NULL;

    // Uppercase the given name
    for (i = 0; name[i] != 0 && i < sizeof(buf); ++i)
        buf[i] = toupper(name[i]);
    if (i >= sizeof(buf))
        return NULL;
    buf[i] = 0;

    // If a 4 char name passed then 1st check it as a 4cc (case sensitive)
    if (i == 4) {
        if ((fi = drmu_fmt_info_find_fmt(fourcc_code(name[0], name[1], name[2], name[3]))) != NULL)
            return fi;
    }

    // Not a 4cc look for name
    for (const drmu_fmt_info_t *p = format_info; p->fourcc; ++p) {
        if (strcmp(p->name, buf) == 0)
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
    return fmt_info == NULL ? 0 : fmt_info->fourcc;
}
const char * drmu_fmt_info_name(const drmu_fmt_info_t * const fmt_info)
{
    return fmt_info == NULL ? "????" : fmt_info->name;
}
unsigned int drmu_fmt_info_pixel_bits(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? 0 : fmt_info->bpp;
}
unsigned int drmu_fmt_info_plane_count(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? 0 : fmt_info->plane_count;
}
bool drmu_fmt_info_is_yuv(const drmu_fmt_info_t * const fmt_info)
{
    return fmt_info != NULL && fmt_info->is_yuv;
}
unsigned int drmu_fmt_info_wdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n)
{
    return fmt_info->planes[plane_n].xdiv * fmt_info->planes[0].bpg / fmt_info->planes[plane_n].bpg;
}
unsigned int drmu_fmt_info_hdiv(const drmu_fmt_info_t * const fmt_info, const unsigned int plane_n)
{
    return fmt_info->planes[plane_n].ydiv;
}
drmu_chroma_siting_t drmu_fmt_info_chroma_siting(const drmu_fmt_info_t * const fmt_info)
{
    return !fmt_info ? DRMU_CHROMA_SITING_TOP_LEFT : fmt_info->chroma_siting;
}

#endif

