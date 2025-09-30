#include "drmu_writeback.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "drmu_pool.h"
#include "pollqueue.h"

#include <assert.h>

struct drmu_writeback_output_s
{
    atomic_int ref_count;

    drmu_rect_t req_rect;
    drmu_rect_t fb_rect;
    unsigned int rot;
    unsigned int req_rot;
    uint32_t fmt;

    drmu_env_t * du;  // Derived from dout - just a copy for ease of use
    drmu_output_t * dout;

    drmu_writeback_fb_prep_fns_t prep_fns;
    void * prep_v;
};

typedef struct writeback_fb_env_s
{
    atomic_int ref_count;

    drmu_writeback_output_t * dof;
    drmu_fb_t * fb;
    struct polltask * pt;

    drmu_writeback_fb_done_fns_t done_fns;
    void * done_v;
} writeback_fb_env_t;

static void
writeback_fb_env_free(writeback_fb_env_t * const wbe)
{
    polltask_delete(&wbe->pt);
    wbe->done_fns.unref(&wbe->done_v);
    drmu_fb_unref(&wbe->fb);
    drmu_writeback_unref(&wbe->dof);
    free(wbe);
}

static void
writeback_fb_env_unref(writeback_fb_env_t ** const ppwbe)
{
    writeback_fb_env_t * const wbe = *ppwbe;

    if (wbe == NULL)
        return;
    *ppwbe = NULL;

    if (atomic_fetch_sub(&wbe->ref_count, 1) != 0)
        return;
    writeback_fb_env_free(wbe);
}

static writeback_fb_env_t *
writeback_fb_env_ref(writeback_fb_env_t * const wbe)
{
    atomic_fetch_add(&wbe->ref_count, 1);
    return wbe;
}

static void
writeback_fb_polltask_done(void * v, short revents)
{
    writeback_fb_env_t * wbe = v;

    close(drmu_fb_out_fence_take_fd(wbe->fb));
    if (revents != 0)
        wbe->done_fns.done(wbe->done_v, wbe->fb);
    polltask_delete(&wbe->pt);
}

static void
writeback_fb_commit_cb(void * v, uint64_t value)
{
    writeback_fb_env_t * const wbe = v;
    drmu_env_t * const du = drmu_output_env(wbe->dof->dout);

    wbe->pt = polltask_new(drmu_env_pollqueue(du), (int)value, POLLIN, writeback_fb_polltask_done, wbe);
    pollqueue_add_task(wbe->pt, 1000);
}

static void
writeback_fb_unref_cb(void * v)
{
    writeback_fb_env_t * wbe = v;
    writeback_fb_env_unref(&wbe);
}

static void
writeback_fb_ref_cb(void * v)
{
    writeback_fb_env_t * wbe = v;
    writeback_fb_env_ref(wbe);
}

static void
writeback_done_null_done_cb(void * v, struct drmu_fb_s * fb)
{
    (void)v;
    (void)fb;
}

static void *
writeback_done_null_ref_cb(void * v)
{
    return v;
}

static void
writeback_done_null_unref_cb(void ** ppv)
{
    *ppv = NULL;
}

