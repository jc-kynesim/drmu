#ifndef _DRMU_DRMU_PIPE_H
#define _DRMU_DRMU_PIPE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_colorop_s;
typedef struct drmu_colorop_s drmu_colorop_t;

uint32_t drmu_colorop_next_id(const drmu_colorop_t * const cop);

struct drmu_pipe_s;
typedef struct drmu_pipe_s drmu_pipe_t;

struct drmu_env_s;

// Construct a pipe from its head pid
drmu_pipe_t * drmu_pipe_new_pid(struct drmu_env_s * const du, const uint32_t pid);
void drmu_pipe_unref(drmu_pipe_t ** ppPipe);
drmu_pipe_t * drmu_pipe_ref(drmu_pipe_t * pipe);

drmu_colorop_t * drmu_pipe_colorop_n(const drmu_pipe_t * const pipe, unsigned int n);

#ifdef __cplusplus
}
#endif

#endif
