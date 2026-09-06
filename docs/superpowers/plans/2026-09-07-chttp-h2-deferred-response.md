# CHTTP HTTP/2 Deferred Response Implementation Plan

> Execute inline with test-driven checkpoints. Do not add C or CMake fallback paths.

**Goal:** Complete Salts issue #214 by giving regular HTTP/2 server streams the same bounded,
generation-checked, exactly-once deferred terminal response API as HTTP/1.1.

**Architecture:** Embed one shared deferred control abstraction in the stable H1 connection and H2
stream slots. Worker threads only copy bounded response data and publish atomic state; the existing
CNet owner thread submits H2 frames and owns stream retirement.

**Spec:** `docs/CHTTP_H2_DEFERRED_RESPONSE_DESIGN.md`

## Task 1: Prove the missing H2 path

- [x] Add an h2c handler that captures a deferred handle and returns without a response.
- [x] Complete it from the test thread and assert the response reaches the original stream.
- [x] Run only `chttp_h2_server_test` and observe RED from the current `SALTS_ENOTSUP` path.

## Task 2: Share bounded deferred control

- [x] Add a private deferred control block and make the existing H1 path use it without changing
      public layout or return codes.
- [x] Initialize one independent deferred response builder per H2 stream slot.
- [x] Set and clear callback-scoped request pointers around H2 dispatch.
- [x] Make H2 dispatch stop before synchronous submission when the response was deferred.
- [x] Add owner-thread H2 READY/CANCELED progress and make the h2c success test GREEN.
- [x] Re-run H1 deferred/token tests after the refactor.

## Task 3: Close races and prove isolation

- [x] Quarantine a stable H2 stream slot when RST/peer close races a `WRITING` worker.
- [x] Make pending/ready handles stale on RST_STREAM and peer close.
- [x] Include H2 deferred controls in connection reuse and server-stop liveness checks.
- [x] Test duplicate and stale handles, response capacity failure followed by cancel, RST_STREAM,
      and a successful sibling stream.
- [x] Prove a deferred stream progresses while another stream remains flow-control blocked.
- [x] Test GOAWAY stop/drain waits for the admitted handle and then completes.

## Task 4: Cover both transports and public contract

- [x] Exercise deferred response through TLS ALPN `h2` as well as h2c prior knowledge.
- [x] Update `chttp/include/chttp/chttp.h`, `chttp/README.md`, and C++ header probes.
- [x] Link the design to issue #214 and document unsupported deferred streaming bodies.

## Task 5: Verify and deliver

- [x] Format modified C/C++ sources and run `git diff --check`.
- [x] Run focused Debug+ASan tests, adjacent H1/H2/TLS tests, and full Release CTest.
- [x] Run the Debug install preset and installed-package verification.
- [x] Sync CodeGraph, inspect impact/diff, commit, push, open the issue-linked PR, and request review.
