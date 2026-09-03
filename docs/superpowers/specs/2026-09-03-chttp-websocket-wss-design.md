# CHTTP HTTP/1.1 WebSocket 与 WSS 设计

> 阶段说明：本文记录最初的 HTTP/1.1 WS/WSS 范围；后续 HTTP/2 能力以
> [CHTTP HTTP/2 WebSocket（RFC 8441）设计](2026-09-03-chttp-rfc8441-websocket-design.md) 为准。

## 背景与范围

CHTTP 已在后台线程中拥有 CNet listener、HTTP/1.1 llhttp parser、HTTP/2 engine、路由、middleware 与 Session；CNet 已提供基于 `tools/wsparser` 的 RFC 6455 会话引擎和透明 TLS transport。本阶段补齐 HTTP/1.1 WebSocket opening handshake，使同一公开 API 同时支持 `ws://` 与 `wss://`。

本阶段不实现 HTTP/2 Extended CONNECT（RFC 8441）、WebSocket 扩展压缩或代理隧道。请求协商到 `h2` 时不会隐式回退到 HTTP/1.1；WSS 客户端 TLS profile 必须选择 `http/1.1`。

规范依据：[RFC 6455](https://www.rfc-editor.org/rfc/rfc6455.html)。

## 候选方案

1. 在 CHTTP 再实现一套帧 parser。实现直接，但会复制 CNet 已有 masking、fragmentation、UTF-8、control frame 与 close 状态机，形成两个事实源，拒绝。
2. 把 HTTP Upgrade 放进 CNet。可接近 transport，但会让 CNet 依赖 HTTP header、路由、middleware 和 Session，穿透分层，拒绝。
3. CHTTP 负责 opening handshake，成功后用 Adapter 把原连接交给 CNet WebSocket engine。HTTP 语义与会话状态各有唯一事实源，选择此方案。

## 架构与状态归属

- llhttp 是 HTTP/1.1 header 语法事实源。CHTTP 在 `on_headers_complete` 验证 RFC 6455 header，并用 `HPE_PAUSED_UPGRADE` 精确停在 header 末尾。
- CHTTP route/middleware/session 是 Upgrade admission 的事实源。只有显式 WebSocket route 可以接受 Upgrade；普通 HTTP route 的现有行为不变。
- CNet TLS 是加密、证书校验和 ALPN 的事实源。Upgrade 之后 CHTTP 看到的仍是相同明文 byte stream，所以 WS/WSS 不分叉帧逻辑。
- CNet WebSocket engine 是 frame/message/close 状态的事实源；CHTTP 不直接包含或导出 `tools/wsparser` 类型。
- 每条 server connection 仍由单一后台 owner thread 推进。公开 server WebSocket handle 与事件 data 都是 callback-scoped borrowed view，不支持跨线程保存或调用。

连接状态按以下顺序迁移：

`HTTP_1_1 -> WS_HANDSHAKE -> WS_OPEN -> WS_CLOSING -> TRANSPORT_CLOSED`

失败语义：握手格式错误返回明确 HTTP 4xx 并关闭；协议帧错误由 CNet engine 发送对应 Close；TLS/ALPN 错误直接失败，不降级到明文 WS。

## 数据、所有权与背压协议

- 每个 WebSocket session 初始化固定的 `max_frame_bytes`、`max_message_bytes` 与 `max_buffered_input_bytes`。
- CNet receive view 在 callback 返回时失效。若 client 将首帧与 HTTP Upgrade 合并发送，CHTTP 仅复制 header 后剩余 bytes 到预分配、以 `network.receive_buffer_bytes` 为硬上限的 upgrade buffer；101 写完成后再 feed。
- CNet engine 只保留一个完整待写 frame。transport busy 时返回/传播 `TURBO_EBUSY`，不静默丢帧、不无界排队。
- server `on_open` 在 admission callback 中执行；在 101 尚未写出时提交的首个 frame 进入 CNet engine 的单一 retained slot，101 完成后 flush，因此线序始终是 handshake 在前。
- server event payload 只在 event callback 内有效。同步 client 把事件复制进固定容量队列；`receive` 返回的 view 在下一次 client 操作前有效，队列满时明确返回 `TURBO_ENOBUFS`，不无界增长。
- transport terminal callback 恰好通知 CNet engine 一次；connection cleanup 恰好 destroy engine 一次。

## 公开接口

Server 新增显式 WebSocket route options，包含路径、route middleware、三个容量上限、`on_open`、`on_event` 与 user。现有全局 middleware 和 Session 同样参与 Upgrade admission。发送 text/binary/ping/pong/close 通过 CHTTP wrapper 委托给 CNet engine。

同步 client 接受完整 `ws://`/`wss://` URI、可选 HTTP headers 与可复用 CHTTP TLS profile。`connect/send/receive/close/destroy` 内部驱动 CNet，调用者不接触 poller。连接对象 single-owner，不允许并发调用。

## 兼容性与迁移

- 仅新增 API；现有 HTTP client/server API 与 wire behavior 保持不变。
- `tools/wsparser` 继续只由 CNet 私有链接，下游仅看到 CHTTP/CNet 稳定类型。
- WSS 是严格 TLS：`wss://` 没有证书验证失败后的 `ws://` fallback。
- H2 server 仍服务普通 HTTP/2；WebSocket route 经 H2 请求时返回不支持，而不是切换协议。

## 验证范围

- 握手：RFC 示例 Accept、重复/缺失 header、token 大小写、版本 426、无效 key、body/Transfer-Encoding 拒绝。
- parser：Upgrade 精确 consumed bytes、合并首帧、普通请求不回归。
- server：route params、global/route middleware、Session header、echo、ping/pong、close、frame/message/input hard limits。
- client：ws/wss end-to-end、证书校验、ALPN 必须为 HTTP/1.1、无 caller poller、deadline 和 terminal cleanup。
- 构建：C/C++ public header、installed consumer、Release focused/full tests 与 ASan focused tests。
