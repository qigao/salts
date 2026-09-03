# CHTTP Async File I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace CHTTP file-transfer data-path blocking with one bounded CFlow asynchronous file runtime per CHTTP owner while preserving the existing H1/H2 public file helpers.

**Architecture:** CFlow gains a shareable file runtime that owns one native backend, actor, executor, completion lane, and fixed operation slots. Individual files own only their native handle and callback state. CHTTP owns one runtime per async client/server, and a protocol-neutral transfer state machine maps file readiness onto H1 send/receive admission or H2 source WAIT/resume and flow-control credit.

**Tech Stack:** C11, CFlow actor/native IOCP/io_uring file operations, CNet, CHTTP H1/llhttp, CHTTP H2 engine, Rocida Core filesystem, TinyTest, CMake Presets.

**Spec:** `docs/CHTTP_STREAMING_FILE_DESIGN.md`

## Global Constraints

- Existing `chttp_post_file`, `chttp_put_file`, `chttp_download_file`, and `chttp_server_response_file` signatures and synchronous caller-visible completion remain unchanged.
- Existing generic `chttp_body_source` and `chttp_body_sink` callbacks remain synchronous and callback-scoped.
- One CHTTP owner mutates HTTP request, connection, stream, and transfer state; native file workers publish terminal completions and wake the owner only.
- Every accepted file operation borrows its buffer until its terminal callback returns; cancellation never releases the buffer or file handle early.
- Runtime, request-slot, completion, transfer, and retained-byte capacities are hard bounds validated with checked arithmetic.
- H1 failure after headers closes the connection; H2 failure resets only the affected stream and preserves sibling streams.
- No unsupported native backend silently falls back to synchronous I/O.
- llhttp, native file handles, CFlow actors, and H2 engine types remain outside the CHTTP public ABI.

---

### Task 1: Shared CFlow file runtime

**Files:**
- Modify: `cflow/include/cflow/io_file.h`
- Modify: `cflow/src/io_file.c`
- Modify: `cflow/tests/cflow_io_file_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Produces: `cflow_io_file_runtime`, `cflow_io_file_runtime_config`, runtime init/run/close/quiescent/stats/destroy functions, and optional `cflow_io_file_config.runtime` injection.
- Preserves: a null injected runtime creates the existing private per-file runtime.

- [ ] **Step 1: Write failing tests**

Add real-file tests which open two files against one runtime, submit one operation to each, drive only `cflow_io_file_runtime_run_ready()`, and assert both callbacks execute on the driver thread. Add capacity, close-with-live-file, shared-runtime lifetime, concurrent/reentrant drive, wake, and cancellation-drain cases.

- [ ] **Step 2: Run the focused test and verify RED**

Run `cflow_io_file_test`; compilation must fail because the runtime API does not yet exist.

- [ ] **Step 3: Implement the runtime**

Use this ownership split:

```c
typedef struct cflow_io_file_runtime { void *impl; } cflow_io_file_runtime;
typedef struct cflow_io_file { void *impl; } cflow_io_file;
```

The runtime owns backend/executor/actor/gate/global slots. Each slot stores its owning file, operation, request id, and delivery state. Each file owns the native handle, access flags, callback, close flag, and whether its runtime is borrowed or private.

- [ ] **Step 4: Verify GREEN**

Run `cflow_io_file_test` and `cflow_header_cpp_test` and require both pass.

### Task 2: External CNet owner wake

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/tests/cnet_api_test.c`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Produces: `int cnet_client_wake(cnet_client *client);`

- [ ] **Step 1: Write failing wake test**

Start a polling thread with a long timeout, wait until it enters polling, call `cnet_client_wake()` from another thread, and assert poll returns before the timeout with zero callbacks. Also assert null and stopped clients return their documented errors.

- [ ] **Step 2: Run and verify RED**

Run `cnet_api_test`; compilation must fail because `cnet_client_wake` is absent.

- [ ] **Step 3: Implement bounded wake forwarding**

Forward the call to the existing shard/event wake primitive without invoking callbacks or changing connection state. Do not acquire a lock across backend wake.

- [ ] **Step 4: Verify GREEN**

Run `cnet_api_test` and `cnet_header_cpp_test`.

### Task 3: H2 source readiness

**Files:**
- Modify: `chttp/src/chttp_h2_proto.h`
- Modify: `chttp/src/chttp_h2_proto.c`
- Modify: `chttp/tests/chttp_h2_proto_test.c`

**Interfaces:**
- Produces: a private DATA/WAIT/EOF/ERROR source result and `chttp_h2_proto_resume_source()`.

- [ ] **Step 1: Write failing readiness tests**

Submit a streaming response whose source first returns WAIT. Assert `send()` emits no DATA or END_STREAM, `want_write()` becomes false, resume makes it true, DATA is emitted once, and a later EOF emits one END_STREAM. Run the same test with a sibling stream and assert the sibling continues while the first source waits.

- [ ] **Step 2: Run and verify RED**

