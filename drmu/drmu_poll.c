// Functions to merge commits onto the next vsync without blocking
// Keeps references to everything that is in use
//
// Merged commits for the next flip are committed on the previous
// flips callback. This is very safe timing-wise and requires no knowledge
// of vsync rates but gives a worst case latency of nearly 2 flips for any
// given commit.

#include "drmu_poll.h"

#include "drmu.h"
#include "drmu_log.h"
#include "pollqueue.h"

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libdrm/drm_mode.h>

//----------------------------------------------------------------------------
//
// Atomic Q fns (internal)

typedef struct flips_ent_s {
    drmu_atomic_t * da;
    unsigned int tag;
} flip_ent_t;

typedef struct next_flips_s {
    flip_ent_t * flips;
    unsigned int len;
    unsigned int size;
    unsigned int n;
} next_flips_t;

static bool
next_flip_is_empty(const next_flips_t * const nf)
{
    return nf->len == 0;
}

static drmu_atomic_t *
next_flip_head(const next_flips_t * const nf)
{
    return next_flip_is_empty(nf) ? NULL : nf->flips[nf->n].da;
}

static drmu_atomic_t **
next_flip_find_tag(const next_flips_t * const nf, const unsigned int tag)
{
    unsigned int n;
    unsigned int i;

    n = nf->n + nf->len - 1 < nf->size ?
        nf->n + nf->len - 1 :
        nf->n + nf->len - 1 - nf->size;

    for (i = 0; i != nf->len; ++i) {
        if (nf->flips[n].tag == tag)
            return &nf->flips[n].da;
        n = (n == 0) ? nf->size - 1 : n - 1;
    }
    return NULL;
}

static drmu_atomic_t *
next_flip_pop_head(next_flips_t * const nf)
{
    drmu_atomic_t * flip;

    if (next_flip_is_empty(nf))
        return NULL;
    flip = nf->flips[nf->n].da;
    nf->n = nf->n + 1 >= nf->size ? 0 : nf->n + 1;
    --nf->len;
    return flip;
}

static drmu_atomic_t **
next_flip_add_tail(next_flips_t * const nf, unsigned int tag)
{
    const unsigned int oldlen = nf->len;

    if (oldlen < nf->size) {
        unsigned int n = nf->n + oldlen;
        if (n >= nf->size)
            n -= nf->size;
        nf->flips[n].da = NULL;
        nf->flips[n].tag = tag;
        ++nf->len;
        return &nf->flips[n].da;
    }
    else {
        // Given the circular buffer can't just realloc, must alloc & rebuild
        const unsigned int newsize = (oldlen < 8) ? 8 : oldlen * 2;
        flip_ent_t * newflips = malloc(sizeof(*nf->flips) * newsize);
        if (newflips == NULL)
            return NULL;

        assert(oldlen == nf->size);
        memcpy(newflips, nf->flips + nf->n, (nf->size - nf->n) * sizeof(*nf->flips));
        memcpy(newflips + (nf->size - nf->n), nf->flips, nf->n * sizeof(*nf->flips));
        newflips[oldlen].da = NULL;
        newflips[oldlen].tag = tag;
        free(nf->flips);

        nf->flips = newflips;
        nf->size = newsize;
        nf->len = oldlen + 1;
        nf->n = 0;

        return &newflips[oldlen].da;
    }
}

static bool
next_flip_discard_head(next_flips_t * const nf)
{
    drmu_atomic_t * da = next_flip_pop_head(nf);
    if (da == NULL)
        return false;
    drmu_atomic_unref(&da);
    return true;
}

static void
next_flip_init(next_flips_t * const nf)
{
    memset(nf, 0, sizeof(*nf));
}

// Zap next flip q - leaves in a state that a new add will work
static void
next_flip_uninit(next_flips_t * const nf)
{
    while (!next_flip_is_empty(nf)) {
        drmu_atomic_run_commit_callbacks(next_flip_head(nf));
        next_flip_discard_head(nf);
    }
    free(nf->flips);
    memset(nf, 0, sizeof(*nf));
}

//-----------------------------------------------------------------------------

struct drmu_queue_s {
    atomic_int ref_count;

    drmu_env_t * du;

    bool discard_last;
    bool lock_on_commit;
    bool locked;
    unsigned int retry_count;
    unsigned int qno; // Handy for debug

    pthread_mutex_t lock;
    pthread_cond_t cond;
    next_flips_t next;
    drmu_atomic_t * cur_flip;
    drmu_atomic_t * last_flip;

    bool wants_prod;
    struct pollqueue * pq;
    struct polltask * prod_pt;

    // Finish vars
    bool env_restore_req;
    sem_t * finish_sem;
};

