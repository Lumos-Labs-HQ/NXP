/*
 * NXP macOS/BSD kqueue Event Loop
 *
 * Uses kqueue for socket I/O multiplexing and pipe for cross-thread wakeup.
 */
#ifdef __APPLE__

#include "../event_loop.h"
#include "../platform_socket.h"
#include "../platform_time.h"

#include <sys/event.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

#define KQUEUE_MAX_EVENTS 64
#define INITIAL_SOURCE_CAP 64

typedef struct nxp_kqueue_source {
    nxp_socket  *sock;
    nxp_event_cb cb;
    void        *user_data;
    uint32_t     events;
    bool         active;
} nxp_kqueue_source;

typedef struct nxp_kqueue_loop {
    nxp_event_loop     base;
    int                kqueue_fd;
    int                wakeup_fds[2];  /* pipe() for wakeup */
    nxp_kqueue_source *sources;
    uint32_t           source_cap;
} nxp_kqueue_loop;

static void nxp_to_kevent_flags(uint32_t nxp_events,
                                int16_t *filter, uint16_t *flags) {
    *filter = 0;
    *flags  = EV_ADD | EV_ENABLE;
    if (nxp_events & NXP_EVENT_READ)  *filter |= EVFILT_READ;
    if (nxp_events & NXP_EVENT_WRITE) *filter |= EVFILT_WRITE;
}

static uint32_t kevent_to_nxp_events(int16_t filter, uint16_t flags) {
    uint32_t nxp = 0;
    if (filter == EVFILT_READ)  nxp |= NXP_EVENT_READ;
    if (filter == EVFILT_WRITE) nxp |= NXP_EVENT_WRITE;
    if (flags & EV_ERROR)       nxp |= NXP_EVENT_ERROR;
    return nxp;
}

static nxp_result ensure_source_capacity(nxp_kqueue_loop *kloop, int fd) {
    if (fd < 0) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    uint32_t ufd = (uint32_t)fd;
    if (ufd < kloop->source_cap) return NXP_SUCCESS;

    uint32_t new_cap = kloop->source_cap;
    while (new_cap <= ufd) {
        new_cap *= 2;
    }

    nxp_kqueue_source *new_sources = (nxp_kqueue_source *)realloc(
        kloop->sources, (size_t)new_cap * sizeof(nxp_kqueue_source));
    if (new_sources == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    memset(&new_sources[kloop->source_cap], 0,
           (size_t)(new_cap - kloop->source_cap) * sizeof(nxp_kqueue_source));

    kloop->sources    = new_sources;
    kloop->source_cap = new_cap;
    return NXP_SUCCESS;
}

static void kqueue_destroy(nxp_event_loop *loop) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    if (kloop->wakeup_fds[0] >= 0) close(kloop->wakeup_fds[0]);
    if (kloop->wakeup_fds[1] >= 0) close(kloop->wakeup_fds[1]);
    if (kloop->kqueue_fd >= 0) close(kloop->kqueue_fd);
    free(kloop->sources);
    free(kloop);
}

static nxp_result kqueue_add_socket(nxp_event_loop *loop, nxp_socket *sock,
                                    uint32_t events, nxp_event_cb cb,
                                    void *ud) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);
    if (fd < 0) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    nxp_result r = ensure_source_capacity(kloop, fd);
    if (r.code != NXP_OK) return r;

    struct kevent kev[2];
    int nchanges = 0;

    if (events & NXP_EVENT_READ) {
        EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & NXP_EVENT_WRITE) {
        EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_WRITE,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (kevent(kloop->kqueue_fd, kev, nchanges, nullptr, 0, nullptr) < 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    kloop->sources[(uint32_t)fd] = (nxp_kqueue_source){
        .sock      = sock,
        .cb        = cb,
        .user_data = ud,
        .events    = events,
        .active    = true,
    };

    return NXP_SUCCESS;
}

static nxp_result kqueue_mod_socket(nxp_event_loop *loop, nxp_socket *sock,
                                    uint32_t events) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);

    if (fd < 0 || (uint32_t)fd >= kloop->source_cap ||
        !kloop->sources[(uint32_t)fd].active) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    /* Remove old filters then re-add */
    struct kevent kev[2];
    int nchanges = 0;

    EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_READ,
           EV_DELETE, 0, 0, nullptr);
    EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_WRITE,
           EV_DELETE, 0, 0, nullptr);
    kevent(kloop->kqueue_fd, kev, nchanges, nullptr, 0, nullptr);

    nchanges = 0;
    if (events & NXP_EVENT_READ) {
        EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_READ,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }
    if (events & NXP_EVENT_WRITE) {
        EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_WRITE,
               EV_ADD | EV_ENABLE, 0, 0, nullptr);
    }

    if (nchanges > 0 &&
        kevent(kloop->kqueue_fd, kev, nchanges, nullptr, 0, nullptr) < 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    kloop->sources[(uint32_t)fd].events = events;
    return NXP_SUCCESS;
}

