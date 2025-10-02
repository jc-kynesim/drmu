#include "drmu_writeback.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_pool.h"
#include "pollqueue.h"

#include <assert.h>

struct drmu_writeback_env_s {
    atomic_int ref_count;

    drmu_env_t * du;
    drmu_output_t * dout;
    drmu_queue_t * dq;
    struct pollqueue * pq;

    // Find the primary plane for use with writeback_fb
    // We will want something more complex/comprehensive for general render ops
    drmu_plane_t * plane_pri;

    atomic_int tag_n;

    sem_t * finish_sem;
};

static void
writeback_env_free(drmu_writeback_env_t * const wbe)
{
    sem_t * const finish_sem = wbe->finish_sem;

    drmu_queue_finish(&wbe->dq);
    pollqueue_finish(&wbe->pq);
    drmu_plane_unref(&wbe->plane_pri);
    drmu_output_unref(&wbe->dout);
    drmu_env_unref(&wbe->du);
    free(wbe);

    if (finish_sem != NULL)
        sem_post(finish_sem);
}

// Get a "unique" non-zero tag no
static unsigned int
writeback_env_tag_new(drmu_writeback_env_t * const wbe)
{
    unsigned int n;
    while ((n = atomic_fetch_add(&wbe->tag_n, 1)) == 0)
        /* Loop */;
    return n;
}

drmu_writeback_env_t *
drmu_writeback_env_new(struct drmu_env_s * const du)
{
    drmu_writeback_env_t * wbe = calloc(1, sizeof(*wbe));

    if (wbe == NULL)
        return NULL;

    wbe->du = drmu_env_ref(du);

    if ((wbe->dq = drmu_queue_new(du)) == NULL) {
        drmu_err(du, "Cannot allocate queue");
        goto fail;
    }
    drmu_queue_keep_last_set(wbe->dq, false);
    drmu_queue_lock_on_commit_set(wbe->dq, true);

    if ((wbe->dout = drmu_output_new(du)) == NULL) {
        drmu_err(du, "Cannot allocate output");
        goto fail;
    }

    if (drmu_output_modeset_allow(wbe->dout, true) != 0) {
        drmu_err(du, "Failed to allow modeset");
        goto fail;
    }

    if (drmu_output_add_writeback(wbe->dout) != 0) {
        drmu_err(du, "Failed to add writeback");
        goto fail;
    }

    if ((wbe->plane_pri = drmu_output_plane_ref_primary(wbe->dout)) == NULL) {
        drmu_err(du, "Failed to find prmary plane");
        goto fail;
    }

    if ((wbe->pq = pollqueue_new()) == NULL) {
        drmu_err(du, "Failed to get pollqueue");
        goto fail;
    }

    return wbe;

fail:
    writeback_env_free(wbe);
    return NULL;
}

drmu_writeback_env_t *
drmu_writeback_env_ref(drmu_writeback_env_t * const wbe)
{
    if (wbe != NULL)
        atomic_fetch_add(&wbe->ref_count, 1);
    return wbe;
}

void
drmu_writeback_env_unref(drmu_writeback_env_t ** const ppwbe)
{
    drmu_writeback_env_t * const wbe = *ppwbe;
    int n;

    if (wbe == NULL)
        return;
    n = atomic_fetch_sub(&wbe->ref_count, 1);
    if (n != 0)
        return;

    writeback_env_free(wbe);
}

void
drmu_writeback_env_finish(drmu_writeback_env_t ** const ppwbe)
{
    sem_t sem;

    if (*ppwbe == NULL)
        return;

    sem_init(&sem, 0, 0);
    (*ppwbe)->finish_sem = &sem;
    drmu_writeback_env_unref(ppwbe);
    while (sem_wait(&sem) != 0 && errno == EINTR)
        /* Loop */;
    sem_destroy(&sem);
}

struct drmu_output_s *
drmu_writeback_env_output(const drmu_writeback_env_t * const wbe)
{
    return wbe == NULL ? NULL : wbe->dout;
}

