# CHTTP HTTP/1.1 Server Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded HTTP/1.1 CHTTP application server over CNet whose ordinary users register routes and never call a poller.

**Architecture:** Extend CNet with a nonblocking TCP listener, accepted-socket ownership transfer, ordered write completion, and send-then-close. Build one background-owner CHTTP server over those primitives, strict llhttp request parsing, an immutable static/dynamic route and middleware table, bounded Cookie Sessions, borrowed request views, and copying response builders.

**Tech Stack:** C11, CNet, NativeIO, llhttp, TurboUtils platform threads, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-09-01-chttp-http1-server.md`

---

## Task 1: CNet listener contract

- [x] Add failing listener/adoption/send-and-close cases to `cnet/tests/cnet_api_test.c` and header coverage to `cnet/tests/cnet_header_cpp_test.cpp`.
- [x] Build and run the focused tests to record RED failures from missing declarations.
- [x] Add listener and ordered close declarations to `cnet/include/cnet/cnet.h`.
- [x] Add `cnet/src/cnet_listener.c`, accepted TCP adoption in `cnet/src/cnet_transport.c`, and the minimal command/owner/shard/client plumbing.
- [x] Add `cnet/src/cnet_listener.c` to `cnet/CMakeLists.txt` and make the focused tests GREEN.

## Task 2: CHTTP server API and parser

- [x] Add failing public API/header cases in `chttp/tests/chttp_api_test.c` and `chttp/tests/chttp_header_cpp_test.cpp`.
- [x] Add failing parser cases in a new `chttp/tests/chttp_server_parser_test.c` for fragmentation, keep-alive, Host, Expect, and configured limits.
- [x] Add the server types, lifecycle, routing, request getters, response helpers, and stats declarations to `chttp/include/chttp/chttp.h`.
- [x] Implement bounded request parsing and response serialization in `chttp/src/chttp_server_parser.c` and shared declarations in `chttp/src/chttp_server_internal.h`.
- [x] Register the new parser test in `chttp/tests/CMakeLists.txt` and make it GREEN.

## Task 3: Route and response behavior

- [x] Add failing route-table and response-builder cases to `chttp/tests/chttp_server_parser_test.c` or a focused `chttp_server_test.c`.
- [x] Implement immutable static/dynamic routing, duplicate rejection, path/method distinction, header validation, body copying, HEAD serialization, and built-in error responses.
- [x] Verify 200, 404, 405, 413, 414, 417, 431, 500, 505, HTTP/1.0 field-ignore,
  and unsupported Upgrade invitation behavior with focused tests.
- [x] Add global/per-route middleware, single-use `next`, bounded Session continuity, full-capacity failure, and invalidate/reuse coverage.

## Task 4: No-poll server lifecycle

- [x] Add a real-socket `chttp/tests/chttp_server_test.c` that starts a server on loopback port zero and calls it with the existing requests-style client.
- [x] Record RED for missing background lifecycle behavior.
- [x] Implement `chttp/src/chttp_server.c`: synchronous CNet initialization/listener bind, background owner launch, listener acceptance, CNet polling, slot lifecycle, keep-alive dispatch, stop timeout, destroy, and synchronized stats.
- [x] Test multiple endpoints over kept-alive connections, HEAD, 404/405, Session capacity, and stop/destroy without caller polling.
- [x] Add sources and the new test target to CMake and make all CNet/CHTTP/CRPC focused tests GREEN.

## Task 5: Documentation and installed package

- [x] Add a complete server example and lifecycle/ownership notes to `chttp/README.md`.
- [x] Update the CHTTP section of the repository book with the CNet listener, route, request-view, response-builder, keep-alive, and executor boundaries.
- [x] Update `tests/install_consumer/consumer.c` and `tests/install_consumer/consumer.cpp` to compile the new API.
- [x] Verify no experimental feature gates or stale names were introduced.

## Task 6: Verification and integration

- [x] Run clang-format on changed C/C++ files and `git diff --check`.
- [x] Run focused Release CNet/CHTTP/CRPC tests.
- [x] Run `verify_installed_package` and its generated consumers.
- [x] Run the complete Release build and CTest suite.
- [x] Run focused ASan tests through the public development preset.
- [x] Synchronize CodeGraph, inspect affected callers/tests, and ensure `.codegraph/` is untracked.
- [x] Review the complete diff for ownership, timeout, shutdown, and public API compatibility.
- [ ] Commit, push `feat/chttp-server`, create a PR, and report CI/merge status without claiming results not observed.
