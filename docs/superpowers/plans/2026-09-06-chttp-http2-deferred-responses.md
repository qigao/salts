# CHTTP HTTP/2 Deferred Responses Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the existing generation-checked CHTTP deferred-response API from HTTP/1.1 to ordinary HTTP/2 streams with bounded cross-thread reply ownership, stream-scoped cancellation, safe borrowed-body lifetime, and H1 compatibility.

**Architecture:** Keep the public `chttp_server_deferred` layout and functions unchanged. Replace the H1-only connection pointer coupling with a private transport-neutral deferred control; H1 embeds one control per connection, while each H2 connection owns a stable pool of exactly `h2_stream_capacity` controls separate from reusable stream slots. H2 uses `IDLE -> PENDING -> WRITING -> READY -> SUBMITTED -> IDLE`, with `CANCELED` for terminal transport loss; `SUBMITTED` retains the copied response builder until the exact H2 stream closes because the protocol engine borrows response body bytes after submit.

**Tech Stack:** C11, CHTTP server/runtime, private CHTTP HTTP/2 protocol engine, CNet, TinyTest, CMake presets, GitHub Actions for final Linux/macOS/Windows exact-head evidence.

**Spec:** `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design.md`

**Normative amendments:**
- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment.md`
- `docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design-amendment-2.md`

**Fixed design base:** `0b1cdb3a2e23080dd53c18428c3dfec3592d3559`

## Global Constraints

- Do not change the public layout of `chttp_server_deferred`, `chttp_server_deferred_response`, or `chttp_server_config`.
- Do not add a new public symbol, H2-only deferred handle, deferred-capacity option, future/promise abstraction, graph rollback contract, or protocol fallback.
- Public `chttp_server_deferred.impl` always identifies one stable private deferred-control record, never a connection/stream tagged pointer.
- Each H2 connection owns exactly `h2_stream_capacity` stable deferred controls; controls are separate from reusable application-stream slots.
- Only `IDLE` controls are claimable. `PENDING`, `WRITING`, `READY`, `SUBMITTED`, and `CANCELED` all consume bounded deferred capacity.
- `PENDING` cancellation releases the H2 stream slot immediately while keeping the public lease generation-safe.
- `WRITING` cancellation quarantines only that H2 stream slot until the bounded worker copy exits.
- A successful worker reply clears the public handle at `READY`; for H2 the copied builder remains control-owned through `SUBMITTED` until exact stream terminal close.
- H1 may return its control to `IDLE` after serializing into connection-owned outbound bytes because no H2-style borrowed body remains.
- RST_STREAM/cancel/error on one H2 stream never resets or stalls sibling streams.
- Server-initiated GOAWAY/stop drains admitted deferred work; successful stop requires every deferred control to be `IDLE`.
- A finite stop timeout preserves server/control storage and outstanding handles for a later retry.
- `chttp_server_response_defer()` remains unsupported for Session-backed requests and WebSocket opening callbacks.
- Deferred streaming sources and deferred file responses remain unsupported.
- JWT claims remain callback-borrowed; async consumers copy required identity data before handler return.
- Downstream async work reserves a CHTTP deferred lease before graph/application admission. If application admission rejects, the handler terminalizes the already-reserved lease with `chttp_server_deferred_reply()` using its selected 429/503 response.
- Use deterministic synchronization for the `WRITING`/RST race; no sleep-based race proof.
- Final evidence covers h2c, TLS ALPN `h2`, H1 compatibility, public C/C++ headers, Linux, Windows, and macOS at one implementation SHA.
- If implementation requires changing `chttp_h2_proto.c` semantics instead of using its existing stream-close callback and borrowed-body contract, stop for architecture review.

---

## File Structure

- `chttp/include/chttp/chttp.h` — public documentation only; no layout/signature changes.
- `chttp/src/chttp_server_runtime.h` — private deferred-control state, defer target, H1 connection embedding, private deterministic test probes.
- `chttp/src/chttp_server_response.c` — protocol-neutral `defer()` and worker-side `deferred_reply()` state/copy logic.
- `chttp/src/chttp_server.c` — H1 acquire/progress, common owner wake, stop/reuse accounting.
- `chttp/src/chttp_h2_server.h` — private H2 deferred progress/activity and test-only terminal seam.
- `chttp/src/chttp_h2_server.c` — H2 control pool, stream generation/link, READY/SUBMITTED, cancellation/quarantine/release.
- `chttp/tests/chttp_response_test.c` — private control characterization.
- `chttp/tests/chttp_server_test.c` — H1 compatibility/transport-loss coverage.
- `chttp/tests/chttp_h2_deferred_test.c` — new focused H2 deferred-response contract suite.
- `chttp/tests/CMakeLists.txt` — focused test registration.
- `chttp/tests/chttp_websocket_test.c` — H1/RFC8441 WebSocket defer rejection.
- `chttp/tests/chttp_header_cpp_test.cpp` — public compile compatibility.
- `chttp/README.md` — H1/H2 deferred ownership/capacity/lifecycle and lease-first async ordering.

Do not move or modify generic H2 protocol semantics, JWT internals, CNet, CFlow, or TurboFlow production code for #214.

---

### Task 1: Introduce a Common Deferred Control and Preserve H1 Success Semantics

**Files:**
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/tests/chttp_response_test.c`
- Test: `chttp/tests/chttp_server_test.c`

**Interfaces:**

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

`chttp_server_response_builder` replaces the H1-only `connection` member with:

```c
chttp_server_defer_target defer_target;
```

The stable private control contains:

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

chttp_server_deferred_state
chttp_server_deferred_control_state(const chttp_server_deferred_control *control);
```

H1 embeds one `chttp_server_deferred_control deferred_control;` per `chttp_server_connection`.

- [ ] **Step 1: Write the compile RED**

In `chttp/tests/chttp_response_test.c`, include `chttp_server_runtime.h` and add:

```c
it("initializes a transport-neutral deferred control in IDLE") {
  chttp_server_deferred_control control = {0};
  check_equal(chttp_server_deferred_control_state(&control),
              CHTTP_SERVER_DEFERRED_IDLE);
}
```

- [ ] **Step 2: Verify RED**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target chttp_response_test
```

Expected: compile failure because the private control/state helper does not exist.

- [ ] **Step 3: Add common state, initialize H1 copied storage, and attach the H1 defer target**

In `chttp_server_runtime.h`, add the private enums/control/target and turn `chttp_server_response_builder` into a named struct. Remove `builder->connection`. Embed the common control in `chttp_server_connection`; after all uses migrate, remove the old standalone `deferred_state` and `deferred_generation` fields.

During H1 connection initialization, initialize the one H1 copied reply builder immediately, preserving existing H1 allocation semantics:

```c
chttp_server_deferred_control *control = &connection->deferred_control;
control->server = server;
control->transport_kind = CHTTP_SERVER_DEFERRED_TRANSPORT_H1;
atomic_init(&control->state, CHTTP_SERVER_DEFERRED_IDLE);
atomic_init(&control->cancel_requested, 0);
status = chttp_server_response_builder_init(&control->reply_builder, &server->config);
if (status != SALTS_OK) return status;
control->reply_builder.server = server;
control->reply_response.impl = &control->reply_builder;
control->reply_builder_initialized = true;
```

Destroy that builder from H1 connection destroy.

Add H1 acquire:

```c
static int chttp_server_h1_deferred_acquire(
    void *user,
    chttp_server_response_builder *base,
    chttp_server_deferred_control **out_control) {
  chttp_server_connection *connection = (chttp_server_connection *)user;
  chttp_server_deferred_control *control;
  int expected = CHTTP_SERVER_DEFERRED_IDLE;

  if (connection == NULL || base == NULL || out_control == NULL)
    return SALTS_EINVAL;
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

Use WRITING here only as an unpublished reservation state. During connection initialization install:

```c
connection->request_state.response_builder.defer_target =
    (chttp_server_defer_target){chttp_server_h1_deferred_acquire, connection};
```

- [ ] **Step 4: Move public defer/reply to control identity**

`chttp_server_response_defer()` validates response/builder/server/callback/session rules, calls `builder->defer_target.acquire()`, preserves the H1 request snapshot, seals `builder->deferred = true`, publishes `{control, control->generation, 0}`, then release-stores PENDING.

`chttp_server_deferred_reply()` casts `impl` only to `chttp_server_deferred_control *`, validates generation, CASes PENDING -> WRITING, copies into `control->reply_builder`, then publishes READY and clears the public handle. Copy/validation/capacity failures restore PENDING and leave the handle unchanged.

- [ ] **Step 5: Migrate H1 owner progress/reuse/stop predicates**

Use `connection->deferred_control.state` and `control.reply_builder`. H1 still serializes the complete reply into `connection->outbound`; after successful serialization reset copied reply state and publish IDLE. A connection slot is reusable only when its control is IDLE.

- [ ] **Step 6: Run H1/private regression**

```bash
cmake --build --preset build-default-linux --target chttp_response_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_response_test|chttp_server_test)$' --output-on-failure
```

Expected: PASS.

- [ ] **Step 7: Commit**

```bash
git add chttp/src/chttp_server_runtime.h \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_server.c \
        chttp/tests/chttp_response_test.c \
        chttp/tests/chttp_server_test.c
git commit -m "refactor(chttp): unify deferred response control"
```

**Reviewer gate:** no public layout change, no connection/stream pointer tag, H1 success/retry/duplicate/stale tests green.

---

### Task 2: Add the H2 Control Pool and Basic Cross-Thread Deferred Reply

**Files:**
- Create: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/CMakeLists.txt`
- Modify: `chttp/src/chttp_h2_server.h`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_server_runtime.h`

**Interfaces:**

```c
int chttp_h2_server_connection_deferred_progress(chttp_h2_server_connection *h2);
bool chttp_h2_server_connection_deferred_active(const chttp_h2_server_connection *h2);
```

Each H2 application stream gains:

```c
uint32_t generation;
chttp_server_deferred_control *deferred_control;
uint32_t deferred_generation;
bool deferred_quarantined;
```

Each H2 connection gains:

```c
chttp_server_deferred_control *deferred_controls;
size_t deferred_control_capacity; /* exactly stream_capacity */
```

The focused test file defines deterministic deadlines once:

```c
enum {
  CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS = 5000,
  CHTTP_H2_DEFERRED_TEST_STOP_TIMEOUT_MS = 20
};
```

- [ ] **Step 1: Register the focused test target and write basic RED**

Add:

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

Start the test with:

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
  if (probe == NULL || request == NULL || request->http_major != 2u)
    return SALTS_EPROTO;
  status = chttp_server_response_defer(response, &deferred);
  if (status != SALTS_OK) return status;
  probe->deferred = deferred;
  atomic_store_explicit(&probe->acquired, 1, memory_order_release);
  return SALTS_OK;
}
```

