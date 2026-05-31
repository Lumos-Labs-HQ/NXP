# NXP Multi-Transport Architecture

## Goal
Single `nxp_connect("ws://...")` / `nxp_connect("nxp://...")` API.
All transports feed the same sans-I/O core via byte streams.

## Current vs Target

```
NOW:
  nxp_connect("host", port) → hardcoded UDP socket → nxp_conn
  nxp_listen("host", port)  → hardcoded UDP socket → nxp_listener

TARGET:
  nxp_connect("nxp://host:port")  → UDP backend      → nxp_conn
  nxp_connect("ws://host:port")   → WebSocket backend → nxp_conn
  nxp_connect("ntc://host:port")  → raw TCP backend   → nxp_conn
  nxp_connect("nrtc://host:port") → WebRTC backend    → nxp_conn
  nxp_listen("ws://0.0.0.0:8080") → WebSocket server  → nxp_listener
```

## Transport Vtable

```c
// src/platform/transport.h — replaces platform_socket.h as the abstraction

typedef struct nxp_transport_ops {
    // Create a client connection to a URL
    nxp_result (*connect)(const char *url, nxp_transport **out);

    // Create a server listener on a URL
    nxp_result (*listen)(const char *url, nxp_transport_listener **out);

    // Send data through the transport
    ssize_t    (*send)(nxp_transport *t, const uint8_t *data, size_t len);

    // Receive data (non-blocking)
    ssize_t    (*recv)(nxp_transport *t, uint8_t *buf, size_t cap);

    // Close transport
    void       (*close)(nxp_transport *t);

    // Get native handle for epoll registration (fd / socket / etc)
    intptr_t   (*native_handle)(nxp_transport *t);

    // Get peer address (for routing replies)
    nxp_result (*peer_addr)(nxp_transport *t, nxp_addr *out);
} nxp_transport_ops;

typedef struct nxp_transport {
    const nxp_transport_ops *ops;
    void                    *state;  // Backend-specific state
} nxp_transport;

typedef struct nxp_transport_listener {
    const nxp_transport_ops *ops;
    void                    *state;
    nxp_transport *(*accept)(struct nxp_transport_listener *ln);
} nxp_transport_listener;
```

## Backend Implementations

### 1. UDP Backend (already exists, needs wrapping)
**Priority: Immediate** — refactor what we have into the vtable
```
src/transport/udp/
  transport_udp.c    — wraps nxp_socket into nxp_transport
```
- connect: create UDP socket, bind ephemeral
- listen: create UDP socket, bind to port
- send/recv: wrap sendto/recvfrom
- Already works, just wrapping

### 2. WebSocket Backend
**Priority: High** — enables browser connectivity
```
src/transport/websocket/
  transport_ws.c     — TCP socket + HTTP upgrade + WS framing
  ws_frame.c         — WebSocket frame encode/decode
  ws_handshake.c     — HTTP Upgrade: 101 Switching Protocols
```
- connect: TCP connect, send HTTP Upgrade request
- listen: TCP bind + listen, handle Upgrade on accept
- send/recv: WS frame encapsulation (opcode BINARY, mask client→server)
- Uses posix TCP sockets directly (no dependency on libwebsockets)
- Simple enough to hand-roll (RFC 6455 is ~200 lines of framing code)

### 3. Raw TCP Backend
**Priority: Medium** — fallback when UDP blocked
```
src/transport/tcp/
  transport_tcp.c    — TCP framing with NXP packet on top
```
- NXP packets over TCP need a 2-byte length prefix (TCP is stream, not datagram)
- Otherwise identical to UDP backend
- Useful for corporate networks blocking UDP

### 4. WebRTC-Style Backend
**Priority: Later** — complex, requires external libraries
```
src/transport/webrtc/
  transport_rtc.c    — libdatachannel wrapper
```
- Uses libdatachannel (BSD-licensed, minimal WebRTC implementation)
- ICE/STUN handled by the library
- Data channels map 1:1 to NXP streams
- SCTP over DTLS over UDP underneath
- This is a 3rd-party dep, not hand-rolled

## URL Parsing

