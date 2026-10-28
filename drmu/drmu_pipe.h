#ifndef _DRMU_DRMU_PIPE_H
#define _DRMU_DRMU_PIPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_pipe_s;
typedef struct drmu_pipe_s drmu_pipe_t;

struct drmu_env_s;

// Construct a pipe from its head pid
drmu_pipe_t * drmu_pipe_new_pid(struct drmu_env_s * const du, const uint32_t pid);

void drmu_pipe_unref(drmu_pipe_t ** ppPipe);
drmu_pipe_t * drmu_pipe_ref(drmu_pipe_t * pipe);

#ifdef __cplusplus
}
#endif

#endif
