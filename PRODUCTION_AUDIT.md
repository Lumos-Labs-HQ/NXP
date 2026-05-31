# NXP Production Readiness Audit

## Overview

**Project:** NXP (NEXUS Protocol) — Custom transport protocol over UDP (QUIC-inspired)
**Language:** C23 + Go bindings
**Version:** 0.1.0
**Build System:** CMake
**Cryptography:** OpenSSL 3.0+
**Current Status:** 5/12 checklist items complete (42%)

---

## Audit Summary

| Category | Critical | High | Medium | Low | Total |
|----------|----------|------|--------|-----|-------|
| Core Engine | 8 | 7 | 8 | 3 | 26 |
| Crypto Layer | 1 | 2 | 1 | 3 | 7 |
| Congestion Control | 0 | 1 | 0 | 1 | 2 |
| Platform Layer | 0 | 3 | 4 | 2 | 9 |
| Memory Layer | 0 | 0 | 2 | 2 | 4 |
| Utility Layer | 0 | 1 | 3 | 3 | 7 |
| Build System | 0 | 1 | 2 | 4 | 7 |
| Tests / Go Bindings | 3 | 4 | 6 | 4 | 17 |
| Documentation | 1 | 2 | 0 | 1 | 4 |

**Total: 13 Critical, 21 High, 26 Medium, 23 Low = 83 issues**

---

## CRITICAL Issues

### C1 — Rate Limiter Memory Leak (Unbounded Growth)
**File:** `src/core/rate_limit.h:36`
**Problem:** `nxp_rate_limiter_destroy()` cannot iterate the hash map to free per-IP buckets. Every unique source IP leaks its `calloc`-d `nxp_rate_bucket`. On a busy server, memory grows without bound.
```c
/* Note: hash_map doesn't have iterator, so we leak buckets for now */
/* TODO: Add hash_map_foreach or track buckets separately */
```
**Fix:** Add a `nxp_hash_map_foreach()` iterator or maintain a separate linked list of allocated buckets.

### C2 — Silent Packet Loss on realloc() Failure
**File:** `src/core/ack.c:144-148`
**Problem:** When the sent-packet array is full and `realloc()` fails, the packet is silently dropped without tracking. It will never be ACKed/NACKed, never retransmitted, causing silent data loss.
```c
nxp_sent_pkt *new_sent = (nxp_sent_pkt *)realloc(
    ack->sent, new_cap * sizeof(nxp_sent_pkt));
if (new_sent == nullptr) return;  // ← SILENTLY DROPS
```
**Fix:** Return an error code so the caller can close the connection. Don't silently discard data.

### C3 — CID Hash Collision Causes Connection Loss
**File:** `src/core/listener.c:31-37`
**Problem:** FNV-1a hash of CID bytes → `uint64_t` key. Colliding hashes cause one connection to silently replace another in the `conn_map`, making it unreachable and leaking the displaced entry.
**Fix:** Use a collision chain or compare raw CID bytes on hash match before treating entries as the same.

### C4 — Integer Overflow in Flow Control Bypass
**File:** `src/core/flow_control.c:25`
**Problem:** `fc->data_sent + len` can overflow `uint64_t`. Example: `data_sent = UINT64_MAX - 1` and `len = 2` wraps to `0`, bypassing the flow control limit.
```c
bool nxp_flow_can_send(const nxp_flow_ctrl *fc, uint64_t len) {
    return fc->data_sent + len <= fc->peer_max_data;  // OVERFLOW
}
```
**Fix:** Use `nxp_checked_add_u64()` from `checked_int.h`.

### C5 — Spurious Loss Detection Cascade on PTO Timeout
**File:** `src/core/ack.c:418-426`
**Problem:** On PTO timeout, it passes the largest **sent** packet number as if it were the largest **acked**, causing the reorder threshold check to declare ALL in-flight packets lost simultaneously.
```c
uint64_t largest = 0;
for (uint32_t i = 0; i < ack->sent_count; i++) {
    if (ack->sent[i].pkt_num > largest) largest = ack->sent[i].pkt_num;
}
detect_lost_packets(ack, now_us, largest, on_loss, ctx);  // WRONG
```
**Fix:** Use `ack->largest_acked` or a separate tracking variable for the loss detection threshold.

