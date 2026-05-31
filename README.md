# NXP — NEXUS Protocol

**Custom transport protocol over UDP** (QUIC-inspired)

NXP is a high-performance, secure transport protocol designed for low-latency applications. It provides reliable
streams, congestion control (BBR), encryption (AEAD via OpenSSL), and connection migration over UDP.

## Features

- **Reliable streams** — Bidirectional and unidirectional streams with flow control
- **BBR congestion control** — Bandwidth-based pacing with RTT probing
- **AEAD encryption** — AES-256-GCM and ChaCha20-Poly1305 via OpenSSL 3.0+
- **1-RTT handshake** — X25519 key exchange with HKDF key derivation
- **0-RTT resumption** — Session tickets for early data
- **Connection migration** — Seamless path change without re-handshake
- **Rate limiting** — Per-IP packet rate limiting for DoS protection
- **Sans-I/O design** — Protocol engine independent of socket I/O
- **Go bindings** — Native Go package wrapping the C library

## Build Requirements

- **C23 compiler** (GCC 13+, Clang 16+)
- **CMake 3.25+**
- **OpenSSL 3.0+** (libcrypto)
- **pthread** (Linux/macOS)

## Quick Start

```bash
git clone <repo-url> nxp
cd nxp
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
ctest
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `NXP_BUILD_TESTS` | ON | Build test suite |
| `NXP_BUILD_EXAMPLES` | ON | Build example applications |
| `NXP_ENABLE_LOGGING` | OFF | Enable Quill logging framework |
| `NXP_ENABLE_ASAN` | OFF | Enable AddressSanitizer |
| `NXP_ENABLE_UBSAN` | OFF | Enable UndefinedBehaviorSanitizer |
| `NXP_ENABLE_IO_URING` | OFF | Enable io_uring backend (Linux) |

## Usage

```c
#include <nxp/nxp.h>

// Create a connection config
nxp_conn_config cfg = {
    .idle_timeout_us = 30'000'000,
    .initial_max_data = 1'000'000,
    .initial_max_stream_data = 256'000,
};

// Create a connection
nxp_conn *conn = nxp_conn_create(&cfg, false);

// Open a stream and send data
uint64_t stream_id;
nxp_conn_open_stream(conn, &stream_id, NXP_STREAM_RELIABLE, false);
nxp_conn_stream_send(conn, stream_id, (uint8_t*)"hello", 5, false);

// Drive the connection (Sans-I/O: you handle the sockets)
uint8_t packet[1500];
ssize_t n = nxp_conn_send(conn, packet, sizeof(packet), nxp_time_now_us());
// Send `n` bytes via UDP socket

// Receive a packet and feed it to the connection
nxp_conn_recv(conn, recv_buf, recv_len, nxp_time_now_us());
```

## Architecture

```
UDP Socket → nxp_conn_recv() → Frame Parser → Streams
                                          → ACK/Loss
                                          → Flow Control
                                          → Handshake
           nxp_conn_send()   ← Frame Builder ←
```

- **Sans-I/O** — The protocol engine never touches sockets; the caller handles I/O
- **Frame engine** — QUIC-style varint-encoded frames
- **Packet protection** — Header protection + AEAD encryption per packet
- **Loss detection** — Time-based (RACK-style) with reorder threshold

## Platform Support

| Platform | Backend | Status |
|----------|---------|--------|
| Linux | epoll + eventfd | Stable |
| macOS | kqueue + pipe | Supported |
| Windows | IOCP | In development |

## Testing

```bash
cmake --build . --target test_ack
ctest -R test_ack
```

## License

See [LICENSE](LICENSE) file.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for contribution guidelines.
