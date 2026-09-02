# CHTTP

CHTTP 是建立在 CNet 之上的有界 HTTP/1 client/server：

```text
Application / RPC
    -> CHTTP serializer + llhttp session
    -> CNet connection
    -> NativeIO terminal completion
```

CHTTP 随 Rocida 正常构建，source-tree target 是 `turbo_chttp`；安装后通过
`Rocida::CHTTP` 与 `<chttp/chttp.h>` 使用。llhttp 是基础依赖，不再由单独的 manifest
feature 控制。

## 当前能力与明确边界

当前实现提供：

- 面向普通用户的阻塞 `chttp_get()`、`chttp_head()`、`chttp_post()`、`chttp_put()`、
  `chttp_delete()` 和 `chttp_patch()`，内部自行推进 I/O；
- HTTP/1.0、HTTP/1.1 response 的严格增量解析；
- GET、HEAD、POST、PUT、DELETE、PATCH、OPTIONS request serialization；
- 固定长度、chunked、EOF-delimited body，以及有界 1xx informational response；
- TCP、TLS 和本地 Pipe ordered byte stream；
- 同一 `connection_uri + authority + TLS profile identity` 的有界 HTTP/1.1 keep-alive
  连接复用；
- 供 RPC、Executor 和既有 event loop 集成的高级 submit/cancel/poll 接口；
- 用户不接触 poller 的后台 HTTP/1.1/HTTPS server、动态 `:name` 路由与
  GET/HEAD/POST/PUT/DELETE/PATCH/OPTIONS handler；
- 按注册顺序执行的全局/路由 middleware，以及只能调用一次的 `chttp_server_next_call()`；
- 使用系统 CSPRNG session id 的有界内存 Cookie Session，支持 get/set/remove/clear/invalidate；
- start-line、header count、header bytes、request body、response body、request count 的硬上限。

当前有意不提供 HTTP/2/3、WebSocket/Upgrade、CONNECT、redirect、compression、proxy、
streaming upload/download、异步悬挂 server response 或自动 retry。client serializer 发送
`Connection: keep-alive`；final response 只有在 llhttp 判定协议允许持久连接时才回到池中，
`Connection: close`、EOF framing、解析失败、取消和 shutdown 都会关闭该连接。UDP/datagram 在
admission 前返回 `TURBO_ENOTSUP`。

当前 server 对 HTTP/1.0 的 `Expect`/`Upgrade` 字段按协议忽略；HTTP/1.1 只处理
`Expect: 100-continue`。在显式 WebSocket route 尚未实现前，Upgrade invitation 继续按普通
HTTP 请求路由，不发送缺少可接受协议列表的 426。
request target 必须是无 fragment 的 origin-form；Transfer-Encoding 仅接受 HTTP/1.1 的精确
`chunked`，未知 transfer coding 返回 501，HTTP/1.0 的 Transfer-Encoding 返回 400。当前
request API 不暴露 trailer，因此 server 拒绝任何非空 trailer，避免把尾部 Cookie 或
Authorization 错当成普通 header。

后续协议路线只计划把 TurboHTTP 的 HTTP/2 引擎导入 CHTTP，并把 S3 作为 CHTTP 上层模块导入；
HTTP/3 不在范围内。TLS、KCP 与 WebSocket 连接状态归 CNet，CHTTP 只负责 HTTPS policy、WebSocket Upgrade
路由和所有权移交。详细阶段与验证门槛见 `../docs/CHTTP_CNET_PROTOCOL_TODO.md`。

这里的 keep-alive 是 HTTP/1.1 在同一 TCP stream 上的持久连接复用，不等于内核
`SO_KEEPALIVE` 探测参数。TCP keepalive idle/interval/probe count 仍应作为未来 CNet transport
profile，而不是伪装成 CHTTP pool 配置。

`cnet_send()` admission 成功只证明 bytes 已复制进有界 command storage；`observer.on_send`
在完整 ordered write terminal 后报告一次完成。CHTTP server 只在该 callback 后重新申请 receive，
因此同一 keep-alive 连接不会重叠写 response。这个完成事实仍不等于应用级 exactly-once，CHTTP
不会根据断线猜测 peer 是否消费了请求，也不做隐式 retry。

