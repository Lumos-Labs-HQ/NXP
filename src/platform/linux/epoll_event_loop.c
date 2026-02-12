/*
 * NXP Linux epoll Event Loop
 *
 * Uses epoll for socket I/O multiplexing and eventfd for cross-thread wakeup.
 * Registered sockets are tracked in a fd-indexed array for O(1) lookup.
 */
#ifndef _WIN32

#include "../event_loop.h"
#include "../platform_socket.h"
#include "../platform_time.h"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Maximum events returned per epoll_wait call */
#define EPOLL_MAX_EVENTS 64

/* Initial fd-indexed source table capacity */
#define INITIAL_SOURCE_CAP 64

/* ── Per-socket registration ───────────────────────────── */

typedef struct nxp_epoll_source {
    nxp_socket     *sock;
    nxp_event_cb    cb;
    void           *user_data;
    uint32_t        events;
    bool            active;
} nxp_epoll_source;

/* ── epoll event loop (embeds base as first field) ─────── */

typedef struct nxp_epoll_loop {
    nxp_event_loop    base;
    int               epoll_fd;
    int               wakeup_fd;       /* eventfd for cross-thread wakeup */
    nxp_epoll_source *sources;         /* Array indexed by fd */
    uint32_t          source_cap;
} nxp_epoll_loop;

/* ── Helpers ───────────────────────────────────────────── */

static uint32_t nxp_to_epoll_events(uint32_t nxp_events) {
    uint32_t ep = 0;
    if (nxp_events & NXP_EVENT_READ)  ep |= EPOLLIN;
    if (nxp_events & NXP_EVENT_WRITE) ep |= EPOLLOUT;
    return ep;
}

static uint32_t epoll_to_nxp_events(uint32_t ep) {
    uint32_t nxp = 0;
    if (ep & EPOLLIN)                nxp |= NXP_EVENT_READ;
    if (ep & EPOLLOUT)               nxp |= NXP_EVENT_WRITE;
    if (ep & (EPOLLERR | EPOLLHUP))  nxp |= NXP_EVENT_ERROR;
    return nxp;
}

static nxp_result ensure_source_capacity(nxp_epoll_loop *eloop, int fd) {
    if (fd < 0) return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);

    uint32_t ufd = (uint32_t)fd;
    if (ufd < eloop->source_cap) return NXP_SUCCESS;

    uint32_t new_cap = eloop->source_cap;
    while (new_cap <= ufd) {
        new_cap *= 2;
    }

    nxp_epoll_source *new_sources = (nxp_epoll_source *)realloc(
        eloop->sources, (size_t)new_cap * sizeof(nxp_epoll_source));
    if (new_sources == nullptr) return NXP_ERROR(NXP_ERR_OUT_OF_MEMORY);

    /* Zero-init new slots */
    memset(&new_sources[eloop->source_cap], 0,
           (size_t)(new_cap - eloop->source_cap) * sizeof(nxp_epoll_source));

    eloop->sources    = new_sources;
    eloop->source_cap = new_cap;
    return NXP_SUCCESS;
}

/* ── vtable implementations ────────────────────────────── */

static void epoll_destroy(nxp_event_loop *loop) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    if (eloop->wakeup_fd >= 0) close(eloop->wakeup_fd);
    if (eloop->epoll_fd >= 0)  close(eloop->epoll_fd);
    free(eloop->sources);
    free(eloop);
}

static nxp_result epoll_add_socket(nxp_event_loop *loop, nxp_socket *sock,
                                   uint32_t events, nxp_event_cb cb,
                                   void *ud) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);

    nxp_result r = ensure_source_capacity(eloop, fd);
    if (r.code != NXP_OK) return r;

    struct epoll_event ev = {
        .events  = nxp_to_epoll_events(events),
        .data.fd = fd,
    };

    if (epoll_ctl(eloop->epoll_fd, EPOLL_CTL_ADD, fd, &ev) < 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    eloop->sources[(uint32_t)fd] = (nxp_epoll_source){
        .sock      = sock,
        .cb        = cb,
        .user_data = ud,
        .events    = events,
        .active    = true,
    };

    return NXP_SUCCESS;
}