drmu_plane_t *
drmu_writeback_env_fmt_plane(drmu_writeback_env_t * const wbe,
                             drmu_output_t * const dest_dout, const unsigned int types,
                             uint32_t * const pFmt)
{
    size_t fmt_count = 1;
    const uint32_t * fmts = drmu_conn_writeback_formats(drmu_output_conn(wbe->dout, 0), &fmt_count);

    // This is a simple & stupid search.
    // We expect the 1st format we try to be both "good enough" and compatible with the dest dout
    for (size_t i = 0; i != fmt_count; ++i) {
        const uint32_t fmt = fmts[i];

        // *** Kludge for Pi not supporting rotation on this
        if (fmt == DRM_FORMAT_BGR888 || fmt == DRM_FORMAT_RGB888)
            continue;

        drmu_plane_t * const dp = drmu_output_plane_ref_format(dest_dout, types, fmt, 0);

        if (dp != NULL) {
            *pFmt = fmts[i];
            return dp;
        }
    }
    *pFmt = 0;
    return NULL;
}

//-----------------------------------------------------------------------------

typedef struct wbq_ent_s {
    atomic_int ref_count;

    drmu_fb_t * fb;
    bool done;
    struct pollqueue * pq;
    struct polltask * pt;
    drmu_writeback_fb_done_fn * done_fn;
    void * done_v;
    drmu_queue_t * dqueue;
} wbq_ent_t;

static void
wbq_ent_free(wbq_ent_t * const ent)
{
    if (ent == NULL)
        return;

    if (!ent->done)
        ent->done_fn(ent->done_v, NULL);

    drmu_fb_unref(&ent->fb);
    // In normal useg these two should already be unreffed
    polltask_delete(&ent->pt);
    pollqueue_unref(&ent->pq);
    drmu_queue_unref(&ent->dqueue);
    free(ent);
}

static wbq_ent_t *
wbq_ent_ref(wbq_ent_t * const ent)
{
    if (ent == NULL)
        return NULL;
    atomic_fetch_add(&ent->ref_count, 1);
    return ent;
}

static void
wbq_ent_unref(wbq_ent_t ** const ppent)
{
    wbq_ent_t * const ent = *ppent;
    int n;

    if (ent == NULL)
        return;
    *ppent = NULL;

    n = atomic_fetch_sub(&ent->ref_count, 1);
    if (n != 0)
        return;

    wbq_ent_free(ent);
}

static void
writeback_fb_ent_polltask_done(void * v, short revents)
{
    wbq_ent_t * ent = v;

    drmu_queue_unlock(ent->dqueue);

    close(drmu_fb_out_fence_take_fd(ent->fb));
    if (revents != 0) {
        ent->done_fn(ent->done_v, ent->fb);
        ent->done = true;
    }
    polltask_delete(&ent->pt);
    drmu_fb_unref(&ent->fb);
    wbq_ent_unref(&ent);
}

static void
writeback_fb_ent_commit_cb(void * v, int fd, drmu_fb_t * dfb)
{
    wbq_ent_t * ent = v;

    if (fd != -1 && dfb != NULL) {
        ent->pt = polltask_new(ent->pq, fd, POLLIN, writeback_fb_ent_polltask_done, wbq_ent_ref(ent));
        pollqueue_add_task(ent->pt, 1000);
        pollqueue_unref(&ent->pq);
    }
    else {
        wbq_ent_unref(&ent);
    }
}

struct drmu_writeback_fb_s {
    atomic_int ref_count;

    drmu_writeback_env_t * wbe;
    drmu_pool_t * pool;

    unsigned int q_tag;
    drmu_queue_merge_t q_merge;
};

static void
writeback_fb_free(drmu_writeback_fb_t * const wbq)
{
    drmu_writeback_env_unref(&wbq->wbe);
    drmu_pool_unref(&wbq->pool);
    free(wbq);
}

drmu_writeback_fb_t *
drmu_writeback_fb_new(drmu_writeback_env_t * const wbe, drmu_pool_t * const fb_pool)
{
    drmu_writeback_fb_t * const wbq = calloc(1, sizeof(*wbq));

    if (wbq == NULL)
        return NULL;

    wbq->wbe = drmu_writeback_env_ref(wbe);
    wbq->pool = drmu_pool_ref(fb_pool);
    wbq->q_tag = writeback_env_tag_new(wbe);
    wbq->q_merge = DRMU_QUEUE_MERGE_REPLACE;

    return wbq;
}

