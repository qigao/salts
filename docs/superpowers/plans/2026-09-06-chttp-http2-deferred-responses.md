# CHTTP HTTP/2 Deferred Responses Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the existing generation-checked CHTTP deferred-response API from HTTP/1.1 to ordinary HTTP/2 streams with bounded cross-thread reply ownership, stream-scoped cancellation, safe borrowed-body lifetime, and H1 compatibility.

**Architecture:** Keep the public `chttp_server_deferred` layout and functions unchanged. Replace the H1-only connection pointer coupling with a private transport-neutral deferred control; H1 embeds one control per connection, while each H2 connection owns a stable pool of `h2_stream_capacity` controls separate from reusable stream slots. H2 uses `IDLE -> PENDING -> WRITING -> READY -> SUBMITTED -> IDLE`, with `CANCELED` for terminal transport loss; `SUBMITTED` retains the copied response builder until the exact H2 stream closes because the protocol engine borrows body bytes after submit.

**Tech Stack:** C11, CHTTP server/runtime, private CHTTP HTTP/2 protocol engine, CNet, TinyTest, CMake presets, GitHub Actions for final Linux/macOS/Windows exact-head evidence.

**Spec:** `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design.md`

**Normative amendments:**
- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment.md`
- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment-2.md`

## Global Constraints

- Do not change the public layout of `chttp_server_deferred`, `chttp_server_deferred_response`, or `chttp_server_config`.
- Do not add a new public symbol, H2-only deferred handle, deferred-capacity option, future/promise abstraction, or protocol fallback.
- Public `chttp_server_deferred.impl` must always identify one stable private deferred-control record, never a connection/stream tagged pointer.
- H2 deferred controls are stable records separate from reusable H2 application-stream slots; each H2 connection owns exactly `h2_stream_capacity` controls.
- Only `IDLE` controls are claimable. `PENDING`, `WRITING`, `READY`, `SUBMITTED`, and `CANCELED` all consume bounded deferred capacity.
- `PENDING` cancellation releases the H2 stream slot immediately while keeping the public lease generation-safe.
- `WRITING` cancellation quarantines only that H2 stream slot until the bounded worker copy exits.
- A successful worker reply clears the public handle at `READY`; for H2 the copied builder remains control-owned through `SUBMITTED` until exact stream terminal close.
- H1 may return its control to `IDLE` after serializing into connection-owned outbound bytes because no H2-style borrowed body remains.
- RST_STREAM/cancel/error on one H2 stream must not reset or stall sibling streams.
- Server-initiated GOAWAY/stop drains admitted deferred work; successful stop requires every deferred control to be `IDLE`.
- A finite stop timeout must preserve server/control storage and valid outstanding handles for a later retry.
- `chttp_server_response_defer()` remains unsupported for Session-backed requests and WebSocket opening callbacks.
- Deferred streaming sources and deferred file responses remain unsupported.
- JWT claims remain callback-borrowed; async consumers copy identity data before handler return.
- Downstream async work must reserve a CHTTP deferred lease before graph/application admission. If graph admission then rejects, the handler terminalizes the already-reserved lease with `chttp_server_deferred_reply()` using the chosen 429/503 response.
- Use deterministic synchronization for the `WRITING`/RST race; do not use sleeps as proof.
- Final evidence must cover h2c and TLS ALPN `h2`, H1 compatibility, public C/C++ headers, Linux, Windows, and macOS at the same implementation SHA.
- If implementation requires changing `chttp_h2_proto.c` semantics rather than using its existing stream-close callback and borrowed-body contract, stop for architecture review instead of expanding scope.

---

## File Structure

The implementation stays inside CHTTP and keeps existing large production files intact.

- `chttp/include/chttp/chttp.h` — public documentation only; no layout/signature changes.
- `chttp/src/chttp_server_runtime.h` — private deferred-control state, transport-neutral defer target, H1 connection embedding.
- `chttp/src/chttp_server_response.c` — protocol-neutral `defer()` and worker-side `deferred_reply()` copy/state machine.
- `chttp/src/chttp_server.c` — H1 control acquisition/progress, common owner wake/stop accounting, connection reuse restrictions.
- `chttp/src/chttp_h2_server.h` — private owner-progress/active-query boundary used by `chttp_server.c`.
- `chttp/src/chttp_h2_server.c` — H2 control pool, stream generation/link, defer acquisition, READY submission, SUBMITTED retention, stream cancellation/quarantine/release.
- `chttp/tests/chttp_response_test.c` — private common-control characterization/validation.
- `chttp/tests/chttp_server_test.c` — H1 compatibility and terminal-disconnect behavior.
- `chttp/tests/chttp_h2_deferred_test.c` — new focused H2 deferred-response contract suite.
- `chttp/tests/CMakeLists.txt` — register the focused test target.
- `chttp/tests/chttp_header_cpp_test.cpp` — compile-only public-layout/API compatibility assertion if needed.
- `chttp/README.md` — H1/H2 deferred ownership, capacity, cancellation, stop, and async-admission ordering.

Do not move generic H2 protocol code, JWT code, CNet, CFlow, or TurboFlow in this issue.

---

### Task 1: Introduce the Common Deferred Control Without Changing H1 Success Semantics

**Files:**
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/tests/chttp_response_test.c`
- Test: `chttp/tests/chttp_server_test.c`

**Interfaces:**
- Produces private state values:

```c
typedef enum chttp_server_deferred_state {
  CHTTP_SERVER_DEFERRED_IDLE = 0,
  CHTTP_SERVER_DEFERRED_PENDING,
  CHTTP_SERVER_DEFERRED_WRITING,
  CHTTP_SERVER_DEFERRED_READY,
  CHTTP_SERVER_DEFERRED_SUBMITTED,
  CHTTP_SERVER_DEFERRED_CANCELED
} chttp_server_deferred_state;

