# CRPC TLS/HTTP2 Transport Parity Design

## 背景与证据

CRPC 已把 `connection_uri`、`authority` 与 `target` 作为每次调用参数，但
`crpc_request_reply()` 和 `crpc_async_client_submit()` 构造 CHTTP options 时没有传递
`chttp_protocol` 与 `chttp_tls_profile`。因此当前 CRPC 被固定在明文 HTTP/1.1，尽管 CHTTP 已支持
HTTP/1.1、HTTP/2、TLS、ALPN 和按 transport identity 复用连接。

本改动保持 CRPC 的 JSON-RPC envelope、deadline、CMeta callable 与 CSerde reader 行为不变，只补齐
CRPC 到 CHTTP 的 transport 参数映射。

## 决策

在 `crpc_options` 中增加：

```c
const chttp_tls_profile *tls;
chttp_protocol protocol;
```

选择 per-call 参数而不是复制到 `crpc_client_config`，理由如下：

- 与 `chttp_options` / `chttp_request_options` 的事实源一致；
- 同一个 CRPC client 可以调用不同 origin、TLS profile 或 HTTP 版本；
- CHTTP 继续独占连接池 identity、ALPN 校验、profile retain/release 与错误语义；
- 零初始化仍表示 `CHTTP_HTTP_1_1` 且无 TLS，既有调用源码和行为兼容。

CRPC 不解析 `tls://`，也不读取 TLS profile 内部字段。参数只在 submit/request-reply 调用期间借用，
CHTTP 在 admission 中按既有契约 retain 所需 profile。

## 状态、错误与兼容性

- 同步 client 仍为 single-owner、一次只允许一个 request/reply。
- async client 仍为单 progress owner；submit 成功后 exactly-once terminal callback。
- TLS、ALPN、URI/protocol 不匹配错误原样从 CHTTP 传播，例如 `TURBO_EPROTONOSUPPORT`。
- HTTP/2 使用 CHTTP 的 multiplexed session pool；CRPC 不创建第二套连接或 stream 状态。
- `crpc_options` 是公开结构体，字段追加会改变结构尺寸。源码级零初始化兼容；按值跨 DLL ABI 不兼容。
  本仓库当前 CRPC 是静态库，但 installed C/C++ consumer 与头文件 ABI 编译测试必须同步更新。

## 验证

- 明文 HTTP/2 同步 request/reply；
- TLS HTTP/1.1 同步 request/reply；
- TLS HTTP/2 async submit/poll；
- profile/protocol 不匹配 fail-fast；
- 既有 HTTP/1.1、多 endpoint、deadline、cancel、callable 与 ownership 回归；
- installed C/C++ consumer 编译。