static nxp_result epoll_mod_socket(nxp_event_loop *loop, nxp_socket *sock,
                                   uint32_t events) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);

    if (fd < 0 || (uint32_t)fd >= eloop->source_cap ||
        !eloop->sources[(uint32_t)fd].active) {
        return NXP_ERROR(NXP_ERR_INVALID_ARGUMENT);
    }

    struct epoll_event ev = {
        .events  = nxp_to_epoll_events(events),
        .data.fd = fd,
    };

    if (epoll_ctl(eloop->epoll_fd, EPOLL_CTL_MOD, fd, &ev) < 0) {
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    eloop->sources[(uint32_t)fd].events = events;
    return NXP_SUCCESS;
}

static void epoll_del_socket(nxp_event_loop *loop, nxp_socket *sock) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    int fd = (int)nxp_socket_get_native_handle(sock);

    if (fd < 0 || (uint32_t)fd >= eloop->source_cap ||
        !eloop->sources[(uint32_t)fd].active) {
        return;
    }

    epoll_ctl(eloop->epoll_fd, EPOLL_CTL_DEL, fd, nullptr);
    eloop->sources[(uint32_t)fd].active = false;
}

static nxp_result epoll_poll(nxp_event_loop *loop, int64_t timeout_ms) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    struct epoll_event events[EPOLL_MAX_EVENTS];

    int timeout = (timeout_ms < 0) ? -1 : (int)timeout_ms;
    int n = epoll_wait(eloop->epoll_fd, events, EPOLL_MAX_EVENTS, timeout);

    if (n < 0) {
        if (errno == EINTR) return NXP_SUCCESS;
        return NXP_ERROR(NXP_ERR_PLATFORM);
    }

    for (int i = 0; i < n; i++) {
        int fd = events[i].data.fd;

        /* Handle wakeup eventfd */
        if (fd == eloop->wakeup_fd) {
            uint64_t val;
            ssize_t ignored = read(eloop->wakeup_fd, &val, sizeof(val));
            (void)ignored;
            continue;
        }

        /* Dispatch socket event */
        if (fd >= 0 && (uint32_t)fd < eloop->source_cap &&
            eloop->sources[(uint32_t)fd].active) {
            nxp_epoll_source *src = &eloop->sources[(uint32_t)fd];
            uint32_t nxp_events = epoll_to_nxp_events(events[i].events);
            src->cb(loop, src->sock, nxp_events, src->user_data);
        }
    }

    return NXP_SUCCESS;
}

static void epoll_wakeup(nxp_event_loop *loop) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)loop;
    uint64_t val = 1;
    ssize_t ignored = write(eloop->wakeup_fd, &val, sizeof(val));
    (void)ignored;
}

/* ── vtable ────────────────────────────────────────────── */

static const nxp_event_loop_ops epoll_ops = {
    .destroy    = epoll_destroy,
    .add_socket = epoll_add_socket,
    .mod_socket = epoll_mod_socket,
    .del_socket = epoll_del_socket,
    .poll       = epoll_poll,
    .wakeup     = epoll_wakeup,
};

/* ── Factory ───────────────────────────────────────────── */

nxp_event_loop *nxp_event_loop_create(void) {
    nxp_epoll_loop *eloop = (nxp_epoll_loop *)calloc(1, sizeof(nxp_epoll_loop));
    if (eloop == nullptr) return nullptr;

    eloop->epoll_fd  = -1;
    eloop->wakeup_fd = -1;

    /* Create epoll instance */
    eloop->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (eloop->epoll_fd < 0) goto fail;

    /* Create eventfd for cross-thread wakeup */
    eloop->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (eloop->wakeup_fd < 0) goto fail;

    /* Register wakeup fd with epoll */
    struct epoll_event ev = {
        .events  = EPOLLIN,
        .data.fd = eloop->wakeup_fd,
    };
    if (epoll_ctl(eloop->epoll_fd, EPOLL_CTL_ADD, eloop->wakeup_fd, &ev) < 0) {
        goto fail;
    }

    /* Allocate fd-indexed source table */
    eloop->source_cap = INITIAL_SOURCE_CAP;
    eloop->sources = (nxp_epoll_source *)calloc(
        INITIAL_SOURCE_CAP, sizeof(nxp_epoll_source));
    if (eloop->sources == nullptr) goto fail;

    /* Initialize base (timer heap, ops vtable) */
    nxp_event_loop_init_base(&eloop->base, &epoll_ops);

    return &eloop->base;

fail:
    if (eloop->wakeup_fd >= 0) close(eloop->wakeup_fd);
    if (eloop->epoll_fd >= 0)  close(eloop->epoll_fd);
    free(eloop->sources);
    free(eloop);
    return nullptr;
}

#endif /* !_WIN32 */