typedef enum chttp_server_deferred_transport {
  CHTTP_SERVER_DEFERRED_TRANSPORT_NONE = 0,
  CHTTP_SERVER_DEFERRED_TRANSPORT_H1,
  CHTTP_SERVER_DEFERRED_TRANSPORT_H2
} chttp_server_deferred_transport;
```

- Produces a transport-neutral defer target on every handler response builder:

```c
typedef struct chttp_server_response_builder chttp_server_response_builder;
typedef struct chttp_server_deferred_control chttp_server_deferred_control;

typedef int (*chttp_server_deferred_acquire_fn)(
    void *user,
    chttp_server_response_builder *base,
    chttp_server_deferred_control **out_control);

typedef struct chttp_server_defer_target {
  chttp_server_deferred_acquire_fn acquire;
  void *user;
} chttp_server_defer_target;
```

- `chttp_server_response_builder` replaces the H1-only `connection` member with:

```c
chttp_server_defer_target defer_target;
```

- `chttp_server_deferred_control` owns the stable public-handle identity and copied reply storage. It must contain at least:

```c
struct chttp_server_deferred_control {
  chttp_server_impl *server;
  chttp_server_response_builder *base_builder;
  chttp_server_response_builder reply_builder;
  chttp_server_response reply_response;
  void *transport;
  atomic_int state;
  atomic_int cancel_requested;
  uint32_t generation;
  uint32_t transport_generation;
  chttp_server_deferred_transport transport_kind;
  bool reply_builder_initialized;
};
```

- H1 embeds one `chttp_server_deferred_control deferred_control;` in `chttp_server_connection` and keeps its existing deferred request snapshot/serialization fields where H1 owner progress needs them.

- [ ] **Step 1: Write a private compile/characterization RED for the new control boundary**

In `chttp/tests/chttp_response_test.c`, include `chttp_server_runtime.h` and add a spec that references the new private state/type and verifies zero/IDLE initialization through a helper added in the same task:

```c
it("initializes a transport-neutral deferred control in IDLE") {
  chttp_server_deferred_control control = {0};
  check_equal(chttp_server_deferred_control_state(&control), CHTTP_SERVER_DEFERRED_IDLE);
}
```

Declare the private read helper in `chttp_server_runtime.h`:

```c
chttp_server_deferred_state
chttp_server_deferred_control_state(const chttp_server_deferred_control *control);
```

This must fail to compile before the private type/helper exists.

- [ ] **Step 2: Run the RED**

Linux command:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target chttp_response_test
```

Expected: compile failure naming the missing `chttp_server_deferred_control` and/or `chttp_server_deferred_control_state`.

- [ ] **Step 3: Add the common private state and H1 defer-target acquisition**

In `chttp_server_runtime.h`:

1. forward-declare the response-builder/control types;
2. add the state/transport enums and defer-target callback;
3. turn the response-builder typedef into a named struct so the callback may refer to it;
4. replace `builder->connection` with `builder->defer_target`;
5. embed `deferred_control` in each H1 connection;
6. remove the old standalone `deferred_state`/`deferred_generation` fields after their uses migrate.

In `chttp_server.c`, add an H1 acquire callback with failure-atomic publication:

```c
static int chttp_server_h1_deferred_acquire(
    void *user,
    chttp_server_response_builder *base,
    chttp_server_deferred_control **out_control) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  chttp_server_deferred_control *control;
  int expected = CHTTP_SERVER_DEFERRED_IDLE;

  if (connection == NULL || base == NULL || out_control == NULL) return SALTS_EINVAL;
  *out_control = NULL;
  control = &connection->deferred_control;
  if (!atomic_compare_exchange_strong_explicit(
          &control->state, &expected, CHTTP_SERVER_DEFERRED_WRITING,
          memory_order_acq_rel, memory_order_acquire))
    return SALTS_EALREADY;

  ++control->generation;
  if (control->generation == 0u) ++control->generation;
  control->server = connection->server;
  control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H1;
  control->transport = connection;
  control->transport_generation = 0u;
  control->base_builder = base;
  atomic_store_explicit(&control->cancel_requested, 0, memory_order_release);
  *out_control = control;
  return SALTS_OK;
}
```

Use WRITING here only as an unpublished reservation state; `chttp_server_response_defer()` publishes the fully initialized lease as PENDING after it snapshots the H1 request and seals the builder.

Initialize H1 builder target during connection initialization:

```c
connection->request_state.response_builder.defer_target =
    (chttp_server_defer_target){chttp_server_h1_deferred_acquire, connection};
```

- [ ] **Step 4: Make `chttp_server_response_defer()` and `deferred_reply()` operate on controls**

`chttp_server_response_defer()` must:

1. validate callback/server/builder/session rules;
2. call `builder->defer_target.acquire(...)`;
3. preserve H1 request snapshot logic when transport is H1;
4. set `builder->deferred = true` only after acquisition succeeds;
5. publish `out_deferred = {control, control->generation, 0}`;
6. release-store PENDING.

`chttp_server_deferred_reply()` must no longer cast `impl` to `chttp_server_connection *`. It must cast to `chttp_server_deferred_control *`, validate generation, CAS PENDING -> WRITING, copy into `control->reply_builder`, then publish READY and clear the caller handle on success.

Keep copy failures retryable by returning WRITING -> PENDING before returning the copy error.

- [ ] **Step 5: Migrate H1 owner progress to the embedded control**

`chttp_server_deferred_progress()` keeps existing H1 wire serialization but reads `connection->deferred_control.state` and `control.reply_builder`. After successful H1 serialization into connection-owned `outbound`, reset the copied builder and return the H1 control to IDLE.

The connection free/reuse predicate and shutdown active predicate must use the common control state:

```c
atomic_load_explicit(&connection->deferred_control.state, memory_order_acquire) ==
    CHTTP_SERVER_DEFERRED_IDLE
```

- [ ] **Step 6: Run H1/private regression tests**

