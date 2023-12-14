#ifndef POLLQUEUE_H_
#define POLLQUEUE_H_

#include <poll.h>

struct polltask;
struct pollqueue;

// Max number of tasks that can be Qed
#define POLLQUEUE_MAX_QUEUE 128

// Create a new polltask
// Holds a reference on the pollqueue until the polltask is deleted
//
// pq       pollqueue this task belongs to
// fd       fd to poll
// events   Events to wait for (POLLxxx)
// revents  Event that triggered the callback
//          0 => timeout
// v        User pointer to callback
struct polltask *polltask_new(struct pollqueue *const pq,
                              const int fd, const short events,
                              void (*const fn)(void *v, short revents),
                              void *const v);
// polltask suitable for timing (i.e. has no trigger event)
struct polltask *polltask_new_timer(struct pollqueue *const pq,
                              void (*const fn)(void *v, short revents),
                              void *const v);

// deletes the task
// Safe to call if *ppt == NULL
// It is safe to call whilst a polltask is queued (and may be triggered)
// Callback may occur whilst this is in progress but will not occur
// once it is done. (*ppt is nulled only once the callback can not occur)
// May be called in a polltask callback
// If called from outside the polltask thread and this causes the pollqueue
// to be deleted then it will wait for the polltask thread to terminate
// before returning.
void polltask_delete(struct polltask **const ppt);

// Queue a polltask
// timeout_ms == -1 => never
// May be called from the polltask callback
// May only be added once (currently)
void pollqueue_add_task(struct polltask *const pt, const int timeout);

// Run a callback once on the poll thread
int pollqueue_callback_once(struct pollqueue *const pq,
                            void (*const fn)(void *v, short revents),
                            void *const v);

// Create a pollqueue
// Generates a new thread to do the polltask callbacks
struct pollqueue * pollqueue_new(void);

// Unref a pollqueue
// Will be deleted once all polltasks (Qed or otherwise) are deleted too
// Will not wait for polltask termination whether or not this is the last
// ref.
void pollqueue_unref(struct pollqueue **const ppq);

// Unrefs a pollqueue and then waits for the polltask thread to terminate
// before returning.
void pollqueue_finish(struct pollqueue **const ppq);

// Add a reference to a pollqueue
struct pollqueue * pollqueue_ref(struct pollqueue *const pq);

// Set pre & post poll functions
// Both set s.t. there is always matched pre - poll - post even when
// changing the fns.
// pollfd will be added to polls if *pfd set (leave unset if not wanted)
// One or both of pre/post may be 0 (uncalled)
// Will wait if needed until the poll thread is not in the old pre/poll/post
// sequence
void pollqueue_set_pre_post(struct pollqueue *const pq,
                            void (*fn_pre)(void *v, struct pollfd *pfd),
                            void (*fn_post)(void *v, short revents),
                            void *v);

#endif /* POLLQUEUE_H_ */
