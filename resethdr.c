#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>

#include "drmu.h"
#include "drmu_log.h"
#include <drm_fourcc.h>

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);
    if (n >= 255)
        n = 255;
    buf[n] = '\n';
    fwrite(buf, n + 1, 1, stderr);
}

#define DRM_MODULE "vc4"
int main(int argc, char *argv[])
{
    drmu_env_t * du = NULL;
    drmu_crtc_t * dc = NULL;
    drmu_atomic_t * da = NULL;

    const char * colorspace = DRMU_CRTC_COLORSPACE_DEFAULT;

    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
        colorspace = argv[2];

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_INFO
        };
        if ((du = drmu_env_new_xlease(&log)) == NULL &&
            (du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    drmu_env_modeset_allow(du, true);

    if ((dc = drmu_crtc_new_find(du)) == NULL)
        goto fail;

    if ((da = drmu_atomic_new(du)) == NULL)
        goto fail;

    if (drmu_atomic_crtc_hdr_metadata_set(da, dc, NULL))
        goto fail;
    if (drmu_atomic_crtc_colorspace_set(da, dc, colorspace, false))
        fprintf(stderr, "Failed to set colorspace '%s'\n", colorspace);

    if (drmu_atomic_commit(da, 0) != 0)
        fprintf(stderr, "Failed to commit modechange\n");

    printf("Set colorspace '%s'\n", colorspace);

fail:
    drmu_atomic_unref(&da);
    drmu_crtc_delete(&dc);
    drmu_env_delete(&du);
    return 0;
}

