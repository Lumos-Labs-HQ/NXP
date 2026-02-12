/*
 * NXP Windows Threading - CreateThread, SRWLOCK, CONDITION_VARIABLE
 */
#ifdef _WIN32

#include "../platform_thread.h"
#include <windows.h>
#include <stdlib.h>

/* ── Thread ─────────────────────────────────────────────── */

struct nxp_thread {
    HANDLE       handle;
    nxp_thread_fn fn;
    void         *arg;
};

static DWORD WINAPI thread_entry(LPVOID param) {
    nxp_thread *t = (nxp_thread *)param;
    t->fn(t->arg);
    return 0;
}

nxp_result nxp_thread_create(nxp_thread **out, nxp_thread_fn fn, void *arg) {
    nxp_thread *t = (nxp_thread *)malloc(sizeof(nxp_thread));
    if (t == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    t->fn  = fn;
    t->arg = arg;
    t->handle = CreateThread(nullptr, 0, thread_entry, t, 0, nullptr);

    if (t->handle == nullptr) {
        free(t);
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    *out = t;
    return NXP_SUCCESS;
}

void nxp_thread_join(nxp_thread *t) {
    if (t == nullptr) return;
    WaitForSingleObject(t->handle, INFINITE);
}

void nxp_thread_destroy(nxp_thread *t) {
    if (t == nullptr) return;
    CloseHandle(t->handle);
    free(t);
}

/* ── Mutex (SRWLOCK) ────────────────────────────────────── */

struct nxp_mutex {
    SRWLOCK lock;
};

nxp_mutex *nxp_mutex_create(void) {
    nxp_mutex *m = (nxp_mutex *)malloc(sizeof(nxp_mutex));
    if (m == nullptr) return nullptr;
    InitializeSRWLock(&m->lock);
    return m;
}

void nxp_mutex_destroy(nxp_mutex *m) {
    /* SRWLOCK doesn't need explicit cleanup */
    free(m);
}

void nxp_mutex_lock(nxp_mutex *m) {
    AcquireSRWLockExclusive(&m->lock);
}

void nxp_mutex_unlock(nxp_mutex *m) {
    ReleaseSRWLockExclusive(&m->lock);
}

bool nxp_mutex_trylock(nxp_mutex *m) {
    return TryAcquireSRWLockExclusive(&m->lock) != 0;
}

/* ── Condition Variable ─────────────────────────────────── */

struct nxp_condvar {
    CONDITION_VARIABLE cv;
};

nxp_condvar *nxp_condvar_create(void) {
    nxp_condvar *cv = (nxp_condvar *)malloc(sizeof(nxp_condvar));
    if (cv == nullptr) return nullptr;
    InitializeConditionVariable(&cv->cv);
    return cv;
}

void nxp_condvar_destroy(nxp_condvar *cv) {
    /* CONDITION_VARIABLE doesn't need explicit cleanup */
    free(cv);
}

void nxp_condvar_wait(nxp_condvar *cv, nxp_mutex *m) {
    SleepConditionVariableSRW(&cv->cv, &m->lock, INFINITE, 0);
}

bool nxp_condvar_wait_timeout(nxp_condvar *cv, nxp_mutex *m, uint64_t us) {
    DWORD ms = (DWORD)(us / 1000);
    if (ms == 0 && us > 0) ms = 1;
    return SleepConditionVariableSRW(&cv->cv, &m->lock, ms, 0) != 0;
}

void nxp_condvar_signal(nxp_condvar *cv) {
    WakeConditionVariable(&cv->cv);
}

void nxp_condvar_broadcast(nxp_condvar *cv) {
    WakeAllConditionVariable(&cv->cv);
}

#endif /* _WIN32 */
