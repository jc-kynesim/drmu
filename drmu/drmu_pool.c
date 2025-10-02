#include "drmu_pool.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "drmu.h"
#include "drmu_log.h"

//----------------------------------------------------------------------------
//
// Pool fns

typedef struct drmu_fb_slot_s {
    struct drmu_fb_s * fb;
    struct drmu_fb_slot_s * next;
    struct drmu_fb_slot_s * prev;
} drmu_fb_slot_t;

typedef struct drmu_fb_list_s {
    drmu_fb_slot_t * head;      // Double linked list of free FBs; LRU @ head
    drmu_fb_slot_t * tail;
    drmu_fb_slot_t * unused;    // Single linked list of unused slots
} drmu_fb_list_t;

struct drmu_pool_s {
    atomic_int ref_count;       // 0 == 1 ref for ease of init
    bool dead;                  // Pool killed - never alloc again

    unsigned int fb_count;      // FBs allocated (not free count)
    unsigned int fb_max;        // Max FBs to allocate

    struct drmu_env_s * du;     // Logging only - not reffed

    drmu_pool_callback_fns_t callback_fns;
    void * callback_v;

    pthread_mutex_t lock;

    drmu_fb_list_t free_fbs;    // Free FB list header
    drmu_fb_slot_t * slots;     // [fb_max]
};

static void
fb_list_add_tail(drmu_fb_list_t * const fbl, drmu_fb_t * const dfb)
{
    drmu_fb_slot_t * const slot = fbl->unused;

    assert(slot != NULL);
    fbl->unused = slot->next;
    slot->fb = dfb;
    slot->next = NULL;

    if (fbl->tail == NULL)
        fbl->head = slot;
    else
        fbl->tail->next = slot;
    slot->prev = fbl->tail;
    fbl->tail = slot;
}

static drmu_fb_t *
fb_list_extract(drmu_fb_list_t * const fbl, drmu_fb_slot_t * const slot)
{
    drmu_fb_t * dfb;

    if (slot == NULL)
        return NULL;

    if (slot->prev == NULL)
        fbl->head = slot->next;
    else
        slot->prev->next = slot->next;

    if (slot->next == NULL)
        fbl->tail = slot->prev;
    else
        slot->next->prev = slot->prev;

    dfb = slot->fb;
    slot->fb = NULL;
    slot->next = fbl->unused;
    slot->prev = NULL;
    fbl->unused = slot;
    return dfb;
}

static drmu_fb_t *
fb_list_extract_head(drmu_fb_list_t * const fbl)
{
    return fb_list_extract(fbl, fbl->head);
}

static void
pool_free_pool(drmu_pool_t * const pool)
{
    drmu_fb_t * dfb;
    pthread_mutex_lock(&pool->lock);
    while ((dfb = fb_list_extract_head(&pool->free_fbs)) != NULL) {
        --pool->fb_count;
        pthread_mutex_unlock(&pool->lock);
        drmu_fb_unref(&dfb);
        pthread_mutex_lock(&pool->lock);
    }
    pthread_mutex_unlock(&pool->lock);
}

static void
pool_free(drmu_pool_t * const pool)
{
    void *const v = pool->callback_v;
    const drmu_pool_on_delete_fn on_delete_fn = pool->callback_fns.on_delete_fn;
    pool_free_pool(pool);
    free(pool->slots);
    pthread_mutex_destroy(&pool->lock);
    free(pool);

    on_delete_fn(v);
}

void
drmu_pool_unref(drmu_pool_t ** const pppool)
{
    drmu_pool_t * const pool = *pppool;
    int n;

    if (pool == NULL)
        return;
    *pppool = NULL;

    n = atomic_fetch_sub(&pool->ref_count, 1);
    assert(n >= 0);
    if (n == 0)
        pool_free(pool);
}

drmu_pool_t *
drmu_pool_ref(drmu_pool_t * const pool)
{
    if (pool != NULL)
        atomic_fetch_add(&pool->ref_count, 1);
    return pool;
}

drmu_pool_t *
drmu_pool_new_alloc(drmu_env_t * const du, const unsigned int total_fbs_max,
                    const drmu_pool_callback_fns_t * const cb_fns,
                    void * const v)
{
    drmu_pool_t * const pool = calloc(1, sizeof(*pool));
    unsigned int i;

    if (pool == NULL)
        goto fail0;
    if ((pool->slots = calloc(total_fbs_max, sizeof(*pool->slots))) == NULL)
        goto fail1;

    pool->du = du;
    pool->fb_max = total_fbs_max;
    pool->callback_fns = *cb_fns;
    pool->callback_v = v;

    for (i = 1; i != total_fbs_max; ++i)
        pool->slots[i - 1].next = pool->slots + i;
    pool->free_fbs.unused = pool->slots + 0;

    pthread_mutex_init(&pool->lock, NULL);

    return pool;

fail1:
    free(pool);
fail0:
    cb_fns->on_delete_fn(v);
    drmu_err(du, "Failed pool env alloc");
    return NULL;
}