Use `chttp_async_client` with `protocol = CHTTP_HTTP_2`. Reply from another thread with:

```c
const chttp_server_deferred_response reply = {
    .size = sizeof(reply),
    .status_code = 200u,
    .content_type = "text/plain",
    .body = "deferred-h2",
    .body_size = sizeof("deferred-h2") - 1u};
```

Require one 200 completion with exact body `deferred-h2`.

- [ ] **Step 2: Verify RED**

```bash
cmake --preset release-linux-ninja
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$' --output-on-failure
```

Expected: FAIL because H2 defer is unsupported or dispatch tries to submit immediately.

- [ ] **Step 3: Allocate stable controls and stream generations**

During H2 connection init:

```c
h2->deferred_control_capacity = h2->stream_capacity;
h2->deferred_controls = calloc(h2->deferred_control_capacity,
                               sizeof(*h2->deferred_controls));
```

For each control set `server`, transport kind H2, atomically initialize state IDLE/cancel_requested 0, and keep `reply_builder_initialized = false`. H2 reply builder header/body storage is lazy.

During stream acquire:

```c
++stream->generation;
if (stream->generation == 0u) ++stream->generation;
```

Do not clear `generation` in stream reset/physical prepare.

- [ ] **Step 4: Attach H2 defer target and claim only IDLE controls**

```c
stream->request_state.response_builder.defer_target =
    (chttp_server_defer_target){chttp_h2_server_deferred_acquire, stream};
```

`chttp_h2_server_deferred_acquire()` scans only the fixed array. It captures stream pointer/index, stream generation, base builder, server, transport kind, and a new nonzero control generation. No free control -> `SALTS_ENOBUFS`, no published handle.

- [ ] **Step 5: Stop H2 dispatch after successful defer**

Refactor response submit to:

```c
static int chttp_h2_server_submit_response_from(
    chttp_h2_server_stream *stream,
    chttp_server_response_builder *builder);
```

Then:

```c
status = chttp_server_dispatch_request(&stream->request_state, &request);
if (status != SALTS_OK) return status;
if (stream->request_state.response_builder.deferred) return SALTS_OK;
return chttp_h2_server_submit_response_from(
    stream, &stream->request_state.response_builder);
```

- [ ] **Step 6: Add owner READY -> SUBMITTED progress**

READY + exact matching stream generation calls `submit_response_from(stream, &control->reply_builder)`. Successful protocol submit publishes SUBMITTED and keeps the builder intact. A vanished/mismatched stream drops READY and returns IDLE. Transient output pressure leaves READY for owner retry.

Call H2 deferred progress in the server owner loop before/after CNet poll alongside H1 deferred progress.

- [ ] **Step 7: Release SUBMITTED only from exact stream terminal close**

On terminal close, verify stream/control generations. For SUBMITTED:

```c
chttp_server_response_builder_reset(&control->reply_builder);
control->base_builder = NULL;
control->transport = NULL;
atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_IDLE,
                      memory_order_release);
```

Only after protocol terminal close may the application stream slot reset/reuse.

- [ ] **Step 8: Include H2 deferred controls in connection reuse/stop activity**

`chttp_h2_server_connection_deferred_active()` returns true for any non-IDLE control. A server connection record with old H2 deferred controls cannot be reused/destroyed.

- [ ] **Step 9: Run regression**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/CMakeLists.txt \
        chttp/src/chttp_h2_server.h \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c \
        chttp/src/chttp_server_runtime.h
git commit -m "feat(chttp): defer HTTP2 server responses"
```

**Reviewer gate:** successful worker reply clears the public lease at READY, but the H2 builder remains live in SUBMITTED until exact stream close.

---

### Task 3: Prove Copy Bounds, Failure Atomicity, Lease-First App Admission, and Pool Backpressure

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`

- [ ] **Step 1: Add header retention/override RED**

Before defer:

```c
check_equal(chttp_server_response_set_header(response, "X-Base", "before"), SALTS_OK);
check_equal(chttp_server_response_set_header(response, "X-Replace", "old"), SALTS_OK);
```

Deferred reply adds:

```c
const chttp_header headers[] = {
    {"X-Replace", "new"},
    {"X-Deferred", "yes"}};
```

Require `X-Base: before`, `X-Replace: new`, and `X-Deferred: yes` with correct final values.

- [ ] **Step 2: Add retryable copy RED**

First reply attempt uses `body_size = max_buffered_response_body_bytes + 1` and returns `SALTS_EMSGSIZE` with handle unchanged. Second attempt on the same handle uses a valid small body and succeeds.

- [ ] **Step 3: Add amendment-1 lease-first rejection RED**

Handler copies one request fact, successfully reserves deferred lease, simulates application/graph admission rejection, immediately calls `chttp_server_deferred_reply()` in the handler with 503, and returns `SALTS_OK`.

Require:

```c
check_equal(probe.defer_status, SALTS_OK);
check_equal(probe.application_admitted, 0);
check_equal(probe.immediate_reply_status, SALTS_OK);
check_equal(response_status, 503u);
check_equal(completion_calls, (size_t)1u);
```

A sibling H2 request on the same connection returns 200.

- [ ] **Step 4: Add pool-exhaustion RED without breaking synchronous H2**

Use `h2_stream_capacity = 2`. Hold two handles PENDING. A third handler attempts defer; on `SALTS_ENOBUFS` it sends ordinary synchronous 503:

```c
status = chttp_server_response_defer(response, &deferred);
if (status == SALTS_ENOBUFS)
  return chttp_server_reply(response, 503u, "text/plain", "busy", 4u);
```

The 503 completes. Complete/drain one pending deferred response to terminal close; a later request obtains defer capacity again.

- [ ] **Step 5: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$' --output-on-failure
```

- [ ] **Step 6: Make reply-builder initialization lazy and rollback exact**

On first H2 WRITING use:

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

Copy base headers first; caller deferred headers then replace/add by existing case-insensitive response-header semantics. Any copy/validation/allocation failure resets copied reply state, returns WRITING -> PENDING, and leaves caller handle unchanged.

- [ ] **Step 7: Preserve defer failure atomicity**

H2 control exhaustion or pre-publication error leaves:

```c
builder->deferred == false
*out_deferred == CHTTP_SERVER_DEFERRED_INIT
```

No application work may be admitted after such failure.

- [ ] **Step 8: Run focused + H1 regression and commit**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_h2_deferred_test|chttp_server_test)$' --output-on-failure

git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c
git commit -m "test(chttp): prove bounded HTTP2 deferred replies"
```

**Reviewer gate:** async application admission never precedes successful CHTTP lease acquisition; failed defer leaves synchronous response available.

---

### Task 4: Add PENDING Cancellation, Stale Generations, Physical Close, and Sibling Isolation

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/chttp_server_test.c`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`

- [ ] **Step 1: Add PENDING RST RED**

Defer H2 request A. Before worker reply:

```c
check_equal(chttp_async_request_cancel(&client, request_a), SALTS_OK);
```

Poll to client terminal cancellation. Submit synchronous sibling B and require 200 on the same accepted H2 connection. Consume A's old server lease:

```c
check_equal(chttp_server_deferred_reply(&deferred_a, &dummy), SALTS_ECANCELED);
check_null(deferred_a.impl);
check_equal(deferred_a.generation, 0u);
```

- [ ] **Step 2: Add canceled-control capacity/recovery RED**

With capacity 2, create/cancel deferred A and B while retaining both application handles. Synchronous C still succeeds. Another defer attempt returns `SALTS_ENOBUFS`. Terminalize A -> `SALTS_ECANCELED`; later D obtains a deferred control.

- [ ] **Step 3: Add stale copied-handle RED**

```c
chttp_server_deferred stale = deferred_a;
```

After real A terminalization and control reuse at a later generation:

```c
check_equal(chttp_server_deferred_reply(&stale, &dummy), SALTS_ENOENT);
```

Current generation remains unaffected.

- [ ] **Step 4: Add physical H2 close RED**

Hold two PENDING deferred streams on one physical connection, close/destroy the client, then require both later server-side reply attempts return `SALTS_ECANCELED`. Only after both handles are consumed may the server connection record accept a new physical H2 peer.

- [ ] **Step 5: Add H1 transport-loss terminal regression**

Defer H1, disconnect client before worker reply, then require exact lease terminalization with `SALTS_ECANCELED`. Existing live-H1 success/retry remains unchanged.

- [ ] **Step 6: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test chttp_server_test
ctest --preset test-release-linux -R '^(chttp_h2_deferred_test|chttp_server_test)$' --output-on-failure
```

- [ ] **Step 7: Implement PENDING -> CANCELED detach**

```c
int expected = CHTTP_SERVER_DEFERRED_PENDING;
if (atomic_compare_exchange_strong_explicit(
        &control->state, &expected, CHTTP_SERVER_DEFERRED_CANCELED,
        memory_order_acq_rel, memory_order_acquire)) {
  control->base_builder = NULL;
  control->transport = NULL;
  stream->deferred_control = NULL;
  stream->deferred_generation = 0u;
}
```

Now reset/reuse application stream slot. Stable control stays CANCELED until application consumes the handle. H1 disconnect publishes equivalent transport cancellation without freeing control memory.

- [ ] **Step 8: Implement public reply result mapping**

After generation validation:

- exact CANCELED -> clear handle, `SALTS_ECANCELED`;
- IDLE or generation mismatch -> `SALTS_ENOENT`;
- WRITING/READY/SUBMITTED duplicate -> `SALTS_EALREADY`;
- only PENDING acquires WRITING.

Cancellation observed during retryable copy failure wins and returns `SALTS_ECANCELED`.

- [ ] **Step 9: Run regression and commit**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$' \
  --output-on-failure

git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/chttp_server_test.c \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c
git commit -m "feat(chttp): isolate canceled HTTP2 deferred streams"
```

**Reviewer gate:** PENDING cancellation releases only stream transport state; canceled application work occupies control capacity but not H2 stream capacity.

---

### Task 5: Prove the Deterministic WRITING/RST Race

**Files:**
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/tests/chttp_h2_deferred_test.c`

**Private non-installed test seam:**

```c
typedef void (*chttp_server_deferred_write_probe_fn)(void *user);

void chttp_server_deferred_test_set_write_probe(
    chttp_server_deferred_write_probe_fn probe,
    void *user);
```

The probe executes immediately after successful PENDING -> WRITING and before reply-builder reset/copy. NULL means no behavior change.

- [ ] **Step 1: Add deterministic gate**

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

- [ ] **Step 2: Add WRITING/RST RED**

With `h2_stream_capacity = 2`: stream A defers; worker enters WRITING and blocks; wait `entered == 1`; cancel A; prove A application slot is quarantined, not reset; prove the other slot serves synchronous sibling B; release worker; worker returns `SALTS_ECANCELED` and clears A; owner releases quarantined A; later request safely reuses capacity.

- [ ] **Step 3: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$' --output-on-failure
```