```bash
cmake --build --preset build-default-linux --target chttp_response_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_response_test|chttp_server_test)$'
```

Expected: both PASS; no H2 behavior is enabled yet.

- [ ] **Step 7: Commit Task 1**

```bash
git add chttp/src/chttp_server_runtime.h \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_server.c \
        chttp/tests/chttp_response_test.c \
        chttp/tests/chttp_server_test.c
git commit -m "refactor(chttp): unify deferred response control"
```

**Reviewer gate:** H1 success/retry/duplicate/stale behavior must remain green; reject any implementation that introduces an H2 pointer tag or public-layout change.

---

### Task 2: Add H2 Deferred-Control Pool and Basic Cross-Thread Happy Path

**Files:**
- Create: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/CMakeLists.txt`
- Modify: `chttp/src/chttp_h2_server.h`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_server_runtime.h`

**Interfaces:**
- H2 private owner boundary consumed by `chttp_server.c`:

```c
int chttp_h2_server_connection_deferred_progress(chttp_h2_server_connection *h2);
bool chttp_h2_server_connection_deferred_active(const chttp_h2_server_connection *h2);
```

- Each H2 application stream gains:

```c
uint32_t generation;
chttp_server_deferred_control *deferred_control;
uint32_t deferred_generation;
bool deferred_quarantined;
```

- Each H2 connection owns:

```c
chttp_server_deferred_control *deferred_controls;
size_t deferred_control_capacity; /* exactly stream_capacity */
```

- [ ] **Step 1: Register a focused H2 deferred test target**

Add to `chttp/tests/CMakeLists.txt`:

```cmake
cmake_add_test(chttp_h2_deferred_test
  SOURCES chttp_h2_deferred_test.c
  LIBS salts_chttp Salts::TinyTest
  INCLUDES ../src
  FOLDER "chttp/tests")
set_target_properties(chttp_h2_deferred_test PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)
```

Start `chttp_h2_deferred_test.c` with a server probe that stores one public deferred handle and signals acquisition atomically:

```c
typedef struct chttp_h2_deferred_probe {
  chttp_server_deferred deferred;
  atomic_int acquired;
} chttp_h2_deferred_probe;

static int chttp_h2_deferred_handler(
    void *user,
    const chttp_server_request_view *request,
    chttp_server_response *response) {
  chttp_h2_deferred_probe *probe = (chttp_h2_deferred_probe *)user;
  chttp_server_deferred deferred = CHTTP_SERVER_DEFERRED_INIT;
  int status;
  if (probe == NULL || request == NULL || request->http_major != 2u) return SALTS_EPROTO;
  status = chttp_server_response_defer(response, &deferred);
  if (status != SALTS_OK) return status;
  probe->deferred = deferred;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return SALTS_OK;
}
```

The first test uses `chttp_async_client` with `protocol = CHTTP_HTTP_2`, waits for the handler to publish the handle, replies from another thread with:

```c
const chttp_server_deferred_response reply = {
    .size = sizeof(reply),
    .status_code = 200u,
    .content_type = "text/plain",
    .body = "deferred-h2",
    .body_size = sizeof("deferred-h2") - 1u};
```

and requires exactly one 200 response with body `deferred-h2`.

- [ ] **Step 2: Run the basic H2 RED**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$'
```

Expected: FAIL because H2 `chttp_server_response_defer()` still returns `SALTS_ENOTSUP` or H2 dispatch immediately tries to submit an un-replied builder.

- [ ] **Step 3: Allocate stable H2 controls and add stream generations**

During `chttp_h2_server_connection_init()`:

```c
h2->deferred_control_capacity = h2->stream_capacity;
h2->deferred_controls = calloc(h2->deferred_control_capacity,
                               sizeof(*h2->deferred_controls));
```

Initialize each control with server lifetime and IDLE state. Do not eagerly allocate each reply builder's header/body storage; set `reply_builder_initialized = false` and initialize it lazily on the first WRITING copy.

In stream acquire:

```c
++stream->generation;
if (stream->generation == 0u) ++stream->generation;
```

Do not clear `generation` in ordinary stream reset or physical H2 prepare.

- [ ] **Step 4: Attach an H2 defer target to every acquired ordinary stream**

After request-state reset/acquire, install:

```c
stream->request_state.response_builder.defer_target =
    (chttp_server_defer_target){chttp_h2_server_deferred_acquire, stream};
```

`chttp_h2_server_deferred_acquire()` scans only the connection's stable control pool for IDLE, reserves one control, captures `stream`, `stream->generation`, and the base builder, and returns `SALTS_ENOBUFS` if none is available.

It must not allocate a new control dynamically.

- [ ] **Step 5: Make ordinary H2 dispatch stop after successful defer**

Change `chttp_h2_server_dispatch()`:

```c
status = chttp_server_dispatch_request(&stream->request_state, &request);
if (status != SALTS_OK) return status;
if (stream->request_state.response_builder.deferred) return SALTS_OK;
return chttp_h2_server_submit_response_from(
    stream, &stream->request_state.response_builder);
```

Refactor the existing submit helper to:

```c
static int chttp_h2_server_submit_response_from(
    chttp_h2_server_stream *stream,
    chttp_server_response_builder *builder);
```

Synchronous calls pass the request-state builder. Deferred progress later passes the control-owned builder.

- [ ] **Step 6: Add owner-side READY -> SUBMITTED progress**

`chttp_h2_server_connection_deferred_progress()` scans the fixed control array. For a READY control whose captured stream generation still matches:

1. call `chttp_h2_server_submit_response_from(stream, &control->reply_builder)`;
2. on success, publish SUBMITTED and leave the reply builder intact;
3. set/retain the exact stream->control link;
4. on stream no longer matching, drop/reset the READY builder and return the control to IDLE;
5. on transient submit pressure (`SALTS_ENOBUFS`/`SALTS_EBUSY` where applicable), leave READY for later owner retry.

Call H2 deferred progress from the existing server owner loop before/after `cnet_client_poll()` alongside H1 deferred progress.

- [ ] **Step 7: Release SUBMITTED only from exact stream terminal close**

In `chttp_h2_server_stream_close()`, after the protocol stream is terminal/detached, verify stream generation and control generation. If linked control is SUBMITTED:

```c
chttp_server_response_builder_reset(&control->reply_builder);
control->base_builder = NULL;
control->transport = NULL;
atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE,
                      memory_order_release);