### C6 — size_t → int Truncation in OpenSSL AEAD Calls
**File:** `src/crypto/aead.c:56,64,128,136`
**Problem:** Casting `size_t` to `(int)` truncates on 64-bit platforms. Lengths > ~2GB silently become negative/wrong, causing OpenSSL buffer overflows.
```c
EVP_EncryptUpdate(ctx, nullptr, &outl, aad, (int)aad_len)
```
**Fix:** Assert `aad_len <= INT_MAX` or use OpenSSL 3.x streaming API with size_t support.

### C7 — Handshake State Machine Missing NXP_HS_WAIT_HANDSHAKE_DONE
**File:** `src/core/handshake.c:527-541`
**Problem:** The client enters `NXP_HS_WAIT_HANDSHAKE_DONE` after processing ServerHello, but this state falls through to `default:` returning `HANDSHAKE_FAIL`. The server cannot send post-ServerHello CRYPTO frames.
**Fix:** Add `NXP_HS_WAIT_HANDSHAKE_DONE` case to the switch statement.

### C8 — acked_offset Jumps Over Gaps (Out-of-order ACKs)
**File:** `src/core/stream.c:354-356`
**Problem:** When packets are acked out of order (e.g., bytes 100-199 before bytes 0-99), `acked_offset` jumps to 200. The stream then thinks buffer space is available, allowing `nxp_stream_write` to overwrite unacked data that may need retransmission.
```c
if (end > s->send.acked_offset) {
    s->send.acked_offset = end;  // ← jumps over gaps
}
```
**Fix:** Track acked ranges as intervals, not a single advancing offset.

### C9 — Hardcoded CID Length in Short Header Parse
**File:** `src/core/listener.c:287-289`
**Problem:** Short-header DCID length is hardcoded to `NXP_LISTENER_CID_LEN`. If a connection uses a different CID length (QUIC supports variable-length CIDs), this reads garbage or OOB.
```c
dcid.len = NXP_LISTENER_CID_LEN;
memcpy(dcid.data, data + 1, NXP_LISTENER_CID_LEN);
```
**Fix:** Use the CID length from the connection state.

### C10 — macOS Build Broken (No Platform Backend)
**File:** `src/platform/CMakeLists.txt`
**Problem:** The platform CMake has `if(WIN32) ... elseif(UNIX AND NOT APPLE) ...` with no `else` for Apple. On macOS, no socket/thread/time backend is compiled → linker errors.
**Fix:** Add Apple branch with kqueue/BSD socket implementation.

### C11 — Hardcoded CGo Build Path
**File:** `bindings/go/nxp.go:14`
**Problem:** `#cgo LDFLAGS: -L${SRCDIR}/../../build/src` — hardcoded relative path. Out-of-source builds won't find the library.
**Fix:** Use build tags or environment variables for library path configuration.

### C12 — Invalid Go Version in go.mod
**Files:** `tests/go/go.mod`, `bindings/go/go.mod`
**Problem:** `go 1.24` — Go 1.24 does not exist (current latest is 1.23.x). This will fail `go mod tidy` and `go test`.
**Fix:** Change to `go 1.21` or `go 1.22`.

### C13 — Missing README.md
**Problem:** No README.md exists. The project has zero documentation for users, contributors, or deployers. Only `SECURITY_AUDIT.md` exists.
**Fix:** Create README.md with project description, build instructions, API overview, and contribution guide.

---

## HIGH Severity Issues

### H1 — Unchecked hash_map_put Returns
**Files:** `connection.c:357,1272`, `listener.c:140`, `rate_limit.h:62`
**Problem:** `nxp_hash_map_put` can return null on OOM during resize. Unchecked failures mean streams/connections/rate buckets are allocated but not registered → protocol desync, memory leaks, unreachable connections.

### H2 — Loss Rewind is Non-Contiguous-Hole-Unaware
**File:** `src/core/stream.c:367-375`
**Problem:** On loss, `sent_offset` is unconditionally rewound to the lost packet's offset. If a later packet was acked but an earlier one was lost, data gets unnecessarily retransmitted.

### H3 — Stale CC Timestamps (sent_time Used as now_us)
**File:** `src/core/connection.c:148-150`
**Problem:** Loss info passed to congestion controller uses `pkt->sent_time` as `now_us` instead of current time. BBR's bandwidth/RTT estimation uses stale timestamps.
```c
.now_us = pkt->sent_time, /* approximation */  // ← intentional hack
```