- [ ] **Step 4: Implement WRITING cancellation handoff**

Stream terminal while WRITING:

```c
atomic_store_explicit(&control->cancel_requested, 1, memory_order_release);
stream->deferred_quarantined = true;
```

Detach protocol user-data/decrement active protocol-stream accounting exactly once, but do not reset request/base-builder storage.

After bounded worker copy, check `cancel_requested` before restoring PENDING or publishing READY. If set:

```c
chttp_server_response_builder_reset(&control->reply_builder);
control->base_builder = NULL;
atomic_store_explicit(&control->state, CHTTP_SERVER_DEFERRED_CANCELED,
                      memory_order_release);
*deferred = (chttp_server_deferred)CHTTP_SERVER_DEFERRED_INIT;
(void)cnet_client_wake(&control->server->network);
return SALTS_ECANCELED;
```

Owner sees CANCELED + quarantined stream after writer exit, resets stream, then returns control IDLE because worker consumed public lease.

- [ ] **Step 5: Run H2 regression and commit**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_h2_proto_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_h2_proto_test)$' \
  --output-on-failure

git add chttp/src/chttp_server_runtime.h \
        chttp/src/chttp_server_response.c \
        chttp/src/chttp_h2_server.c \
        chttp/tests/chttp_h2_deferred_test.c
git commit -m "test(chttp): prove HTTP2 deferred cancel race"
```

**Reviewer gate:** no sleep-based proof, no owner reset of WRITING storage, no worker H2 protocol call.

---

### Task 6: Lock READY/SUBMITTED Borrowed-Body Lifetime and Stop/GOAWAY Drain

**Files:**
- Modify: `chttp/src/chttp_h2_server.h`
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Modify: `chttp/src/chttp_server.c`

**Private deterministic terminal seam:**

Expose only from non-installed `chttp_h2_server.h` for white-box state testing:

```c
int chttp_h2_server_deferred_test_terminal(
    chttp_server_deferred_control *control,
    uint32_t stream_generation);
```

It invokes the same internal READY/SUBMITTED terminal-release helper used by real stream close; it does not call protocol functions or exist in the public header. The test uses it only to make READY-before-owner-submit terminalization deterministic.

- [ ] **Step 1: Add amendment-2 borrowed-body RED**

Use deterministic 96 KiB body and small send chunk:

```c
enum { BODY_BYTES = 96 * 1024 };
config.stream_chunk_bytes = 1024u;
config.max_response_body_bytes = BODY_BYTES;
config.max_buffered_response_body_bytes = BODY_BYTES;
```

Before successful worker reply save stable pointer:

```c
chttp_server_deferred_control *control =
    (chttp_server_deferred_control *)deferred.impl;
check_equal(chttp_server_deferred_reply(&deferred, &reply), SALTS_OK);
```

Do not advance client flow-control enough to finish stream. Wait through server progress until:

```c
chttp_server_deferred_control_state(control) == CHTTP_SERVER_DEFERRED_SUBMITTED
```

Require not IDLE while outbound body remains. Resume client progress, verify every byte, wait exact stream terminal close, require IDLE, then successfully defer later request using released capacity.

- [ ] **Step 2: Add deterministic READY-before-submit terminal RED**

Use a dedicated control/stream from the focused test. After a valid copied reply reaches READY but before H2 owner submission, invoke the private terminal seam with the exact captured stream generation. Require copied builder is dropped/reset and control returns IDLE. Repeat with a wrong generation and require no release/mutation.

This proves READY terminal behavior independently from OS scheduling; the real stream-close path must call the same internal helper.

- [ ] **Step 3: Add stop-timeout RED with exact deadline**

Hold one PENDING lease and call:

```c
check_equal(chttp_server_stop(&server,
                              CHTTP_H2_DEFERRED_TEST_STOP_TIMEOUT_MS),
            SALTS_ETIMEDOUT);
```

The server/control/handle remain valid. Complete handle, drain response, retry with `CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS`, require `SALTS_OK`.

- [ ] **Step 4: Add server GOAWAY drain RED**

After handler defer, start stop on another thread using `CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS`. Complete accepted lease while stop drains. Client receives full response; stream closes; control IDLE; stop `SALTS_OK`.

- [ ] **Step 5: Run RED**

```bash
cmake --build --preset build-default-linux --target chttp_h2_deferred_test
ctest --preset test-release-linux -R '^chttp_h2_deferred_test$' --output-on-failure
```

- [ ] **Step 6: Enforce READY -> SUBMITTED -> terminal-close release**

`chttp_h2_server_submit_response_from()` never resets deferred reply storage on successful submit. Publish SUBMITTED only after submit succeeds. Real stream close and the private test seam both use one internal release helper that validates stream/control generation before resetting storage.

Connection termination terminalizes protocol streams before deferred-control storage can be destroyed/reused.

- [ ] **Step 7: Include every non-IDLE control in stop/reuse accounting**

`chttp_h2_server_connection_deferred_active()` returns true for PENDING, WRITING, READY, SUBMITTED, CANCELED.

- [ ] **Step 8: Run regression and commit**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_h2_server_test chttp_server_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test)$' \
  --output-on-failure

git add chttp/src/chttp_h2_server.h \
        chttp/tests/chttp_h2_deferred_test.c \
        chttp/src/chttp_h2_server.c \
        chttp/src/chttp_server.c
git commit -m "fix(chttp): retain deferred HTTP2 bodies through send"
```

