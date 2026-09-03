# CHTTP RFC 8441 WebSocket Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Make the existing CHTTP WebSocket server route and blocking client support HTTP/2 Extended CONNECT over h2c and WSS+h2 while retaining HTTP/1.1 behavior.

**Architecture:** Keep CNet as the sole RFC 6455 frame engine. Introduce a transport-neutral server peer with H1 and H2 adapters, add narrow open-stream/bidirectional-DATA primitives to the private H2 engine, and select H1 or H2 from the existing client options. Each H2 stream owns bounded outbound frame storage and returns flow-control credit only after synchronous frame consumption.

**Tech Stack:** C11, CHTTP, CNet WebSocket/wsparser, private CHTTP HTTP/2+HPACK engine, NativeIO/CNet TLS, TinyTest, CMake presets.

---

### Task 1: Add H2 Extended CONNECT protocol primitives

**Files:**
- Modify: `chttp/src/chttp_h2_proto.h`
- Modify: `chttp/src/chttp_h2_proto.c`
- Test: `chttp/tests/chttp_h2_proto_test.c`

- [x] Add a failing in-memory test proving client DATA cannot currently be sent on an open request stream.
- [x] Add an explicit client request-headers API with caller-controlled `END_STREAM` and allow DATA from either endpoint on an open local send side.
- [x] Expose whether initial peer SETTINGS has arrived and test `SETTINGS_ENABLE_CONNECT_PROTOCOL=1` round-trip.
- [x] Test bidirectional DATA, flow-control consumption, and independent stream close.

### Task 2: Build a transport-neutral server WebSocket peer

**Files:**
- Create: `chttp/src/chttp_websocket_server_internal.h`
- Modify: `chttp/src/chttp_websocket_server.c`
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server.c`
- Test: `chttp/tests/chttp_websocket_test.c`

- [x] Add a failing H1 regression test that exercises queued output from `on_open` through the peer boundary.
- [x] Move the public handle, CNet engine, route and callback scope into one peer object.
- [x] Implement the existing H1 connection as a transport adapter without changing handshake or close behavior.
- [x] Re-run all H1 WS/WSS tests before adding H2 behavior.

### Task 3: Accept RFC 8441 streams in the H2 server

**Files:**
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_h2_server.h`
- Modify: `chttp/src/chttp_server_route.c`
- Test: `chttp/tests/chttp_h2_server_test.c`

- [x] Add failing raw-peer tests for server SETTINGS and a valid Extended CONNECT handshake.
- [x] Parse and validate CONNECT/`:protocol=websocket` without weakening normal request validation.
- [x] Run the existing WebSocket route middleware/session/open chain and return `:status=200` without `END_STREAM`.
- [x] Adapt H2 DATA and stream close into the CNet engine; use bounded per-stream staging for outbound DATA.
- [x] Test echo, ping/pong, close, malformed frames, rejection, capacity pressure, and unaffected sibling HTTP streams.

### Task 4: Add HTTP/2 to the blocking WebSocket client

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_websocket_client.c`
- Test: `chttp/tests/chttp_websocket_test.c`
- Test: `chttp/tests/chttp_header_cpp_test.cpp`

- [x] Add `protocol` at the end of versioned connect options and failing h2c/WSS+h2 tests.
- [x] Negotiate SETTINGS before Extended CONNECT and reject absent/disabled capability without H1 fallback.
- [x] Submit RFC 8441 pseudo-headers, accept only a final 200, and route DATA/END_STREAM through the existing client-role WebSocket engine.
- [x] Preserve the blocking no-poller send/receive/close contract and bounded event ownership.

### Task 5: Document, package and verify

**Files:**
- Modify: `chttp/README.md`
- Modify: `docs/CHTTP_CNET_PROTOCOL_TODO.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`
- Modify: `chttp/tests/CMakeLists.txt`

- [x] Document H1 Upgrade versus H2 Extended CONNECT, explicit protocol selection, WSS ALPN and current one-stream client scope.
- [x] Format touched C/C++ files and run focused protocol/server/WebSocket/TLS tests.
- [x] Run focused ASan tests, installed-package verification, and the full public Release preset.
- [x] Sync CodeGraph, run `git diff --check`, and report evidence plus residual risks.
