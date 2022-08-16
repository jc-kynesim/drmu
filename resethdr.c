#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <memory.h>

#include "drmu.h"
#include "drmu_output.h"
#include "drmu_log.h"
#include <drm_fourcc.h>

static void
drmu_log_stderr_cb(void * v, enum drmu_log_level_e level, const char * fmt, va_list vl)
{
    char buf[256];
    int n = vsnprintf(buf, 255, fmt, vl);

    (void)v;
    (void)level;

    if (n >= 255)
        n = 255;
    buf[n] = '\n';
    fwrite(buf, n + 1, 1, stderr);
}

#define DRM_MODULE "vc4"
int main(int argc, char *argv[])
{
    drmu_env_t * du = NULL;
    drmu_output_t * dout = NULL;
    drmu_conn_t * dn = NULL;
    drmu_atomic_t * da = NULL;
    int rv;

    const char * colorspace = DRMU_COLORSPACE_DEFAULT;

    if (argc >= 3 && strcmp(argv[1], "-c") == 0)
        colorspace = argv[2];

    {
        const drmu_log_env_t log = {
            .fn = drmu_log_stderr_cb,
            .v = NULL,
            .max_level = DRMU_LOG_LEVEL_ALL
        };
        if ((du = drmu_env_new_xlease(&log)) == NULL &&
            (du = drmu_env_new_open(DRM_MODULE, &log)) == NULL)
            goto fail;
    }

    if ((dout = drmu_output_new(du)) == NULL)
        goto fail;

    drmu_output_max_bpc_allow(dout, true);
    drmu_output_modeset_allow(dout, true);

    if (drmu_output_add_output(dout, NULL) != 0)
        goto fail;
    dn = drmu_output_conn(dout, 0);

    if ((da = drmu_atomic_new(du)) == NULL)
        goto fail;

    if (drmu_atomic_conn_add_hdr_metadata(da, dn, NULL))
        goto fail;
    if (drmu_atomic_conn_add_colorspace(da, dn, colorspace))
        fprintf(stderr, "Failed to set colorspace '%s'\n", colorspace);
    if (drmu_atomic_conn_add_hi_bpc(da, dn, false))
        fprintf(stderr, "Failed to reset hi bpc\n");

    if ((rv = drmu_atomic_commit(da, DRM_MODE_ATOMIC_ALLOW_MODESET)) != 0)
        fprintf(stderr, "Failed to commit modechange: %s\n", strerror(-rv));

    printf("Set colorspace '%s'\n", colorspace);

fail:
    drmu_atomic_unref(&da);
    drmu_output_unref(&dout);
    drmu_env_unref(&du);
    return 0;
}