**Reviewer gate:** `chttp_h2_proto_submit_response_ex()` borrows body storage; reset/reuse before exact terminal stream close is a blocker.

---

### Task 7: Add TLS/JWT/Unsupported-Boundary Parity and Public Documentation

**Files:**
- Modify: `chttp/tests/chttp_h2_deferred_test.c`
- Modify: `chttp/tests/chttp_server_test.c`
- Modify: `chttp/tests/chttp_websocket_test.c`
- Modify: `chttp/tests/chttp_header_cpp_test.cpp`
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/README.md`

- [ ] **Step 1: Add TLS ALPN `h2` happy path**

Include `chttp_tls_test_material.h`. Build server TLS config from `CHTTP_TLS_TEST_CERTIFICATE` and `CHTTP_TLS_TEST_KEY`, enable H2, negotiate exactly `h2`. Build client trust/profile from `CHTTP_TLS_TEST_CERTIFICATE` and H2 ALPN.

Construct URI from the runtime port, not a hard-coded placeholder:

```c
char connection_uri[128];
uint16_t port = 0u;
check_equal(chttp_server_port(&server, &port), SALTS_OK);
check_true(snprintf(connection_uri, sizeof(connection_uri),
                    "tls://127.0.0.1:%u", (unsigned int)port) > 0);
```

Run deferred GET with `protocol = CHTTP_HTTP_2`; require exact 200/body and no H1 fallback.

- [ ] **Step 2: Add TLS sibling cancellation parity**

Over same ALPN-h2 connection: defer A, cancel A, synchronous sibling B returns 200, then terminalize A server lease with `SALTS_ECANCELED`.

- [ ] **Step 3: Add TLS stop/drain parity**

Defer one accepted request, initiate stop/GOAWAY with `CHTTP_H2_DEFERRED_TEST_TIMEOUT_MS`, publish reply, verify response and successful stop.

- [ ] **Step 4: Add JWT protected defer**

Use a 32-byte HS256 key and `chttp_server_route_with_jwt_bearer()`. Handler copies callback-time subject:

```c
if (request->jwt_claims == NULL || request->jwt_claims->subject == NULL)
  return SALTS_EPROTO;
snprintf(probe->subject, sizeof(probe->subject), "%s",
         request->jwt_claims->subject);
```

Worker uses only copied `probe->subject`, never `request->jwt_claims`. Valid token -> deferred 200; missing token -> admission 401 with no handler/defer call.

- [ ] **Step 5: Keep Session defer unsupported**

For H1 and H2 handlers with live Session state:

```c
check_equal(chttp_server_response_defer(response, &deferred), SALTS_ENOTSUP);
```

No handle published; handler can send ordinary synchronous response.

- [ ] **Step 6: Keep WebSocket opening defer unsupported**

In H1 Upgrade and RFC8441 `on_open`, attempt defer and require `SALTS_ENOTSUP`. Existing normal WebSocket opens remain green.

- [ ] **Step 7: Update public comments without ABI change**

```c
/**
 * One generation-checked deferred HTTP response. The handle is completed
 * exactly once by `chttp_server_deferred_reply()` and must not outlive the
 * server's successful stop. Supported for ordinary HTTP/1.1 and HTTP/2
 * handlers; Session-backed requests and WebSocket opening callbacks are
 * unsupported.
 */
```

Update `chttp_server_response_defer()` docs with H1 one-per-connection capacity, H2 `h2_stream_capacity` control bound, stream-scoped RST cancellation, failure-atomic `SALTS_ENOBUFS`, and successful worker transfer with private H2 storage retained until stream terminal close. No signature/public-struct change.

- [ ] **Step 8: Update README with exact async ownership order**

```text
handler copies owned request facts
  -> reserve CHTTP deferred lease
  -> lease failure: ordinary synchronous bounded error
  -> attempt app/graph admission
  -> app rejection: deferred_reply(429/503) immediately
  -> app accepted: retain owned app data + deferred handle
  -> every terminal app outcome calls deferred_reply exactly once
```

Explain PENDING/WRITING/READY/SUBMITTED/CANCELED as lifecycle behavior only, not public API.

- [ ] **Step 9: Run focused parity/public-header tests**

```bash
cmake --build --preset build-default-linux --target \
  chttp_h2_deferred_test chttp_server_test chttp_websocket_test \
  chttp_jwt_test chttp_header_cpp_test
ctest --preset test-release-linux -R \
  '^(chttp_h2_deferred_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$' \
  --output-on-failure
```

Expected: PASS.

- [ ] **Step 10: Commit**

```bash
git add chttp/tests/chttp_h2_deferred_test.c \
        chttp/tests/chttp_server_test.c \
        chttp/tests/chttp_websocket_test.c \
        chttp/tests/chttp_header_cpp_test.cpp \
        chttp/include/chttp/chttp.h \
        chttp/README.md