## HTTP Server：路由、中间件与 Session

普通服务端用户只注册 handler/middleware，然后调用 `chttp_server_start()`；后台线程独占 CNet
poller，业务代码不调用 `cnet_client_poll()`。路由 path 不含 query，可包含完整 segment 参数，
例如 `/users/:user/posts/:post`。静态路由优先于参数路由，HEAD 在没有显式 HEAD route 时回退
GET handler，但只发送 headers。

全局 middleware 先于 route middleware 执行。middleware 可以调用一次
`chttp_server_next_call()`，也可以直接 `chttp_server_reply()` 形成认证、CORS、限流或错误处理中断。
request、route param、header、body、session value 与 response builder 都是 handler-scoped；不得跨
callback、线程 handoff 或 coroutine suspension 保存裸指针。

开启 Session 后，Cookie 只保存 128-bit 随机 session id，实际 key/value 留在 server 的固定容量
存储中。Cookie 默认带 `Path=/; HttpOnly; SameSite=Lax`，可配置 `Secure`；idle timeout 滑动刷新。
达到 `session_capacity` 时不会驱逐仍有效的会话，新的 `chttp_session_set()` 返回
`TURBO_ENOBUFS`。当前 store 是进程内内存，不承诺重启持久化、多进程共享或分布式一致性。

```c
#include <chttp/chttp.h>
#include <string.h>

static int health(void *user, const chttp_server_request_view *request,
                  chttp_server_response *response) {
  const char *name = chttp_server_request_param(request, "name");
  (void)user;
  return chttp_server_reply(response, 200, "text/plain", name, name != NULL ? strlen(name) : 0);
}

static int add_security_headers(void *user, const chttp_server_request_view *request,
                                chttp_server_response *response, chttp_server_next *next) {
  int status;
  (void)user;
  (void)request;
  status = chttp_server_response_set_header(response, "X-Content-Type-Options", "nosniff");
  return status == TURBO_OK ? chttp_server_next_call(next) : status;
}

int main(void) {
  chttp_server server = {0};
  const chttp_server_config config = {
    .host = "127.0.0.1", .port = 8080, .backlog = 128,
    .network = {
      .backend = NATIVE_IO_BACKEND_IOCP,
      .connection_capacity = 128, .command_capacity = 256,
      .request_capacity = 256, .completion_batch_capacity = 64,
      .event_capacity = 256, .max_send_bytes = 64 * 1024,
      .receive_buffer_bytes = 16 * 1024,
      .read_timeout_ms = 30000, .write_timeout_ms = 5000,
    },
    .route_capacity = 64, .middleware_capacity = 16,
    .max_route_middleware_count = 8, .max_route_param_count = 8,
    .max_route_param_bytes = 4096, .max_target_bytes = 4096,
    .max_header_count = 64, .max_header_bytes = 16 * 1024,
    .max_request_body_bytes = 32 * 1024,
    .max_response_header_count = 64, .max_response_header_bytes = 8 * 1024,
    .max_response_body_bytes = 32 * 1024,
    .session_capacity = 1024, .session_entry_capacity = 16,
    .max_session_key_bytes = 64, .max_session_value_bytes = 1024,
    .session_idle_timeout_ms = 30 * 60 * 1000,
    .session_cookie_name = "sid", .session_cookie_secure = 0,
    .poll_slice_ms = 5,
  };
  int status = chttp_server_init(&server, &config);
  int started = 0;
  if (status == TURBO_OK) status = chttp_server_use(&server, add_security_headers, NULL);
  if (status == TURBO_OK) status = chttp_server_get(&server, "/health/:name", health, NULL);
  if (status == TURBO_OK) {
    status = chttp_server_start(&server);
    started = status == TURBO_OK;
  }
  /* Wait here for the application's shutdown signal. */
  if (started) {
    const int stop_status = chttp_server_stop(&server, 0);
    if (status == TURBO_OK) status = stop_status;
  }
  if (server.impl != NULL) {
    const int destroy_status = chttp_server_destroy(&server);
    if (status == TURBO_OK) status = destroy_status;
  }
  return status == TURBO_OK ? 0 : 1;
}
```

