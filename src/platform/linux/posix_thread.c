/*
 * NXP Linux Threading - pthreads
 */
#ifndef _WIN32

#include "../platform_thread.h"
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include <time.h>

/* ── Thread ─────────────────────────────────────────────── */

struct nxp_thread {
    pthread_t     handle;
    nxp_thread_fn fn;
    void         *arg;
};

static void *thread_entry(void *param) {
    nxp_thread *t = (nxp_thread *)param;
    t->fn(t->arg);
    return nullptr;
}

nxp_result nxp_thread_create(nxp_thread **out, nxp_thread_fn fn, void *arg) {
    nxp_thread *t = (nxp_thread *)malloc(sizeof(nxp_thread));
    if (t == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    t->fn  = fn;
    t->arg = arg;

    if (pthread_create(&t->handle, nullptr, thread_entry, t) != 0) {
        free(t);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    *out = t;
    return NXP_SUCCESS;
}

void nxp_thread_join(nxp_thread *t) {
    if (t == nullptr) return;
    pthread_join(t->handle, nullptr);
}

void nxp_thread_destroy(nxp_thread *t) {
    free(t);
}

/* ── Mutex ──────────────────────────────────────────────── */

struct nxp_mutex {
    pthread_mutex_t lock;
};

nxp_mutex *nxp_mutex_create(void) {
    nxp_mutex *m = (nxp_mutex *)malloc(sizeof(nxp_mutex));
    if (m == nullptr) return nullptr;
    pthread_mutex_init(&m->lock, nullptr);
    return m;
}

void nxp_mutex_destroy(nxp_mutex *m) {
    if (m == nullptr) return;
    pthread_mutex_destroy(&m->lock);
    free(m);
}

void nxp_mutex_lock(nxp_mutex *m) {
    pthread_mutex_lock(&m->lock);
}

void nxp_mutex_unlock(nxp_mutex *m) {
    pthread_mutex_unlock(&m->lock);
}

bool nxp_mutex_trylock(nxp_mutex *m) {
    return pthread_mutex_trylock(&m->lock) == 0;
}

/* ── Condition Variable ─────────────────────────────────── */

struct nxp_condvar {
    pthread_cond_t cv;
};

nxp_condvar *nxp_condvar_create(void) {
    nxp_condvar *cv = (nxp_condvar *)malloc(sizeof(nxp_condvar));
    if (cv == nullptr) return nullptr;
    pthread_cond_init(&cv->cv, nullptr);
    return cv;
}

void nxp_condvar_destroy(nxp_condvar *cv) {
    if (cv == nullptr) return;
    pthread_cond_destroy(&cv->cv);
    free(cv);
}

void nxp_condvar_wait(nxp_condvar *cv, nxp_mutex *m) {
    pthread_cond_wait(&cv->cv, &m->lock);
}

bool nxp_condvar_wait_timeout(nxp_condvar *cv, nxp_mutex *m, uint64_t us) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += (time_t)(us / 1000000ULL);
    ts.tv_nsec += (long)((us % 1000000ULL) * 1000ULL);
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000L;
    }
    return pthread_cond_timedwait(&cv->cv, &m->lock, &ts) == 0;
}

void nxp_condvar_signal(nxp_condvar *cv) {
    pthread_cond_signal(&cv->cv);
}

void nxp_condvar_broadcast(nxp_condvar *cv) {
    pthread_cond_broadcast(&cv->cv);
}

#endif /* !_WIN32 */