static int
writeback_next_atomic_cb(drmu_env_t * du, struct drmu_atomic_s ** ppda, void * v)
{
    drmu_writeback_output_t * const dof = v;
    writeback_fb_env_t * const wbe = calloc(1, sizeof(*wbe));
    drmu_atomic_t * da;
    int rv = -ENOMEM;
    static const drmu_writeback_fb_done_fns_t done_fns_null = {
        .done  = writeback_done_null_done_cb,
        .ref   = writeback_done_null_ref_cb,
        .unref = writeback_done_null_unref_cb,
    };

    static const drmu_atomic_prop_fns_t fb_prop_fns = {
        .ref    = writeback_fb_ref_cb,
        .unref  = writeback_fb_unref_cb,
        .commit = writeback_fb_commit_cb,
    };

    if (wbe == NULL)
        return -ENOMEM;

    wbe->dof = drmu_writeback_ref(dof);
    wbe->done_fns = done_fns_null;

    if ((da = drmu_atomic_new(du)) == NULL) {
        drmu_err(du, "Failed to create atomic for queue");
        goto fail;
    }
    // *** POOL!
    if ((wbe->fb = drmu_fb_new_dumb(du, dof->fb_rect.w, dof->fb_rect.h, dof->fmt)) == NULL) {
        drmu_err(du, "Failed to create fb");
        goto fail;
    }
    if ((rv = drmu_atomic_output_add_writeback_fb_rotate(da, dof->dout, wbe->fb, dof->rot)) != 0) {
        drmu_err(du, "Failed to add writeback fb\n");
        goto fail;
    }

    if ((rv = dof->prep_fns.prep(dof->prep_v, wbe->fb, &wbe->done_fns, &wbe->done_v)) != 0) {
        drmu_err(du, "Failed prep");
        goto fail;
    }

    drmu_fb_add_fence_callbacks(wbe->fb, &fb_prop_fns, wbe);

    *ppda = da;
    return 0;

fail:
    *ppda = NULL;
    drmu_atomic_unref(&da);
    writeback_fb_env_free(wbe);
    return rv;
}

//----------------------------------------------------------------------------

static void
writeback_free(drmu_writeback_output_t * const dof)
{
    dof->prep_fns.unref(&dof->prep_v);
    drmu_output_unref(&dof->dout);
    free(dof);
}

void
drmu_writeback_unref(drmu_writeback_output_t ** const ppdof)
{
    drmu_writeback_output_t * const dof = *ppdof;

    if (dof == NULL)
        return;
    *ppdof = NULL;

    if (atomic_fetch_sub(&dof->ref_count, 1) != 0)
        return;

    writeback_free(dof);
}

drmu_writeback_output_t *
drmu_writeback_ref(drmu_writeback_output_t * const dof)
{
    if (dof == NULL)
        return NULL;
    atomic_fetch_add(&dof->ref_count, 1);
    return dof;
}

static int
writeback_prep_null_prep_cb(void * v, struct drmu_fb_s * fb, drmu_writeback_fb_done_fns_t * fns, void ** ppv)
{
    (void)v;
    (void)fb;
    (void)fns;
    (void)ppv;
    return 0;
}

static void *
writeback_prep_null_ref_cb(void * v)
{
    return v;
}

static void
writeback_prep_null_unref_cb(void ** ppv)
{
    *ppv = NULL;
}

drmu_writeback_output_t *
drmu_writeback_output_new(drmu_output_t * const dout, drmu_atomic_q_t * dq,
                          const drmu_writeback_fb_prep_fns_t * prep_fns, void * prep_v)
{
    drmu_writeback_output_t * const dof = calloc(1, sizeof(*dof));
    drmu_env_t * du = drmu_output_env(dout);
    static const drmu_writeback_fb_prep_fns_t prep_fns_null = {
        .prep  = writeback_prep_null_prep_cb,
        .ref   = writeback_prep_null_ref_cb,
        .unref = writeback_prep_null_unref_cb,
    };

    if (prep_fns == NULL)
        prep_fns = &prep_fns_null;

    if (dof == NULL) {
        prep_fns->unref(prep_v);
        return NULL;
    }

    if (dq == NULL)
        dq = drmu_env_queue_default(du);

    dof->req_rect = drmu_rect_wh(1920, 1080);
    dof->fb_rect = dof->req_rect;
    dof->rot = DRMU_ROTATION_0;
    dof->fmt = DRM_FORMAT_ARGB8888;
    dof->dout = drmu_output_ref(dout);
    dof->du = du;
    dof->prep_fns.prep  = prep_fns->prep  ? prep_fns->prep  : prep_fns_null.prep;
    dof->prep_fns.ref   = prep_fns->ref   ? prep_fns->ref   : prep_fns_null.ref;
    dof->prep_fns.unref = prep_fns->unref ? prep_fns->unref : prep_fns_null.unref;
    dof->prep_v = prep_v;

    // Put off till we have callbacks set
    if (du == NULL)
        goto fail;

    if (drmu_output_modeset_allow(dout, true) != 0) {
        drmu_err(du, "Failed to allow modeset");
        goto fail;
    }

    if (drmu_output_add_writeback(dout) != 0) {
        drmu_err(du, "Failed to add writeback");
        goto fail;
    }

    if (drmu_env_queue_next_atomic_fn_set(dq, writeback_next_atomic_cb, dof) != 0) {
        drmu_err(du, "Failed to set writeback queue function");
        goto fail;
    }

    return dof;

fail:
    writeback_free(dof);
    return NULL;
}