Run `chttp_h2_proto_test`; compilation must fail because WAIT/resume are absent.

- [ ] **Step 3: Implement WAIT/resume**

Store `source_waiting` per stream. WAIT commits no frame and excludes that source from `want_write`. Resume validates a live matching stream, clears waiting, and invokes the existing write-wake callback after the state transition.

- [ ] **Step 4: Verify GREEN**

Run `chttp_h2_proto_test`, `chttp_h2_client_test`, and `chttp_h2_server_test`.

### Task 4: Protocol-neutral outbound file transfer

**Files:**
- Create: `chttp/src/chttp_file_transfer.h`
- Create: `chttp/src/chttp_file_transfer.c`
- Modify: `chttp/src/chttp_files.c`
- Modify: `chttp/src/chttp_client.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/CMakeLists.txt`
- Test: `chttp/tests/chttp_requests_test.c`
- Test: `chttp/tests/chttp_server_test.c`
- Test: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Consumes: shared file runtime, CNet wake, H2 WAIT/resume.
- Produces: `OPEN -> READ_PENDING -> CHUNK_READY -> NETWORK_PENDING -> EOF/CANCELLED` state machine and H1/H2 transport operations selected once per transfer.

- [ ] **Step 1: Write failing slow-file tests**

Use an injectable test file-runtime strategy that withholds completion. Assert server/client network polling remains responsive, no source call reports EOF while the read is pending, and cancellation retains the chunk until terminal completion.

- [ ] **Step 2: Run and verify RED**

Run focused requests and server tests; they must show the existing synchronous file callbacks cannot represent pending readiness.

- [ ] **Step 3: Implement the outbound state machine**

Allocate one bounded chunk lease per active outbound transfer. H1 retains it until send completion; H2 copies it into the protocol output buffer during resumed source pull and immediately makes the lease reusable. All progress callbacks run on the CHTTP owner.

- [ ] **Step 4: Verify GREEN**

Run the focused H1/H2 file tests and existing connection-reuse tests.

### Task 5: Async file receive and H2 credit

**Files:**
- Modify: `chttp/src/chttp_file_transfer.c`
- Modify: `chttp/src/chttp_response.c`
- Modify: `chttp/src/chttp_server_parser.c`
- Modify: `chttp/src/chttp_h2_session.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Test: `chttp/tests/chttp_requests_test.c`
- Test: `chttp/tests/chttp_server_test.c`
- Test: `chttp/tests/chttp_h2_client_test.c`
- Test: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Produces: `RECEIVING -> WRITE_PENDING -> WRITE_COMPLETED -> RECEIVING -> FLUSH/CLOSE/COMMIT` and delayed H2 flow-control consumption.

- [ ] **Step 1: Write failing backpressure tests**

Withhold file-write completion and assert H1 does not admit another parser chunk, H2 does not return stream credit, sibling H2 streams continue, capacity exhaustion returns a distinct error, and cancel removes the temporary file only after terminal completion.

- [ ] **Step 2: Run and verify RED**

Run focused client/server H1/H2 tests and confirm current synchronous sink behavior violates the withheld-completion expectations.

- [ ] **Step 3: Implement bounded receive leases**

Reserve enough fixed chunk leases at file-transfer admission to cover the configured H2 receive window. File offsets are assigned by the owner before submit, so completion order cannot corrupt file position. Return stream and connection credit only after successful completion; on stream failure return connection credit for already-received bytes and send RST_STREAM.

- [ ] **Step 4: Verify GREEN**

Run all focused file, H1 parser, H2 protocol, and sibling-isolation tests.

### Task 6: Transactional close and full verification

**Files:**
- Modify: `chttp/src/chttp_files.c`
- Modify: `chttp/README.md`
- Modify: `cflow/README.md`
- Modify: `docs/CHTTP_STREAMING_FILE_DESIGN.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

**Interfaces:**
- Produces: documented shutdown, error stages, temporary-file commit, and installed-package contracts.

- [ ] **Step 1: Add failure-path tests**

Cover short read/write, backend rejection, flush failure, non-2xx response, source length mismatch, connection loss, stop during read/write, and destroy-before-quiescence. Assert the original destination survives and temporary files are removed.

- [ ] **Step 2: Verify failure tests RED where behavior is missing**

Run the narrow test filters and record the expected failing behavior before each fix.

- [ ] **Step 3: Complete cleanup and documentation**

Stop admission, wake owner, cancel operations, drain terminal completions, release leases, close handles, then destroy runtime. Preserve the first useful error and expose stage plus native status only through CHTTP-owned error fields.

- [ ] **Step 4: Run release and sanitizer verification**

Run focused targets first, then CTest filters for `cflow_`, `cnet_`, and `chttp_`, installed-package verification, complete Release tests, and supported sanitizer-focused tests. Finish with `codegraph sync .`, `git diff --check`, and a source-tree scan proving llhttp/CFlow native types remain private to downstream CHTTP consumers.
