#ifndef _DRMU_WRITEBACK_H
#define _DRMU_WRITEBACK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_output_s;
struct drmu_fb_s;

typedef struct drmu_output_forward_s drmu_writeback_output_t;

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

drmu_writeback_output_t * drmu_writeback_output_new(struct drmu_output_s * const dout, const unsigned int qno,
                                                    const drmu_writeback_fb_prep_fns_t * prep_fns, void * prep_v);
drmu_writeback_output_t * drmu_writeback_ref(drmu_writeback_output_t * const dof);
void drmu_writeback_unref(drmu_writeback_output_t ** const ppdof);

int drmu_writeback_size_set(drmu_writeback_output_t * const dof, const unsigned int w, const unsigned int h);
int drmu_writeback_rotation_set(drmu_writeback_output_t * const dof, const unsigned int rot);
int drmu_writeback_fmt_set(drmu_writeback_output_t * const dof, const uint32_t fmt);

#ifdef __cplusplus
}
#endif

#endif