static void
queue_prod_cb(void * v, short revents)
{
    drmu_queue_t * const aq = v;
    drmu_env_t * const du = aq->du;
    (void)revents;
    uint32_t flags = DRM_MODE_ATOMIC_ALLOW_MODESET;
    unsigned int prod_time = 0;
    int rv;

    // cur_flip, last_flip only used here so can be used outside lock

    pthread_mutex_lock(&aq->lock);

    if (!aq->locked) {
        if (aq->cur_flip == NULL)
            aq->cur_flip = next_flip_pop_head(&aq->next);
        if (aq->cur_flip != NULL)
            aq->locked = aq->lock_on_commit;
        else {
            aq->wants_prod = true;
            pthread_cond_broadcast(&aq->cond);
        }
    }

    pthread_mutex_unlock(&aq->lock);

    if (aq->cur_flip == NULL)
        return;

    rv = drmu_atomic_commit(aq->cur_flip, flags);

    if (rv == 0) {
        if (aq->retry_count != 0)
            drmu_warn(du, "[%d]: Atomic commit OK", aq->qno);
        aq->retry_count = 0;

        // This is the only place last_flip is written when not shutting down
        // so we don't need the lock
        if (aq->discard_last) {
            pthread_mutex_lock(&aq->lock);
            if (aq->locked) {
                assert(aq->last_flip == NULL);
                aq->last_flip = aq->cur_flip;
                aq->cur_flip = NULL;
            }
            pthread_mutex_unlock(&aq->lock);

            // Writeback doesn't need to keep the source once done
            drmu_atomic_unref(&aq->cur_flip);
        }
        else {
            // Must merge cur into last rather than just replace last as there may
            // still be things on screen not updated by the current commit
            drmu_atomic_move_merge(&aq->last_flip, &aq->cur_flip);
        }
    }
    else if (rv == -EBUSY && ++aq->retry_count < 16) {
        // This really shouldn't happen but we observe that the 1st commit after
        // a modeset often fails with BUSY.  It seems to be fine on a 10ms retry
        // but allow some more in case we need a bit longer in some cases
        // *** This may never happen now with blocking commits
        drmu_warn(du, "[%d]: Atomic commit BUSY", aq->qno);
        prod_time = 20;
    }
    else {
        drmu_err(du, "[%d]: Atomic commit failed: %s", aq->qno, strerror(-rv));
        drmu_atomic_dump(aq->cur_flip);
        drmu_atomic_unref(&aq->cur_flip);
        aq->retry_count = 0;
        // We haven't had a good commit so _unlock must not be called so no
        // mutex required
        aq->locked = false;
    }

    pollqueue_add_task(aq->prod_pt, prod_time);
}

static void
queue_free(drmu_queue_t * const aq)
{
    sem_t * const finish_sem = aq->finish_sem;

    // Delete will wait for a running polltask to finish
    polltask_delete(&aq->prod_pt);

    next_flip_uninit(&aq->next);
    drmu_atomic_unref(&aq->cur_flip);

    if (aq->env_restore_req)
        drmu_env_int_restore(aq->du);

    drmu_atomic_unref(&aq->last_flip);

    pollqueue_finish(&aq->pq);

    pthread_cond_destroy(&aq->cond);
    pthread_mutex_destroy(&aq->lock);

    drmu_env_unref(&aq->du);
    free(aq);

    // If we are waiting for this to die - signal it
    if (finish_sem != NULL)
        sem_post(finish_sem);
}

static void
queue_finish(drmu_queue_t ** const ppAq, const bool wants_restore)
{
    sem_t sem;

    if (*ppAq == NULL)
        return;

    sem_init(&sem, 0, 0);
    (*ppAq)->env_restore_req = wants_restore;
    (*ppAq)->finish_sem = &sem;
    drmu_queue_unref(ppAq);

    while (sem_wait(&sem) != 0 && errno == EINTR)
        /* loop */;
    sem_destroy(&sem);
}

//-----------------------------------------------------------------------------
//
// Default Q setup & destroy functions

// Kill the Q
// Ordering goes:
//   Shutdown Q events
//   Restore old state
//   Clear previous atomics
// Must be done in that order or we end up destroying buffers that are
// still in use
static void
poll_destroy(drmu_queue_t ** ppAq, drmu_env_t * du)
{
    (void)du;
    queue_finish(ppAq, true);
}

static drmu_queue_t *
poll_new(drmu_env_t * du)
{
    return drmu_queue_new(du);
}

//-----------------------------------------------------------------------------
//
// External functions
// Somewhat broken naming scheme for historic reasons

drmu_queue_t *
drmu_queue_new(struct drmu_env_s * const du)
{
    drmu_queue_t * const aq = calloc(1, sizeof(*aq));
    pthread_condattr_t condattr;
    static atomic_uint qcount = 0;

    if (aq == NULL)
        return NULL;

    aq->du = drmu_env_ref(du);
    next_flip_init(&aq->next);
    aq->cur_flip = NULL;
    aq->last_flip = NULL;
    aq->qno = atomic_fetch_add(&qcount, 1);
    aq->wants_prod = true;

    pthread_mutex_init(&aq->lock, NULL);

    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&aq->cond, &condattr);
    pthread_condattr_destroy(&condattr);

    if ((aq->pq = pollqueue_new()) == NULL)
        goto fail;
    if ((aq->prod_pt = polltask_new(aq->pq, -1, 0, queue_prod_cb, aq)) == NULL)
        goto fail;
    return aq;