git commit -m "docs(chttp): document HTTP2 deferred response contract"
```

**Reviewer gate:** public ABI unchanged; JWT identity does not escape callback lifetime; Session/WS/streaming/file scope excluded.

---

### Task 8: Exact-Head Cross-Platform Verification, Cleanup, and Review Gate

**Files:**
- Create temporarily: `.github/workflows/chttp-h2-deferred-verifier.yml`
- Delete before final clean head: `.github/workflows/chttp-h2-deferred-verifier.yml`
- No production change unless verification exposes an in-scope defect.

**Fixed base for provenance:** `0b1cdb3a2e23080dd53c18428c3dfec3592d3559`

- [ ] **Step 1: Freeze implementation head and audit scope**

```bash
VERIFIED_IMPLEMENTATION_SHA="$(git rev-parse HEAD)"
printf '%s\n' "$VERIFIED_IMPLEMENTATION_SHA"
git diff --name-only 0b1cdb3a2e23080dd53c18428c3dfec3592d3559...HEAD
```

Allowed final content paths:

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

A needed additional production path requires architecture review before adding it.

- [ ] **Step 2: Create temporary three-host verifier with exact checkout input**

Header:

```yaml
name: CHTTP H2 deferred exact-head verifier

on:
  workflow_dispatch:
    inputs:
      ref:
        description: Exact commit SHA to verify
        required: true
        type: string

permissions:
  contents: read
```

Every job checks out:

```yaml
- uses: actions/checkout@v4
  with:
    ref: ${{ inputs.ref }}
```

and runs `git rev-parse HEAD`; recorded SHA must equal `$VERIFIED_IMPLEMENTATION_SHA` used for dispatch.

**Linux:**

```yaml
linux:
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
      with:
        ref: ${{ inputs.ref }}
    - name: Setup re2c and vcpkg
      shell: bash
      run: |
        sudo apt-get update
        sudo apt-get install -y nasm re2c
        echo "PROJECT_ROOT=$GITHUB_WORKSPACE" >> "$GITHUB_ENV"
        if [ -n "${VCPKG_INSTALLATION_ROOT:-}" ]; then
          echo "VCPKG_ROOT=$VCPKG_INSTALLATION_ROOT" >> "$GITHUB_ENV"
        else
          VCPKG_BIN="$(readlink -f "$(command -v vcpkg)")"
          echo "VCPKG_ROOT=$(dirname "$VCPKG_BIN")" >> "$GITHUB_ENV"
        fi
    - name: Configure
      run: cmake --preset release-linux-ninja
    - name: Build
      run: cmake --build --preset build-default-linux
    - name: Focused tests
      run: >-
        ctest --preset test-release-linux -R
        '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$'
        --output-on-failure
    - name: Full suite
      run: ctest --preset test-release-linux --output-on-failure