// Negotiate?????

int
drmu_writeback_size_set(drmu_writeback_output_t * const dof, const unsigned int w, const unsigned int h)
{
    dof->req_rect = drmu_rect_wh(w, h);
    dof->fb_rect = dof->req_rect;
    return 0;
}

int
drmu_writeback_rotation_set(drmu_writeback_output_t * const dof, const unsigned int req_rot)
{
    unsigned int rot = req_rot;
    if (!drmu_conn_has_rotation(drmu_output_conn(dof->dout, 0), req_rot)) {
        rot = drmu_rotation_is_transposed(req_rot) ?
                DRMU_ROTATION_TRANSPOSE : DRMU_ROTATION_0;
        if (!drmu_conn_has_rotation(drmu_output_conn(dof->dout, 0), rot)) {
            drmu_err(dof->du, "Rotation not supported by connector");
            return -EINVAL;
        }
    }

    dof->req_rot = req_rot;
    dof->rot = rot;
    return 0;
}

// Source rotation required to achieve desired rotation
unsigned int
drmu_writeback_rotation_src(const drmu_writeback_output_t * const dof)
{
    // dof->rot must be transpose if we are here
    return drmu_rotation_suba(dof->req_rot, dof->rot);
}

int
drmu_writeback_fmt_set(drmu_writeback_output_t * const dof, const uint32_t fmt)
{
    dof->fmt = fmt;
    return 0;
}

uint32_t
drmu_writeback_fmt(const drmu_writeback_output_t * const dof)
{
    return dof->fmt;
}

drmu_plane_t *
drmu_writeback_output_fmt_plane(drmu_writeback_output_t * const dof, drmu_output_t * const dest_dout, const unsigned int types)
{
    size_t fmt_count = 1;
    const uint32_t * fmts = drmu_conn_writeback_formats(drmu_output_conn(dof->dout, 0), &fmt_count);

    // This is a simple & stupid search.
    // We expect the 1st format we try to be both "good enough" and compatible with the dest dout
    for (size_t i = 0; i != fmt_count; ++i) {
        const uint32_t fmt = fmts[i];

        // *** Kludge for Pi not supporting this
        // *** We could try a test commit
        if (dof->rot == DRMU_ROTATION_TRANSPOSE && (fmt == DRM_FORMAT_BGR888 || fmt == DRM_FORMAT_RGB888))
            continue;

        drmu_plane_t * const dp = drmu_output_plane_ref_format(dest_dout, types, fmt, 0);

        if (dp != NULL) {
            drmu_info(dof->du, "Format: %s", drmu_log_fourcc(fmts[i]));
            dof->fmt = fmts[i];
            return dp;
        }
    }
    return NULL;
}

//=============================================================================

struct drmu_writeback_env_s {
    atomic_int ref_count;

    drmu_env_t * du;
    drmu_output_t * dout;
    drmu_atomic_q_t * dq;

    atomic_int tag_n;
};

static void
writeback_env_free(drmu_writeback_env_t * const wbe)
{
    drmu_output_unref(&wbe->dout);
    drmu_queue_unref(&wbe->dq);
    drmu_env_unref(&wbe->du);
    free(wbe);
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

    if (wbe == NULL)
        return;
    if (atomic_fetch_sub(&wbe->ref_count, 1) != 0)
        return;

    writeback_env_free(wbe);
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
            drmu_info(wbe->du, "Format: %s", drmu_log_fourcc(fmts[i]));
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

    drmu_env_t * du;
    drmu_fb_t * fb;
    bool done;
    struct pollqueue * pq;
    struct polltask * pt;
    drmu_writeback_fb_done_fn * done_fn;
    void * done_v;
} wbq_ent_t;