非 Windows 平台应把 backend 换成实际支持的 EPOLL 或 KQUEUE。handler 在单一 owner thread 上
串行执行，不能做阻塞数据库/文件 I/O，也不能从
handler 调用 stop/destroy。Castle 风格的预加载模板/静态资源可以直接 reply；真正异步数据库、
文件 streaming 或 Actor continuation 需要未来带 owning request token 的 suspend/resume API。

## 用户接口：requests-style 调用不暴露 poller

普通用户只使用 `chttp_client_init()`、`chttp_get/head/post/put/delete/patch()`、
`chttp_response_destroy()` 和 `chttp_client_destroy()`。一次调用在当前线程阻塞，内部驱动
CHTTP → CNet → NativeIO，返回后 `chttp_response` 的 status、reason、headers 和 body 都由
调用方拥有；用户不创建 poller，也不需要提供 Executor 或 worker thread。

一个 client 同一时刻只允许一个调用，并归一个线程所有。需要并行请求时，可以把多个互不
共享的 client 交给应用已有的 `turbo_threadpool` 或 Executor；不能让多个 worker 同时推进同一个
client。CHTTP 不创建隐藏的全局线程池，也不会把阻塞工作转交给不可控的后台线程。

`timeout_ms` 限制等待 HTTP result 的时间。到期后 CHTTP 会取消并等待底层 terminal 状态，再让
client 恢复到可安全复用或销毁的状态；因此底层 terminal drain 所需时间可能使函数返回略晚于
该 deadline。错误通过函数返回值与 `chttp_error` 的 status、native_status、stage 一起交付。

## 配置与连接参数

`chttp_client_config.network` 完整承载 CNet owner 的 backend、connection/command/native
request/event capacity、单次 send/receive bytes 和 connect/read/write timeout。CHTTP 自己只
增加 HTTP message/session 上限。`network.read_timeout_ms` 也约束 idle 连接等待 peer EOF 的时间；
零仍表示不设置该 deadline：

| 字段 | 单位与满额行为 |
|---|---|
| `request_capacity` | busy + idle + closing 的 HTTP connection slot；满时 `TURBO_ENOBUFS` |
| `max_start_line_bytes` | request/status line；超限 `TURBO_EMSGSIZE` |
| `max_header_count` | 包含自动生成的 Host、Content-Length、Connection；超限 `TURBO_EMSGSIZE` |
| `max_header_bytes` | header line bytes（不含最终空行）；超限 `TURBO_EMSGSIZE` |
| `max_request_body_bytes` | copied request body；超限 `TURBO_EMSGSIZE` |
| `max_response_body_bytes` | buffered response body；超限 `TURBO_EMSGSIZE` |
| `max_informational_responses` | 一个 final response 前允许的 1xx 数量 |

每次 requests-style call 或高级 submit 再提供三项不同事实：

- `connection_uri`：CNet endpoint，例如 `tcp://127.0.0.1:8080` 或
  `tls://127.0.0.1:8443`；
- `authority`：HTTP Host/virtual-host，例如 `api.example.test:8080`；
- `target`：HTTP origin-form target，例如 `/v1/users?id=7`。

三者不能合并为一个模糊 URL：连接 endpoint、HTTP authority 和 request target 的归属不同，
Pipe、TLS SNI 与 virtual host 也需要能独立组合。CHTTP 不把 `https://` 当作模糊 URL 解析，
HTTPS 明确使用 `tls://` connection URI 和可选的 `chttp_tls_profile`。