```

Then reset/reuse the application stream slot.

- [ ] **Step 8: Make server stop/reuse see H2 deferred activity**

`chttp_h2_server_connection_deferred_active()` returns true if any control is non-IDLE. Connection reuse and global shutdown completion must not destroy/reuse a physical `chttp_server_connection` record while its old H2 object still has non-IDLE controls.

- [ ] **Step 9: Run the basic H2 + H1 regression**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$'
```

Expected: PASS.

- [ ] **Step 10: Commit Task 2**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/CMakeLists.txt \
        chttp/src/chttp_h2_server.h \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c \
        chttp/src/chttp_server_runtime.h
git commit -m "feat(chttp): defer HTTP2 server responses"
```

**Reviewer gate:** a successful H2 deferred reply must clear the public handle while the private control remains SUBMITTED until stream close; reject any READY -> IDLE implementation.

---

### Task 3: Prove Bounded Copy Semantics, Lease-First Application Admission, and Control Backpressure

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`

**Interfaces:**
- Consumes the Task 2 control pool and `submit_response_from()`.
- Produces no new public API.

- [ ] **Step 1: Add RED tests for retained/overridden headers and retryable copy failure**

Before calling defer, the handler sets:

```c
check_equal(chttp_server_response_set_header(response, "X-Base", "before"), SALTS_OK);
check_equal(chttp_server_response_set_header(response, "X-Replace", "old"), SALTS_OK);
```

Worker reply supplies:

```c
const chttp_header headers[] = {
    {"X-Replace", "new"},
    {"X-Deferred", "yes"}};
```

Require the client response to contain `X-Base: before`, `X-Replace: new`, and `X-Deferred: yes` exactly once.

For retryability, first call `chttp_server_deferred_reply()` with `body_size = max_buffered_response_body_bytes + 1` and require `SALTS_EMSGSIZE` with handle unchanged. Then retry the same handle with a small valid body and require success.

- [ ] **Step 2: Add the lease-before-application-admission regression from amendment 1**

Use a handler that:

1. copies a small request fact into owned probe storage;
2. calls `chttp_server_response_defer()`;
3. simulates graph/application admission failure immediately;
4. calls `chttp_server_deferred_reply()` inside the handler with a 503 response;
5. returns `SALTS_OK`.

Required assertions:

```c
check_equal(probe.defer_status, SALTS_OK);
check_equal(probe.application_admitted, 0);
check_equal(probe.immediate_reply_status, SALTS_OK);
check_equal(client_response.status_code, 503u);
check_equal(client_completion_calls, (size_t)1u);
```

Then send a sibling H2 request on the same connection and require 200.

- [ ] **Step 3: Add control-pool exhaustion without consuming synchronous stream service**

Configure `h2_stream_capacity = 2`. Submit two requests whose handlers defer and deliberately retain their handles in PENDING. Submit a third request whose handler attempts defer; require:

```c
status = chttp_server_response_defer(response, &deferred);
if (status == SALTS_ENOBUFS)
  return chttp_server_reply(response, 503u, "text/plain", "busy", 4u);
```

The third synchronous 503 must complete without closing either deferred sibling or the H2 connection.

Complete one pending lease, drain its response to stream terminal, then submit a fourth request and require defer admission succeeds again.

- [ ] **Step 4: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$'
```

Expected before fixes: at least one failure in header retention/override, retry restoration, immediate handler terminalization, or control-capacity recovery.

- [ ] **Step 5: Make lazy reply-builder initialization and copy rollback exact**

In worker reply logic:

```c
if (!control->reply_builder_initialized) {
  status = chttp_server_response_builder_init(&control->reply_builder,
                                              &control->server->config);
  if (status != SALTS_OK) {
    atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_PENDING,
                          memory_order_release);
    return status;
  }
  control->reply_builder.server = control->server;
  control->reply_response.impl = &control->reply_builder;
  control->reply_builder_initialized = true;
}
```

Copy base headers first, then caller headers so caller values replace same-name base values. Any validation/capacity/allocation failure resets only copied reply state and returns WRITING -> PENDING with the caller handle unchanged.

- [ ] **Step 6: Preserve failure atomicity for `defer()` pool exhaustion**

If H2 acquire returns `SALTS_ENOBUFS`, leave:

```c
builder->deferred == false
*out_deferred == CHTTP_SERVER_DEFERRED_INIT
```

so the handler can issue an ordinary bounded synchronous response. No control generation may be published on failure.

- [ ] **Step 7: Run focused and H1 regression**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_h2_deferred_test|chttp_server_test)$'
```

Expected: PASS.

