# CHTTP HTTP/2 WebSocket（RFC 8441）设计

## 背景与目标

CHTTP 已经具备 HTTP/1.1 WebSocket/WSS、HTTP/2 client/server 与 CNet WebSocket 帧引擎。RFC 8441 不再使用 HTTP/1.1 的 `Upgrade`，而是在已协商 HTTP/2 的连接上用 Extended CONNECT 建立一条长期双向流。本改动让同一个 `chttp_server_websocket()` 路由以及同一个同步 WebSocket client 同时支持 HTTP/1.1 与 HTTP/2，不新增第二套帧解析器。

范围包括 h2c 与 TLS+h2（WSS）。不实现 HTTP/3、WebSocket 扩展、子协议协商，也不把一个同步 WebSocket client 扩展为多流连接池。

## 协议不变量

- HTTP/2 server 在首个 SETTINGS 中发送 `SETTINGS_ENABLE_CONNECT_PROTOCOL(0x8)=1`。
- HTTP/2 client 必须收到 peer SETTINGS 且该值为 1，才能发送 Extended CONNECT；缺失或为 0 时返回协议不支持错误，不回退 HTTP/1.1。
- 请求伪头固定为 `:method=CONNECT`、`:protocol=websocket`、匹配传输安全性的 `:scheme`、非空 `:authority` 与 origin-form `:path`。
- HTTP/2 握手不发送或验证 `Connection`、`Upgrade`、`Sec-WebSocket-Key`、`Sec-WebSocket-Accept`；必须发送并验证 `sec-websocket-version: 13`。
- 成功响应是 `:status=200` 且不带 `END_STREAM`。其他状态是普通握手拒绝响应。
- RFC 6455 帧字节直接放入 HTTP/2 DATA。客户端帧仍由现有 CNet 引擎执行 masking，服务端帧不 masking。
- peer `END_STREAM` 表示底层传输关闭；正常 WebSocket 关闭仍优先交换 Close frame。协议错误只 reset 当前流，不伤害 sibling stream。

## 架构选择

### 方案比较

1. 在 H2 server/client 内复制 WebSocket 帧状态机：实现直接，但会产生两个事实源，修复 masking、fragmentation、close/ping 语义时容易漂移。
2. 让 H2 伪装成 H1 字节流：能复用现有文件，但混淆握手与帧边界，并会错误引入 Upgrade/Key/Accept 语义。
3. 在现有 CNet WebSocket 引擎下增加窄 transport adapter：H1 写入 connection outbound，H2 写入指定 stream DATA；公共 API 与事件保持一致。

选择方案 3。公共 `chttp_websocket` 指向传输中立 peer；peer 持有唯一 CNet WebSocket engine，并通过 adapter 完成写入、flush 与 transport-close。H1 connection 和 H2 stream 分别拥有 peer，生命周期不交叉。

## 状态与所有权

每个 H2 stream 是 WebSocket 会话的唯一 owner，持有：

- route、request/session state 与 callback-scoped 公共 handle；
- CNet WebSocket engine；
- 一块有硬上限的 outbound frame staging buffer；
- `HANDSHAKE / OPEN / CLOSING / CLOSED` 传输状态。

收到的 DATA 是 H2 callback 内借用 view，只在 callback 内同步喂给 CNet engine，不跨 callback 或协程挂起保存。CNet write callback 产生的 frame 会复制到 stream-owned staging buffer；H2 engine 借用该 buffer，直到 `chttp_h2_proto_stream_output_pending()` 为 false 才释放占用。一个 stream 同时最多保留一帧，满时显式返回 `SALTS_EBUSY`/`SALTS_ENOBUFS`，由 CNet engine 的 flush 协议重试，不做无界分配。

H1 行为不变：connection 仍是会话 owner，只是通过相同 peer/adapter 进入已有 outbound send path。

## 服务端流程

