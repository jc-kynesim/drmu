#ifndef _DRMU_DRMU_POLL_H
#define _DRMU_DRMU_POLL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_atomic_s;
struct drmu_crtc_s;
struct drmu_env_s;

// Q the atomic on its associated env
//
// in-progress = The commit has been done but no ack yet
// pending     = Commit Qed to be done when the in-progress commit has
//               completed
//
// If no in-progress commit then this will be committed immediately
// otherwise it becomes the pending commit
// If there is a pending commit this atomic will be merged with it
// Commits are done with the PAGE_FLIP flag set so we expect the ack
// on the next page flip.
int drmu_atomic_queue(struct drmu_atomic_s ** ppda);
// Wait for there to be no pending commit (there may be a commit in
// progress)
int drmu_env_queue_wait(struct drmu_env_s * const du);

// Queueing mode

typedef enum drmu_env_queue_mode_s {
        DRMU_ENV_QUEUE_MODE_INVALID = -1,
        DRMU_ENV_QUEUE_MODE_ASAP = 0,
        DRMU_ENV_QUEUE_MODE_VSYNC,
        DRMU_ENV_QUEUE_MODE_OFFSET_VSYNC,
} drmu_env_queue_mode_t;

int drmu_env_queue_mode_set(struct drmu_env_s * const du, const drmu_env_queue_mode_t mode,
                        const struct drmu_crtc_s * const dc, const int offset_us);

// Possibly useful stats for debugging jerky video

typedef struct drmu_env_queue_stats_s {
    uint32_t crtc_id;
    // Sequence numbers increment on every vsync
    uint32_t sequence_first;
    uint32_t sequence_last;
    // Commit flips seen - beware that you need (flip_count - 1) for
    // calculating frame rates and missed vsyncs
    unsigned int flip_count;
    // atomic_queue ops done- beware that this is a simple count of
    // drmu_atomic_queue on this env so may not be useful if multiple
    // threads queue stuff
    unsigned int queue_count;
    // count of times a queue request is merged with an existing one
    unsigned int merge_count;
    // Times using CLOCK_MONOTONIC in micro seconds
    uint64_t time_us_first;
    uint64_t time_us_last;
} drmu_env_queue_stats_t;

// Retrieve stats
// If stats_buf == NULL then nothing written - stats may still be reset
// stats_buf_len should be sizeof(*stats_buf)
// If (reset) then stats are reset after retrieval
void drmu_env_queue_stats_get(struct drmu_env_s * const du,
                              drmu_env_queue_stats_t * const stats_buf,
                              const size_t stats_buf_len, const bool reset);

#ifdef __cplusplus
}
#endif
#endif

