#ifndef _DRMU_DRMU_POLL_H
#define _DRMU_DRMU_POLL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_env_s;
struct drmu_atomic_s;

struct drmu_atomic_q_s;
typedef struct drmu_atomic_q_s drmu_atomic_q_t;

drmu_atomic_q_t * drmu_queue_new(struct drmu_env_s * const du);
drmu_atomic_q_t * drmu_queue_ref(drmu_atomic_q_t * const dq);
void drmu_queue_unref(drmu_atomic_q_t ** const ppdq);

typedef enum drmu_queue_merge_e {
    DRMU_QUEUE_MERGE_MERGE,   // Merge with last existing (default)
    DRMU_QUEUE_MERGE_DROP,    // Drop new request
    DRMU_QUEUE_MERGE_REPLACE, // Replace previous request
    DRMU_QUEUE_MERGE_QUEUE,   // Queue both
} drmu_queue_merge_t;

// Q the atomic on its associated env in the given q
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
int drmu_queue_queue_tagged(drmu_atomic_q_t * const aq,
                            const unsigned int tag, const drmu_queue_merge_t qmerge,
                            struct drmu_atomic_s ** ppda);
static inline int
drmu_queue_queue(drmu_atomic_q_t * const aq, struct drmu_atomic_s ** const ppda)
{
    return drmu_queue_queue_tagged(aq, 0, DRMU_QUEUE_MERGE_MERGE, ppda);
}

// Default Q
drmu_atomic_q_t * drmu_env_queue_default(struct drmu_env_s * const du);

// drmu_atomic_queue_qno(ppda, 0)
int drmu_atomic_queue(struct drmu_atomic_s ** ppda);

// Wait for there to be no pending commit (there may be a commit in
// progress)
int drmu_queue_wait(drmu_atomic_q_t * const aq);

// Wait on default q
int drmu_env_queue_wait(struct drmu_env_s * const du);

// Keep a ref on the previous atomics until overwren by new atomics
// Needed for most displays, not needed for writeback
// Default queue keep_last state is true
void drmu_queue_keep_last_set(drmu_atomic_q_t * const aq, const bool keep_last);

struct pollqueue;
struct pollqueue * drmu_env_pollqueue(struct drmu_env_s * const du);

#ifdef __cplusplus
}
#endif
#endif

