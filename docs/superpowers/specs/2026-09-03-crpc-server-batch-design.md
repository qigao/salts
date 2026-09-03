# CRPC Server, Batch and Notification Design

## 目标与范围

新增建立在 CHTTP server 之上的有界 JSON-RPC 2.0 server，统一承载 HTTP/1.1、HTTP/2 与 TLS；支持
多个 endpoint、CMeta method metadata、CSerde params/result、notification 与 batch。普通 HTTP 路由、
middleware 和 Session 继续由同一个 CHTTP server 提供。

本阶段不实现 RPC streaming、HTTP/3、service discovery、自动 retry、跨线程 handler dispatch 或动态
unregister。它们需要不同的生命周期与背压协议，不与 unary/batch server 混合。

## 公开 API

```c
typedef struct crpc_server { void *impl; } crpc_server;

typedef cserde_status (*crpc_encode_value_fn)(void *user, cserde_writer *writer);

typedef struct crpc_server_request_view {
  const chttp_server_request_view *http;
  const char *target;
  const char *method;
  uint64_t request_id;
  int notification;
  cserde_reader *params;
  const cmeta_callable *callable;
} crpc_server_request_view;

typedef struct crpc_server_response { void *impl; } crpc_server_response;

typedef int (*crpc_server_method_fn)(void *user,
                                     const crpc_server_request_view *request,
                                     crpc_server_response *response);

typedef struct crpc_server_config {
  chttp_server_config http;
  size_t method_capacity;
  size_t max_method_bytes;
  size_t max_json_depth;
  size_t max_batch_items;
} crpc_server_config;

int crpc_server_init(crpc_server *server, const crpc_server_config *config);
chttp_server *crpc_server_http(crpc_server *server);
int crpc_server_register(crpc_server *server, const char *target,
                         const crpc_method *method,
                         crpc_server_method_fn handler, void *user);
int crpc_server_response_result(crpc_server_response *response,
                                crpc_encode_value_fn encode, void *user);
int crpc_server_response_error(crpc_server_response *response, int64_t code,
                               const char *message,
                               crpc_encode_value_fn encode_data, void *data_user);
int crpc_server_start(crpc_server *server);
int crpc_server_port(const crpc_server *server, uint16_t *out_port);
int crpc_server_stop(crpc_server *server, uint32_t timeout_ms);
int crpc_server_destroy(crpc_server *server);
```

`crpc_server_http()` 返回 server 内部 CHTTP owner 的 borrowed pointer，供 start 前注册普通路由、全局
middleware、route middleware 与 Session 行为。调用方不得 destroy 该 pointer；listener 生命周期只由
`crpc_server_*` 控制。

一个 `target + service.name` 是方法 identity。同一个 target 可以注册多个 method；同一个 server 可以
注册多个 target。第一次看到 target 时，CRPC 向 CHTTP 注册一个 POST route；方法注册只允许在 start
之前进行，duplicate 返回 `TURBO_EALREADY`，容量耗尽返回 `TURBO_ENOBUFS`。target 是固定
origin-form path，CRPC 拒绝 CHTTP `:segment` 动态 route pattern，避免注册 pattern 与实际 request
path 形成两个方法事实源。

## 数据与所有权协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 一个 HTTP request 内的一条 JSON-RPC object，或最多 `max_batch_items` 条 object |
| 事实源 | CHTTP request body 是 wire 输入；一次 dispatch 中的 JSON root 是解析后的唯一事实源 |
| 方法所有权 | init 预留固定 method slots；register 成功后 CRPC 拥有 target/wire-method 副本与 callable value，handler/user 仍借用到 destroy |
| request view | `http`、target、method、params reader 与 callable 仅在 handler callback 内有效 |
| response | response API 在 handler 内立即编码为有界 owning bytes；handler 返回后不保留 encoder/user/message 借用 |
| topology | CHTTP 当前一个 server worker 是唯一 dispatch owner；不承诺并发调用同一 server 控制 API |
| ordering | batch 按输入顺序执行；非 notification response 按输入顺序输出 |
| capacity | method、method bytes、HTTP body、JSON depth、batch items、CHTTP buffered response body 均为硬上限；CRPC 要求显式配置非零 buffered response 上限，并按 `max_batch_items` 验证总预算可容纳逐项最坏内置协议错误与 array framing；所有乘加先检查溢出 |
| backpressure | 注册满返回 `TURBO_ENOBUFS`；request/output 超限返回明确 JSON-RPC/HTTP error，不扩为无界队列 |
| shutdown | stop 先关闭 CHTTP admission 并 drain 已接受 handler；成功后 destroy JSON registry 与 CHTTP owner |

