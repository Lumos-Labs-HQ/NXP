/*
 * NXP Platform Threading Abstraction
 *
 * Threads, mutexes, and condition variables.
 */
#ifndef NXP_PLATFORM_THREAD_H
#define NXP_PLATFORM_THREAD_H

#include <stdbool.h>
#include <stdint.h>
#include "nxp/nxp_error.h"

/* Opaque thread handle */
typedef struct nxp_thread nxp_thread;

/* Thread function signature */
typedef void (*nxp_thread_fn)(void *arg);

/* Thread operations */
[[nodiscard]] nxp_result nxp_thread_create(nxp_thread **out, nxp_thread_fn fn, void *arg);
void nxp_thread_join(nxp_thread *t);
void nxp_thread_destroy(nxp_thread *t);

/* Mutex */
typedef struct nxp_mutex nxp_mutex;

[[nodiscard]] nxp_mutex *nxp_mutex_create(void);
void nxp_mutex_destroy(nxp_mutex *m);
void nxp_mutex_lock(nxp_mutex *m);
void nxp_mutex_unlock(nxp_mutex *m);
[[nodiscard]] bool nxp_mutex_trylock(nxp_mutex *m);

/* Condition variable */
typedef struct nxp_condvar nxp_condvar;

[[nodiscard]] nxp_condvar *nxp_condvar_create(void);
void nxp_condvar_destroy(nxp_condvar *cv);
void nxp_condvar_wait(nxp_condvar *cv, nxp_mutex *m);
[[nodiscard]] bool nxp_condvar_wait_timeout(nxp_condvar *cv, nxp_mutex *m, uint64_t us);
void nxp_condvar_signal(nxp_condvar *cv);
void nxp_condvar_broadcast(nxp_condvar *cv);

#endif /* NXP_PLATFORM_THREAD_H */
