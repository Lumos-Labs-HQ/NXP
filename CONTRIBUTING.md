# Contributing to NXP

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone <your-fork-url>`
3. Create a branch: `git checkout -b feature/my-feature`

## Build & Test

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug
cmake --build .
ctest
```

Run individual tests:
```bash
ctest -R test_ack
```

## Code Style

- **C23** with no extensions (`-std=c23`)
- `nullptr` for null pointers
- `[[nodiscard]]` on functions returning error codes or allocated memory
- Return `nxp_result` for operations that can fail
- Static functions for file-internal helpers
- Error codes from `include/nxp/nxp_error.h`

## Commit Messages

Use conventional commits:
```
feat: add connection migration support
fix: handle hash_map OOM in stream registration
docs: document BBR congestion control parameters
```

## Pull Request Checklist

- [ ] Code compiles with no warnings (`-Wall -Wextra`)
- [ ] All tests pass (`ctest`)
- [ ] New features have tests
- [ ] Public API is documented in headers
- [ ] No `malloc` without corresponding `free`

## Project Structure

```
src/core/         — Protocol engine (Sans-I/O)
src/crypto/       — OpenSSL 3.0 backend
src/congestion/   — BBR congestion control
src/platform/     — Platform abstraction (epoll, kqueue, IOCP)
src/memory/       — Custom allocators and pools
src/util/         — Data structures and utilities
src/logging/      — Quill-based logging (optional)
tests/unit/       — Unit tests
tests/go/         — Go integration tests
bindings/go/      — Go language bindings
examples/         — Example applications
```

## Questions?

Open an issue on the repository.
