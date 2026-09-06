# CNet 同连接 TLS 升级设计

## 背景

SMTP STARTTLS、IMAP STARTTLS 与 POP3 STLS 都要求在既有 TCP 连接上完成协议协商后原地切换到 TLS。重新连接会丢失协议状态，不能视为兼容实现。CNet 当前只支持从 `tls://` 建连或在 accept 时立即进入 TLS，因此 SaltsNet 邮件协议仍被 CoroNet 阻塞。

## 决策

在 CNet 流连接上增加显式的客户端和服务端 TLS 升级入口：

- `cnet_start_tls()` 使用一次性 `cnet_tls_client_config` 或可复用 `cnet_tls_client`。
- `cnet_start_tls_server()` 使用可复用 `cnet_tls_server`。
- 升级保留原 `cnet_connection` 的 slot 与 generation；连接记录和 owner session 仍是唯一事实源。
- owner 发布 `CNET_CONNECTION_TLS_HANDSHAKING`，握手成功后再次发布 `CNET_CONNECTION_CONNECTED`；失败发布现有 `FAILED`，错误阶段为 `handshake`。
- 握手失败后关闭连接，禁止恢复明文或自动重连。

## 准入契约

升级只接受满足以下条件的连接：

- 当前传输是已连接的明文 TCP；UDP、Pipe、直接 TLS 和失效句柄均拒绝。
- 没有已准入或在途的 send、receive、close 或另一项 TLS 升级。
- client 已配置固定 `tls_io_buffer_bytes` 与 `tls_handshake_timeout_ms`。
- 客户端升级必须得到非空、边界内的验证主机名；验证和 SNI 不可关闭。

公开入口同步校验并返回 `SALTS_EBUSY`、`SALTS_ENOTSUP`、`SALTS_ENOENT`、`SALTS_EINVAL` 或有界队列错误。成功仅表示升级命令已被有界队列接纳，最终结果由状态回调报告。

## 所有权、容量与关闭

- 一次升级是一个固定大小命令；配置字符串在准入期间消费，队列仅复制 owner payload。
- 一次性配置同步构造 TLS context；可复用 context 在准入时增加引用。成功发布后，该引用转移给 owner 命令，TLS state 初始化后再转移给 session。
- 发布失败、陈旧命令、验证失败和终止路径必须各释放一次 context；不能泄漏或重复释放。
- 每个升级会话继续使用 client 配置的两侧 BIO 和 scratch buffer 硬上限，不新增无界缓存或线程。
- stop/close 可取消握手；context 只在命令被消费或 session 终止后释放。

## 状态迁移

`OPEN(TCP) -> PROTOCOL_HANDSHAKING(TLS) -> OPEN(TLS)`。

升级命令由同一 poll owner 顺序消费。公开 client 在成功准入时立即禁止新的收发，并把连接标记为 TLS 未连接；owner 进入握手后发布 handshaking，成功 CONNECTED 前 TLS 查询返回 `SALTS_ENOTCONN`。

## 兼容性与验证

枚举新值追加在尾部，保留现有数值。既有 `tls://` 和 accept-TLS 行为不变。验证包括：

- session 状态迁移单测；
- command payload 复制与边界单测；
- public API 参数、状态和在途收发拒绝单测；
- loopback 明文协商后双端原地 TLS、证书校验、加密收发和失败不降级集成测试；
- 既有 CNet API/owner/TLS/command 回归测试。

## 回滚

删除新增 API、命令类型、状态值与迁移分支即可恢复原行为；既有连接格式、持久数据和安装布局没有迁移。
