#ifndef _DRMU_WRITEBACK_H
#define _DRMU_WRITEBACK_H

#include <stdint.h>

#include "drmu_math.h"

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_output_s;
struct drmu_fb_s;
struct drmu_plane_s;
struct drmu_atomic_q_s;


struct drmu_env_s;
struct drmu_plane_s;
struct drmu_output_s;
struct drmu_atomic_s;
struct drmu_pool_s;

struct drmu_writeback_env_s;
typedef struct drmu_writeback_env_s drmu_writeback_env_t;

drmu_writeback_env_t * drmu_writeback_env_new(struct drmu_env_s * const du);
drmu_writeback_env_t * drmu_writeback_env_ref(drmu_writeback_env_t * const wbe);
void drmu_writeback_env_unref(drmu_writeback_env_t ** const ppwbe);

// Output associated with Q (and therefore conn & crtc)
struct drmu_output_s * drmu_writeback_env_output(const drmu_writeback_env_t * const wbe);

// Find and ref a plane in dest_dout that is compatible with a format that the
// writeback connector can produce. The format is returned in *pFmt
// Types is a bit field of acceptable plane types (DRMU_PLANE_TYPE_xxx), 0 => any
// cf. drmu_output_plane_ref_format
// Returns NULL if nothing compatible found
struct drmu_plane_s * drmu_writeback_env_fmt_plane(drmu_writeback_env_t * const wbe,
                                                   struct drmu_output_s * const dest_dout, const unsigned int types,
                                                   uint32_t * const pFmt);

struct drmu_writeback_fb_s;
typedef struct drmu_writeback_fb_s drmu_writeback_fb_t;

// fb_pool is the pool to alloc wb fbs from
drmu_writeback_fb_t * drmu_writeback_fb_new(drmu_writeback_env_t * const wbe, struct drmu_pool_s * const fb_pool);
drmu_writeback_fb_t * drmu_writeback_fb_ref(drmu_writeback_fb_t * const wbq);
void drmu_writeback_fb_unref(drmu_writeback_fb_t ** const ppwbq);

// Returns rot supported by Q that enables req_rot
// Use drmu_rotation_suba(req_rot, <rv>) to get needed fb rotation
unsigned int drmu_writeback_fb_queue_rotation(const drmu_writeback_fb_t * const wbq, const unsigned int req_rot);

drmu_rect_t drmu_writeback_fb_queue_rect(const drmu_writeback_fb_t * const wbq, const drmu_rect_t dest_rect);

// dfb NULL if writeback failed or abandoned
typedef void drmu_writeback_fb_done_fn(void * v, struct drmu_fb_s * dfb);

int drmu_writeback_fb_queue(drmu_writeback_fb_t * wbq,
                            const drmu_rect_t dest_rect, const unsigned int rot, const uint32_t fmt,
                            drmu_writeback_fb_done_fn * const done_fn, void * const v,
                            struct drmu_atomic_s ** const ppda);


#ifdef __cplusplus
}
#endif

#endif