- [ ] **Step 8: Commit Task 3**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c
git commit -m "test(chttp): prove bounded HTTP2 deferred replies"
```

**Reviewer gate:** graph/application admission may never precede successful lease acquisition; a failed defer must leave the synchronous response builder usable.

---

### Task 4: Add PENDING Cancellation, Stale Generation, Physical-Close, and Sibling Isolation

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/chttp_server_test.c`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`

**Interfaces:**
- Uses common `CANCELED` state.
- No new public API.

- [ ] **Step 1: Add RED for PENDING RST_STREAM**

Use `chttp_async_client_submit()` to create a deferred H2 request. After handler acquisition but before worker reply:

```c
check_equal(chttp_async_request_cancel(&client, request), SALTS_OK);
```

Poll until the client observes terminal cancellation. On the server side, immediately submit a new synchronous sibling request and require it succeeds on the same accepted H2 connection.

Then call the old application handle with a valid dummy response:

```c
check_equal(chttp_server_deferred_reply(&deferred, &dummy), SALTS_ECANCELED);
check_null(deferred.impl);
check_equal(deferred.generation, 0u);
```

- [ ] **Step 2: Add RED for canceled-control capacity and recovery**

With `h2_stream_capacity = 2`:

1. create/defer stream A and cancel it, retaining handle A;
2. create/defer stream B and cancel it, retaining handle B;
3. prove a synchronous stream C still succeeds;
4. prove another handler's defer attempt returns `SALTS_ENOBUFS` because controls A/B are CANCELED and occupied;
5. terminalize A with `deferred_reply()` -> `SALTS_ECANCELED`;
6. a later stream D must now obtain a deferred control.

This distinguishes protocol-stream capacity from async deferred-work capacity.

- [ ] **Step 3: Add RED for stale copied handles**

Copy a valid handle before terminalization:

```c
chttp_server_deferred stale = deferred;
```

After the real handle terminalizes and the same control is later reused for another generation, require:

```c
check_equal(chttp_server_deferred_reply(&stale, &dummy), SALTS_ENOENT);
```

The new generation must remain unaffected.

- [ ] **Step 4: Add RED for physical H2 connection close**

Create at least two PENDING deferred streams on one connection, close/destroy the client transport, and require both later application reply attempts return `SALTS_ECANCELED`. No server control may be freed until each public lease is consumed. After both are consumed, the server connection slot may accept a new physical H2 connection.

- [ ] **Step 5: Add H1 transport-loss terminal behavior regression**

In `chttp_server_test.c`, defer an H1 request, disconnect the client before reply, then require a later `chttp_server_deferred_reply()` terminalizes that exact lease with `SALTS_ECANCELED` rather than publishing a response into a dead transport. Existing H1 success/retry cases must remain unchanged.

- [ ] **Step 6: Run cancellation RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_h2_deferred_test|chttp_server_test)$'
```

Expected: FAIL until owner-side transport close can atomically terminalize PENDING controls.

- [ ] **Step 7: Implement PENDING -> CANCELED detach**

On exact H2 stream close while control is PENDING:

```c
int expected = CHTTP_SERVER_DEFERRED_PENDING;
if (atomic_compare_exchange_strong_explicit(
        &control->state, &expected, CHTTP_SERVER_DEFERRED_CANCELED,
        memory_order_acq_rel, memory_order_acquire)) {
  control->base_builder = NULL;
  control->transport = NULL;
  stream->deferred_control = NULL;
  stream->deferred_generation = 0u;
  /* stream slot may now reset/reuse */
}
```

Do not reset the control-owned reply builder merely because the application handle still exists; if it had not entered WRITING there should be no current copied response to preserve.

On H1 disconnect, publish equivalent transport cancellation without invalidating the stable control memory.

- [ ] **Step 8: Teach `deferred_reply()` terminal cancellation semantics**

After generation validation:

- exact CANCELED -> clear caller handle and return `SALTS_ECANCELED`;
- IDLE or generation mismatch -> `SALTS_ENOENT`;
- WRITING/READY/SUBMITTED -> `SALTS_EALREADY` for copied duplicate handles;
- only PENDING may acquire WRITING.

Cancellation must win over a retryable copy error if cancel is observed after WRITING ownership was acquired.

- [ ] **Step 9: Run cancellation/sibling regressions**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$'
```

Expected: PASS.

- [ ] **Step 10: Commit Task 4**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/chttp_server_test.c \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c
git commit -m "feat(chttp): isolate canceled HTTP2 deferred streams"
```

**Reviewer gate:** PENDING RST must release only the application stream slot, not the stable control or connection; synchronous siblings must progress while canceled application handles remain outstanding.

---

### Task 5: Prove and Implement the Deterministic WRITING/RST Race

**Files:**
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/tests/chttp_h2_deferred_test.c`

**Interfaces:**
- Adds a private, non-installed deterministic test seam only:

```c
typedef void (*chttp_server_deferred_write_probe_fn)(void *user);

void chttp_server_deferred_test_set_write_probe(
    chttp_server_deferred_write_probe_fn probe,
    void *user);
```

The probe runs immediately after PENDING -> WRITING and before the worker resets/copies the control reply builder. Production behavior is unchanged when the probe is NULL.

- [ ] **Step 1: Write a deterministic blocking probe**

In the test, use `salts_mutex_t`/`salts_cond_t` or atomics to create:

```c
typedef struct chttp_h2_deferred_write_gate {
  atomic_int entered;
  atomic_int release;
} chttp_h2_deferred_write_gate;

static void chttp_h2_deferred_write_probe(void *user) {
  chttp_h2_deferred_write_gate *gate = user;
  atomic_store_explicit(&gate->entered, 1, memory_order_release);
  while (!atomic_load_explicit(&gate->release, memory_order_acquire))
    salts_thread_yield();
}
```

This is deterministic because the test waits for `entered == 1` before issuing RST; no timing sleep is used.

- [ ] **Step 2: Add the WRITING/RST RED**

Sequence:

1. H2 handler defers stream A;
2. worker calls `deferred_reply()` and blocks inside the private probe after it owns WRITING;
3. client cancels A with RST_STREAM;
4. submit another request while worker is blocked;
5. prove the closed A stream slot itself is quarantined and is not reset under the worker's base-builder read;
6. prove a different free sibling slot may still serve a synchronous request when capacity allows;
7. release probe;
8. worker must return `SALTS_ECANCELED` and clear A's public handle;
9. owner then releases/reset A's quarantined stream slot;
10. a later request reuses capacity safely.

Use `h2_stream_capacity = 2` so one quarantined slot and one live sibling make the topology observable.

- [ ] **Step 3: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$'
```

Expected: FAIL because current stream-close reset destroys request state while the worker owns WRITING.

