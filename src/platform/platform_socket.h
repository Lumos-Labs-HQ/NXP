/*
 * NXP Platform Socket Abstraction
 *
 * UDP socket operations and address utilities.
 */
#ifndef NXP_PLATFORM_SOCKET_H
#define NXP_PLATFORM_SOCKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "nxp/nxp_types.h"
#include "nxp/nxp_error.h"

/* Portable ssize_t */
#ifdef _WIN32
    #include <basetsd.h>
    typedef SSIZE_T ssize_t;
#else
    #include <sys/types.h>
#endif

/* Opaque socket handle */
typedef struct nxp_socket nxp_socket;

/* Initialize platform sockets (call once at startup) */
[[nodiscard]] nxp_result nxp_socket_init(void);

/* Cleanup platform sockets (call once at shutdown) */
void nxp_socket_cleanup(void);

/* Create a UDP socket bound to the given address */
[[nodiscard]] nxp_result nxp_socket_create_udp(const nxp_addr *bind_addr, nxp_socket **out);

/* Close a socket */
void nxp_socket_close(nxp_socket *sock);

/* Send a datagram */
[[nodiscard]] ssize_t nxp_socket_sendto(
    nxp_socket     *sock,
    const uint8_t  *data,
    size_t          len,
    const nxp_addr *to
);

/* Receive a datagram (non-blocking) */
[[nodiscard]] ssize_t nxp_socket_recvfrom(
    nxp_socket *sock,
    uint8_t    *buf,
    size_t      buf_len,
    nxp_addr   *from
);

/* Set socket to non-blocking mode */
[[nodiscard]] nxp_result nxp_socket_set_nonblocking(nxp_socket *sock);

/* Get the native socket handle (fd on POSIX, SOCKET on Windows) */
[[nodiscard]] intptr_t nxp_socket_get_native_handle(nxp_socket *sock);

/* Get the local bound address (useful after binding to port 0) */
[[nodiscard]] nxp_result nxp_socket_get_local_addr(nxp_socket *sock, nxp_addr *out);

/* Address utilities */
[[nodiscard]] nxp_result nxp_addr_from_string(const char *str, uint16_t port, nxp_addr *out);
[[nodiscard]] bool nxp_addr_equal(const nxp_addr *a, const nxp_addr *b);
void nxp_addr_to_string(const nxp_addr *addr, char *buf, size_t buf_len);

#endif /* NXP_PLATFORM_SOCKET_H */
