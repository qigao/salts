# CRPC Server, Batch and Notification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** 在 CHTTP server 之上实现有界、严格 JSON-RPC 2.0 的 CRPC server，支持多个 endpoint、CMeta method、CSerde 参数/结果、notifications 与有序 batch，并让用户无需直接管理 poller。

**Architecture:** `crpc_server` 独占一个 `chttp_server`，以 `target + wire method` 为注册键。CHTTP 负责 transport、TLS、H1/H2、middleware 与 session；CRPC 负责 JSON-RPC validation、dispatch、响应 envelope 与通知抑制。请求视图只在 handler 回调内有效，响应在 helper 调用时立即编码为有界 owning bytes。

**Tech Stack:** C11, CMeta typed callable, CSerde reader/writer, CHTTP/CNet, TinyTest, CMake Presets

**Spec:** `docs/superpowers/specs/2026-09-03-crpc-server-batch-design.md`

## Global Constraints

- 公开 API 必须完整实现；不暴露 incomplete flag 或占位函数。
- 所有输入尺寸、方法数量、batch 数与 JSON 深度均有硬上限和 checked arithmetic。
- `params` reader、HTTP request view 和 callable view 在 handler 返回时失效，不得跨 callback、协程挂起或 slot 复用保存。
- dispatch 在 CHTTP server worker 上单 owner 顺序执行；handler/user 为 borrowed，必须活到 server destroy。
- 注册只允许在 start 前；运行期不提供 unregister，避免 CHTTP route 生命周期悬空。
- notification 必须执行 handler 但绝不输出 JSON-RPC response；全通知 batch 返回 HTTP 204。
- streaming、H3、服务发现与 server-side retry 不属于本计划。

---

## Task 1: Freeze the public server ABI with compile tests

**Files:**
- Modify: `crpc/include/crpc/crpc.h`
- Modify: `crpc/tests/crpc_header_cpp_test.cpp`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

- [x] Add opaque server, request view, response handle, encoder and method callback declarations from the design spec.
- [x] Add `crpc_server_config` with embedded `chttp_server_config`, method capacity, method byte limit, JSON depth and batch item limit.
- [x] Add declarations for init, borrowed HTTP access, registration, result/error completion, start, port, stop and destroy.
- [x] Compile first and record linker failures as the red state; header use must compile in both C and C++.

## Task 2: Build bounded JSON value and response-envelope helpers

**Files:**
- Modify: `crpc/src/crpc_internal.h`
- Modify: `crpc/src/crpc_json.c`
- Modify: `crpc/tests/crpc_json_test.c`

- [x] Add tests for scalar, array, map and null result encoders, explicit error data, omitted error data and writer overflow.
- [x] Generalize the existing CSerde JSON writer with an internal `require_structured_root` policy so client params remain array/map-only while server result/data may be any JSON value.
- [x] Add internal helpers that immediately create complete owning envelopes:

```c
int crpc_json_encode_result(uint64_t id, crpc_encode_value_fn encode,
                            void *user, size_t max_depth,
                            size_t max_bytes, crpc_encoded_request *out);
int crpc_json_encode_error(bool null_id, uint64_t id, int64_t code,
                           const char *message,
                           crpc_encode_value_fn encode_data,
                           void *data_user, size_t max_depth,
                           size_t max_bytes, crpc_encoded_request *out);
```

- [x] Preserve the existing client request encoder behavior and run `crpc_json_test` green.

## Task 3: Implement unary server lifecycle and method registry

**Files:**
- Create: `crpc/src/crpc_server.c`
- Modify: `crpc/src/crpc_internal.h`
- Modify: `crpc/CMakeLists.txt`
- Create: `crpc/tests/crpc_server_test.c`
- Modify: `crpc/tests/CMakeLists.txt`

- [x] Add failing tests for config validation, duplicate method registration, duplicate target route reuse, registration after start, lifecycle forwarding and a successful unary call.
- [x] Preallocate the bounded method table during init and copy validated target/wire-method strings during registration.
- [x] Copy the bound `cmeta_callable` into the method record; do not retain a pointer to the caller's temporary service metadata.
- [x] Register exactly one CHTTP POST route for each distinct target and route it to the shared CRPC adapter.
- [x] Parse and validate a single JSON-RPC object, create a callback-scoped CSerde params reader, dispatch the handler, and require exactly one completion for ordinary calls.
- [x] Return protocol errors with `application/json` and transport/configuration failures through existing Turbo error codes.
- [x] Run `crpc_server_test` green.

## Task 4: Implement notification semantics

**Files:**
- Modify: `crpc/src/crpc_server.c`
- Modify: `crpc/tests/crpc_server_test.c`

- [x] Add failing tests for known notification execution, unknown-method notification suppression, invalid notification suppression and handler completion calls during notifications.
- [x] Treat absent `id` as notification and retain the distinction from malformed or unsupported ids.
- [x] Execute valid notification handlers, mark completion without encoding bytes, and return HTTP 204 with an empty body.
- [x] Ensure notification errors never leak an id-null response.
- [x] Run the focused notification test filter green.

## Task 5: Implement bounded ordered batches

**Files:**
- Modify: `crpc/src/crpc_server.c`
- Modify: `crpc/src/crpc_internal.h`
- Modify: `crpc/tests/crpc_server_test.c`

- [x] Add failing tests for mixed calls/notifications, empty batch, invalid batch elements, all-notification batch, response order and `max_batch_items + 1` rejection.
- [x] Parse a nonempty top-level array and reject excess elements before dispatch.
- [x] Dispatch elements sequentially in input order and collect only non-notification owning response fragments.
- [x] Serialize collected fragments as one bounded JSON array; return HTTP 204 when none remain.
- [x] Return `-32600` with null id for invalid elements and preserve input order among emitted responses.
- [x] Run the batch filter and full `crpc_server_test` green.

## Task 6: Verify endpoints, CMeta/CSerde, middleware, sessions and transports

**Files:**
- Modify: `crpc/tests/crpc_server_test.c`
- Modify: `crpc/README.md`

- [x] Test two methods on `/rpc/math` and one method on `/rpc/status` using the same server and client authority.
- [x] Verify CMeta wire names and copied callables, structured params and scalar/structured CSerde results.
- [x] Use `crpc_server_http()` before start to install a CHTTP middleware/session path and verify it coexists with CRPC routes.
- [x] Run the same unary and batch behavior over H1, cleartext H2 and TLS+H2.
- [x] Document ownership, endpoint identity, single-worker dispatch, registration cutoff, HTTP status mapping and batch limits.

## Task 7: Format, verify ABI and review the complete change

**Files:**
- Modify: all changed CRPC C/C++ sources and docs only as required by verification

- [x] Run `clang-format` on changed C/C++ files.
- [x] Run:

```powershell
cmake --build --preset win-release-user --target crpc_json_test crpc_api_test crpc_transport_test crpc_server_test crpc_header_cpp_test
ctest --preset win-release-user -R "^(crpc_|chttp_server_test|s3_file_test)" --output-on-failure
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

- [x] Run installed-package C and C++ consumers, `git diff --check`, and `codegraph sync .`.
- [x] Review ownership, capacity arithmetic, callback invalidation, notification suppression, shutdown and public ABI before committing.
