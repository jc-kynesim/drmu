#include "runcube.h"

#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>

#include "common.h"
#include "drm-common.h"

#include <drmu_output.h>

struct runcube_env_s {
    atomic_int kill;
    struct drmu_output_s * dout;
    bool thread_ok;
    pthread_t thread_id;
};

static void *
cube_thread(void * v)
{
    runcube_env_t * const rce = v;
    struct drmu_output_s * const dout = rce->dout;
    uint32_t format = DRM_FORMAT_ARGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    const struct egl *egl;
    const struct gbm *gbm;
    struct drm *drm;

    drm = init_drmu_dout(dout, 1000, format);
    gbm = init_gbm_drmu(drm->du, drm->mode->hdisplay, drm->mode->vdisplay, format, modifier);
    egl = init_cube_smooth(gbm, 0);

    while (!atomic_load(&rce->kill)) {
        cube_run_drmu(drm, gbm, egl);
    }

    // ***** Should have cleanup here but cube demo doesn't bother

    return NULL;
}

runcube_env_t *
runcube_drmu_start(struct drmu_output_s * const dout)
{
    runcube_env_t * rce = calloc(1, sizeof(*rce));

    if (rce == NULL)
        return NULL;

    rce->dout = drmu_output_ref(dout);
    if (pthread_create(&rce->thread_id, NULL, cube_thread, rce) != 0)
        goto fail;
    rce->thread_ok = true;

    return rce;

fail:
    runcube_drmu_stop(&rce);
    return NULL;
}

void
runcube_drmu_stop(runcube_env_t ** const ppRce)
{
    runcube_env_t * const rce = *ppRce;
    if (rce == NULL)
        return;
    *ppRce = NULL;

    if (rce->thread_ok) {
        atomic_store(&rce->kill, 1);
        pthread_join(rce->thread_id, NULL);
    }

    drmu_output_unref(&rce->dout);
    free(rce);
}