pool key 是经过验证后的 `connection_uri + authority + TLS profile identity` 精确组合，`target`
不参与。因此同一站点的 `/users`、`/orders` 与 `/status` 可以顺序复用一条连接，但不同
authority 或不同 TLS profile 不做隐式 coalescing。内容相同但分别初始化的 profile 也属于不同
安全域；需要复用时应共享同一个 immutable profile。
每条连接同一时刻最多承载一个 request，不实现 HTTP pipelining。pool 满且没有同 key idle slot
时，高级 submit 会开始关闭一个不匹配 idle slot，并返回 `TURBO_ENOBUFS`；调用方 poll 后再重试。
requests-style client 则在本次调用 deadline 内自行推进该关闭并重新提交，因此普通用户切换站点
仍不需要接触 poller。这里的重新提交发生在新 request admission 之前，不是断线后的 HTTP retry。

## HTTPS 与 TLS profile

客户端先用 `chttp_tls_profile_init()` 把 CA、可选客户端证书、SNI/verified identity 与 ALPN
构造成可复用 profile，再把它放入 `chttp_options.tls` 或 `chttp_request_options.tls`。CHTTP
只支持空 ALPN 或唯一的 `http/1.1`；`h2` 和其他协议会返回 `TURBO_ENOTSUP`，避免协商出非 H1
协议后仍发送 H1 wire bytes。profile public wrapper 可在 request admission 后销毁；busy/idle slot
持有内部引用，直到连接关闭。CNet 始终执行 peer 与 hostname verification，不提供不安全开关。

服务端把 `cnet_tls_server_config` 放入 `chttp_server_config.tls`。`chttp_server_init()` 同步建立并
拥有 TLS context，因此证书配置字符串在 init 返回后即可释放；start 后仍由原有后台 owner
accept 和推进握手。TLS 模式要求 `network.tls_io_buffer_bytes >= CNET_TLS_MIN_IO_BUFFER_BYTES` 且
`tls_handshake_timeout_ms` 非零，配置或证书错误在监听前失败，不回退明文。mTLS 继续使用 CNet 的
`CNET_TLS_CLIENT_AUTH_REQUIRED` 与显式 CA source。HTTPS Session 应设置
`session_cookie_secure = 1`。

## 高级 event-loop 接口

`chttp_async_client_submit()`、`chttp_async_request_cancel()`、`chttp_async_client_poll()`、
`chttp_async_client_stop()` 和 `chttp_async_client_destroy()` 是供 CRPC、Executor 或已有事件循环
适配器使用的高级接口，不是普通 HTTP 调用流程。只有这类集成层负责调用
`chttp_async_client_poll()`；应用业务代码默认使用 requests-style API。

## Ownership、状态与背压

CHTTP 与内嵌 CNet 共享一个 progress owner。`submit/cancel/poll/stop/destroy` 不得并发调用；
completion callback 在 `chttp_async_client_poll()` 或 `chttp_async_client_stop()` 的调用线程内同步执行。
callback 可以取消另一条已接受 request，但不能递归 poll/stop/destroy，也不能直接 submit 新
request；submit 必须等 callback 返回后再进行，否则返回 `TURBO_EBUSY`。

```text
FREE
  -> CONNECTING
  -> BUSY / SEND_ADMITTED
  -> RECEIVE_ONE (exactly one demand)
  -> PARSE
       -> RECEIVE_ONE
       -> RESULT_DELIVERED
            -> IDLE -> BUSY (same origin reuse)
            -> CLOSING
  -> TRANSPORT_TERMINAL
  -> RECYCLE
```

- 新连接 submit 成功前由 CNet 复制 URI，CHTTP 复制 pool key、retain TLS profile，并由 serializer 复制
  method/authority/target/headers/body；
- `options.user` 是 borrowed，必须存活到 completion callback 返回；
- response、header string 和 body 只在 completion callback 期间有效；需要跨 callback 或协程
  挂起保留时，调用方必须复制；
- 每次只调用 `cnet_receive(connection, 1)`；当前 byte chunk 消费并确认 parser/body 仍有容量后
  才申请下一个 receive value；idle slot 也只保留一个 receive demand，用于观察 EOF/read timeout；
- idle slot 收到没有对应 request 的 bytes 属于协议异常，该连接直接失效并关闭；
- cancel/stop 只是关闭请求，资源仍保留到 CNet terminal callback 后才 recycle；
- submit 成功恰好产生一次 completion callback。立即 admission 失败不产生 callback。