1. H2 connection prepare 时启用 local Extended CONNECT SETTINGS。
2. HEADERS parser 识别 `CONNECT` 与 `:protocol=websocket`，严格验证伪头、版本头和禁用头；普通 HTTP/2 请求继续原路径。
3. 根据 `:path` 用 GET 语义查找现有 WebSocket route，执行相同全局/route middleware、Session admission 和 `on_open`。
4. handler 返回普通 response 时按拒绝响应结束流；否则提交 `:status=200`，保持流打开，再 flush `on_open` 中排队的首帧。
5. 后续 DATA 同步进入 WebSocket engine，并在已消费后归还 stream/connection window credit。
6. WebSocket engine 结束后提交空 DATA+END_STREAM；传输/协议失败时 reset 当前流。

## 客户端流程与公开接口

在 `chttp_websocket_connect_options` 尾部加入 `chttp_protocol protocol`。零值保持 HTTP/1.1，现有源码初始化器行为不变；`CHTTP_HTTP_2` 显式选择 RFC 8441，绝不自动 fallback。

- `ws:// + HTTP/2` 使用 h2 prior knowledge。
- `wss:// + HTTP/2` 要求 TLS profile 明确协商 `h2`。
- client 连接后发送 preface/SETTINGS，等待 server SETTINGS，验证 Extended CONNECT 能力，再发送开放流的 CONNECT HEADERS。
- 收到 200 后初始化/开放 WebSocket engine；非 200 返回 `out_http_status` 并结束连接。
- `send/receive/close` 保持现有阻塞 convenience API，用户不操作 poller。

这保持 API 形状统一，但同步 client 仍是一条连接上的一个 WebSocket stream。未来若需要同一 H2 连接承载多个 WebSocket，应在独立 async/multiplexed owner 上设计 stream handle，不能偷偷改变现有 client 的并发契约。

## 错误语义

- 参数、URI、H1/H2 TLS profile 不匹配：`SALTS_EINVAL` 或 `SALTS_EPROTONOSUPPORT`，连接前 fail fast。
- peer 未发布 Extended CONNECT：`SALTS_EPROTONOSUPPORT`。
- 非 200：返回 HTTP rejection，`out_http_status` 保留状态码。
- WebSocket frame 违反 RFC 6455：当前 H2 stream `RST_STREAM(PROTOCOL_ERROR)`。
- bounded input/output 超限：当前 stream `RST_STREAM(ENHANCE_YOUR_CALM)` 或公开 `SALTS_EMSGSIZE/SALTS_ENOBUFS`。
- connection-level H2 framing/HPACK 错误：沿用 H2 engine 的 GOAWAY/connection close。

## 兼容性、迁移与回滚

源码兼容：指定初始化器未设置新增字段时仍为 H1。因为 options 使用 exact-size ABI guard，本次新增字段需要同步 C/C++ header test、installed consumer 与版本文档；旧二进制 options 不能与新库混用，这与当前 major ABI 演进规则一致。

服务端已有 WebSocket route 自动接受 H2，不需要业务代码迁移。未启用 HTTP/2 的 server 行为完全不变。

若 RFC 8441 路径需要回滚，可移除 H2 adapter、停止发布 SETTINGS 0x8 并拒绝 `:protocol`，H1 peer adapter 与现有公开 API仍可保留。

## 验证范围

- H2 proto：SETTINGS 0x8、开放 request stream、双向 DATA、END_STREAM、flow control、stream-local reset。
- H2 server：Extended CONNECT 成功、同一路由 middleware/session、文本/二进制/ping/pong/close、错误握手、普通 H2 sibling 不受影响。
- client：h2c 与 WSS+h2、未发布 SETTINGS、非 200、timeout、事件所有权、连续 send/receive/close。
- 回归：现有 H1 WS/WSS、HTTP/1.1、H2 request/reply、TLS、installed C/C++ consumer。
- 内存与并发：focused ASan；单 connection owner thread、不跨 callback 保存 borrowed DATA。

规范依据：[RFC 8441 - Bootstrapping WebSockets with HTTP/2](https://www.rfc-editor.org/rfc/rfc8441.html)。
