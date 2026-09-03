# CHTTP HTTP/2 WebSocket Session Pool Implementation Plan

> **For Codex:** Execute each task test-first and run the stated focused verification before moving
> to the next behavior.

**Goal:** Provide a requests-style, fixed-capacity pool that multiplexes independent RFC 8441
WebSocket sessions over one HTTP/2 connection.

**Architecture:** A new pool owns the shared CNet transport and `chttp_h2_proto`; generation-checked
slots own per-stream CNet WebSocket engines, queues, payload storage, and stable pending-frame
storage. Public calls synchronously drive the shared event loop.

**Tech Stack:** C11, CNet, CHTTP H2 protocol engine, URI Parser, TinyTest, CMake Presets.

---

### Task 1: Freeze the public contract with failing tests

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/tests/chttp_websocket_test.c`
- Modify: `chttp/tests/chttp_header_cpp_test.cpp`

1. Add tests that open two targets, exchange distinct messages, close one stream, and keep the
   sibling usable on one accepted connection.
2. Add tests for full capacity, stale handles, invalid H1 protocol, and mismatched origin.
3. Compile the focused target and record the expected missing-symbol failure before implementation.

### Task 2: Implement the bounded pool and session state machines

**Files:**
- Create: `chttp/src/chttp_websocket_pool.c`
- Modify: `chttp/CMakeLists.txt`

1. Validate configuration and checked allocation sizes.
2. Implement first-open connection establishment, SETTINGS/Extended CONNECT admission, and exact
   origin/TLS matching for subsequent streams.
3. Route H2 callbacks to generation-owned slots and copy events into per-slot bounded storage.
4. Implement per-session send, receive, close, error isolation, stale-handle validation, and
   connection-wide destruction.
5. Run the focused WebSocket and header tests.

### Task 3: Document and verify downstream consumption

**Files:**
- Modify: `chttp/README.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

1. Document topology, limits, ownership, origin reuse, errors, and a complete two-session example.
2. Compile installed C and C++ consumers so every public symbol/type is exported correctly.
3. Run focused CHTTP/CNet tests, installed-package verification, formatting, and `git diff --check`.
