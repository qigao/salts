# CRPC

CRPC 是建立在 CHTTP 之上的有界 JSON-RPC 2.0 unary client：

```text
CMeta method semantics + CSerde params/result
    -> CRPC JSON-RPC envelope and deadline
    -> CHTTP POST and HTTP limits
    -> CNet connection
    -> NativeIO terminal completion
```

CRPC 随 Rocida 正常构建，source-tree target 是 `turbo_crpc`；安装后通过
`Rocida::CRPC` 与 `<crpc/crpc.h>` 使用。它公开的是纯 C API，不依赖 experimental
feature 或编译开关。

## 当前能力

- 编码 JSON-RPC 2.0 request，并严格校验 result/error response envelope；
- 面向普通用户的 `crpc_client_init()`、`crpc_request_reply()` 与
  `crpc_client_destroy()`，内部自行推进 HTTP/CNet；
- 面向 Executor、Actor 和事件循环的 `crpc_async_client_*` submit/cancel/poll 接口；
- 使用 `uint64_t` request id，拒绝同一个 client 中重复的 active id；
- 通过 CSerde token writer 编码 Array/Map params，通过 single-pass reader 消费 result 或 error
  data；
- 可选绑定 `cmeta_callable`，把 signature、effects 和 properties 与本地 method 语义一起交付；
- 区分 JSON-RPC remote error 与 transport、HTTP、deadline、decode、envelope failure；
- generation-checked request handle、显式 cancel、exactly-once terminal callback；
- request count、method bytes、JSON depth、HTTP headers/body 和底层网络资源均有硬上限。

当前只实现 client-side JSON-RPC over HTTP POST，不提供 server、batch、notification、streaming、
自动 retry、service discovery、TLS 或 HTTP/2/3。TLS 和连接能力属于 CNet transport，不能由 CRPC
绕过 CHTTP/CNet 私自建立第二套 socket runtime。

## CMeta 与 CSerde 的职责

`crpc_method` 的 `service` 和 `name` 决定 wire method；可选 `cmeta_callable` 描述本地 callable 的
signature、调用协议、effects 和 properties。CRPC 在 admission 期间复制并绑定 metadata，因而
response 读取的是稳定的 method semantic snapshot。CRPC 不用 CMeta 猜测 wire schema，也不在
运行时调用该 callable。

params encoder 必须向 `cserde_writer` 写入恰好一个 Array 或 Map root；CRPC 负责结束 writer，调用
方不能自行 finish。`crpc_response_view` 中的 result reader 与 remote error message/data 只在异步
completion callback 内有效；`crpc_request_reply()` 返回的 owning `crpc_response` 则保持这些
reader/view 有效，直到调用 `crpc_response_destroy()`。两种 reader 都是单遍读取。

## 两种调用风格

普通业务代码采用 request/reply：先以完整的 `crpc_client_config` 调用 `crpc_client_init()`，再把
`connection_uri`、`authority`、`target`、method、request id 和 deadline 放入 `crpc_options`，交给
`crpc_request_reply()`；消费结果后调用 `crpc_response_destroy()`，最终调用
`crpc_client_destroy()`。

同一个 client 可以保持 `connection_uri` 与 `authority` 不变，只替换 `target` 和 `method`，依次
调用同一站点的多个 endpoint。CRPC 直接继承 CHTTP 的有界 HTTP/1.1 keep-alive pool：服务器
允许持久连接时，这些顺序 request/reply 可以复用同一 TCP socket；服务器返回
`Connection: close`、连接超时、解析失败、取消或 shutdown 时则关闭并回收。pool key、容量、
背压与线程归属均由 CHTTP/CNet 定义，CRPC 不维护第二份连接状态。

需要并发、取消或统一 progress owner 时采用 `crpc_async_client_init()`、
`crpc_async_client_submit()`、`crpc_async_request_cancel()`、`crpc_async_client_poll()`、
`crpc_async_client_stop()` 与 `crpc_async_client_destroy()`。业务层不应为了发起一次 RPC 而调用
poll。

## 状态、deadline 与错误

`crpc_async_client_submit()` 成功表示 request 已被 CHTTP 接受，并保证以后恰好一次 terminal
callback；立即 admission 失败不产生 callback。`request_id` 是 JSON-RPC wire identity，
`crpc_request` 是本地 generation-checked cancellation handle，二者不能混用。

RPC `deadline_ms` 从 call 开始计时，并约束编码、HTTP 和网络等待；CNet 的 connect/read/write
timeout 仍是独立 transport deadline。RPC deadline 到期后只请求下层取消，payload、parser、
callback state 和 handle 必须保留到 CHTTP/CNet/NativeIO 给出 terminal result 后才能回收。

`crpc_error` 保留四类上下文：Rocida status、native status、HTTP status 与 stable stage。
合法的 JSON-RPC error object 以 `CRPC_RESPONSE_REMOTE_ERROR` 返回，不伪装成 transport failure。
metadata 不能覆盖 CRPC 自己拥有的 Content-Type、Accept、Host、Content-Length 或 Connection。

一个 async client 是单 progress owner。submit/cancel/poll/stop/destroy 不得并发；callback 可以
取消另一条 active request，但不能递归 submit/poll/stop/destroy。`crpc_async_client_poll()` 是
框架级异步入口，适合由 RPC runtime、Executor 或 event-loop adapter 统一驱动。一个 blocking
client 同一时刻只接受一个 request/reply；并行同步调用应使用多个独立 client。

连接复用不等于 pipelining 或 retry：每条 HTTP/1.1 连接同一时刻仍只有一个 RPC，且任何已接受
send 之后的断线都按原错误路径交付，不自动重放可能带副作用的 method。

## 构建与验证

Windows 命令需要先进入 Visual Studio x64 开发环境：

```text
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target \
  crpc_json_test crpc_api_test crpc_header_cpp_test
ctest --preset win-release-user -R "^crpc_" --output-on-failure
```

测试覆盖 bounded codec、UTF-8、uint64/int64 数值边界、malformed envelope、remote error、真实
HTTP round trip、同站点多 endpoint request/reply、owning response、CMeta callable metadata、
deadline、重复 id、manual cancel 和 callback reentrancy。
