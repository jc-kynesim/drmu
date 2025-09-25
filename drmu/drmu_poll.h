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
int drmu_queue_queue(drmu_atomic_q_t * const aq, struct drmu_atomic_s ** ppda);

// Default Q
drmu_atomic_q_t * drmu_env_queue_default(struct drmu_env_s * const du);

// drmu_atomic_queue_qno(ppda, 0)
int drmu_atomic_queue(struct drmu_atomic_s ** ppda);
// Wait for there to be no pending commit (there may be a commit in
// progress)
int drmu_env_queue_wait_qno(struct drmu_env_s * const du, const unsigned int qno);
// drmu_env_queue_wait_qno(du, 0)
int drmu_env_queue_wait(struct drmu_env_s * const du);


typedef int (*drmu_queue_next_atomic_fn)(struct drmu_env_s * du, struct drmu_atomic_s ** ppda, void * v);
int drmu_env_queue_next_atomic_fn_set(drmu_atomic_q_t * const aq, const drmu_queue_next_atomic_fn fn, void * const v);

// Set Q queue behaviour
// do_merge == true   New commits merged with next uncommitted [default]
// do_merge == false  New commits queued separately
int drmu_env_queue_next_merge_set(drmu_atomic_q_t * const dq, const bool do_merge);

struct pollqueue;
struct pollqueue * drmu_env_pollqueue(struct drmu_env_s * const du);

#ifdef __cplusplus
}
#endif
#endif

