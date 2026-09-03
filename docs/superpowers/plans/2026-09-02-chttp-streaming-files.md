# CHTTP H1/H2 Streaming Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 CHTTP 的 H1/H2 client/server 实现统一、有界、可背压的正文 source/sink，并在其上提供文件上传、原子下载和服务端文件响应。

**Architecture:** 公开层只暴露协议无关的 callback strategy；H1 adapter 负责 Content-Length/chunked 和单 send-in-flight，H2 adapter 负责 DATA/END_STREAM 与窗口 credit。同步文件便利层使用 `turbo_fs`，下载通过同目录临时文件、fsync、close、rename 形成事务提交。

**Tech Stack:** C11、CHTTP、CNet、llhttp（PRIVATE）、CHTTP H2 engine（PRIVATE）、Rocida Core `turbo_fs`、TinyTest、CMake Presets。

**Spec:** `docs/CHTTP_STREAMING_FILE_DESIGN.md`

## Global Constraints

- `body/body_size` 与 `body_source` 互斥，旧的完整缓冲调用行为保持不变。
- `stream_chunk_bytes == 0` 选择 64 KiB；非零值是每个活跃 source 的硬内存上限。
- `max_request_body_bytes` 与 `max_response_body_bytes` 始终限制累计正文大小。
- source/sink callback 不得阻塞或重入 owner；borrowed chunk 在 callback 返回时失效。
- H1 source/sink 失败关闭独占连接；H2 source/sink 失败只 reset 当前 stream。
- 文件 I/O 只使用 Rocida `turbo_fs`；下载仅在 2xx、完整写入、fsync、close 后原子 rename。
- llhttp、H2 engine、文件句柄和第三方错误类型不得进入公开头文件。

---