params reader 是 single-pass borrowed view。reader、method string 与 HTTP header pointers 在 handler 返回时
失效，不得跨线程、协程挂起或保存到 batch 下一 item。CMeta callable 在 register 时 bind 并按值复制，
不依赖声明 translation unit 的 descriptor 地址 identity。

## JSON-RPC 语义

- request 必须是 object 或非空 array；batch 条数不得超过 `max_batch_items`；
- `jsonrpc` 必须唯一且等于 `"2.0"`；`method` 必须唯一、UTF-8、非空且不以 `rpc.` 开头；
- `params` 可缺省，存在时必须为 array 或 object；
- 本库延续 client 契约，只接受 unsigned 64-bit integer id；缺少 id 表示 notification；
- notification 会执行已注册 handler，但无论成功、method-not-found 或 handler error，都不产生 response；
- 单个 notification 或全 notification batch 返回 HTTP 204、空 body；
- parse error 返回 `-32700`，invalid request 返回 `-32600`，method not found 返回 `-32601`，
  handler 可显式返回任意 JSON-RPC error，未回复或内部编码失败映射为 `-32603`；
- batch 中 invalid item 产生带 `id:null` 的 error；其他非 notification item 保留其 uint64 id；
- batch response 排除 notifications，保留其余 input order；
- batch 按实际 item 数从总 response 上限导出相同的 per-item quota；某项 encoder 超限映射为保留该项 id 的 `-32603`，后续元素仍执行，聚合结果始终保持 JSON array；
- 成功和 JSON-RPC application error 使用 HTTP 200 与 `application/json`。CHTTP 自身的 method mismatch、
  body limit、route limit 或 transport failure 保持 CHTTP 的 HTTP/transport 错误语义。

## Handler 与编码规则

handler 必须对普通 call 恰好调用一次 `crpc_server_response_result()` 或
`crpc_server_response_error()`；第二次调用返回 `TURBO_EALREADY`。NULL result encoder 编码 JSON
`null`；NULL error-data encoder 表示省略 `data`。encoder 必须写恰好一个完整 JSON value，CRPC 负责
writer finish；scalar、array、map 都允许。

notification 的 response helper 只标记完成并跳过实际编码，避免制造不会上 wire 的 payload。handler
未调用 response helper 对普通 call 视为 internal error；对 notification 仍返回 204。

## 架构与文件边界

- `crpc_server.c`：server 生命周期、方法 registry、CHTTP route adapter、single/batch dispatch；
- `crpc_json.c`：复用现有 bounded CSerde JSON writer，并新增 server envelope 编码辅助；
- `crpc_internal.h`：只放 CRPC 私有 codec contract，不暴露 parser 类型到公开头；
- `crpc_server_test.c`：H1/H2/TLS、multiple endpoint、CMeta/CSerde、notification/batch 与错误边界；
- `crpc.h`：opaque server、callback views 与生命周期文档。

不从 TurboHTTP/Iris 复制 ownership 模型。Iris 的多 endpoint、notification suppression 与 batch 顺序是
行为参考；CRPC 使用自身的 bounded CSerde writer、opaque owner 与 CHTTP 生命周期，避免 raw JSON 字符串
成为 handler 接口。

## 兼容性与回滚

新增 API 是 additive；既有 client 行为不变。`crpc_server_config` 为新结构，不存在旧 ABI。CRPC 继续只
依赖已有 CHTTP、CMeta、CSerde、JsonCSerdeAdapter 与 Core，不新增第三方依赖。

若 server 验证失败，可独立回滚 `crpc_server.c`、server public declarations/test/CMake entry，而保留
TLS/H2 client parity。由于 CHTTP route 没有运行期 unregister，`crpc_server_destroy()` 只能在 CHTTP
成功 stop/destroy 后释放 registry，不能以 fallback 强制释放。

## 验证矩阵

- H1、cleartext H2、TLS H1、TLS H2 同一 public server API；
- 同 target 多 method、同站点多 target、duplicate/capacity/start 后注册；
- params array/map、scalar result、remote error data、CMeta callable snapshot；
- single notification、unknown notification、mixed batch、all-notification batch、empty/oversized batch；
- malformed JSON、duplicate fields、invalid version/method/params/id、missing method、handler no reply；
- output/body/depth bounds、second response、stop/destroy lifecycle；
- C/C++ public header、installed consumer、full CTest。