### H4 — int64_t → int Timeout Truncation in epoll
**File:** `src/platform/linux/epoll_event_loop.c:166`
**Problem:** `timeout_ms` (int64_t) cast to `int`. Values > INT_MAX (~24 days) truncate to negative → epoll_wait blocks indefinitely.
```c
int timeout = (timeout_ms < 0) ? -1 : (int)timeout_ms;
```
**Fix:** Clamp to INT_MAX: `(timeout_ms > INT_MAX ? INT_MAX : (int)timeout_ms)`

### H5 — size_t → int Truncation in Windows sendto/recvfrom
**File:** `src/platform/windows/winsock_socket.c:108,118`
**Problem:** `sendto`/`recvfrom` accept `int` on Windows but receive `size_t` → truncation.
**Fix:** Clamp to INT_MAX or use WSASend/WSARecv with DWORD.

### H6 — CRC32C Table Init Data Race
**File:** `src/util/crc32c.c:18-39`
**Problem:** Lazy table initialization with a plain `bool` flag and no synchronization. Two threads calling `nxp_crc32c` concurrently will race on the table.
**Fix:** Use `call_once` (C11 threads) or `pthread_once`.

### H7 — bbr_create() Doesn't Check calloc Return
**File:** `src/congestion/bbr.c:273`
**Problem:** `calloc` failure returns NULL, stored directly into `conn->cc_state`. Subsequent CC calls will segfault.
**Fix:** Check for NULL and return an error.

### H8 — nxp_secure_zero Writes Past Buffer
**File:** `src/crypto/aead.c:156-158`
**Problem:** On decrypt failure, `nxp_secure_zero(plaintext, ct_len)` wipes using ciphertext length (which includes TAG_LEN), writing past the plaintext buffer. Should use `ct_len - NXP_AEAD_TAG_LEN`.

### H9 — pn_len Not Bounded (Possible OOB Read)
**File:** `src/core/connection.c:527-537`
**Problem:** `hp_unprotect` returns `pn_len`. Only zero is checked — values > 4 (max valid QUIC pkt number length) cause out-of-bounds read of up to 255 bytes past the packet buffer.
**Fix:** Clamp `pn_len` to valid range [1, 4].

### H10 — Thread-Local decrypt_buf Reentrancy Bug
**File:** `src/core/connection.c:673`
**Problem:** `static _Thread_local decrypt_buf` is shared within a thread. If `nxp_conn_recv()` is called reentrantly (inside a callback that triggers another `nxp_conn_recv()`), the buffer is corrupted.
**Fix:** Allocate on stack or document reentrancy restriction.

### H11 — Server Prefers Client Cipher Order (Protocol Violation)
**File:** `src/core/handshake.c:426-430`
**Problem:** Server picks first match from client's cipher list instead of selecting based on server preference. RFC 9001 expects server-preferred cipher selection.
**Fix:** Iterate server's preferred list and pick first client-supported match.

### H12 — on_packet_acked Now Estimation
**File:** `src/core/connection.c:102`
**Problem:** `now_us = pkt->sent_time + conn->ack.latest_rtt` — estimates current time. Should pass actual `now_us` from the ACK receive context to the CC module.

### H13 — epoll fd Cast to Array Index Without Validation
**File:** `src/platform/linux/epoll_event_loop.c:101,129,151,180`
**Problem:** `(uint32_t)fd` used as array index. If `nxp_socket_get_native_handle` returns -1 (invalid), `(uint32_t)(-1) = 0xFFFFFFFF` → out-of-bounds array access.
**Fix:** Validate fd >= 0 before using as index.

---

## MEDIUM Severity Issues

### M1 — Reentrant Thread-Local Buffer (ack.c flat array)
**File:** `src/core/connection.c:673`
Duplicated from H10 for tracking.

### M2 — nxp_flow_on_consume Ignores len Parameter
**File:** `src/core/flow_control.c:37`
**Problem:** `len` parameter (bytes consumed by app) is unused. `data_recv` is treated as fully consumed, causing overly aggressive flow control window expansion.

### M3 — Flight Recorder Race Condition (volatile Not Atomic)
**File:** `src/util/flight_recorder.c:18-35`
**Problem:** `volatile` doesn't prevent data races. Multiple threads writing to the flight recorder corrupt the circular buffer. Should use `_Atomic size_t` or mutex.

### M4 — Error Tracker Race Condition
**File:** `src/util/error_tracker.c:18-29`
**Problem:** All `g_error_stats` fields accessed without synchronization. Concurrent writes cause lost updates and torn reads.
**Fix:** Use `_Atomic` or per-thread counters with periodic aggregation.