static int
pool_fb_pre_delete_cb(drmu_fb_t * dfb, void * v)
{
    drmu_pool_t * pool = v;

    // Ensure we cannot end up in a delete loop
    drmu_fb_pre_delete_unset(dfb);

    // If dead set then might as well delete now
    // It should all work without this shortcut but this reclaims
    // storage quicker
    if (pool->dead) {
        drmu_pool_unref(&pool);
        return 0;
    }

    drmu_fb_ref(dfb);  // Restore ref

    pthread_mutex_lock(&pool->lock);
    fb_list_add_tail(&pool->free_fbs, dfb);
    pthread_mutex_unlock(&pool->lock);

    // May cause suicide & recursion on fb delete, but that should be OK as
    // the 1 we return here should cause simple exit of fb delete
    drmu_pool_unref(&pool);
    return 1;  // Stop delete
}

drmu_fb_t *
drmu_pool_fb_new(drmu_pool_t * const pool, uint32_t w, uint32_t h, const uint32_t format, const uint64_t mod)
{
    drmu_fb_t * dfb;
    drmu_fb_slot_t * slot;

    pthread_mutex_lock(&pool->lock);

    // If pool killed then _fb_new must fail
    if (pool->dead)
        goto fail_unlock;

    slot = pool->free_fbs.head;
    while (slot != NULL) {
        dfb = slot->fb;
        if (pool->callback_fns.try_reuse_fn(dfb, w, h, format, mod)) {
            fb_list_extract(&pool->free_fbs, slot);
            pthread_mutex_unlock(&pool->lock);
            goto found;
        }
        slot = slot->next;
    }
    // Nothing reusable
    dfb = NULL;

    // Simply allocate new buffers until we hit fb_max then free LRU
    // first. If nothing to free then fail.
    if (pool->fb_count++ >= pool->fb_max) {
        --pool->fb_count;
        if ((dfb = fb_list_extract_head(&pool->free_fbs)) == NULL)
            goto fail_unlock;
    }
    pthread_mutex_unlock(&pool->lock);

    drmu_fb_unref(&dfb);  // Will free the dfb as pre-delete CB will be unset

    if ((dfb = pool->callback_fns.alloc_fn(pool->callback_v, w, h, format, mod)) == NULL) {
        pthread_mutex_lock(&pool->lock);
        --pool->fb_count;
        goto fail_unlock;
    }

found:
    drmu_fb_pre_delete_set(dfb, pool_fb_pre_delete_cb, drmu_pool_ref(pool));
    return dfb;

fail_unlock:
    pthread_mutex_unlock(&pool->lock);
    return NULL;
}

// Mark pool as dead (i.e. no new allocs) and unref it
// Simple unref will also work but this reclaims storage faster
// Actual pool structure will persist until all referencing fbs are deleted too
void
drmu_pool_kill(drmu_pool_t ** const pppool)
{
    drmu_pool_t * pool = *pppool;

    if (pool == NULL)
        return;
    *pppool = NULL;

    pool->dead = true;
    pool_free_pool(pool);

    drmu_pool_unref(&pool);
}

//----------------------------------------------------------------------------
//
// Dumb pool setup

static drmu_fb_t *
pool_dumb_alloc_cb(void * const v, const uint32_t w, const uint32_t h, const uint32_t format, const uint64_t mod)
{
    return drmu_fb_new_dumb_mod(v, w, h, format, mod);
}

static void
pool_dumb_on_delete_cb(void * const v)
{
    drmu_env_t * du = v;
    drmu_env_unref(&du);
}

drmu_pool_t *
drmu_pool_new_dumb(drmu_env_t * const du, unsigned int total_fbs_max)
{
    static const drmu_pool_callback_fns_t fns = {
        .alloc_fn = pool_dumb_alloc_cb,
        .on_delete_fn = pool_dumb_on_delete_cb,
        .try_reuse_fn = drmu_fb_try_reuse,
    };
    return drmu_pool_new_alloc(du, total_fbs_max, &fns, drmu_env_ref(du));
}