```c
// src/util/url.h
typedef enum {
    NXP_TRANSPORT_UDP,
    NXP_TRANSPORT_WS,
    NXP_TRANSPORT_TCP,
    NXP_TRANSPORT_RTC,
    NXP_TRANSPORT_UNKNOWN,
} nxp_transport_type;

typedef struct nxp_url {
    nxp_transport_type type;
    char  host[256];
    uint16_t port;
    char  path[256];       // for ws:// and nrtc://
    bool  tls;             // wss:// or nrtcs://
} nxp_url;

nxp_result nxp_url_parse(const char *url_str, nxp_url *out);
```

Scheme mapping:
| URL | Transport | Default Port |
|-----|-----------|--------------|
| `nxp://` | UDP (NXP native) | 8443 |
| `ws://` | WebSocket | 80 |
| `wss://` | WebSocket + TLS | 443 |
| `ntc://` | TCP (NXP-over-TCP) | 8443 |
| `nrtc://` | WebRTC DataChannel | — (ICE negotiation) |

## API Bridge Changes

Current `nxp_api_conn` holds `nxp_socket *sock`. Replace with `nxp_transport *transport`:

```c
typedef struct nxp_api_conn {
    nxp_conn      *conn;
    nxp_transport *transport;  // ← was nxp_socket *sock
    nxp_timer     *timer;
    bool           owns_transport;
    // ... rest unchanged
} nxp_api_conn;
```

Event loop stays the same — it registers `transport->ops->native_handle(t)` for epoll.

## New Public API

```c
// include/nxp/nxp.h additions:

// Connect to a transport URL
nxp_result nxp_connect_url(const char *url,
                           nxp_conn_cb on_connected,
                           nxp_conn_cb on_closed,
                           void *user_data,
                           nxp_conn **out);

// Listen on a transport URL
nxp_result nxp_listen_url(const char *url,
                          nxp_listener_cb on_new_conn,
                          void *user_data,
                          nxp_listener **out);

// Old API stays for compatibility, now wraps nxp_connect_url
nxp_result nxp_connect(const nxp_config *config,
                       const char *host, uint16_t port,
                       ...);  // internally calls nxp_connect_url("nxp://host:port")
```

## Build System

```cmake
# Transport backends — all optional, all default ON

option(NXP_TRANSPORT_UDP    "UDP (NXPNATIVE)" ON)
option(NXP_TRANSPORT_WEBSOCKET "WebSocket"    ON)
option(NXP_TRANSPORT_TCP    "Raw TCP"         ON)
option(NXP_TRANSPORT_RTC    "WebRTC"          OFF)  # needs libdatachannel

if(NXP_TRANSPORT_WEBSOCKET)
    add_subdirectory(src/transport/websocket)
endif()
```

## Implementation Order

### Phase 1 (this week): Transport Abstraction
- Create `src/platform/transport.h` with vtable
- Wrap existing UDP into `src/transport/udp/transport_udp.c`
- Wire `nxp_api_conn` to use `nxp_transport` instead of `nxp_socket`
- All existing tests must pass (internal refactor, no API change)

### Phase 2: WebSocket
- Implement WS handshake (HTTP Upgrade)
- Implement WS framing (RFC 6455)
- Wire into event loop
- Test: `nxp_connect("ws://127.0.0.1:8080")` in E2E test

### Phase 3: TCP Fallback
- 2-byte length prefix framing
- Stream buffering (TCP is not datagram-preserving)
- Handshake works the same as UDP

### Phase 4: All-in-One API
- URL parsing
- `nxp_connect_url()` / `nxp_listen_url()`
- Backend auto-detection from URL

### Phase 5: WebRTC
- Integrate libdatachannel
- Map NXP streams to DataChannels
- ICE negotiation via NXP signaling

## Files Changed

```
NEW:
  src/platform/transport.h              ← transport vtable
  src/platform/transport.c              ← backend registry
  src/util/url.h                        ← URL parser
  src/util/url.c
  src/transport/udp/transport_udp.c     ← wraps current UDP
  src/transport/websocket/transport_ws.c
  src/transport/websocket/ws_frame.c
  src/transport/websocket/ws_handshake.c
  src/transport/tcp/transport_tcp.c
  TRANSPORT_ARCHITECTURE.md

MODIFIED:
  src/api/api_internal.h     ← sock → transport
  src/api/nxp_api.c          ← sock → transport
  include/nxp/nxp.h          ← new URL-based API
  CMakeLists.txt             ← transport backends
  src/CMakeLists.txt         ← new subdirs
```