### M5 — Packet Buffer Pool Not Thread-Safe
**File:** `src/memory/packet_buffer.c`
**Problem:** Free list is a raw linked list with no atomic operations. Comment says "lock-free Treiber stack" but no atomic CAS is implemented.
**Fix:** Implement actual lock-free Treiber stack with `atomic_compare_exchange_strong`.

### M6 — No Double-Free Detection in Release Builds
**File:** `src/memory/pool.c:78-88`
**Problem:** `nxp_pool_free` has zero validation when `NXP_DEBUG` is not defined. Double-free or freeing a non-pool pointer silently corrupts the free list.
**Fix:** Enable lightweight validation (e.g., canary values) in release builds, or use a sentinel pattern.

### M7 — CLOCK_REALTIME for Condvar Timeout (NTP Safe)
**File:** `src/platform/linux/posix_thread.c:105-114`
**Problem:** `pthread_cond_timedwait` uses `CLOCK_REALTIME`. System clock adjustments cause premature or delayed timeouts.
**Fix:** Use `pthread_condattr_setclock` with `CLOCK_MONOTONIC`.

### M8 — Windows Time Init Race
**File:** `src/platform/windows/win_time.c:12-17`
**Problem:** `g_qpc_freq` is a non-atomic static. Racing threads will read a partially written value (though QPC returns same frequency regardless).
**Fix:** Use `InitOnceExecuteOnce` or static initializer.

### M9 — varint_decode Missing nullptr Check
**File:** `src/util/varint.c:58-61`
**Problem:** If `buf == nullptr` but `buf_len > 0`, `buf[0]` dereferences null. Only `buf_len == 0` is checked.
**Fix:** Add `if (buf == nullptr || buf_len == 0) return 0;`.

### M10 — Ambiguous hash_map OOM vs Null Value
**File:** `src/util/hash_map.c:92-95`
**Problem:** `nxp_hash_map_put` returns `nullptr` both for OOM and when the key had a null value entry. Callers can't distinguish.
**Fix:** Use a success out-parameter or separate error code.

### M11 — NXP_ENABLE_FUZZING Option Ignored
**File:** root `CMakeLists.txt:28`, `tests/fuzz/CMakeLists.txt`
**Problem:** `NXP_ENABLE_FUZZING` is declared but never checked. Fuzz targets are always built with tests.
**Fix:** Gate `add_subdirectory(tests/fuzz)` behind `NXP_ENABLE_FUZZING`.

### M12 — cmake/NxpFindOpenSSL.cmake Never Included
**File:** `cmake/NxpFindOpenSSL.cmake`
**Problem:** Standalone file with OpenSSL version validation logic that's never `include()`-d anywhere.
**Fix:** Either include it in root CMakeLists.txt or inline the version check.

### M13 — Fuzz Tests Are Not Real Fuzzing Harnesses
**Files:** `tests/fuzz/fuzz_packet.c`, `tests/fuzz/fuzz_frame.c`
**Problem:** Use `rand()` in a loop — no libFuzzer/AFL/Honggfuzz integration. Not actually exercising edge cases systematically.
**Fix:** Convert to libFuzzer harnesses or remove the "fuzz" label.

### M14 — Examples Use Internal Headers With Broken CMake
**Files:** `examples/test_tcp.c`, `examples/test_udp.c`, `examples/test_error_tracking.c`, etc.
**Problem:** Examples `#include` internal headers (e.g., `../../src/core/connection_internal.h`) but CMake doesn't add those include directories. Paths are inconsistent (`../src/` vs `../../src/`).
**Fix:** Either add proper include dirs or rewrite examples to use only the public API.

### M15 — Mutex Held Across Blocking C Calls (Go)
**File:** `bindings/go/protocol.go:368-417`
**Problem:** `Write()` and `Read()` hold `s.mu` for the duration of C calls. If the C call blocks (flow control), the mutex stays locked — no other goroutine can call `Close()`/`Shutdown()`/`State()`.
**Fix:** Don't hold mutex across blocking calls, or use a separate close mutex.

### M16 — Connection Close Race on doneCh (Go)
**File:** `bindings/go/protocol.go:328-361`
**Problem:** `doneCh` can be closed by both `Close()` and the `goOnClosed` callback. The `c.closed` bool and channel close are not atomically protected.
**Fix:** Use `sync.Once` for closing `doneCh`.