- [ ] **Step 4: Implement WRITING cancellation handoff**

On stream close when state is WRITING:

```c
atomic_store_explicit(&control->cancel_requested, 1, memory_order_release);
stream->deferred_quarantined = true;
```

Do not call `chttp_h2_server_stream_reset()` on that application stream yet. Detach the protocol stream user-data as required by the existing protocol callback, decrement active protocol-stream accounting once, but leave request/base-builder storage intact until worker exits.

After the worker finishes bounded copy/validation, it checks `cancel_requested` before retrying PENDING or publishing READY. If set:

```c
chttp_server_response_builder_reset(&control->reply_builder);
control->base_builder = NULL;
atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_CANCELED,
                      memory_order_release);
*deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
(void)cnet_client_wake(&control->server->network);
return SALTS_ECANCELED;
```

Owner progress observes CANCELED + quarantined stream + no writer and finally resets/releases the stream and then returns the control to IDLE because this writer consumed the public lease.

- [ ] **Step 5: Ensure only the affected stream is quarantined**

No H2 connection close, GOAWAY error, or sibling reset may be generated for this application cancellation. The client cancel completion and later sibling 200 response must both occur on the same physical H2 connection.

- [ ] **Step 6: Run focused + H2 core regression**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_h2_proto_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_h2_proto_test)$'
```

Expected: PASS.

- [ ] **Step 7: Commit Task 5**

```bash
git add chttp/src/chttp_server_runtime.h \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c \
        chttp/tests/chttp_h2_deferred_test.c
git commit -m "test(chttp): prove HTTP2 deferred cancel race"
```

**Reviewer gate:** no sleep-based race proof; owner must never reset a WRITING base builder, and worker must never call H2 protocol functions.

---

### Task 6: Lock SUBMITTED Borrowed-Body Lifetime and Stop/GOAWAY Drain

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`

**Interfaces:**
- Consumes `SUBMITTED` state from Task 2.
- No new public API.

- [ ] **Step 1: Add the borrowed-body lifetime RED from amendment 2**

Configure a response body larger than the default H2 stream window and a small server stream chunk:

```c
enum { BODY_BYTES = 96 * 1024 };
config.stream_chunk_bytes = 1024u;
config.max_response_body_bytes = BODY_BYTES;
config.max_buffered_response_body_bytes = BODY_BYTES;
```

Create the large body with a deterministic byte pattern, defer one H2 request, and call `deferred_reply()` successfully. Save the private control pointer for white-box state observation before the public handle is cleared:

```c
chttp_server_deferred_control *control =
    (chttp_server_deferred_control *)deferred.impl;
check_equal(chttp_server_deferred_reply(&deferred, &reply), SALTS_OK);
```

Do not poll the client enough to return stream flow-control credit yet. Poll/wait server-side until:

```c
chttp_server_deferred_control_state(control) == CHTTP_SERVER_DEFERRED_SUBMITTED
```

Require it is not IDLE while the H2 protocol still has outbound body pending.

Then resume client progress, verify all 96 KiB exactly, wait for exact stream terminal close, require control becomes IDLE, and issue a later deferred request to prove safe reuse.

- [ ] **Step 2: Add READY-before-submit close regression**

Publish READY, then close/reset the peer stream before owner submission. Require owner drops the copied response and returns control to IDLE without an application callback/result, because the application handle was already consumed at READY.

- [ ] **Step 3: Add stop timeout with PENDING lease**

Start server stop with a finite short deadline while one application lease remains PENDING. Require `SALTS_ETIMEDOUT`, server object remains valid, and handle still works. Complete the handle, drain response, retry stop, require `SALTS_OK`.

- [ ] **Step 4: Add server-initiated GOAWAY drain with accepted deferred stream**

Start a longer stop in another thread after the handler has already deferred. Existing stop sends GOAWAY. Complete the accepted deferred lease while stop is draining. Require the client receives the full response, the stream closes, the control returns IDLE, and stop returns `SALTS_OK`.

No new stream admitted after drain start may be required for this proof.

- [ ] **Step 5: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$'
```

Expected: FAIL if owner releases builder at submit, if stop destroys outstanding lease storage, or if GOAWAY treats accepted deferred work as canceled.

- [ ] **Step 6: Make SUBMITTED release occur only on exact terminal stream close**

Ensure `chttp_h2_server_submit_response_from()` never resets the deferred builder on successful submit. READY -> SUBMITTED is release-published only after protocol submit succeeds.

`chttp_h2_server_stream_close()` is the normal SUBMITTED -> IDLE release point. Connection termination closes protocol streams first; only after callbacks have detached all borrowed bodies may H2 deferred-control storage be destroyed/reused.

- [ ] **Step 7: Include all deferred controls in stop/reuse accounting**

`chttp_h2_server_connection_deferred_active()` must return true for every state except IDLE. The server's successful shutdown condition therefore includes CANCELED and SUBMITTED controls, not just active protocol streams.

- [ ] **Step 8: Run focused, H1, and H2 drain regression**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$'
```

Expected: PASS.

- [ ] **Step 9: Commit Task 6**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c
git commit -m "fix(chttp): retain deferred HTTP2 bodies through send"
```

**Reviewer gate:** `chttp_h2_proto_submit_response_ex()` borrows body bytes; any code that resets/reuses a deferred builder before exact stream close is a blocker.

---

### Task 7: Add TLS/JWT/Unsupported-Boundary Parity and Public Documentation

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/chttp_server_test.c`
- Modify: `chttp/tests/chttp_websocket_test.c`
- Modify: `chttp/tests/chttp_header_cpp_test.cpp`
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/README.md`

**Interfaces:**
- Public function signatures/layout remain exactly unchanged.
- Documentation changes H1-only wording to H1/H2 ordinary HTTP wording.

- [ ] **Step 1: Add TLS ALPN `h2` happy-path parity**

Use `chttp/tests/chttp_tls_test_material.h`. Build server TLS config with the existing test certificate/key, enable HTTP/2, and configure ALPN so the negotiated protocol is exactly `h2`. Build the client TLS profile trusting `CHTTP_TLS_TEST_CERTIFICATE` and requiring `h2`.

Run the same cross-thread deferred GET over `tls://127.0.0.1:<port>` and require exact 200/body. Do not permit H1 fallback.

