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

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <libdrm/drm_mode.h>

#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif

//----------------------------------------------------------------------------
//
// Atomic Q fns (internal)

typedef struct drmu_atomic_q_s {
    pthread_mutex_t lock;
    pthread_cond_t cond;
    drmu_atomic_t * next_flip;
    drmu_atomic_t * cur_flip;
    drmu_atomic_t * last_flip;
    unsigned int retry_count;
    struct polltask * retry_task;

    drmu_env_queue_stats_t stats;
} drmu_atomic_q_t;

// Needs locked
static int
atomic_q_attempt_commit_next(drmu_atomic_q_t * const aq)
{
    drmu_env_t * const du = drmu_atomic_env(aq->next_flip);
    uint32_t flags = DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET;
    int rv;

    if ((rv = drmu_atomic_commit(aq->next_flip, flags)) == 0) {
        if (aq->retry_count != 0)
            drmu_warn(du, "%s: Atomic commit OK", __func__);
        aq->cur_flip = aq->next_flip;
        aq->next_flip = NULL;
        aq->retry_count = 0;
    }
    else if (rv == -EBUSY && ++aq->retry_count < 16 && aq->retry_task != NULL) {
        // This really shouldn't happen but we observe that the 1st commit after
        // a modeset often fails with BUSY.  It seems to be fine on a 10ms retry
        // but allow some more in case ww need a bit longer in some cases
        drmu_warn(du, "%s: Atomic commit BUSY", __func__);
        pollqueue_add_task(aq->retry_task, 20);
        rv = 0;
    }
    else {
        drmu_err(du, "%s: Atomic commit failed: %s", __func__, strerror(-rv));
        drmu_atomic_dump(aq->next_flip);
        drmu_atomic_unref(&aq->next_flip);
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

    if (aq->next_flip != NULL && aq->cur_flip == NULL)
        atomic_q_attempt_commit_next(aq);

    pthread_mutex_unlock(&aq->lock);
}

// Called after an atomic commit has completed
// not called on every vsync, so if we haven't committed anything this won't be called
static void
page_flip_cb(drmu_env_t * const du, drmu_atomic_q_t * const aq, const struct drm_event_vblank * const vb)
{
    drmu_atomic_t * const da = (void *)(uintptr_t)vb->user_data;

    // At this point:
    //  next   The atomic we are about to commit
    //  cur    The last atomic we committed, now in use (must be != NULL)
    //  last   The atomic that has just become obsolete

    pthread_mutex_lock(&aq->lock);

    aq->stats.crtc_id = vb->crtc_id;
    aq->stats.sequence_last = vb->sequence;
    aq->stats.time_us_last = (uint64_t)vb->tv_sec * 1000000 + vb->tv_usec;
    if (aq->stats.flip_count++ == 0) {
        aq->stats.sequence_first = aq->stats.sequence_last;
        aq->stats.time_us_first = aq->stats.time_us_last;
    }

    if (da != aq->cur_flip) {
        drmu_err(du, "%s: User data el (%p) != cur (%p)", __func__, da, aq->cur_flip);
    }

    // Must merge cur into last rather than just replace last as there may
    // still be things on screen not updated by the current commit
    drmu_atomic_move_merge(&aq->last_flip, &aq->cur_flip);

    if (aq->next_flip != NULL)
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
    drmu_atomic_run_commit_callbacks(aq->next_flip);
    drmu_atomic_unref(&aq->next_flip);
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
    drmu_atomic_unref(&aq->next_flip);
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
atomic_q_init(drmu_atomic_q_t * const aq)
{
    pthread_condattr_t condattr;

    aq->next_flip = NULL;
    aq->cur_flip = NULL;
    aq->last_flip = NULL;
    pthread_mutex_init(&aq->lock, NULL);

    pthread_condattr_init(&condattr);
    pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC);
    pthread_cond_init(&aq->cond, &condattr);
    pthread_condattr_destroy(&condattr);
}

//-----------------------------------------------------------------------------
//
// Event polling functions

typedef struct drmu_poll_env_s {
    drmu_env_t * du;

    struct pollqueue * pq;
    struct polltask * pt;

    // global env for atomic flip
    drmu_atomic_q_t aq;

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

                page_flip_cb(du, &pe->aq, vb);
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
    // pt & pq should already be free by now but may not be in some error cleanup
    // situations.

    polltask_delete(&pe->pt);
    atomic_q_uninit(&pe->aq);
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

    if (pe == NULL)
        return;
    *ppPe = NULL;

    atomic_q_kill(&pe->aq);

    polltask_delete(&pe->pt);
    // All polltasks must be dead before calling _finish (including the aq
    // retry task) or it will hang
    pollqueue_finish(&pe->pq);

    drmu_env_int_restore(du);

    atomic_q_clear_flips(&pe->aq);

    poll_free(pe);
}

static drmu_poll_env_t *
poll_new(drmu_env_t * du)
{
    drmu_poll_env_t * const pe = calloc(1, sizeof(*pe));

    pe->du = du;
    atomic_q_init(&pe->aq);

    if ((pe->pq = pollqueue_new()) == NULL) {
        drmu_err(du, "Failed to create pollqueue");
        goto fail;
    }
    if ((pe->pt = polltask_new(pe->pq, drmu_fd(du), POLLIN | POLLPRI, evt_polltask_cb, pe)) == NULL) {
        drmu_err(du, "Failed to create polltask");
        goto fail;
    }

    if (atomic_q_set_retry(&pe->aq, pe->pq) != 0) {
        drmu_err(du, "Failed to create retry polltask");
        goto fail;
    }

    pollqueue_add_task(pe->pt, 1000);

    return pe;

fail:
    poll_free(pe);
    return NULL;
}

//-----------------------------------------------------------------------------
//
// External functions
// Somewhat broken naming scheme for historic reasons

int
drmu_atomic_queue(drmu_atomic_t ** ppda)
{
    int rv;
    drmu_env_t * const du = drmu_atomic_env(*ppda);
    drmu_poll_env_t * pe;
    drmu_atomic_q_t * aq;

    if (du == NULL)
        return 0;

    if ((rv = drmu_env_int_poll_set(du, poll_new, poll_destroy, &pe)) != 0) {
        drmu_atomic_unref(ppda);
        return rv;
    }

    aq = &pe->aq;

    pthread_mutex_lock(&aq->lock);

    ++aq->stats.queue_count;
    if (aq->next_flip)
        ++aq->stats.merge_count;

    rv = drmu_atomic_move_merge(&aq->next_flip, ppda);

    // No pending commit?
    if (rv == 0 && aq->cur_flip == NULL)
        rv = atomic_q_attempt_commit_next(aq);

    pthread_mutex_unlock(&aq->lock);
    return rv;
}

int
drmu_env_queue_wait(drmu_env_t * const du)
{
    drmu_poll_env_t *const pe = drmu_env_int_poll_get(du);
    int rv = 0;

    if (pe != NULL) {
        drmu_atomic_q_t *const aq = &pe->aq;
        struct timespec ts;

        pthread_mutex_lock(&aq->lock);
        clock_gettime(CLOCK_MONOTONIC, &ts);
        ts.tv_sec += 1;  // We should never timeout if all is well - 1 sec is plenty

        // Next should clear quickly
        while (aq->next_flip != NULL) {
            if ((rv = pthread_cond_timedwait(&aq->cond, &aq->lock, &ts)) != 0)
                break;
        }

        pthread_mutex_unlock(&aq->lock);
    }
    return rv;
}

// Retrieve stats
void
drmu_env_queue_stats_get(struct drmu_env_s * const du,
                         drmu_env_queue_stats_t * const stats_buf,
                         const size_t stats_buf_len, const bool reset)
{
    drmu_poll_env_t *const pe = drmu_env_int_poll_get(du);
    const size_t stats_size = sizeof(pe->aq.stats);

    if (stats_buf != NULL && (pe == NULL || stats_buf_len > stats_size))
        memset(stats_buf, 0, stats_buf_len);

    if (pe != NULL) {
        drmu_atomic_q_t *const aq = &pe->aq;

        pthread_mutex_lock(&aq->lock);
        if (stats_buf != NULL)
            memcpy(stats_buf, &aq->stats, MIN(sizeof(aq->stats), stats_buf_len));
        if (reset)
            memset(&aq->stats, 0, stats_size);
        pthread_mutex_unlock(&aq->lock);
    }
}