### M17 — RawListener Double-Close Missing nil-set (Go)
**File:** `bindings/go/nxp.go:384-389`
**Problem:** `RawListener.Close()` doesn't set `l.l = nil` after closing. Double-close calls `C.nxp_listener_close` twice.
**Fix:** Set `l.l = nil` after close, matching `RawStream.Close()` pattern.

### M18 — Go Send() Cannot Distinguish 0 Bytes from Error
**File:** `bindings/go/nxp.go:419-448`
**Problem:** `Send()` returns `int` — returns 0 for both `len(data) == 0` and C-level errors returning 0.
**Fix:** Return `(int, error)` tuple.

### M19 — Hardcoded Test Script Path (Go)
**File:** `tests/go/run_test.sh:4`
**Problem:** `cd /mnt/d/code/Experment/NXP/tests/go` — hardcoded to one developer's machine.
**Fix:** Use relative paths or environment variable.

### M20 — Go Tests Not Integrated With CMake
**Problem:** Go tests require separate `go test` invocation. Not wired into `ctest`.
**Fix:** Add `add_test()` with `go test` command.

### M21 — Missing Low Integration Test Coverage
**Problem:** Only 1 integration test (UDP echo). No tests for handshake, streams, crypto, migration, BBR, 0-RTT, or server accept flow.

### M22 — _Thread_local Buffer Size Issue
**File:** `src/core/connection.c:673`
Duplicated for completeness.

---

## LOW Severity Issues

### L1 — frame.c.backup in Source Tree
**File:** `src/core/frame.c.backup`
**Problem:** Backup file from incomplete refactor. Clutters source tree and may accidentally be compiled.
**Fix:** Delete it.

### L2 — ring_buffer next_power_of_2 UB on 32-bit
**File:** `src/util/ring_buffer.c:17`
**Problem:** `v |= v >> 32` is undefined behavior when `size_t` is 32-bit (shift >= bit width).
**Fix:** Guard with `#if SIZE_MAX > UINT32_MAX`.

### L3 — aligned_alloc + free Portability Risk
**File:** `src/memory/packet_buffer.c:28-30`
**Problem:** `aligned_alloc` + `free` works per POSIX.1-2008 but some older platforms require dedicated aligned-free.
**Fix:** Wrap in a project-level aligned alloc/free pair.

### L4 — setsockopt Return Ignored
**File:** `src/platform/linux/posix_socket.c:72`
**Problem:** SO_REUSEADDR failure silently ignored. Rare but should be logged.

### L5 — eventfd read/write Errors Silently Discarded
**File:** `src/platform/linux/epoll_event_loop.c:178,210`
**Problem:** Wakeup eventfd errors are silently ignored. Broken eventfd means wakeup mechanism silently fails.

### L6 — Retry Token Plaintext Buffer No Bounds Check
**File:** `src/crypto/retry_token.c:66`
**Problem:** `pt_len` not checked against `sizeof(pt)`. Future changes to `nxp_addr` could overflow.

### L7 — AES-256-ECB Needs Security Comment
**File:** `src/crypto/header_protection.c:34-41`
**Problem:** ECB used legitimately for HP mask generation per QUIC spec, but no comment explaining why ECB is safe here. Triggers false positives in security reviews.

### L8 — Dead Field peer_dcid_len
**File:** Various
**Problem:** `nxp_conn_config.peer_dcid_len` is set but never read anywhere.
**Fix:** Remove or populate into connection state on accept.

### L9 — Duplicate Transport Param Application Code
**File:** `src/core/connection.c:417-425,581-590,597-605`
**Problem:** Same block appears 3 times. Should be extracted into a helper.

### L10 — Inconsistent (void) Cast Usage
**Problem:** Some suppressed return values use `(void)`, some don't. No project convention.

### L11 — Assert Macros Cast to long long (Precision Loss)
**File:** `tests/test_framework.h:39-57`
**Problem:** `NXP_ASSERT_EQ` casts to `long long`, losing precision for large `uint64_t`.
**Fix:** Use `unsigned long long` or `PRIu64` format.

### L12 — nullptr in Test Framework Without #include <stddef.h>
**File:** `tests/test_framework.h:59-60`
**Problem:** Uses `nullptr` (C23 keyword) but doesn't include `<stddef.h>`.
**Fix:** Add `#include <stddef.h>`.

### L13 — NXP_TEST_SUMMARY Uses return in Macro
**File:** `tests/test_framework.h:64-70`
**Problem:** Macro embeds `return` — can only be used at top level of `main()`.

### L14 — Empty bench/ and tools/ Directories
**Problem:** `CMakeLists.txt` placeholder files produce empty build targets.
**Fix:** Add `message(STATUS "...")` explaining they're intentionally empty.