- [ ] **Step 2: Add TLS sibling cancellation parity**

Over the same ALPN-h2 setup, defer stream A, cancel A, prove sibling B returns 200, then terminalize A's application handle with `SALTS_ECANCELED`.

- [ ] **Step 3: Add TLS stop/drain parity**

Over ALPN h2, defer one accepted request, initiate server stop/GOAWAY, publish the reply, verify client response and successful stop.

These three TLS tests are the required minimum TLS parity from the spec; do not duplicate every h2c matrix case.

- [ ] **Step 4: Add JWT-protected deferred route coverage**

Create a valid HS256 token/validator using a 32-byte key. Protect the H2 route with `chttp_server_route_with_jwt_bearer()`. In the handler, copy the authenticated subject into owned probe storage before defer:

```c
check_not_null(request->jwt_claims);
check_equal(request->jwt_claims->subject, "alice");
snprintf(probe->subject, sizeof(probe->subject), "%s",
         request->jwt_claims->subject);
```

Worker completion must use only the copied `probe->subject`, never the callback-scoped `jwt_claims` pointer. Require 200 and copied identity value remains correct.

Also require missing Authorization is rejected by admission before handler/defer exactly as #213 already guarantees.

- [ ] **Step 5: Lock Session-backed defer as unsupported**

Add/extend H1 and H2 tests where a live Session is exposed to the handler. Calling:

```c
chttp_server_response_defer(response, &deferred)
```

must return `SALTS_ENOTSUP`, publish no handle, and leave the handler able to issue an ordinary synchronous response.

- [ ] **Step 6: Lock WebSocket opening defer as unsupported**

In H1 Upgrade and RFC 8441 `on_open`, call `chttp_server_response_defer(response, &deferred)` and require `SALTS_ENOTSUP`. The normal WebSocket open path must remain unaffected when the callback does not attempt defer.

- [ ] **Step 7: Update public header comments without layout/signature changes**

Change the deferred-handle comment from H1-only to ordinary HTTP:

```c
/**
 * One generation-checked deferred HTTP response. The handle is completed
 * exactly once by `chttp_server_deferred_reply()` and must not outlive the
 * server's successful stop. Supported for ordinary HTTP/1.1 and HTTP/2
 * handlers; Session-backed requests and WebSocket opening callbacks are
 * unsupported.
 */
```

Update `chttp_server_response_defer()` documentation to state:

- H1 capacity is one deferred lease per connection;
- H2 capacity is bounded by `h2_stream_capacity` deferred controls per H2 connection;
- H2 RST/cancel is stream-scoped;
- successful worker reply transfers application ownership but H2 may retain copied response storage until stream terminal close;
- `SALTS_ENOBUFS` before lease publication leaves the handler response builder usable.

Do not change the function prototype or public struct.

- [ ] **Step 8: Update README with the lease-first async consumer order**

Document this exact order:

```text
handler copies owned request facts
  -> reserve CHTTP deferred lease
  -> if lease reservation fails: ordinary synchronous bounded error
  -> attempt application/graph admission
  -> if application admission fails: deferred_reply(429/503) immediately
  -> if admitted: retain only owned app data + deferred handle
  -> every terminal app outcome calls deferred_reply exactly once
```

Document PENDING/WRITING/READY/SUBMITTED/CANCELED only as lifecycle explanation, not as public enum/API.

- [ ] **Step 9: Run focused parity + public-header tests**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_server_test chttp_websocket_test \
  chttp_jwt_test chttp_header_cpp_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$'
```

Expected: PASS.

- [ ] **Step 10: Commit Task 7**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/chttp_server_test.c \
        chttp/tests/chttp_websocket_test.c \
        chttp/tests/chttp_header_cpp_test.cpp \
        chttp/include/chttp/chttp.h \
        chttp/README.md
git commit -m "docs(chttp): document HTTP2 deferred response contract"
```

**Reviewer gate:** public ABI must be unchanged; JWT identity may not escape callback lifetime; Session/WS/streaming/file deferred capability must not leak into scope.

---

### Task 8: Exact-Head Cross-Platform Verification, Scope Audit, and Review Gate

**Files:**
- Create temporarily: `.github/workflows/chttp-h2-deferred-verifier.yml`
- Delete before final clean head: `.github/workflows/chttp-h2-deferred-verifier.yml`
- No production changes unless verification exposes an in-scope defect.

**Interfaces:**
- Consumes the final implementation SHA from Tasks 1-7.
- Produces exact-head Linux/macOS/Windows evidence and a clean branch with no verification scaffolding.

- [ ] **Step 1: Freeze the implementation head and audit scope**

Record:

```bash
git rev-parse HEAD

git diff --name-only <design-base>...HEAD
```

The allowed production/test/doc scope is only:

```text
chttp/include/chttp/chttp.h
chttp/src/chttp_server_runtime.h
chttp/src/chttp_server_response.c
chttp/src/chttp_server.c
chttp/src/chttp_h2_server.h
chttp/src/chttp_h2_server.c
chttp/tests/CMakeLists.txt
chttp/tests/chttp_response_test.c
chttp/tests/chttp_server_test.c
chttp/tests/chttp_h2_deferred_test.c
chttp/tests/chttp_websocket_test.c
chttp/tests/chttp_header_cpp_test.cpp
chttp/README.md
docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design.md
docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment.md
docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment-2.md
docs/superpowers/plans/2026-09-06-chttp-http2-deferred-responses.md
```

If implementation needs another production file, stop and justify it before adding it.

