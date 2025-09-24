#include "drmu_writeback.h"

#include <drm_fourcc.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"
#include "pollqueue.h"

//*****
#include <stdio.h>

struct drmu_writeback_output_s
{
    atomic_int ref_count;

    drmu_rect_t req_rect;
    drmu_rect_t fb_rect;
    unsigned int rot;
    unsigned int req_rot;
    uint32_t fmt;

    unsigned int qno;
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
    printf("%s %p\n", __func__, (void*)wbe);

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
    printf("%s %p\n", __func__, (void*)wbe);
    atomic_fetch_add(&wbe->ref_count, 1);
    return wbe;
}

static void
writeback_fb_polltask_done(void * v, short revents)
{
    writeback_fb_env_t * wbe = v;
    printf("%s %p\n", __func__, (void*)wbe);

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
    printf("%s %p\n", __func__, (void*)wbe);

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
drmu_writeback_output_new(drmu_output_t * const dout, const unsigned int qno,
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

    dof->req_rect = drmu_rect_wh(1920, 1080);
    dof->fb_rect = dof->req_rect;
    dof->rot = DRMU_ROTATION_0;
    dof->fmt = DRM_FORMAT_ARGB8888;
    dof->qno = qno;
    dof->dout = drmu_output_ref(dout);
    dof->du = drmu_output_env(dout);
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

    if (drmu_env_queue_next_atomic_fn_set(du, qno, writeback_next_atomic_cb, dof) != 0) {
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
        if (!drmu_conn_has_rotation(drmu_output_conn(dof->dout, 0), req_rot)) {
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

