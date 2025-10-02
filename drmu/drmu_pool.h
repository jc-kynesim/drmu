#ifndef _DRMU_DRMU_POOL_H
#define _DRMU_DRMU_POOL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_pool_s;
typedef struct drmu_pool_s drmu_pool_t;

struct drmu_env_s;
struct drmu_fb_s;

// fb pool

void drmu_pool_unref(drmu_pool_t ** const pppool);
drmu_pool_t * drmu_pool_ref(drmu_pool_t * const pool);

// cb to allocate a new pool fb
typedef struct drmu_fb_s * (* drmu_pool_alloc_fn)(void * const v, const uint32_t w, const uint32_t h, const uint32_t format, const uint64_t mod);
// cb called when pool deleted or on new_pool failure - takes the same v as alloc
typedef void (* drmu_pool_on_delete_fn)(void * const v);
typedef bool (* drmu_pool_try_reuse_fn)(struct drmu_fb_s * dfb, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);

typedef struct drmu_pool_callback_fns_s {
    drmu_pool_alloc_fn alloc_fn;
    drmu_pool_on_delete_fn on_delete_fn;
    drmu_pool_try_reuse_fn try_reuse_fn;
} drmu_pool_callback_fns_t;

// Create a new pool with custom alloc & pool delete
// If pool creation fails then on_delete_fn(v) called and NULL returned
// Pool entries are not pre-allocated.
drmu_pool_t * drmu_pool_new_alloc(struct drmu_env_s * const du, const unsigned int total_fbs_max,
                                  const drmu_pool_callback_fns_t * const cb_fns,
                                  void * const v);

// Marks the pool as dead & unrefs this reference
//   No allocs will succeed after this
//   All free fbs are unrefed
void drmu_pool_kill(drmu_pool_t ** const pppool);

// Create a new pool of fb allocated from dumb objects
// N.B. BOs are alloced from uncached memory so may be slow to do anything other
// than copy into. (See drmu_dmabuf_ if you want cached data)
drmu_pool_t * drmu_pool_new_dumb(struct drmu_env_s * const du, unsigned int total_fbs_max);

// Allocate a fb from the pool
// Allocations need not be all of the same size but no guarantees are made about
// efficient memory use if this is the case
struct drmu_fb_s * drmu_pool_fb_new(drmu_pool_t * const pool, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod);

#ifdef __cplusplus
}
#endif

#endif