### Task 1: Public streaming contract and validation

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_internal.h`
- Modify: `chttp/src/chttp_request.c`
- Test: `chttp/tests/chttp_request_test.c`
- Test: `chttp/tests/chttp_api_test.c`

**Interfaces:**
- Consumes: existing `chttp_request_options`, `chttp_options`, `chttp_client_config`.
- Produces: `chttp_body_source`, `chttp_body_sink`, appended `body_source/body_sink`, and internal normalized stream config.

- [ ] **Step 1: Write failing validation and header tests**

Add TinyTest cases that initialize source/sink callbacks, assert memory body plus source returns `TURBO_EINVAL`, missing callbacks return `TURBO_EINVAL`, known length above `max_request_body_bytes` returns `TURBO_EMSGSIZE`, and zero `stream_chunk_bytes` normalizes to 65536.

- [ ] **Step 2: Run the focused tests and verify RED**

Run the `chttp_request_test`, `chttp_api_test`, and `chttp_header_cpp_test` targets/tests with `win-release-user`. Expected failure: the public source/sink symbols and config field do not exist.

- [ ] **Step 3: Add the public types and centralized admission validation**

Use these exact declarations:

```c
typedef int (*chttp_body_read_fn)(void *, void *, size_t, size_t *);
typedef int (*chttp_body_write_fn)(void *, const void *, size_t);
typedef struct chttp_body_source {
  chttp_body_read_fn read;
  void *user;
  size_t content_length;
  int content_length_known;
} chttp_body_source;
typedef struct chttp_body_sink {
  chttp_body_write_fn write;
  void *user;
} chttp_body_sink;
```

Append `const chttp_body_source *body_source`, `const chttp_body_sink *body_sink`, and config `size_t stream_chunk_bytes`; normalize zero to the named 64 KiB constant and reject arithmetic/capacity violations.

- [ ] **Step 4: Re-run focused tests and verify GREEN**

Run the same three tests and require all pass.

### Task 2: H1 client source/sink state machine

**Files:**
- Modify: `chttp/src/chttp_request.c`
- Modify: `chttp/src/chttp_response.c`
- Modify: `chttp/src/chttp_client.c`
- Modify: `chttp/src/chttp_internal.h`
- Test: `chttp/tests/chttp_request_test.c`
- Test: `chttp/tests/chttp_response_test.c`
- Test: `chttp/tests/chttp_api_test.c`

**Interfaces:**
- Consumes: Task 1 source/sink contracts and CNet `on_send` completion.
- Produces: H1 Content-Length/chunked source adapter and incremental response sink delivery.

- [ ] **Step 1: Write failing H1 wire and real-socket tests**

Add tests for a known-length POST split across at least three reads, an unknown-length POST producing correct chunked framing, exact-length early EOF returning `TURBO_EPROTO`, response chunks delivered in order to a sink, and sink failure terminating exactly once.

- [ ] **Step 2: Run focused tests and verify RED**

Run the three focused tests. Expected failures are missing source header framing, missing `on_send` progression, and response parser still accumulating body.

- [ ] **Step 3: Implement the one-send-in-flight adapter**

Build headers without copying source bytes; after each CNet `on_send`, pull no more than `stream_chunk_bytes`. Encode unknown chunks as `<hex>\r\n<data>\r\n` and one final `0\r\n\r\n`; enforce exact known length. Initialize response parser with an optional copied sink descriptor and return body NULL plus cumulative size in sink mode.

- [ ] **Step 4: Run focused tests and verify GREEN**

Require all H1 request/response/API tests pass, including existing keep-alive reuse tests.

### Task 3: H2 client source/sink and stream isolation

**Files:**
- Modify: `chttp/src/chttp_h2_session.h`
- Modify: `chttp/src/chttp_h2_session.c`
- Modify: `chttp/src/chttp_h2_proto.c`
- Test: `chttp/tests/chttp_h2_client_test.c`
- Test: `chttp/tests/chttp_h2_proto_test.c`

**Interfaces:**
- Consumes: Task 1 callbacks and private `chttp_h2_proto_submit_request_ex` source hook.
- Produces: H2 DATA source adapter, response sink adapter, exact Content-Length enforcement, and per-stream failure.

- [ ] **Step 1: Write failing multiplexed client tests**

Submit two streams on one session: one streams a multi-chunk request/response, while the other fails its source or sink. Assert the good sibling completes, the bad stream gets one terminal error, connection count stays one, and credited bytes never precede successful sink acceptance.

- [ ] **Step 2: Run H2 tests and verify RED**

Run `chttp_h2_client_test` and `chttp_h2_proto_test`. Expected failure: session still copies full bodies and ignores callbacks.

- [ ] **Step 3: Implement H2 adapters**

Pass a bounded adapter through `chttp_h2_proto_submit_request_ex`; emit Content-Length only when known, enforce exact EOF, and map `(size_t)-1` to the stored source status. Deliver DATA to sink before calling consume; on failure send RST_STREAM and preserve siblings.

- [ ] **Step 4: Re-run H2 and neighboring H1 tests**

Require H2 tests and all H1 client tests pass.

### Task 4: Requests-style file upload and transactional download

**Files:**
- Create: `chttp/src/chttp_files.c`
- Modify: `chttp/CMakeLists.txt`
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_requests.c`
- Test: `chttp/tests/chttp_requests_test.c`

**Interfaces:**
- Consumes: Tasks 1-3 source/sink and `turbo_fs_open/read/write/stat/fsync/close/rename/unlink`.
- Produces: `chttp_post_file`, `chttp_put_file`, `chttp_download_file`, and `chttp_progress_fn`.

- [ ] **Step 1: Write failing H1/H2 file tests**

For both protocols, upload a file larger than one chunk and compare exact server bytes. Download into a path that already contains sentinel data; assert 2xx atomically replaces it, non-2xx and interrupted bodies preserve it, no `.chttp-*.part` file remains, and progress is monotonic with final transferred size.

- [ ] **Step 2: Run requests tests and verify RED**

Build/run `chttp_requests_test`. Expected failure: file APIs are undeclared.

- [ ] **Step 3: Implement file adapters with Rocida Core**

Stat/open uploads and expose an exact known-length source. Create a UUID-named temp beside the destination, write via sink, then fsync/close/rename only for 2xx. Close/unlink on every failure and preserve native status plus stable stage. Add `Rocida::Core` as a PRIVATE CHTTP dependency.

- [ ] **Step 4: Re-run requests tests and verify GREEN**

Require both H1/H2 file cases and existing requests-style cases pass.

