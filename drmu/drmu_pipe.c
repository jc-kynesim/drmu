#include "drmu_pipe.h"

#include "drmu.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

struct drmu_pipe_s {
    atomic_int ref_count;  // 0 == 1 ref for ease of init
    drmu_env_t * du;
};

static void
pipe_free(drmu_pipe_t * const pipe)
{
    free(pipe);
}

void
drmu_pipe_unref(drmu_pipe_t ** const ppPipe)
{
    drmu_pipe_t * const pipe = *ppPipe;

    if (pipe == NULL)
        return;
    *ppPipe = NULL;

    if (atomic_fetch_sub(&pipe->ref_count, 1) != 0)
        return;

    pipe_free(pipe);
}

drmu_pipe_t *
drmu_pipe_ref(drmu_pipe_t * const pipe)
{
    if (pipe != NULL)
        atomic_fetch_add(&pipe->ref_count, 1);
    return pipe;
}

drmu_pipe_t *
drmu_pipe_new_pid(struct drmu_env_s * const du, const uint32_t pid)
{
    drmu_pipe_t * const pipe = calloc(1, sizeof(*pipe));
    (void)pid;

    if (pipe == NULL)
        return NULL;

    atomic_init(&pipe->ref_count, 0);
    pipe->du = du;

    return pipe;
}