drmu_writeback_fb_t *
drmu_writeback_fb_ref(drmu_writeback_fb_t * const wbq)
{
    if (wbq == NULL)
        return NULL;
    atomic_fetch_add(&wbq->ref_count, 1);
    return wbq;
}

void
drmu_writeback_fb_unref(drmu_writeback_fb_t ** const ppwbq)
{
    drmu_writeback_fb_t * const wbq = *ppwbq;
    int n;

    if (wbq == NULL)
        return;
    *ppwbq = NULL;

    n = atomic_fetch_sub(&wbq->ref_count, 1);
    if (n != 0)
        return;

    writeback_fb_free(wbq);
}

static drmu_rect_t
limit_rect(const drmu_rect_t dest_rect, const unsigned int rot_conn)
{
    drmu_rect_t r = dest_rect;

    // Pi has a max input width -> output height if transposed
    if (r.h > 1920 && drmu_rotation_is_transposed(rot_conn))
        r.h = 1920;

    r.x = 0;
    r.y = 0;
    return r;
}


int
drmu_writeback_fb_queue(drmu_writeback_fb_t * wbq,
                        const drmu_rect_t dest_rect, const unsigned int dest_rot, const uint32_t fmt,
                        drmu_writeback_fb_done_fn * const done_fn, void * const v,
                        drmu_fb_t * const fb)
{
    wbq_ent_t * ent = calloc(1, sizeof(*ent));
    drmu_writeback_env_t * const wbe = wbq->wbe;
    drmu_env_t * const du = wbe->du;
    drmu_atomic_t * da = drmu_atomic_new(du);
    unsigned int rot_total;
    unsigned int rot_plane;
    unsigned int rot_conn;
    drmu_rect_t r;
    int rv;

    if (ent == NULL) {
        done_fn(v, NULL);
        rv = -ENOMEM;
        goto fail;
    }

    ent->done_fn = done_fn;
    ent->done_v = v;
    ent->pq = pollqueue_ref(wbe->pq);
    ent->dqueue = drmu_queue_ref(wbe->dq);

    if (da == NULL) {
        rv = -ENOMEM;
        goto fail;
    }

    rot_total = drmu_fb_rotation(fb, dest_rot);

    rot_plane = drmu_rotation_find(rot_total, drmu_plane_rotation_mask(wbe->plane_pri),
                                   drmu_conn_rotation_mask(drmu_output_conn(wbe->dout, 0)));
    if (rot_plane == DRMU_ROTATION_INVALID) {
        drmu_err(du, "Cannot find combination of rotations for %d", rot_total);
        rv = -EINVAL;
        goto fail;
    }
    rot_conn = drmu_rotation_subb(rot_plane, rot_total);

    r = limit_rect(dest_rect, rot_conn);

    if ((rv = drmu_atomic_plane_add_fb(da, wbe->plane_pri, fb,
                                 drmu_rotation_is_transposed(rot_conn) ? drmu_rect_transpose(r) : r)) != 0)
    {
        drmu_err(du, "Failed atomic add fb");
        goto fail;
    }
    drmu_atomic_plane_add_rotation(da, wbe->plane_pri, rot_plane);

    ent->fb = (wbq->pool == NULL) ?
        drmu_fb_new_dumb(du, r.w, r.h, fmt) :
        drmu_pool_fb_new(wbq->pool, r.w, r.h, fmt, 0);

    if (ent->fb == NULL) {
        drmu_err(du, "Failed to create fb");
        rv = -ENOMEM;
        goto fail;
    }

    rv = drmu_atomic_output_add_writeback_fb_callback(da, wbe->dout, ent->fb, rot_conn, writeback_fb_ent_commit_cb, ent);
    ent = NULL; // Ownership taken by call
    if (rv != 0) {
        drmu_err(du, "Failed to add writeback fb\n");
        goto fail;
    }

    if ((rv = drmu_queue_queue_tagged(wbe->dq, wbq->q_tag, wbq->q_merge, &da)) != 0) {
        drmu_err(du, "Failed merge");
        goto fail;
    }

    return 0;

fail:
    drmu_atomic_unref(&da);
    wbq_ent_unref(&ent);
    return rv;
}

