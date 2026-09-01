# CHTTP Keep-Alive Connection Pool Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Reuse bounded HTTP/1.1 connections for sequential requests to the same `connection_uri + authority`, with CRPC inheriting the behavior through CHTTP.

**Architecture:** Keep the existing fixed CHTTP request slots and let a completed slot retain its CNet connection in an explicit idle state. A request first reuses an idle slot with the same origin key; otherwise it opens a new bounded CNet connection. The server's parsed keep-alive decision is authoritative, idle peers stay observable through one receive demand, and stop drains both busy and idle connections.

**Tech Stack:** C11, CNet, NativeIO, llhttp, TinyTest, CMake presets.

**Spec:** `docs/superpowers/plans/2026-09-01-chttp-keep-alive-pool.md` (the protocol below is normative for this phase).

## Global Constraints

- Public synchronous and asynchronous function signatures remain unchanged.
- Pool key is the exact validated pair `connection_uri + authority`; `target` does not participate.
- Capacity unit is one CHTTP slot/connection and remains bounded by `request_capacity <= network.connection_capacity`.
- A full pool rejects advanced admission with `TURBO_ENOBUFS`; the blocking facade drives bounded idle eviction before retrying admission, without growing or queuing requests implicitly.
- CHTTP and CNet retain one progress owner; no internal thread, lock, executor, retry, or pipelining is added.
- A successful response is reusable only when llhttp reports protocol keep-alive and the request was not cancelled and stop has not begun.
- `network.read_timeout_ms` remains the only read/idle deadline; zero continues to disable it.
- An idle receive containing unsolicited bytes invalidates and closes that connection without producing a second request callback.
- Response views remain valid only during the completion callback; the response parser can be released immediately afterward.
- Shutdown stops admission, closes/drains every busy or idle CNet connection, delivers exactly one terminal callback for each accepted unfinished request, then permits destroy.
- Existing dirty-worktree changes are preserved; commits are deferred until the complete repository diff is reviewed.

---

### Task 1: Request persistence signal

**Files:**
- Modify: `chttp/tests/chttp_request_test.c`
- Modify: `chttp/src/chttp_request.c`

**Interfaces:**
- Consumes: `chttp_request_build(const chttp_request_options *, const chttp_limits *, unsigned char **, size_t *)`.
- Produces: HTTP/1.1 request bytes containing `Connection: keep-alive` while continuing to reject caller-supplied `Connection` headers.

- [x] **Step 1: Write the failing serializer expectation**

  Change the literal request expectation from `Connection: close` to `Connection: keep-alive`; the production change that must make it pass is the generated persistence header.

- [x] **Step 2: Run the focused test and verify RED**

  Run `chttp_request_test` from the `win-release-user` preset and require a byte-comparison failure at the connection header.

- [x] **Step 3: Implement the minimal serializer change**

  Replace only the generated connection-header value and its checked byte accounting; do not allow duplicate user connection headers.

- [x] **Step 4: Run the focused test and verify GREEN**

  Rebuild and run `chttp_request_test`; all serializer boundary cases must pass.

### Task 2: Bounded CHTTP connection reuse

**Files:**
- Modify: `chttp/tests/chttp_requests_test.c`
- Modify: `chttp/tests/chttp_api_test.c`
- Modify: `chttp/src/chttp_client.c`
- Modify: `chttp/src/chttp_requests.c`
- Modify: `chttp/include/chttp/chttp.h`

**Interfaces:**
- Consumes: `cnet_connect`, `cnet_send`, `cnet_receive`, `cnet_close`, `llhttp_should_keep_alive` through `chttp_response_view.protocol_keep_alive`.
- Produces: same-origin sequential socket reuse, stale completed request handles, server-close eviction, idle-peer terminal cleanup, and stop-time drain.

- [x] **Step 1: Write the failing same-socket test**

  Add a real loopback server that calls `accept()` once, receives `/first` and `/second` sequentially on that socket, returns a keep-alive response followed by a close response, and asserts both owning results.

