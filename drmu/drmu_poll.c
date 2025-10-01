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

static void
next_flip_uninit(next_flips_t * const nf)
{
    while (next_flip_discard_head(nf))
        /* loop */;
    free(nf->flips);
    memset(nf, 0, sizeof(*nf));
}

struct drmu_atomic_q_s {
    atomic_int ref_count; // 0 - free, 1 - uniniting, 2 - one ref
    pthread_mutex_t lock;
    pthread_cond_t cond;
    next_flips_t next;
    drmu_atomic_t * cur_flip;
    drmu_atomic_t * last_flip;
    unsigned int retry_count;
    struct polltask * retry_task;
    unsigned int qno; // Handy for debug
};

// Needs locked
static int
atomic_q_attempt_commit_next(drmu_atomic_q_t * const aq)
{
    drmu_atomic_t * const da = next_flip_head(&aq->next);
    drmu_env_t * const du = drmu_atomic_env(da);
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET;
    int rv;

    if ((rv = drmu_atomic_commit(da, flags)) == 0) {
        if (aq->retry_count != 0)
            drmu_warn(du, "[%d]: Atomic commit OK: %p", aq->qno, da);
        aq->cur_flip = next_flip_pop_head(&aq->next);
        aq->retry_count = 0;
    }
    else if (rv == -EBUSY && ++aq->retry_count < 16 && aq->retry_task != NULL) {
        // This really shouldn't happen but we observe that the 1st commit after
        // a modeset often fails with BUSY.  It seems to be fine on a 10ms retry
        // but allow some more in case ww need a bit longer in some cases
        drmu_warn(du, "[%d]: Atomic commit BUSY", aq->qno);
        pollqueue_add_task(aq->retry_task, 20);
        rv = 0;
    }
    else {
        drmu_err(du, "[%d]: Atomic commit failed: %s", aq->qno, strerror(-rv));
        drmu_atomic_dump(da);
        next_flip_discard_head(&aq->next);
        aq->retry_count = 0;
    }

    return rv;
}

static void
atomic_q_retry_cb(void * v, short revents)
{
    drmu_atomic_q_t * const aq = v;
    (void)revents;

    pthread_mutex_lock(&aq->lock);

    // If we need a retry then next != NULL && cur == NULL
    // if not that then we've fixed ourselves elsewhere

    if (!next_flip_is_empty(&aq->next) && aq->cur_flip == NULL)
        atomic_q_attempt_commit_next(aq);

    pthread_mutex_unlock(&aq->lock);
}

// Called after an atomic commit has completed
// not called on every vsync, so if we haven't committed anything this won't be called
static void
atomic_page_flip_cb(drmu_env_t * const du, void *user_data)
{
    drmu_atomic_t * const da = user_data;
    drmu_atomic_q_t * const aq = drmu_atomic_queue_get(da);

    // At this point:
    //  next   The atomic we are about to commit
    //  cur    The last atomic we committed, now in use (must be != NULL)
    //  last   The atomic that has just become obsolete

    pthread_mutex_lock(&aq->lock);

    if (da != aq->cur_flip) {
        drmu_err(du, "%s: User data el (%p) != cur (%p)", __func__, da, aq->cur_flip);
    }

    // Must merge cur into last rather than just replace last as there may
    // still be things on screen not updated by the current commit
    drmu_atomic_move_merge(&aq->last_flip, &aq->cur_flip);

    if (!next_flip_is_empty(&aq->next))
        atomic_q_attempt_commit_next(aq);

    pthread_cond_broadcast(&aq->cond);
    pthread_mutex_unlock(&aq->lock);
}

static int
atomic_q_kill(drmu_atomic_q_t * const aq)
{
    struct timespec ts;
    int rv = 0;

    pthread_mutex_lock(&aq->lock);

    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 1;  // We should never timeout if all is well - 1 sec is plenty

    // Can flush next safely - but call commit cbs
    drmu_atomic_run_commit_callbacks(next_flip_head(&aq->next));
    next_flip_discard_head(&aq->next);
    polltask_delete(&aq->retry_task); // If we've got here then retry would not succeed

    // Wait for cur to finish - seems to confuse the world otherwise
    while (aq->cur_flip != NULL) {
        if ((rv = pthread_cond_timedwait(&aq->cond, &aq->lock, &ts)) != 0)
            break;
    }

    pthread_mutex_unlock(&aq->lock);
    return rv;
}

static void
atomic_q_clear_flips(drmu_atomic_q_t * const aq)
{
    next_flip_uninit(&aq->next);
    drmu_atomic_unref(&aq->cur_flip);
    drmu_atomic_unref(&aq->last_flip);
}

static void
atomic_q_uninit(drmu_atomic_q_t * const aq)
{
    polltask_delete(&aq->retry_task);
    atomic_q_clear_flips(aq);
    pthread_cond_destroy(&aq->cond);
    pthread_mutex_destroy(&aq->lock);
}

