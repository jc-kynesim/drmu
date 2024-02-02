#include "drmu_scan.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>

#include "drmu.h"
#include "drmu_log.h"
#include "drmu_output.h"

#define CARD_MAX 16


int
drmu_scan_output(const char * const cname, const drmu_log_env_t * const dlog,
                 drmu_env_t ** const pDu, drmu_output_t ** const pDoutput)
{
    static const char * card_prefix = "/dev/dri/card";
    unsigned int i;

    *pDu = NULL;
    *pDoutput = NULL;

    for (i = 0; i != CARD_MAX; ++i) {
        drmu_env_t * du;
        drmu_output_t * dout = NULL;
        char fname[32];
        int fd;

        drmu_debug_log(dlog, "Try card %d", i);

        sprintf(fname, "%s%d", card_prefix, i);
        while ((fd = open(fname, O_RDWR | O_CLOEXEC)) == -1 && errno == EINTR)
            /* Loop */;
        if (fd == -1) {
            if (errno == ENOENT)
                break;
            continue;
        }

        // Have FD
        if ((du = drmu_env_new_fd(fd, dlog)) == NULL)
            continue;

        if ((dout = drmu_output_new(du)) == NULL)
            goto loop1;

        if (drmu_output_add_output(dout, cname) == 0) {
            *pDu = du;
            *pDoutput = dout;
            return 0;
        }

        drmu_output_unref(&dout);
loop1:
        drmu_env_unref(&du);
    }
    return -ENOENT;
}