- [x] **Step 2: Run the integration test and verify RED**

  Run `chttp_requests_test`; require failure because current response completion always calls `cnet_close()`.

- [x] **Step 3: Add explicit slot lifecycle state**

  Make each fixed slot retain copied `connection_uri` and `authority`, track whether one receive is armed, and distinguish busy, idle, closing, and terminal states without adding a second fact source.

- [x] **Step 4: Reuse only an exact idle origin**

  Prepare request bytes/parser transactionally, find an idle exact-key slot before a free slot, increment the request generation, call `cnet_send()` on the existing handle, and roll back to idle if send admission fails.

- [x] **Step 5: Return a completed connection to the pool**

  After the callback returns, destroy callback-scoped parser storage, mark the slot idle only for a protocol-persistent response, and arm one receive to observe EOF/read timeout. Close on non-persistent response, parser error, cancellation, stop, or unsolicited idle bytes.

- [x] **Step 6: Preserve blocking semantics**

  Treat an idle completed handle as stale so `chttp_requests_wait_recycled()` returns immediately, while a close-pending completed request remains `TURBO_EALREADY` until its slot reaches terminal state.

- [x] **Step 7: Verify GREEN and adjacent lifecycle cases**

  Run `chttp_request_test`, `chttp_response_test`, `chttp_api_test`, and `chttp_requests_test`, including deadline recovery, cancellation, response `Connection: close`, and client stop/destroy.

### Task 3: CRPC inheritance and documentation

**Files:**
- Modify: `crpc/tests/crpc_api_test.c`
- Modify: `chttp/README.md`
- Modify: `crpc/README.md`
- Modify: `book/*` only at the existing CHTTP/CRPC chapter locations found with `rg.exe`.

**Interfaces:**
- Consumes: `crpc_client`, `crpc_request_reply`, and the CHTTP pool behavior.
- Produces: two JSON-RPC targets on one accepted TCP socket, with documented capacity, key, timeout, error, ownership, and shutdown semantics.

- [x] **Step 1: Convert the existing multi-target CRPC test to one socket**

  Make the first JSON-RPC HTTP response persistent, serve both expected calls on one accepted socket, and retain literal expected wire requests.

- [x] **Step 2: Run the CRPC test and verify behavior**

  Build and run `crpc_api_test`; both owning JSON results must remain readable after each blocking call returns.

- [x] **Step 3: Update API and design prose**

  Replace the one-request-per-connection description with the bounded state machine, clarify that client reuse now includes eligible TCP reuse, and state that this phase has no pipelining, retry, background thread, or cross-origin coalescing.

### Task 4: Repository verification

**Files:**
- Verify: `chttp/**`, `crpc/**`, installed consumers, and affected CMake targets.

**Interfaces:**
- Consumes: public CHTTP/CRPC headers and CMake package exports.
- Produces: reproducible Release and development-mode evidence with no formatting or diff errors.

- [x] **Step 1: Format changed C/C++ files**

  Run repository `clang-format` on only the changed CHTTP/CRPC source, header, and test files.

- [x] **Step 2: Run focused Release tests**

  Configure/build with `win-release-user`, then run the `cnet_`, `chttp_`, and `crpc_` CTest filter with `--output-on-failure`.

- [x] **Step 3: Run installed-consumer verification**

  Run `verify_installed_package` so both C and C++ public headers and exported targets compile.

- [x] **Step 4: Run development memory-safety coverage**

  Build the focused CHTTP/CRPC targets with `win-dev-user` and run their CTest filter under the configured sanitizer environment.

- [x] **Step 5: Inspect impact and repository hygiene**

  Run `codegraph sync .`, `codegraph affected` for the changed files, `git diff --check`, and `git status --short`; keep `.codegraph/` uncommitted and report any platform tests not run.