static void
wbq_ent_free(wbq_ent_t * const ent)
{
    if (ent == NULL)
        return;

    if (!ent->done)
        ent->done_fn(ent->done_v, NULL);

    drmu_fb_unref(&ent->fb);
    // In normal useg these two should alreasdty be unreffed
    polltask_delete(&ent->pt);
    pollqueue_unref(&ent->pq);
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

    if (ent == NULL)
        return;
    *ppent = NULL;

    if (atomic_fetch_sub(&ent->ref_count, 1) != 0)
        return;

    wbq_ent_free(ent);
}

static void
writeback_fb_ent_polltask_done(void * v, short revents)
{
    wbq_ent_t * ent = v;

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

    if (wbq == NULL)
        return;
    *ppwbq = NULL;

    if (atomic_fetch_sub(&wbq->ref_count, 1) != 0)
        return;

    writeback_fb_free(wbq);
}


int
drmu_writeback_fb_queue(drmu_writeback_fb_t * wbq,
                        const drmu_rect_t dest_rect, const unsigned int rot, const uint32_t fmt,
                        drmu_writeback_fb_done_fn * const done_fn, void * const v,
                        struct drmu_atomic_s ** const ppda)
{
    wbq_ent_t * ent = calloc(1, sizeof(*ent));
    drmu_writeback_env_t * const wbe = wbq->wbe;
    drmu_env_t * const du = wbe->du;
    int rv;

    if (ent == NULL) {
        rv = -ENOMEM;
        goto fail;
    }

    if (drmu_atomic_is_empty(*ppda)) {
        rv = 0;
        goto fail;
    }

    ent->done_fn = done_fn;
    ent->done_v = v;
    ent->pq = pollqueue_ref(drmu_env_pollqueue(du));

#if 1
    if ((ent->fb = drmu_fb_new_dumb(du, dest_rect.w, dest_rect.h, fmt)) == NULL) {
        drmu_err(du, "Failed to create fb");
        rv = -ENOMEM;
        goto fail;
    }
#else
    if ((ent->fb = drmu_pool_fb_new(wbq->pool, dest_rect.w, dest_rect.h, fmt, 0)) == NULL) {
        drmu_err(du, "Failed to create fb");
        rv = -ENOMEM;
        goto fail;
    }
#endif

    rv = drmu_atomic_output_add_writeback_fb_callback(*ppda, wbe->dout, ent->fb, rot, writeback_fb_ent_commit_cb, ent);
    ent = NULL; // Ownership taken by call
    if (rv != 0) {
        drmu_err(du, "Failed to add writeback fb\n");
        goto fail;
    }

    if ((rv = drmu_queue_queue_tagged(wbe->dq, wbq->q_tag, wbq->q_merge, ppda)) != 0) {
        drmu_err(du, "Failed merge");
        goto fail;
    }

    return 0;

fail:
    drmu_atomic_unref(ppda);
    wbq_ent_unref(&ent);
    return rv;
}

unsigned int
drmu_writeback_fb_queue_rotation(const drmu_writeback_fb_t * const wbq, const unsigned int req_rot)
{
    unsigned int rot = req_rot;
    drmu_writeback_env_t * const wbe = wbq->wbe;
    drmu_env_t * const du = wbe->du;
    drmu_conn_t * const conn = drmu_output_conn(wbe->dout, 0);

    if (!drmu_conn_has_rotation(conn, req_rot)) {
        rot = drmu_rotation_is_transposed(req_rot) ?
                DRMU_ROTATION_TRANSPOSE : DRMU_ROTATION_0;
        if (!drmu_conn_has_rotation(conn, rot)) {
            drmu_err(du, "Rotation not supported by connector");
            return DRMU_ROTATION_0;
        }
    }

    return rot;
}

drmu_rect_t
drmu_writeback_fb_queue_rect(const drmu_writeback_fb_t * const wbq, const drmu_rect_t dest_rect)
{
    drmu_rect_t r = dest_rect;
    (void)wbq;

    // Pi has a max input width -> output height
    if (r.h > 1920)
        r.h = 1920;

    r.x = 0;
    r.y = 0;
    return r;
}

