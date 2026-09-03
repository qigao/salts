# CHTTP HTTP/2 Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the existing background `chttp_server` serve HTTP/1.1 and HTTP/2 on one public route/middleware/Session API, with h2c prior knowledge and TLS ALPN selection.

**Architecture:** Keep one CNet owner thread and one listener. A connection selects H1 or H2 once; H1 continues through llhttp, while H2 uses the existing private bounded protocol engine and one fixed-capacity request state per stream. Both paths call a shared request dispatcher so routing, middleware, Session, errors, and statistics retain one implementation.

**Tech Stack:** C11, CNet TCP/TLS, OpenSSL ALPN, private CHTTP frame/HPACK/protocol engine, llhttp for H1, TinyTest, CMake Presets.

**Spec:** `docs/CHTTP_H2_S3_DESIGN.md`

## Global Constraints

- Zero-initialized H2 fields preserve the current H1-only server behavior.
- h2c means prior knowledge only; `Upgrade: h2c` is not implemented.
- One stream failure must not corrupt HPACK state or terminate sibling streams.
- Handler callbacks remain serial on the server owner thread and must not suspend.
- Every stream and connection resource has a checked, configured hard bound.
- H2 response storage remains alive until the protocol engine no longer borrows it.
- TLS `h2` is selected only by negotiated ALPN and never by fallback.

### Task 1: Shared bounded request state

**Files:**
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_server_route.c`
- Modify: `chttp/src/chttp_session.c`
- Modify: `chttp/tests/chttp_server_test.c`

- [x] Add a focused H1 regression that exercises route params, middleware and Session across reused connections.
- [x] Run `chttp_server_test` and record GREEN before the refactor.
- [x] Extract request-owned response/params/Session fields from the H1 connection into `chttp_server_request_state`.
- [x] Move common dispatch into `chttp_server_dispatch_request()` returning a fully built response without serializing a protocol.
- [x] Re-run `chttp_server_test`; verify H1 wire behavior and statistics remain GREEN.

### Task 2: Public H2 server configuration and RED h2c test

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/tests/chttp_header_cpp_test.cpp`
- Create: `chttp/tests/chttp_h2_server_test.c`
- Modify: `chttp/tests/CMakeLists.txt`

- [x] Append explicit H2 enable/stream/input/output/HPACK/SETTINGS capacities to `chttp_server_config`.
- [x] Add header probes and config validation cases showing zero keeps H1 and incomplete H2 limits fail fast.
- [x] Add a real-socket public H2 client -> public server GET test using route params and middleware.
- [x] Build/run the new test and verify RED because the server still routes all cleartext input to H1.

### Task 3: Bounded h2c server adapter

**Files:**
- Create: `chttp/src/chttp_h2_server.c`
- Create: `chttp/src/chttp_h2_server.h`
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/CMakeLists.txt`

- [x] Implement a fixed per-connection stream registry and preallocated request/header/body/response storage.
- [x] Validate pseudo-header ordering, duplication, method/path/scheme/authority, forbidden connection headers, content-length and trailers.
- [x] Detect the cleartext preface without consuming mismatched H1 bytes; feed selected H2 bytes into the protocol engine.
- [x] Adapt END_STREAM requests to `chttp_server_dispatch_request()` and encode `:status`, lowercase response headers, content-length and HEAD body suppression.
- [x] Restore flow-control credit only after bounded DATA acceptance; reset oversize/malformed streams without killing siblings.
- [x] Flush protocol output through the existing one-send-at-a-time CNet connection path.
- [x] Run H1 and h2c focused tests; verify GREEN.

### Task 4: Multiplexing, isolation and graceful shutdown

**Files:**
- Modify: `chttp/tests/chttp_h2_server_test.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`

- [x] Add one-connection multi-target concurrent stream tests and assert `accepted_connections == 1`.
- [x] Add HEAD, 404/405, body, Cookie Session continuity and request body-limit tests.
- [x] Add malformed/oversize stream isolation tests; retain the protocol engine's connection-error regressions.
- [x] Send GOAWAY when stopping H2 connections, flush admitted responses, confirm delivery with PING/ACK, close, and preserve retryable stop timeout semantics.
- [x] Run focused Release and ASan tests; verify no leaks, UAF, double terminal events or stalled shutdown.

### Task 5: TLS ALPN H1/H2 selection

**Files:**
- Modify: `chttp/src/chttp_tls.c`
- Modify: `chttp/src/chttp_tls.h`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/tests/chttp_tls_test.c`
- Modify: `chttp/tests/chttp_h2_server_test.c`

- [x] Add server ALPN validation for the bounded ordered subset of `h2` and `http/1.1`.
- [x] Add real TLS tests for H2 negotiation and H1 compatibility on the same public server implementation.
- [x] Select the connection protocol from `cnet_tls_negotiated_alpn()` and fail unsupported negotiated values.
- [x] Run TLS, H2 and H1 focused tests; verify GREEN.

### Task 6: Documentation, packaging and completion audit

**Files:**
- Modify: `chttp/README.md`
- Modify: `docs/CHTTP_CNET_PROTOCOL_TODO.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

- [x] Document H1/H2 enablement, h2c prior knowledge, TLS ALPN, ownership, hard bounds and serialized handlers.
- [x] Update the installed C and C++ consumers and public header tests.
- [x] Run focused Release, adjacent CNet/CRPC regression, full Release CTest, focused ASan and installed-package verification.
- [x] Run `clang-format`, `git diff --check`, `codegraph sync .`, and inspect the final diff and generated export interface.
- [x] Mark the H2 server capability complete only when every test above proves the public server path.
