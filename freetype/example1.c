/* Simple example program that uses ticker to scroll a message across the
 * screen
 */

#include <stdio.h>

#include <drmu.h>
#include <drmu_log.h>
#include <drmu_output.h>
#include <drmu_scan.h>

#include "runticker.h"

static void
drmu_log_stderr_cb(void *v, enum drmu_log_level_e level, const char *fmt, va_list vl)
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

int
main(int argc, char **argv)
{
    drmu_env_t *du;
    drmu_output_t *dout;
    runticker_env_t * rte;
    const drmu_mode_simple_params_t * mode;

    const char *const device = NULL;

    static const drmu_log_env_t log = {
        .fn = drmu_log_stderr_cb,
        .v = NULL,
        .max_level = DRMU_LOG_LEVEL_INFO
    };

    if (argc != 3) {
        fprintf(stderr, "usage: %s font sample-text\n", argv[0]);
        exit(1);
    }

    if (drmu_scan_output(device, &log, &du, &dout) != 0) {
        fprintf(stderr, "Failed drmu scan for device\n");
        return 1;
    }
    drmu_env_restore_enable(du);

    mode = drmu_output_mode_simple_params(dout);

    if ((rte = runticker_start(dout,
                               mode->width / 10, mode->height * 8/10, mode->width * 8/10, mode->height / 10,
                               argv[2], argv[1])) == NULL) {
        fprintf(stderr, "Failed to create ticker\n");
        return 1;
    }
    drmu_output_unref(&dout); // Ticker keeps a ref

    getchar();

    runticker_stop(&rte);
    return 0;
}