每个 active request 的 retained HTTP 内存上界可按下式复算：

```text
serialized request <= network.max_send_bytes
+ llhttp/session metadata
+ max_header_count * header metadata
+ max_header_bytes + terminators
+ max_start_line_bytes
+ max_response_body_bytes
```

总预算还需乘 `request_capacity`，再加每个连接的 URI/authority/profile pool key、CNet
command/event/native-request 和 receive buffer 预算。idle slot 已释放上一条 response parser 的
reason/header/body storage，只保留连接、key 和 CNet receive state。

## 普通用户示例

```c
#include <stdio.h>

#include <chttp/chttp.h>

int main(void) {
  chttp_client client = {0};
  chttp_response response = {0};
  chttp_error error = {0};
  const chttp_client_config config = {
    .network = {
      .backend = NATIVE_IO_BACKEND_IOCP,
      .connection_capacity = 1,
      .command_capacity = 8,
      .request_capacity = 4,
      .completion_batch_capacity = 4,
      .event_capacity = 8,
      .max_send_bytes = 64 * 1024,
      .receive_buffer_bytes = 16 * 1024,
      .connect_timeout_ms = 5000,
      .read_timeout_ms = 30000,
      .write_timeout_ms = 5000,
    },
    .request_capacity = 1,
    .max_start_line_bytes = 4096,
    .max_header_count = 64,
    .max_header_bytes = 32 * 1024,
    .max_request_body_bytes = 32 * 1024,
    .max_response_body_bytes = 1024 * 1024,
    .max_informational_responses = 4,
  };
  const chttp_options options = {
    .connection_uri = "tcp://127.0.0.1:8080",
    .authority = "api.example.test:8080",
    .target = "/v1/health",
    .timeout_ms = 30000,
  };
  int status = chttp_client_init(&client, &config);

  if (status == TURBO_OK)
    status = chttp_get(&client, &options, &response, &error);
  if (status == TURBO_OK)
    (void)fwrite(response.body, 1, response.body_size, stdout);
  else
    (void)fprintf(stderr, "CHTTP failed: status=%d native=%d stage=%s\n",
                  error.status, error.native_status,
                  error.stage != NULL ? error.stage : "unknown");

  chttp_response_destroy(&response);
  if (client.impl != NULL) {
    const int destroy_status = chttp_client_destroy(&client, 5000);
    if (status == TURBO_OK) status = destroy_status;
  }
  return status == TURBO_OK ? 0 : 1;
}
```

非 Windows 平台应选择实际支持的 NativeIO backend。生产代码必须消费并传播调用返回的
`error.status`、`error.native_status` 和 `error.stage`。

## 架构决策

候选方案包括直接从 CHTTP 使用 NativeIO、把 llhttp 放进 CNet，以及在 CNet 上建立独立
CHTTP。选择第三种：NativeIO 只拥有 native operation terminal truth；CNet 拥有 DNS、URI、
connection handle、receive demand 和 shutdown；CHTTP 拥有 HTTP framing、parser state 与
message limits；llhttp 是私有 parser adapter。

这一选择增加一个显式层和完整响应 buffering 成本，却避免 HTTP/RPC 重复连接状态机，也避免
第三方 parser 类型进入 CNet API。当前 keep-alive pool 只复用完整 message 之间的空闲连接；若
迁移到 streaming、pipelining 或 retry，需保留当前 message API，并先补 CNet write-terminal
契约。若实现需要回滚，应回退对应提交并恢复上一版安装包；不能依赖运行时 fallback 或已经
移除的构建开关。

## 构建与验证

Windows Release：

```text
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target \
  chttp_request_test chttp_response_test chttp_server_parser_test chttp_server_test \
  chttp_api_test chttp_requests_test chttp_header_cpp_test
ctest --preset win-release-user -R "^chttp_" --output-on-failure
```

Windows 命令必须先进入 `VsDevCmd.bat` 环境。llhttp 的上游 API 与 strict/lenient 安全说明见
[nodejs/llhttp](https://github.com/nodejs/llhttp)。