static void kqueue_del_socket(nxp_event_loop *loop, nxp_socket *sock) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);

    if (fd < 0 || (uint32_t)fd >= kloop->source_cap ||
        !kloop->sources[(uint32_t)fd].active) {
        return;
    }

    struct kevent kev[2];
    int nchanges = 0;
    EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_READ,
           EV_DELETE, 0, 0, nullptr);
    EV_SET(&kev[nchanges++], (uintptr_t)fd, EVFILT_WRITE,
           EV_DELETE, 0, 0, nullptr);
    kevent(kloop->kqueue_fd, kev, nchanges, nullptr, 0, nullptr);

    kloop->sources[(uint32_t)fd].active = false;
}

static nxp_result kqueue_poll(nxp_event_loop *loop, int64_t timeout_ms) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    struct kevent events[KQUEUE_MAX_EVENTS];
    struct timespec ts;
    struct timespec *pts = nullptr;

    if (timeout_ms >= 0) {
        ts.tv_sec  = (time_t)((uint64_t)timeout_ms / 1000ULL);
        ts.tv_nsec = (long)(((uint64_t)timeout_ms % 1000ULL) * 1000000ULL);
        pts = &ts;
    }

    int n = kevent(kloop->kqueue_fd, nullptr, 0, events, KQUEUE_MAX_EVENTS, pts);

    if (n < 0) {
        if (errno == EINTR) return NXP_SUCCESS;
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    for (int i = 0; i < n; i++) {
        int fd = (int)events[i].ident;

        /* Handle wakeup pipe */
        if (fd == kloop->wakeup_fds[0]) {
            char buf[64];
            while (read(kloop->wakeup_fds[0], buf, sizeof(buf)) > 0) {}
            continue;
        }

        if (fd >= 0 && (uint32_t)fd < kloop->source_cap &&
            kloop->sources[(uint32_t)fd].active) {
            nxp_kqueue_source *src = &kloop->sources[(uint32_t)fd];
            uint32_t nxp_events = kevent_to_nxp_events(events[i].filter,
                                                        events[i].flags);
            src->cb(loop, src->sock, nxp_events, src->user_data);
        }
    }

    return NXP_SUCCESS;
}

static void kqueue_wakeup(nxp_event_loop *loop) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)loop;
    char byte = 1;
    ssize_t ignored = write(kloop->wakeup_fds[1], &byte, 1);
    (void)ignored;
}

static const nxp_event_loop_ops kqueue_ops = {
    .destroy    = kqueue_destroy,
    .add_socket = kqueue_add_socket,
    .mod_socket = kqueue_mod_socket,
    .del_socket = kqueue_del_socket,
    .poll       = kqueue_poll,
    .wakeup     = kqueue_wakeup,
};

nxp_event_loop *nxp_event_loop_create(void) {
    nxp_kqueue_loop *kloop = (nxp_kqueue_loop *)calloc(1, sizeof(nxp_kqueue_loop));
    if (kloop == nullptr) return nullptr;

    kloop->kqueue_fd     = -1;
    kloop->wakeup_fds[0] = -1;
    kloop->wakeup_fds[1] = -1;

    kloop->kqueue_fd = kqueue();
    if (kloop->kqueue_fd < 0) goto fail;

    /* Create pipe for cross-thread wakeup */
    if (pipe(kloop->wakeup_fds) < 0) goto fail;
    fcntl(kloop->wakeup_fds[0], F_SETFL, O_NONBLOCK);
    fcntl(kloop->wakeup_fds[1], F_SETFL, O_NONBLOCK);

    struct kevent kev;
    EV_SET(&kev, (uintptr_t)kloop->wakeup_fds[0], EVFILT_READ,
           EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kloop->kqueue_fd, &kev, 1, nullptr, 0, nullptr) < 0) {
        goto fail;
    }

    kloop->source_cap = INITIAL_SOURCE_CAP;
    kloop->sources = (nxp_kqueue_source *)calloc(
        INITIAL_SOURCE_CAP, sizeof(nxp_kqueue_source));
    if (kloop->sources == nullptr) goto fail;

    nxp_event_loop_init_base(&kloop->base, &kqueue_ops);

    return &kloop->base;

fail:
    if (kloop->wakeup_fds[0] >= 0) close(kloop->wakeup_fds[0]);
    if (kloop->wakeup_fds[1] >= 0) close(kloop->wakeup_fds[1]);
    if (kloop->kqueue_fd >= 0) close(kloop->kqueue_fd);
    free(kloop->sources);
    free(kloop);
    return nullptr;
}

#endif /* __APPLE__ */