static int
atomic_q_set_retry(drmu_atomic_q_t * const aq, struct pollqueue * pq)
{
    aq->retry_task = polltask_new_timer(pq, atomic_q_retry_cb, aq);
    return aq->retry_task == NULL ? -ENOMEM : 0;
}

static void
atomic_q_init(drmu_atomic_q_t * const aq, const unsigned int qno)
{
    pthread_condattr_t condattr;

    next_flip_init(&aq->next);
    aq->cur_flip = NULL;
    aq->last_flip = NULL;
    aq->qno = qno;
    pthread_mutex_init(&aq->lock, NULL);

    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&aq->cond, &condattr);
    pthread_condattr_destroy(&condattr);
}

//-----------------------------------------------------------------------------
//
// Event polling functions

#define DRMU_POLL_QUEUE_COUNT   4
#define DRMU_POLL_QUEUE_NO_MAX  (DRMU_POLL_QUEUE_COUNT - 1)

typedef struct drmu_poll_env_s {
    drmu_env_t * du;

    struct pollqueue * pq;
    struct polltask * pt;

    drmu_atomic_q_t * default_q;

    // global env for atomic flip
    drmu_atomic_q_t aqs[DRMU_POLL_QUEUE_COUNT];

} drmu_poll_env_t;

#define EVT(p) ((const struct drm_event *)(p))
static int
evt_read(drmu_poll_env_t * const pe)
{
    drmu_env_t * const du = pe->du;
    uint8_t buf[256] __attribute__((aligned(__BIGGEST_ALIGNMENT__)));
    const ssize_t rlen = read(drmu_fd(du), buf, sizeof(buf));
    size_t i;

    if (rlen < 0) {
        const int err = errno;
        drmu_err(du, "Event read failure: %s", strerror(err));
        return -err;
    }

    for (i = 0;
         i + sizeof(struct drm_event) <= (size_t)rlen && EVT(buf + i)->length <= (size_t)rlen - i;
         i += EVT(buf + i)->length) {
        switch (EVT(buf + i)->type) {
            case DRM_EVENT_FLIP_COMPLETE:
            {
                const struct drm_event_vblank * const vb = (struct drm_event_vblank*)(buf + i);
                if (EVT(buf + i)->length < sizeof(*vb))
                    break;

                atomic_page_flip_cb(du, (void *)(uintptr_t)vb->user_data);
                break;
            }
            default:
                drmu_warn(du, "Unexpected DRM event #%x", EVT(buf + i)->type);
                break;
        }
    }

    if (i != (size_t)rlen)
        drmu_warn(du, "Partial event received: len=%zd, processed=%zd", rlen, i);

    return 0;
}
#undef EVT

static drmu_atomic_q_t *
pollenv_queue_alloc(drmu_poll_env_t *const pe)
{
    unsigned int i;

    for (i = 0; i != DRMU_POLL_QUEUE_COUNT; ++i) {
        drmu_atomic_q_t * const dq = pe->aqs + i;
        int free_val = 0;
        if (atomic_compare_exchange_strong(&dq->ref_count, &free_val, 2)) {
            atomic_q_init(dq, i);
            return dq;
        }
    }

    return NULL;
}

static void
evt_polltask_cb(void * v, short revents)
{
    drmu_poll_env_t * const pe = v;

    if (revents == 0) {
        drmu_debug(pe->du, "%s: Timeout", __func__);
    }
    else {
        evt_read(pe);
    }

    pollqueue_add_task(pe->pt, 1000);
}

static void
poll_free(drmu_poll_env_t * const pe)
{
    unsigned int i;

    // pt & pq should already be free by now but may not be in some error cleanup
    // situations.

    polltask_delete(&pe->pt);
    for (i = 0; i != DRMU_POLL_QUEUE_COUNT; ++i)
        atomic_q_uninit(pe->aqs + i);
    pollqueue_finish(&pe->pq);

    free(pe);
}

// Kill the Q
// Ordering goes:
//   Shutdown Q events
//   Restore old state
//   Clear previous atomics
// Must be done in that order or we end up destroying buffers that are
// still in use
static void
poll_destroy(drmu_poll_env_t ** ppPe, drmu_env_t * du)
{
    drmu_poll_env_t * const pe = *ppPe;
    unsigned int i;

    if (pe == NULL)
        return;
    *ppPe = NULL;

    for (i = 0; i != DRMU_POLL_QUEUE_COUNT; ++i)
        atomic_q_kill(pe->aqs + i);

    polltask_delete(&pe->pt);
    // All polltasks must be dead before calling _finish (including the aq
    // retry task) or it will hang
    pollqueue_finish(&pe->pq);

    drmu_env_int_restore(du);

    for (i = 0; i != DRMU_POLL_QUEUE_COUNT; ++i)
        atomic_q_clear_flips(pe->aqs + i);

    poll_free(pe);
}

