#include "runticker.h"

#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdlib.h>
#include <unistd.h>

#include <sys/eventfd.h>

#include <drmu.h>
#include <drmu_log.h>
#include <drmu_output.h>

#include "ticker.h"

struct runticker_env_s {
    atomic_int kill;
    ticker_env_t *te;
    char *text;
    const char *cchar;
    int prod_fd;
    bool thread_running;
    pthread_t thread_id;
};

static int
next_char_cb(void *v)
{
    runticker_env_t *const dfte = v;

    if (*dfte->cchar == 0)
        dfte->cchar = dfte->text;
    return *dfte->cchar++;
}

static void
do_prod(void *v)
{
    static const uint64_t one = 1;
    runticker_env_t *const dfte = v;
    write(dfte->prod_fd, &one, sizeof(one));
}

static void *
runticker_thread(void * v)
{
    runticker_env_t * const dfte = v;

    while (!atomic_load(&dfte->kill) && ticker_run(dfte->te) >= 0) {
        char evt_buf[8];
        read(dfte->prod_fd, evt_buf, 8);
    }

    return NULL;
}

runticker_env_t *
runticker_start(drmu_output_t * const dout,
                unsigned int x, unsigned int y, unsigned int w, unsigned int h,
                const char * const text,
                const char * const fontfile)
{
    runticker_env_t *dfte = calloc(1, sizeof(*dfte));
    drmu_env_t * const du = drmu_output_env(dout);

    if (dfte == NULL)
        return NULL;

    dfte->prod_fd = -1;
    dfte->text  = strdup(text);
    dfte->cchar = dfte->text;

    if ((dfte->te = ticker_new(dout, x, y, w, h)) == NULL) {
        drmu_err(du, "Failed to create ticker");
        goto fail;
    }

    if (ticker_set_face(dfte->te, fontfile) != 0) {
        drmu_err(du, "Failed to set face\n");
        goto fail;
    }

    ticker_next_char_cb_set(dfte->te, next_char_cb, dfte);

    if ((dfte->prod_fd = eventfd(0, 0)) == -1) {
        drmu_err(du, "Failed to get event fd");
        goto fail;
    }
    ticker_commit_cb_set(dfte->te, do_prod, dfte);

    if (ticker_init(dfte->te) != 0) {
        drmu_err(du, "Failed to init ticker");
        goto fail;
    }

    if (pthread_create(&dfte->thread_id, NULL, runticker_thread, dfte) != 0) {
        drmu_err(du, "Failed to create thread");
        goto fail;
    }
    dfte->thread_running = true;

    return dfte;

fail:
    runticker_stop(&dfte);
    return NULL;
}

void
runticker_stop(runticker_env_t ** const ppDfte)
{
    runticker_env_t * const dfte = *ppDfte;
    if (dfte == NULL)
        return;
    *ppDfte = NULL;

    if (dfte->thread_running) {
        atomic_store(&dfte->kill, 1);
        do_prod(dfte);
        pthread_join(dfte->thread_id, NULL);
    }

    ticker_delete(&dfte->te);
    if (dfte->prod_fd != -1)
        close(dfte->prod_fd);
    free(dfte->text);
    free(dfte);
}

