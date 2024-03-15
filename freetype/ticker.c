/* Simple scrolling ticker
 * Freetype portion heavily derived from the example code in the the Freetype
 * tutorial.
 * Freetype usage is basic - could easily be improved to have things like
 * different colour outlines, rendering in RGB rather than grey etc.
 */

#include "ticker.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <drmu.h>
#include <drmu_dmabuf.h>
#include <drmu_log.h>
#include <drmu_output.h>

#include <drm_fourcc.h>

enum ticker_state_e {
    TICKER_NEW = 0,
    TICKER_NEXT_CHAR,
    TICKER_SCROLL
};

struct ticker_env_s {
    enum ticker_state_e state;

    drmu_env_t *du;
    drmu_output_t *dout;
    drmu_plane_t *dp;
    drmu_fb_t *dfbs[2];
    drmu_dmabuf_env_t *dde;

    uint32_t format;
    uint64_t modifier;

    drmu_rect_t pos;

    FT_Library    library;
    FT_Face       face;

    FT_Vector     pen;                    /* untransformed origin  */
    FT_Bool use_kerning;
    FT_UInt previous;

    unsigned int bn;  // Buffer for render
    int shl;          // Scroll left amount (-ve => need a new char)
    int shl_per_run;  // Amount to scroll per run

    int           target_height;
    int           target_width;
    unsigned int bb_width;

    ticker_next_char_fn next_char_cb;
    void *next_char_v;

    drmu_atomic_commit_fn * commit_cb;
    void * commit_v;
};

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

static inline uint32_t
grey2argb(const uint32_t x)
{
    return ((x << 24) | (x << 16) | (x << 8) | (x));
}

void
draw_bitmap(drmu_fb_t *const dfb,
            FT_Bitmap *bitmap,
            FT_Int      x,
            FT_Int      y)
{
    int  i, j, p, q;
    const int fb_width = drmu_fb_width(dfb);
    const int fb_height = drmu_fb_height(dfb);
    const size_t fb_stride = drmu_fb_pitch(dfb, 0) / 4;
    uint32_t *const image = drmu_fb_data(dfb, 0);

    const int  x_max = MIN(fb_width, (int)(x + bitmap->width));
    const int  y_max = MIN(fb_height, (int)(y + bitmap->rows));

    /* for simplicity, we assume that `bitmap->pixel_mode' */
    /* is `FT_PIXEL_MODE_GRAY' (i.e., not a bitmap font)   */

    for (j = y, q = 0; j < y_max; j++, q++)
    {
        for (i = x, p = 0; i < x_max; i++, p++)
            image[j * fb_stride + i] |= grey2argb(bitmap->buffer[q * bitmap->width + p]);
    }
}

static void
shift_2d(void *dst, const void *src, size_t stride, size_t offset, size_t h)
{
    size_t i;
    uint8_t *d = dst;
    const uint8_t *s = src;

    for (i = 0; i != h; ++i)
    {
        memcpy(d, s + offset, stride - offset);
        memset(d + stride - offset, 0, offset);
        d += stride;
        s += stride;
    }
}

void
ticker_next_char_cb_set(ticker_env_t *const te, const ticker_next_char_fn fn, void *const v)
{
    te->next_char_cb = fn;
    te->next_char_v = v;
}

void
ticker_commit_cb_set(ticker_env_t *const te, void (* commit_cb)(void * v), void * commit_v)
{
    te->commit_cb = commit_cb;
    te->commit_v = commit_v;
}

static int
do_scroll(ticker_env_t *const te)
{
    if (te->shl < 0)
    {
        te->state = TICKER_NEXT_CHAR;
        return 1;
    }
    else
    {
        drmu_fb_t *const fb0 = te->dfbs[te->bn];

        drmu_atomic_t *da = drmu_atomic_new(te->du);
//        printf("tw=%d, pos.w=%d, shl=%d, x=%d\n",
//               te->target_width, (int)te->pos.w, te->shl,
//               te->target_width - (int)te->pos.w - te->shl);
        drmu_fb_crop_frac_set(fb0, drmu_rect_shl16((drmu_rect_t) {
                                                       .x = MAX(0, te->target_width - (int)te->pos.w - te->shl), .y = 0,
                                                       .w = te->pos.w, .h = te->pos.h }));
        drmu_atomic_plane_add_fb(da, te->dp, fb0, te->pos);
        if (te->commit_cb)
            drmu_atomic_add_commit_callback(da, te->commit_cb, te->commit_v);
        drmu_atomic_queue(&da);

        te->shl -= te->shl_per_run;
        return 0;
    }
}