static drmu_poll_env_t *
poll_new(drmu_env_t * du)
{
    drmu_poll_env_t * const pe = calloc(1, sizeof(*pe));
    unsigned int i;

    pe->du = du;

    if ((pe->pq = pollqueue_new()) == NULL) {
        drmu_err(du, "Failed to create pollqueue");
        goto fail;
    }
    if ((pe->pt = polltask_new(pe->pq, drmu_fd(du), POLLIN | POLLPRI, evt_polltask_cb, pe)) == NULL) {
        drmu_err(du, "Failed to create polltask");
        goto fail;
    }

    for (i = 0; i != DRMU_POLL_QUEUE_COUNT; ++i) {
        if (atomic_q_set_retry(pe->aqs + i, pe->pq) != 0) {
            drmu_err(du, "Failed to create retry polltask");
            goto fail;
        }
    }

    pollqueue_add_task(pe->pt, 1000);

    pe->default_q = pollenv_queue_alloc(pe);

    return pe;

fail:
    poll_free(pe);
    return NULL;
}

//-----------------------------------------------------------------------------
//
// External functions
// Somewhat broken naming scheme for historic reasons

drmu_atomic_q_t *
drmu_queue_new(struct drmu_env_s * const du)
{
    drmu_poll_env_t * pe;
    if (drmu_env_int_poll_set(du, poll_new, poll_destroy, &pe) != 0)
        return NULL;
    return pollenv_queue_alloc(pe);
}

drmu_atomic_q_t *
drmu_queue_ref(drmu_atomic_q_t * const dq)
{
    atomic_fetch_add(&dq->ref_count, 1);
    return dq;
}

void
drmu_queue_unref(drmu_atomic_q_t ** const ppdq)
{
    drmu_atomic_q_t * const dq = *ppdq;
    int n;

    if (dq == NULL)
        return;
    *ppdq = NULL;

    n = atomic_fetch_add(&dq->ref_count, 1);
    if (n != 1)
        return;

    atomic_q_uninit(dq);

    atomic_store(&dq->ref_count, 0);
}

int
drmu_queue_queue_tagged(drmu_atomic_q_t * const aq,
                        const unsigned int tag, const drmu_queue_merge_t qmerge,
                        struct drmu_atomic_s ** ppda)
{
    int rv = 0;
    drmu_env_t * const du = drmu_atomic_env(*ppda);
    drmu_atomic_t ** ppna = NULL;
    drmu_atomic_t * discard_da = NULL;

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
            discard_da = *ppna; // Unref cxan take a short while - move outside lock
            *ppna = drmu_atomic_move(ppda);
            break;
        default:
            drmu_err(du, "Bad qmerge value");
            rv = -EINVAL;
            goto fail_unlock;
    }

    drmu_atomic_queue_set(*ppna, aq);

    // No pending commit?
    if (aq->cur_flip == NULL) {
        if ((rv = atomic_q_attempt_commit_next(aq)) != 0)
            goto fail_unlock;
    }

    rv = 0;

fail_unlock:
    pthread_mutex_unlock(&aq->lock);
    drmu_atomic_unref(&discard_da);
fail_unref:
    drmu_atomic_unref(ppda);
    return rv;
}

int
drmu_atomic_queue(drmu_atomic_t ** ppda)
{
    drmu_atomic_q_t * const aq = drmu_env_queue_default(drmu_atomic_env(*ppda));
    return drmu_queue_queue_tagged(aq, 0, DRMU_QUEUE_MERGE_MERGE, ppda);
}

drmu_atomic_q_t *
drmu_env_queue_default(drmu_env_t * const du)
{
    drmu_poll_env_t * pe;
    if (drmu_env_int_poll_set(du, poll_new, poll_destroy, &pe) != 0)
        return NULL;
    return pe->default_q;
}

int
drmu_queue_wait(drmu_atomic_q_t * const aq)
{
    int rv = 0;
    struct timespec ts;

    if (aq == NULL)
        return 0;

    pthread_mutex_lock(&aq->lock);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    ts.tv_sec += 1;  // We should never timeout if all is well - 1 sec is plenty

    // Next should clear quickly
    while (!next_flip_is_empty(&aq->next)) {
        if ((rv = pthread_cond_timedwait(&aq->cond, &aq->lock, &ts)) != 0)
            break;
    }

    pthread_mutex_unlock(&aq->lock);
    return rv;
}

int
drmu_env_queue_wait(drmu_env_t * const du)
{
    return drmu_queue_wait(drmu_env_queue_default(du));
}

struct pollqueue *
drmu_env_pollqueue(drmu_env_t * const du)
{
    drmu_poll_env_t * pe;
    if (drmu_env_int_poll_set(du, poll_new, poll_destroy, &pe) != 0)
        return NULL;
    return pe->pq;
}