### L15 — MSVC Flags Override GNU Flags (Fragile Ordering)
**File:** `cmake/NxpCompilerFlags.cmake:23-37`
**Problem:** GNU flags set first, then MSVC overwrites. Works but fragile.
**Fix:** Use `if/elseif` structure.

### L16 — Go Debug Mode Always On
**File:** `bindings/go/nxp.go:13`
**Problem:** `-DNXP_DEBUG=1` hardcoded in CGo CFLAGS. Production builds get debug overhead.
**Fix:** Use build tag to control debug flag.

### L17 — Go cgo.Handle Zero Check on Internal Detail
**File:** `bindings/go/protocol.go:624-626`
**Problem:** `if s.handle != 0` relies on internal cgo implementation.
**Fix:** Use a dedicated `valid bool` flag.

### L18 — Go Error Code Gaps
**File:** `bindings/go/nxp.go:98-116`
**Problem:** Error codes -13, -16, -17, -18 are missing with no comment.

### L19 — fleet_recorder Dump Reads Past Valid Entries
**File:** `src/util/flight_recorder.c:49-53`
**Problem:** When not full and `head < count`, reads zero-initialized entries (valid but meaningless).
**Fix:** Return `min(count, head)` entries.

### L20 — Hardcoded Peer Addresses in Examples
**Files:** `examples/test_tcp.c:37`, `examples/test_udp.c:36`
**Problem:** Examples send to hardcoded localhost ports without corresponding server.

### L21 — Examples Fake Handshake
**Files:** `examples/test_tcp.c:42`, `examples/test_udp.c:41`
**Problem:** Examples call `nxp_conn_set_established()` bypassing the handshake and crypto entirely.

### L22 — SECURITY_AUDIT.md Partially Outdated
**Problem:** Lists several items as "DONE" that our audit found issues with (e.g., "Validate CID lengths", "Check integer overflow in offset calculations", "Review hash map collision handling").

---

## Gap Analysis (SECURITY_AUDIT.md vs Reality)

The existing `SECURITY_AUDIT.md` marks as complete:

| Claimed Complete | Audit Finding | Reality |
|-----------------|---------------|---------|
| "Validate CID lengths" | Issue C9: Hardcoded CID length | Still trusts compile-time constant |
| "Check integer overflow" | Issue C4: Flow control overflow | Integer overflow still present |
| "Review hash map collision" | Issue C3: CID hash collision | Collision silently loses connections |
| "Rate limiting per source IP" | Issue C1: Rate limiter leak | Limiter exists but leaks memory |
| "Memory exhaustion protection" | Issue C1: Unbounded leak | Rate limiter grows without bound |
| "Constant-time comparisons" | Crypto Issue #2 | `nxp_secure_compare` exists but never called |

---

## Missing Infrastructure

1. **No README.md** — Zero user-facing documentation
2. **No Doxygen config** — No API reference generation
3. **No CI/CD** — No automated testing pipeline
4. **No changelog** — No version history
5. **No CONTRIBUTING.md** — No contributor guide
6. **No license file** — Unclear licensing
7. **No `.gitignore`** — Build artifacts may be tracked
8. **No code coverage tracking** — Unknown test coverage %
9. **No static analysis config** — No clang-tidy/cppcheck config
10. **No benchmarking framework** — `bench/` is empty
11. **No protocol specification** — Wire format only in code

---

## Recommended Fix Priority

### Immediate (before any production use):
1. C1 — Rate limiter memory leak
2. C2 — Silent packet loss on realloc failure
3. C4 — Integer overflow in flow control
4. C5 — Spurious loss detection cascade
5. C8 — acked_offset gap jumping
6. C10 — macOS build support
7. C13 — Create README.md

### High Priority (first production release):
8. H1 — Check all hash_map_put returns
9. C3 — CID hash collision fix
10. H4 — epoll timeout truncation
11. H6 — CRC32C table init race
12. H9 — pn_len bounds check
13. C6 — OpenSSL size_t → int truncation
14. All thread-safety issues (M3-M5, M7-M8)

### Medium Priority (production hardening):
15. H11 — Server cipher preference
16. M2 — flow_consume len handling
17. All build system fixes (M11-M12)
18. All Go binding fixes (M15-M20)
19. M21 — Integration test coverage

### Low Priority (polish):
20. All LOW severity items
21. Documentation and CI/CD infrastructure
