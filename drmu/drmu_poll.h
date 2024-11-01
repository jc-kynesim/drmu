#ifndef _DRMU_DRMU_POLL_H
#define _DRMU_DRMU_POLL_H

#ifdef __cplusplus
extern "C" {
#endif

struct drmu_env_s;
struct drmu_atomic_s;

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

#ifdef __cplusplus
}
#endif
#endif