fail:
    queue_free(aq);
    return aq;
}

drmu_queue_t *
drmu_queue_ref(drmu_queue_t * const dq)
{
    atomic_fetch_add(&dq->ref_count, 1);
    return dq;
}

void
drmu_queue_unref(drmu_queue_t ** const ppdq)
{
    drmu_queue_t * const dq = *ppdq;
    int n;

    if (dq == NULL)
        return;
    *ppdq = NULL;

    n = atomic_fetch_sub(&dq->ref_count, 1);
    if (n != 0)
        return;

    queue_free(dq);
}

void
drmu_queue_finish(drmu_queue_t ** const ppdq)
{
    queue_finish(ppdq, false);
}

int
drmu_queue_queue_tagged(drmu_queue_t * const aq,
                        const unsigned int tag, const drmu_queue_merge_t qmerge,
                        struct drmu_atomic_s ** ppda)
{
    int rv = 0;
    drmu_env_t * const du = drmu_atomic_env(*ppda);
    drmu_atomic_t ** ppna = NULL;
    drmu_atomic_t * discard_da = NULL;
    bool wants_prod = false;

    if (aq == NULL) {
        rv = -EINVAL;
        goto fail_unref;
    }

    if (drmu_atomic_is_empty(*ppda))
        goto fail_unref;  // rv = 0 so not an error really

    pthread_mutex_lock(&aq->lock);

    if (qmerge != DRMU_QUEUE_MERGE_QUEUE)
        ppna = next_flip_find_tag(&aq->next, tag);

    if (ppna == NULL) {
        if ((ppna = next_flip_add_tail(&aq->next, tag)) == NULL) {
            rv = -ENOMEM;
            goto fail_unlock;
        }
    }

    switch (qmerge) {
        case DRMU_QUEUE_MERGE_MERGE:
            if ((rv = drmu_atomic_move_merge(ppna, ppda)) != 0)
                goto fail_unlock;
            break;
        case DRMU_QUEUE_MERGE_QUEUE:
        case DRMU_QUEUE_MERGE_DROP:
            if (*ppna == NULL)
                *ppna = drmu_atomic_move(ppda);
            break;
        case DRMU_QUEUE_MERGE_REPLACE:
            discard_da = *ppna; // Unref can take a short while - move outside lock
            *ppna = drmu_atomic_move(ppda);
            break;
        default:
            drmu_err(du, "Bad qmerge value");
            rv = -EINVAL;
            goto fail_unlock;
    }

    // No pending commit?
    if (aq->wants_prod) {
        aq->wants_prod = false;
        wants_prod = true;
    }

    rv = 0;

fail_unlock:
    pthread_mutex_unlock(&aq->lock);

    // Do outside lock to avoid possible unneeded lock conflict
    if (wants_prod)
        pollqueue_add_task(aq->prod_pt, 0);

    drmu_atomic_unref(&discard_da);
fail_unref:
    drmu_atomic_unref(ppda);
    return rv;
}

int
drmu_atomic_queue(drmu_atomic_t ** ppda)
{
    drmu_queue_t * const aq = drmu_env_queue_default(drmu_atomic_env(*ppda));
    return drmu_queue_queue_tagged(aq, 0, DRMU_QUEUE_MERGE_MERGE, ppda);
}

drmu_queue_t *
drmu_env_queue_default(drmu_env_t * const du)
{
    drmu_queue_t * aq;
    if (drmu_env_int_poll_set(du, poll_new, poll_destroy, &aq) != 0)
        return NULL;
    return aq;
}

int
drmu_queue_wait(drmu_queue_t * const aq)
{
    int rv = 0;
    struct timespec ts;

    if (aq == NULL)
        return 0;

    pthread_mutex_lock(&aq->lock);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 1;  // We should never timeout if all is well - 1 sec is plenty

    // wants_prod will be true once Q empty and commit finished
    while (!aq->wants_prod)
        if ((rv = pthread_cond_timedwait(&aq->cond, &aq->lock, &ts)) != 0)
            break;

    pthread_mutex_unlock(&aq->lock);
    return rv;
}

void
drmu_queue_keep_last_set(drmu_queue_t * const aq, const bool keep_last)
{
    aq->discard_last = !keep_last;
}

void
drmu_queue_lock_on_commit_set(drmu_queue_t * const aq, const bool lock)
{
    aq->lock_on_commit = lock;
}

int
drmu_queue_unlock(drmu_queue_t * const aq)
{
    bool wants_prod = false;
    drmu_atomic_t * da = NULL;

    pthread_mutex_lock(&aq->lock);
    if (aq->locked) {
        aq->locked = false;
        wants_prod = true;

        if (aq->discard_last) {
            da = aq->last_flip;
            aq->last_flip = NULL;
        }
    }
    pthread_mutex_unlock(&aq->lock);

    if (wants_prod)
        pollqueue_add_task(aq->prod_pt, 0);

    drmu_atomic_unref(&da);
    return 0;
}

int
drmu_env_queue_wait(drmu_env_t * const du)
{
    return drmu_queue_wait(drmu_env_queue_default(du));
}

