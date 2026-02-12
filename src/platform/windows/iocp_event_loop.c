/*
 * NXP Windows IOCP Event Loop - Stub
 *
 * Placeholder for Phase 3. The IOCP completion-port based event loop
 * will be fully implemented in a future phase when Windows testing
 * infrastructure is available.
 *
 * All socket operations return NXP_ERR_PLATFORM to indicate that
 * the backend is not yet functional.
 */
#ifdef _WIN32

#include "../event_loop.h"
#include <stdlib.h>

static void iocp_destroy(nxp_event_loop *loop) {
    free(loop);
}

static nxp_result iocp_add_socket(nxp_event_loop *loop, nxp_socket *sock,
                                  uint32_t events, nxp_event_cb cb,
                                  void *ud) {
    (void)loop; (void)sock; (void)events; (void)cb; (void)ud;
    return NXP_ERROR(NXP_ERR_PLATFORM);
}

static nxp_result iocp_mod_socket(nxp_event_loop *loop, nxp_socket *sock,
                                  uint32_t events) {
    (void)loop; (void)sock; (void)events;
    return NXP_ERROR(NXP_ERR_PLATFORM);
}

static void iocp_del_socket(nxp_event_loop *loop, nxp_socket *sock) {
    (void)loop; (void)sock;
}

static nxp_result iocp_poll(nxp_event_loop *loop, int64_t timeout_ms) {
    (void)loop; (void)timeout_ms;
    return NXP_ERROR(NXP_ERR_PLATFORM);
}

static void iocp_wakeup(nxp_event_loop *loop) {
    (void)loop;
}

static const nxp_event_loop_ops iocp_ops = {
    .destroy    = iocp_destroy,
    .add_socket = iocp_add_socket,
    .mod_socket = iocp_mod_socket,
    .del_socket = iocp_del_socket,
    .poll       = iocp_poll,
    .wakeup     = iocp_wakeup,
};

nxp_event_loop *nxp_event_loop_create(void) {
    nxp_event_loop *loop = (nxp_event_loop *)calloc(1, sizeof(nxp_event_loop));
    if (loop == nullptr) return nullptr;
    nxp_event_loop_init_base(loop, &iocp_ops);
    return loop;
}

#endif /* _WIN32 */