static int
do_render(ticker_env_t *const te)
{
    FT_Matrix matrix = {
        .xx = 0x10000L,
        .xy = 0,
        .yx = 0,
        .yy = 0x10000L
    };
    const FT_GlyphSlot slot = te->face->glyph;
    FT_UInt glyph_index;
    int c;
    drmu_fb_t *const fb1 = te->dfbs[te->bn];
    drmu_fb_t *const fb0 = te->dfbs[te->bn ^ 1];
    int shl1;

    /* set transformation */
    FT_Set_Transform(te->face, &matrix, &te->pen);

    c = te->next_char_cb(te->next_char_v);
    if (c <= 0)
    {
        // If the window didn't quite get to end end of the buffer on last
        // scroll then set it there.
        if (te->shl + te->shl_per_run > 0)
        {
            te->shl = 0;
            do_scroll(te);
        }
        return c;
    }

    /* convert character code to glyph index */
    glyph_index = FT_Get_Char_Index(te->face, c);

    /* retrieve kerning distance and move pen position */
    if (te->use_kerning && te->previous && glyph_index)
    {
        FT_Vector delta = { 0, 0 };
        FT_Get_Kerning(te->face, te->previous, glyph_index, FT_KERNING_DEFAULT, &delta);
        te->pen.x += delta.x;
    }

    /* load glyph image into the slot (erase previous one) */
    if (FT_Load_Glyph(te->face, glyph_index, FT_LOAD_RENDER))
    {
        drmu_warn(te->du, "Load Glyph failed");
        return -1;
    }

    drmu_fb_write_start(fb0);
    shl1 = MAX(slot->bitmap_left + slot->bitmap.width, (te->pen.x + slot->advance.x) >> 6) - te->target_width;
    if (shl1 > 0)
    {
        te->pen.x -= shl1 << 6;
        shift_2d(drmu_fb_data(fb0, 0), drmu_fb_data(fb1, 0), drmu_fb_pitch(fb0, 0), shl1 * 4, drmu_fb_height(fb0));
    }

    // now, draw to our target surface (convert position)
    draw_bitmap(fb0, &slot->bitmap, slot->bitmap_left - shl1, te->target_height - slot->bitmap_top);
    drmu_fb_write_end(fb0);

    /* increment pen position */
    te->pen.x += slot->advance.x;
    te->shl += shl1;

    te->previous = glyph_index;
    te->bn ^= 1;
    te->state = TICKER_SCROLL;
    return 1;
}

int
ticker_run(ticker_env_t *const te)
{
    int rv = -1;
    do
    {
        switch (te->state)
        {
            case TICKER_NEW:
            case TICKER_NEXT_CHAR:
                rv = do_render(te);
                break;
            case TICKER_SCROLL:
                rv = do_scroll(te);
                break;
            default:
                break;
        }
    } while (rv == 1);
    return rv;
}

void
ticker_delete(ticker_env_t **ppTicker)
{
    ticker_env_t *const te = *ppTicker;
    if (te == NULL)
        return;

    if (te->dfbs[0])
    {
        drmu_atomic_t *da = drmu_atomic_new(te->du);
        drmu_atomic_plane_clear_add(da, te->dp);
        drmu_atomic_queue(&da);
    }

    drmu_fb_unref(te->dfbs + 0);
    drmu_fb_unref(te->dfbs + 1);
    drmu_dmabuf_env_unref(&te->dde);
    drmu_plane_unref(&te->dp);
    drmu_output_unref(&te->dout);

    FT_Done_Face(te->face);
    FT_Done_FreeType(te->library);

    free(te);
}