- [ ] **Step 2: Create a temporary three-host verifier**

Create `.github/workflows/chttp-h2-deferred-verifier.yml` with `workflow_dispatch` and three jobs: Ubuntu, Windows, macOS. Each job must checkout an explicit input SHA/branch head, perform a fresh configure, build full release, run the focused #214 matrix, then run full CTest where the repository baseline permits it.

Use these repository presets exactly:

Linux:

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux
ctest --preset test-release-linux -R '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$'
ctest --preset test-release-linux
```

Windows:

```powershell
cmake --preset release-win-msvc-ninja
cmake --build --preset build-release-windows
ctest --preset test-release-windows -R "^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$"
ctest --preset test-release-windows
```

macOS:

```bash
cmake --preset release-mac-ninja
cmake --build --preset build-default-mac
ctest --preset test-release-mac -R '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$'
ctest --preset test-release-mac
```

The workflow setup must install/restore the same compiler, Ninja, re2c, and vcpkg prerequisites already used by the repository's current release workflows; do not alter project presets to make CI work.

- [ ] **Step 3: Require exact-head focused matrix green on all three hosts**

The focused matrix must be green everywhere. A #214-focused failure is always a blocker; do not baseline it away.

Required behavioral coverage at the exact SHA:

```text
H1 deferred success/retry/transport-loss
H2 h2c cross-thread success
headers + retryable body failure
lease-first immediate 503 terminalization
control pool exhaustion/recovery
PENDING RST + stale handle + sibling isolation
WRITING deterministic RST race
READY drop-before-submit
SUBMITTED large borrowed body through terminal close
stop timeout/retry
GOAWAY drain
TLS ALPN h2 success/cancel/stop
JWT protected defer
Session/WS unsupported
C++ public header compile
```

- [ ] **Step 4: Handle any unrelated full-suite failure with fixed-base provenance, never a blind whitelist**

If a host's full `ctest` fails outside the focused matrix:

1. record the exact failing test/case and status;
2. checkout the fixed design base commit used by this branch;
3. fresh configure/build that base on the same host;
4. run the exact failing test/case;
5. classify it as pre-existing only if the same failure reproduces on the base;
6. run a baseline-aware remaining suite that excludes only the proven identical baseline case(s);
7. keep all #214-focused tests mandatory.

If the failure does not reproduce on the fixed base, return to the implementation task that introduced it.

- [ ] **Step 5: Run final static/API audits**

At the exact head:

```bash
rg -n 'chttp_server_deferred|CHTTP_SERVER_DEFERRED_' chttp/include chttp/src chttp/tests
rg -n 'HTTP/2 currently returns|HTTP/1\.1 handler can|HTTP/1\.1 response' chttp/README.md chttp/include/chttp/chttp.h
rg -n 'TODO|TBD|FIXME' \
  docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design*.md \
  docs/superpowers/plans/2026-09-06-chttp-http2-deferred-responses.md
```

Review expectations:

- no public H2-only deferred type/symbol;
- no direct public-handle cast to H1 connection or H2 stream;
- no H2 deferred body reset between READY -> SUBMITTED -> terminal stream close;
- no worker H2 protocol call;
- no stream reset of a WRITING base builder;
- no hidden fallback to H1/synchronous execution;
- docs no longer claim H2 is unsupported.

- [ ] **Step 6: Remove temporary verifier and prove the clean tree**

Delete `.github/workflows/chttp-h2-deferred-verifier.yml`, commit cleanup, and compare the clean head to the pre-verifier implementation tree. The only content difference must be removal of the temporary workflow; production/test/doc tree must be bit-for-bit the verified implementation tree.

```bash
git diff --name-only <verified-implementation-sha>...HEAD
```

Expected: only the temporary workflow's add/remove history, with no final `.github/workflows/chttp-h2-deferred-verifier.yml` in the branch diff.

- [ ] **Step 7: Re-run the normal repository PR check on the clean head**

Open/update the PR only after cleanup. Require the normal `C API notation` check to pass on the clean head. Reconfirm no unexpected changed files, unresolved review threads, or head movement before declaring merge-ready.

- [ ] **Step 8: Prepare the final review packet; do not merge automatically**

The PR body must record:

```text
approved design + both normative amendments
final clean head SHA
H1 compatibility result
Linux exact-head result
Windows exact-head result
macOS exact-head result / fixed-base provenance for any unrelated baseline
h2c + TLS ALPN h2 deferred matrix
PENDING/WRITING/READY/SUBMITTED/CANCELED proof summary
public ABI unchanged
no Session/WS/streaming/file scope expansion
TurboFlow lease-before-graph contract
```

Stop at the code-review/integration gate. Merge and issue #214 closure require a separate explicit user decision.

**Reviewer gate:** no merge claim without exact-head evidence and clean verifier removal; no baseline exception for a #214-focused failure.

---

## Plan Self-Review Checklist

Before execution begins, verify these mappings:

- Stable private control + unchanged public handle: Tasks 1-2.
- H2 fixed control pool separate from stream slots: Task 2.
- Failure-atomic defer + retryable copied response: Task 3.
- Lease-before-graph/application admission amendment: Task 3.
- PENDING cancellation + stale generation + physical close + sibling isolation: Task 4.
- Deterministic WRITING/RST quarantine: Task 5.
- READY vs SUBMITTED distinction and borrowed-body lifetime amendment: Task 6.
- GOAWAY/stop timeout/retry: Task 6.
- h2c + TLS ALPN h2: Tasks 2-7.
- JWT callback-lifetime boundary: Task 7.
- Session/WebSocket non-goals: Task 7.
- Public C/C++ docs/API compatibility: Task 7.
- Linux/Windows/macOS exact-head evidence and clean final branch: Task 8.

No task introduces deferred streaming sources, deferred files, a public cancel/abandon API, graph rollback, H2 protocol fallback, or a generic async abstraction.