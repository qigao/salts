# CRPC TLS/HTTP2 Transport Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 CRPC 同步与异步客户端完整继承 CHTTP 的 TLS、HTTP/1.1 与 HTTP/2 transport 能力，同时保持既有零初始化调用默认为明文 HTTP/1.1。

**Architecture:** `crpc_options` 只声明每次调用的 transport 选择；CRPC 不复制证书验证、ALPN、连接池或协议协商逻辑，而是将 `tls` 与 `protocol` 原样传给 CHTTP。同步和异步路径共享相同配置语义与错误码。

**Tech Stack:** C11, CMeta, CSerde, CHTTP/CNet, TinyTest, CMake Presets

**Spec:** `docs/superpowers/specs/2026-09-03-crpc-transport-parity-design.md`

## Global Constraints

- 保持 `crpc_options` 现有字段顺序，只在末尾追加字段；零初始化仍表示明文 HTTP/1.1。
- TLS profile 为 borrowed view，其生命周期遵循 CHTTP 契约；CRPC 不缓存或销毁它。
- 同步与异步路径必须使用同一组字段和错误语义。
- 测试必须真实覆盖 cleartext H2 与 TLS+H2，不以 mock 证明协议透传。
- 所有命令在 `win-release-user` preset 与 VS x64 环境中执行。

---

## Task 1: Preserve the prerequisite H1 asynchronous-file regression fix

**Files:**
- Modify: `chttp/src/chttp_server.c`
- Modify: `chttp/tests/chttp_server_test.c`

- [x] Add a regression that alternates a streamed file response and a normal response over one client connection for 32 rounds and asserts one accepted connection.
- [x] Reproduce the race where receive was rearmed while asynchronous EOF validation still owned `response_streaming`.
- [x] Gate receive/send-pending rearming on `!connection->response_streaming` in both send completion and file-readiness completion.
- [x] Run the focused test and 100 repetitions of the original H1 S3 scenario.
- [x] Run the Release suite and record `246/246` passing before CRPC work begins.

## Task 2: Add failing transport-parity integration tests

**Files:**
- Create: `crpc/tests/crpc_transport_test.c`
- Modify: `crpc/tests/CMakeLists.txt`
- Read/Reuse: `chttp/tests/chttp_tls_test_material.h`

- [x] Add a TinyTest fixture that starts a CHTTP JSON-RPC echo route with HTTP/2 enabled.
- [x] Add a synchronous cleartext-H2 call using this intended option shape:

```c
crpc_options options = {
    .connection_uri = uri,
    .authority = authority,
    .target = "/rpc",
    .method = "echo",
    .request_id = 1u,
    .protocol = CHTTP_HTTP_2,
};
```

- [x] Add an asynchronous cleartext-H2 call and verify callback delivery without changing the callback ownership contract.
- [x] Add a TLS+H2 fixture using the shared test certificate, CHTTP server ALPN `h2`, and a client TLS profile.
- [x] Verify the TLS+H2 synchronous and asynchronous calls fail before implementation because CRPC does not yet forward the new fields.
- [x] Run:

```powershell
cmake --build --preset win-release-user --target crpc_transport_test
ctest --preset win-release-user -R "^crpc_transport_test$" --output-on-failure
```

## Task 3: Expose and forward the transport selection

**Files:**
- Modify: `crpc/include/crpc/crpc.h`
- Modify: `crpc/src/crpc_request_reply.c`
- Modify: `crpc/src/crpc_client.c`
- Modify: `crpc/tests/crpc_header_cpp_test.cpp`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

- [x] Append the public fields without reordering existing fields:

```c
typedef struct crpc_options {
    /* existing fields */
    const chttp_tls_profile *tls;
    chttp_protocol protocol;
} crpc_options;
```

- [x] Forward both fields from `crpc_options` into the synchronous `chttp_options` initializer.
- [x] Forward both fields from `crpc_options` into the asynchronous `chttp_async_options` initializer.
- [x] Extend C and C++ compile consumers to initialize the fields and verify the installed header exposes no third-party transport type.
- [x] Run the transport tests and require all sync/async, H2/TLS cases to pass.

## Task 4: Document compatibility and verify regressions

**Files:**
- Modify: `crpc/README.md`
- Modify: `docs/superpowers/specs/2026-09-03-crpc-transport-parity-design.md` only if implementation changes the approved contract

- [x] Document H1 plaintext defaults, cleartext H2, TLS H1/H2, TLS profile lifetime, and the fact that client reuse is not a promise that every request uses the same TCP connection.
- [x] Run formatting on changed C/C++ files.
- [x] Run:

```powershell
cmake --build --preset win-release-user --target crpc_json_test crpc_api_test crpc_transport_test crpc_header_cpp_test
ctest --preset win-release-user -R "^crpc_" --output-on-failure
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
```

- [x] Run the installed-package consumer and `git diff --check`.
- [x] Review the diff for TLS-profile lifetime, sync/async parity, and accidental dependency leakage before committing.