int
ticker_init(ticker_env_t *const te)
{
    for (unsigned int i = 0; i != 2; ++i)
    {
        te->dfbs[i] = te->dde == NULL ?
            drmu_fb_new_dumb_mod(te->du, te->target_width, te->pos.h, te->format, te->modifier) :
            drmu_fb_new_dmabuf_mod(te->dde, te->target_width, te->pos.h, te->format, te->modifier);
        if (te->dfbs[i] == NULL)
        {
            drmu_err(te->du, "Failed to get frame buffer");
            return -1;
        }
    }

    drmu_fb_write_start(te->dfbs[0]);
    memset(drmu_fb_data(te->dfbs[0], 0), 0x00, drmu_fb_height(te->dfbs[0]) * drmu_fb_pitch(te->dfbs[0], 0));
    drmu_fb_write_end(te->dfbs[0]);
    return 0;
}

int
ticker_set_face(ticker_env_t *const te, const char *const filename)
{
    const FT_Pos buf_height = te->pos.h - 2; // Allow 1 pixel T&B for rounding
    FT_Pos scaled_size;
    FT_Pos bb_height;

// https://freetype.org/freetype2/docs/tutorial/step2.html

    if (FT_New_Face(te->library, filename, 0, &te->face))
    {
        drmu_err(te->du, "Face not found '%s'", filename);
        return -1;
    }

    bb_height = te->face->bbox.yMax - te->face->bbox.yMin;
    te->bb_width = FT_MulDiv(te->face->bbox.xMax - te->face->bbox.xMin, buf_height, bb_height);
    scaled_size = FT_MulDiv(te->face->units_per_EM, buf_height, bb_height);

//    printf("UPer Em=%d, scaled=%ld, height=%ld\n", te->face->units_per_EM, scaled_size, buf_height);
//    printf("BBox=%ld,%ld->%ld,%ld =%ld, bb_scaled_w=%d\n", te->face->bbox.xMin, te->face->bbox.yMin, te->face->bbox.xMax, te->face->bbox.yMax, bb_height, te->bb_width);

    if (FT_Set_Pixel_Sizes(te->face, 0, scaled_size))
    {
        drmu_err(te->du, "Bad char size\n");
        return -1;
    }

    te->pen.y =  FT_MulDiv(-te->face->bbox.yMin * 32, buf_height, bb_height) + 32;
    te->target_height = (int)((FT_Pos)te->pos.h - (te->pen.y >> 6)); // Top for rendering purposes
    te->target_width = MAX(te->bb_width, te->pos.w) + te->bb_width;
    te->pen.x = te->target_width * 64; // Start with X pos @ far right hand side

    te->use_kerning = FT_HAS_KERNING(te->face);
    return 0;
}

int
ticker_set_shl(ticker_env_t *const te, unsigned int shift_pels)
{
    te->shl_per_run = shift_pels;
    return 0;
}

ticker_env_t*
ticker_new(drmu_output_t *dout, unsigned int x, unsigned int y, unsigned int w, unsigned int h)
{
    ticker_env_t *te = calloc(1, sizeof(*te));

    if (te == NULL)
        return NULL;

    te->dout = drmu_output_ref(dout);
    te->du = drmu_output_env(dout);
    te->dde = drmu_dmabuf_env_new_video(te->du);

    te->pos = (drmu_rect_t) { x, y, w, h };
    te->format = DRM_FORMAT_ARGB8888;
    te->modifier = DRM_FORMAT_MOD_LINEAR;
    te->shl_per_run = 3;

    if (FT_Init_FreeType(&te->library) != 0)
    {
        drmu_err(te->du, "Failed to init FreeType");
        goto fail;
    }

    // This doesn't really want to be the primary
    if ((te->dp = drmu_output_plane_ref_format(te->dout, DRMU_PLANE_TYPE_OVERLAY, te->format, te->modifier)) == NULL)
    {
        drmu_err(te->du, "Failed to find output plane");
        goto fail;
    }


    return te;

fail:
    ticker_delete(&te);
    return NULL;
}

