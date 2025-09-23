#ifndef _DRMU_WRITEBACK_H
#define _DRMU_WRITEBACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_output_s;
struct drmu_fb_s;
struct drmu_plane_s;

typedef struct drmu_writeback_output_s drmu_writeback_output_t;

typedef void drmu_writeback_fb_done_done_fn(void * v, struct drmu_fb_s * fb);
typedef void * drmu_writeback_fb_done_ref_fn(void * v);
typedef void drmu_writeback_fb_done_unref_fn(void ** ppv);

typedef struct drmu_writeback_fb_done_fns_s {
    drmu_writeback_fb_done_done_fn * done;
    drmu_writeback_fb_done_ref_fn * ref;
    drmu_writeback_fb_done_unref_fn * unref;
} drmu_writeback_fb_done_fns_t;

typedef int drmu_writeback_fb_prep_prep_fn(void * v, struct drmu_fb_s * fb, drmu_writeback_fb_done_fns_t * fns, void ** ppv);
typedef void * drmu_writeback_fb_prep_ref_fn(void * v);
typedef void drmu_writeback_fb_prep_unref_fn(void ** ppv);

typedef struct drmu_writeback_fb_prep_fns_s {
    drmu_writeback_fb_prep_prep_fn * prep;
    drmu_writeback_fb_prep_ref_fn * ref;
    drmu_writeback_fb_prep_unref_fn * unref;
} drmu_writeback_fb_prep_fns_t;

// Calls prep_fns->unref(prep_v) on error as well as returning NULL
drmu_writeback_output_t * drmu_writeback_output_new(struct drmu_output_s * const dout, const unsigned int qno,
                                                    const drmu_writeback_fb_prep_fns_t * prep_fns, void * prep_v);
drmu_writeback_output_t * drmu_writeback_ref(drmu_writeback_output_t * const dof);
void drmu_writeback_unref(drmu_writeback_output_t ** const ppdof);

// w, h is the destination size i.e. if transposed the source render surface
// is h x w
int drmu_writeback_size_set(drmu_writeback_output_t * const dof, const unsigned int w, const unsigned int h);
int drmu_writeback_rotation_set(drmu_writeback_output_t * const dof, const unsigned int rot);
int drmu_writeback_fmt_set(drmu_writeback_output_t * const dof, const uint32_t fmt);

// return current format
uint32_t drmu_writeback_fmt(const drmu_writeback_output_t * const dof);

// Find and ref a plane in dest_dout that is compatible with a format that the
// writeback connector can produce. The format is set on the writeback, retrieve
// it with drmu_writeback_fmt().
// Types is a bit field of acceptable plane types (DRMU_PLANE_TYPE_xxx), 0 => any
// cf. drmu_output_plane_ref_format
// Returns NULL if nothing compatible found
struct drmu_plane_s * drmu_writeback_output_fmt_plane(drmu_writeback_output_t * const dof,
                                                      struct drmu_output_s * const dest_dout, const unsigned int types);

#ifdef __cplusplus
}
#endif

#endif
