#ifndef _DRMU_DRMU_PIPE_H
#define _DRMU_DRMU_PIPE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Color op value element - values only not linked to the env
// Construct new pipes out of these and then set on a pipe
struct drmu_colorop_s;
typedef struct drmu_colorop_s drmu_colorop_t;

struct drmu_colorop_seq_s;
typedef struct drmu_colorop_seq_s drmu_colorop_seq_t;


struct drmu_pipe_s;
typedef struct drmu_pipe_s drmu_pipe_t;

// Color op pipe element - linked to the env - no values
// Gives types and constraints of the actual color pipe
struct drmu_pipe_colorop_s;
typedef struct drmu_pipe_colorop_s drmu_pipe_colorop_t;

struct drmu_env_s;

typedef enum drmu_colorop_type_e {
    DRMU_COLOROP_TYPE_UNKNOWN = 0,
    DRMU_COLOROP_TYPE_1D_CURVE,
    DRMU_COLOROP_TYPE_1D_LUT,
    DRMU_COLOROP_TYPE_3X4_MATRIX,
    DRMU_COLOROP_TYPE_MULTIPLIER,
    DRMU_COLOROP_TYPE_3D_LUT,
    DRMU_COLOROP_TYPE_COUNT   // Must be last
} drmu_colorop_type_t;

// Value functions
drmu_colorop_t * drmu_colorop_new(drmu_colorop_type_t type);
void drmu_colorop_unref(drmu_colorop_t ** const ppDco);
drmu_colorop_t * drmu_colorop_ref(drmu_colorop_t * const dco);

drmu_colorop_type_t drmu_colorop_type(const drmu_colorop_t * const dco);
void drmu_colorop_bypass_set(drmu_colorop_t * const dco, const bool bypass);

drmu_colorop_seq_t * drmu_colorop_seq_new();
void drmu_colorop_seq_unref(drmu_colorop_seq_t ** const ppCs);
drmu_colorop_seq_t * drmu_colorop_seq_ref(drmu_colorop_seq_t * const cs);

int drmu_colorop_seq_add_tail(drmu_colorop_seq_t * const cs, drmu_colorop_t * const dco);
drmu_colorop_t * drmu_colorop_seq_n(drmu_colorop_seq_t * const cs, const unsigned int n);

// Construct a pipe from its head pid
drmu_pipe_t * drmu_pipe_new_pid(struct drmu_env_s * const du, const uint32_t pid);
void drmu_pipe_unref(drmu_pipe_t ** ppPipe);
drmu_pipe_t * drmu_pipe_ref(drmu_pipe_t * pipe);

const drmu_pipe_colorop_t * drmu_pipe_colorop_n(const drmu_pipe_t * const pipe, unsigned int n);

#ifdef __cplusplus
}
#endif

#endif