```

**macOS:**

```yaml
macos:
  runs-on: macos-15
  steps:
    - uses: actions/checkout@v4
      with:
        ref: ${{ inputs.ref }}
    - name: Setup re2c and vcpkg
      shell: bash
      run: |
        brew install nasm re2c
        if [ -n "${VCPKG_ROOT:-}" ] &&
           [ -f "$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
          true
        elif [ -n "${VCPKG_INSTALLATION_ROOT:-}" ] &&
             [ -f "$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake" ]; then
          echo "VCPKG_ROOT=$VCPKG_INSTALLATION_ROOT" >> "$GITHUB_ENV"
        elif [ -f "$(brew --prefix)/share/vcpkg/scripts/buildsystems/vcpkg.cmake" ]; then
          echo "VCPKG_ROOT=$(brew --prefix)/share/vcpkg" >> "$GITHUB_ENV"
        else
          echo "A valid VCPKG_ROOT is unavailable" >&2
          exit 1
        fi
    - name: Configure
      run: cmake --preset release-mac-ninja
    - name: Build
      run: cmake --build --preset build-default-mac
    - name: Focused tests
      run: >-
        ctest --preset test-release-mac -R
        '^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$'
        --output-on-failure
    - name: Full suite
      run: ctest --preset test-release-mac --output-on-failure
```

**Windows:**

```yaml
windows:
  runs-on: windows-latest
  steps:
    - uses: actions/checkout@v4
      with:
        ref: ${{ inputs.ref }}
    - name: Setup re2c
      shell: powershell
      run: choco install re2c -y --no-progress
    - name: Setup Windows build environment
      shell: pwsh
      run: |
        $ErrorActionPreference = "Stop"
        if ([string]::IsNullOrWhiteSpace($env:VCPKG_INSTALLATION_ROOT)) {
          throw "VCPKG_INSTALLATION_ROOT is unavailable"
        }
        $vcpkgRoot = $env:VCPKG_INSTALLATION_ROOT
        $vcpkgToolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
        if (-not (Test-Path -LiteralPath $vcpkgToolchain -PathType Leaf)) {
          throw "vcpkg toolchain is unavailable: $vcpkgToolchain"
        }
        $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
        $vsInstall = & $vswhere -latest -products '*' `
          -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
          -property installationPath
        if ([string]::IsNullOrWhiteSpace($vsInstall)) {
          throw "Visual Studio x64 C++ toolchain not found"
        }
        "PROJECT_ROOT=$env:GITHUB_WORKSPACE" >> $env:GITHUB_ENV
        "VCPKG_ROOT=$vcpkgRoot" >> $env:GITHUB_ENV
        "VSINSTALL=$vsInstall" >> $env:GITHUB_ENV
    - name: Configure
      shell: cmd
      run: |
        call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 exit /b 1
        cmake --preset release-win-msvc-ninja
    - name: Build
      shell: cmd
      run: |
        call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 exit /b 1
        cmake --build --preset build-release-windows
    - name: Focused tests
      shell: cmd
      run: |
        call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 exit /b 1
        ctest --preset test-release-windows -R "^(chttp_h2_deferred_test|chttp_h2_server_test|chttp_server_test|chttp_websocket_test|chttp_jwt_test|chttp_header_cpp_test)$" --output-on-failure
    - name: Full suite
      shell: cmd
      run: |
        call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul
        if errorlevel 1 exit /b 1
        ctest --preset test-release-windows --output-on-failure
```

Dispatch with exact `ref` equal to `$VERIFIED_IMPLEMENTATION_SHA`.

- [ ] **Step 3: Require focused matrix green on all three hosts**

Mandatory exact-SHA behavior:

```text
H1 deferred success/retry/transport-loss
h2c cross-thread success
retained/replaced headers + retryable body failure
lease-first immediate 503 terminalization
control pool exhaustion/recovery
PENDING RST + stale generation + sibling isolation
WRITING deterministic RST race
READY deterministic drop-before-submit
SUBMITTED large borrowed body through terminal close
stop timeout/retry
GOAWAY drain
TLS ALPN h2 success/cancel/stop
JWT protected defer
Session/WS unsupported
C++ public header compile
```

Any focused failure blocks #214; never baseline it away.

- [ ] **Step 4: Classify unrelated full-suite failures only with fixed-base provenance**

If full suite fails outside focused matrix:

1. record exact test/case/error;
2. checkout `0b1cdb3a2e23080dd53c18428c3dfec3592d3559` in same host environment;
3. fresh configure/build base;
4. run exact failing test/case;
5. classify pre-existing only if same failure reproduces;
6. run remaining baseline-aware full suite excluding only proven identical baseline cases;
7. keep all focused #214 tests mandatory.

Failure not reproduced on fixed base returns to introducing task.

- [ ] **Step 5: Run static/API audits at verified SHA**

```bash
rg -n 'chttp_server_deferred|CHTTP_SERVER_DEFERRED_' chttp/include chttp/src chttp/tests
rg -n 'HTTP/2 currently returns|HTTP/1\.1 handler can|HTTP/1\.1 response' \
  chttp/README.md chttp/include/chttp/chttp.h
rg -n 'T''ODO|T''BD|F''IXME' \
  docs/superpowers/specs/2026-09-06-chttp-http2-deferred-responses-design*.md \
  docs/superpowers/plans/2026-09-06-chttp-http2-deferred-responses.md
```

The last command must return no matches. The split shell literals prevent the audit command from matching itself in this plan.

Required review facts:

- no public H2-only deferred type/symbol;
- no public-handle cast to H1 connection/H2 stream;
- no deferred body reset between READY -> SUBMITTED -> exact terminal close;
- no worker H2 protocol call;
- no reset of WRITING base-builder storage;
- no H1/synchronous fallback;
- docs no longer say H2 defer unsupported.

- [ ] **Step 6: Remove verifier and prove clean final content**

```bash
git rm .github/workflows/chttp-h2-deferred-verifier.yml
git commit -m "chore(ci): remove HTTP2 deferred verifier"
git diff --name-only 0b1cdb3a2e23080dd53c18428c3dfec3592d3559...HEAD
```

Final branch diff contains no temporary verifier file.

Record verified tree before cleanup:

```bash
git show "$VERIFIED_IMPLEMENTATION_SHA^{tree}"
```

After cleanup, compare every non-verifier path against `$VERIFIED_IMPLEMENTATION_SHA`; production/test/doc content cannot change during verifier cleanup.

- [ ] **Step 7: Run normal clean-head PR check and review state**

Open/update PR only after verifier cleanup. Require normal `C API notation` on clean head, then recheck changed files, head/base SHAs, mergeability, reviews, inline threads.

- [ ] **Step 8: Prepare review packet and stop**

PR body records:

```text
approved design + both amendments
fixed design base 0b1cdb3a2e23080dd53c18428c3dfec3592d3559
verified implementation SHA
final clean head SHA
H1 compatibility evidence
Linux exact-head evidence
Windows exact-head evidence
macOS exact-head evidence / proven fixed-base baseline if any
h2c + TLS ALPN h2 matrix
PENDING/WRITING/READY/SUBMITTED/CANCELED proof summary
public ABI unchanged
no Session/WS/streaming/file scope expansion
TurboFlow lease-before-graph ownership contract
```

Stop at code-review/integration gate. Merge and issue #214 closure require separate explicit user decision.

**Reviewer gate:** no merge-ready claim without exact-head focused evidence on all three platforms and removal of temporary verifier scaffolding.

---

## Plan Self-Review Checklist

- Stable control + unchanged public handle: Tasks 1-2.
- Fixed H2 control pool separate from stream slots: Task 2.
- Failure-atomic defer + retryable copy: Task 3.
- Lease-before-graph/application admission amendment: Task 3.
- PENDING cancellation + stale generation + physical close + sibling isolation: Task 4.
- Deterministic WRITING/RST quarantine: Task 5.
- Deterministic READY terminal drop + SUBMITTED borrowed-body lifetime amendment: Task 6.
- GOAWAY/stop timeout/retry: Task 6.
- h2c + TLS ALPN h2: Tasks 2-7.
- JWT callback-lifetime boundary: Task 7.
- Session/WebSocket explicit non-goals: Task 7.
- Public C/C++ docs/API compatibility: Task 7.
- Linux/Windows/macOS exact-head evidence and clean final branch: Task 8.

No task adds deferred streaming, deferred files, a public cancel/abandon API, graph rollback, protocol fallback, or a generic async abstraction.