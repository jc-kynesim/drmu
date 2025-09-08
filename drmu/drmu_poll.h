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
int drmu_atomic_queue_qno(struct drmu_atomic_s ** ppda, const unsigned int qno);
// drmu_atomic_queue_qno(ppda, 0)
int drmu_atomic_queue(struct drmu_atomic_s ** ppda);
// Wait for there to be no pending commit (there may be a commit in
// progress)
int drmu_env_queue_wait_qno(struct drmu_env_s * const du, const unsigned int qno);
// drmu_env_queue_wait_qno(du, 0)
int drmu_env_queue_wait(struct drmu_env_s * const du);


typedef int (*drmu_queue_next_atomic_fn)(struct drmu_env_s * du, struct drmu_atomic_s ** ppda, void * v);
int drmu_env_queue_next_atomic_fn_set(struct drmu_env_s * const du, const unsigned int qno, const drmu_queue_next_atomic_fn fn, void * const v);

#ifdef __cplusplus
}
#endif
#endif