### Task 5: Server streaming upload routes

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_server_runtime.h`
- Modify: `chttp/src/chttp_server_route.c`
- Modify: `chttp/src/chttp_server_parser.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Test: `chttp/tests/chttp_server_test.c`
- Test: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Consumes: public sink and existing route/middleware/final dispatch.
- Produces: route `body_open/body_close`, request `body_streamed`, incremental H1/H2 request delivery.

- [ ] **Step 1: Write failing server upload tests**

Register streaming POST routes for H1 and H2. Assert open once after headers, ordered chunk writes, close once with terminal status, final middleware/session/handler only after successful EOF, max total body enforcement, H1 connection close on sink failure, and H2 sibling stream survival.

- [ ] **Step 2: Run server tests and verify RED**

Run `chttp_server_test` and `chttp_h2_server_test`. Expected failure: route options lack sink callbacks and parsers still allocate complete request bodies.

- [ ] **Step 3: Implement route-scoped streaming state**

Match the route after request headers, call `body_open`, copy the returned sink descriptor into connection/stream state, and deliver chunks before restoring H2 credit. Call `body_close` exactly once. On successful EOF dispatch the existing middleware/session/handler with `body_streamed=1`; keep ordinary routes unchanged.

- [ ] **Step 4: Re-run server tests and verify GREEN**

Require H1/H2 streaming cases plus every existing middleware, Session, 404/405 and shutdown regression pass.

### Task 6: Server response source and file response

**Files:**
- Modify: `chttp/include/chttp/chttp.h`
- Modify: `chttp/src/chttp_server_response.c`
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/src/chttp_h2_server.c`
- Test: `chttp/tests/chttp_server_test.c`
- Test: `chttp/tests/chttp_h2_server_test.c`

**Interfaces:**
- Consumes: public source and server one-send-in-flight/H2 submit-data paths.
- Produces: `chttp_server_response_source` and `chttp_server_response_file` with server-owned file lifecycle.

- [ ] **Step 1: Write failing H1/H2 response tests**

Serve a file larger than one chunk and a callback source through H1/H2; assert exact bytes, Content-Length, HEAD body suppression, file close on peer cancellation, source failure isolation, and unchanged small memory replies.

- [ ] **Step 2: Run server tests and verify RED**

Expected failure: builder only accepts copied memory bodies.

- [ ] **Step 3: Implement response source strategies**

Copy source descriptors into the response state. H1 sends headers then one bounded body/chunk per `on_send`; H2 submits HEADERS then DATA under flow control. The file helper stats/opens with `turbo_fs`, installs an exact-length source, and transfers ownership of its private context to the response terminal cleanup path.

- [ ] **Step 4: Re-run all CHTTP server/client tests**

Require all `^chttp_` tests pass under `win-release-user`.

### Task 7: Documentation, package surface and verification

**Files:**
- Modify: `chttp/README.md`
- Modify: `docs/CHTTP_CNET_PROTOCOL_TODO.md`
- Modify: `docs/CHTTP_H2_S3_DESIGN.md`
- Modify: `book/第十三章：从基础能力到 Modern C——Serialization、RPC、Plugin、Workflow 与更多应用.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`
- Test: `chttp/tests/chttp_header_cpp_test.cpp`

**Interfaces:**
- Consumes: all public APIs from Tasks 1-6.
- Produces: synchronized public documentation and install/export coverage.

- [ ] **Step 1: Add compile-time consumer use before final docs**

Use zero-initialized source/sink in C and C++ installed consumers and take addresses of all file/server streaming functions; build before editing documentation and verify missing surface causes RED if any declaration/export is absent.

- [ ] **Step 2: Update docs with complete contracts and runnable examples**

Document callback lifetime, reentrancy, length/framing, body NULL semantics, total bounds, middleware/session timing, atomic download behavior, error stages and identical H1/H2 call sites.

- [ ] **Step 3: Format and run focused verification**

Run `clang-format -i` on changed C/C++ sources, build CHTTP targets with `win-release-user`, then run `ctest --preset win-release-user -R "^chttp_" --output-on-failure`.

- [ ] **Step 4: Run sanitizer, installed package and full regression verification**

Run focused CHTTP tests with `win-dev-user`, execute `verify_installed_package`, build the full Release preset, and run the full CTest preset. Finish with `codegraph sync .`, `git diff --check`, and `git status --short`; `.codegraph/` must remain untracked/ignored.
