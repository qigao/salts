# CNet UDP 与 KCP 数据路径设计

## 背景与决策

CNet 已支持 `udp://` 的 connected-client 路径，但公开 listener 只接受 TCP。Flowie 需要在不绕过
`Salts::CNet` 的前提下接收 UDP，并需要可组合的 KCP 可靠消息层。

本次采用两个正交接口：

- `cnet_datagram` 是 caller-driven 的绑定 UDP socket，直接复用 NativeIO 的
  `UDP_RECV_FROM`/`UDP_SEND_TO`。
- `cnet_kcp` 是 caller-driven、与 socket 解耦的 KCP session。它通过同步 output callback 把
  KCP datagram 交给 `cnet_datagram_send()`；收到的 UDP payload 由调用方传给
  `cnet_kcp_input()`。
- `cnet_packet_endpoint` 是面向应用的统一门面。UDP/KCP 都通过 generation-safe session
  handle 使用相同的 `session_open/send/poll/session_close/stop/destroy` 生命周期；内部以
  `(peer, conv)` 的预留有界哈希索引路由，UDP 的 conv 固定为零。

没有把 UDP 伪装成 TCP accept：UDP peer 是可复制的地址值，不是 OS 连接。没有把 KCP 塞进
`cnet_client` 的 stream state machine：KCP 的 timer、message boundary 和重传队列归
`cnet_kcp` 所有，避免让 TCP/TLS 的连接状态与错误语义发生隐式变化。

## UDP 所有权与容量协议

`cnet_datagram_init()` 同步创建并绑定一个 socket，同时预分配：一个 receive buffer、
`send_capacity` 个 send slot、`request_capacity` 个 NativeIO request 元数据和
`completion_batch_capacity` 个 completion。所有容量都是硬上限。

- 调用者是唯一 owner；`send`、`receive`、`poll`、`stop`、`destroy` 不得并发。
- `cnet_datagram_wake()` 是唯一允许从非 owner 线程调用的操作。
- `cnet_datagram_receive(demand)` 增加 datagram demand；每个成功 receive callback 消耗一个。
- receive view 和 peer 指针只在 callback 期间有效。
- `cnet_datagram_send()` 在成功返回前复制 peer 与 payload；失败时不保留输入。
- 每个成功 send admission 恰有一次 terminal send callback，状态可以是成功、I/O 失败或取消。
- send slot 满返回 `SALTS_ENOBUFS`，不阻塞、不扩容、不覆盖已接受 payload。
- `stop` 先关闭 admission，再取消并观察全部 request；只有 drain 完成后 `destroy` 才成功。

UDP 不维护隐式 peer cache，因此不会因源地址洪泛产生无界会话状态。应用层若需要会话，必须
自行设置有界 peer/session 容量和过期策略。

## KCP 所有权与容量协议

`cnet_kcp_init()` 创建一个固定 conversation id 的 KCP session，并预分配一个
`max_message_bytes` receive buffer。session 只允许单 owner 访问。

- `cnet_kcp_input()` 只借用一个 UDP datagram 到函数返回，并立即投递所有完整 KCP message。
- receive view 只在 callback 期间有效。
- `cnet_kcp_send()` 由 KCP 复制输入；调用前用 `send_segment_capacity` 对
  `ikcp_waitsnd()` 加本次最坏分片数做 checked admission。
- output view 只在 output callback 期间有效；output 失败由当前 `update` 返回，KCP 保留协议状态，
  调用方稍后继续驱动重传。
- `cnet_kcp_update(now_ms)` 是唯一 timer 入口；`cnet_kcp_check(now_ms)` 返回下一次建议驱动时刻。
- `destroy` 要求调用方已经停止 UDP 输入与 timer 驱动；销毁后所有内部 KCP segment 释放。

`send_segment_capacity` 的单位是 KCP segment。一次 message 的保守需求为
  `ceil(message_bytes / (mtu - 24))`（24 字节是 KCP v1.7 wire header），加法和向上取整均做
  溢出检查。

## 安全边界

裸 `cnet_kcp` 只提供可靠性、排序与重传，不提供认证或加密。它的安全属性等同于明文 UDP，
而不是 TLS。`cnet_secure_kcp` 是独立的安全会话层；统一 endpoint 通过显式
`CNET_KCP_SECURITY_PSK_V1` 选择它，`NONE` 仍表示裸 KCP，不做隐式降级或 wire sniffing。

PSK v1 固定兼容 CoroNet 已发布的 wire protocol：

- `TKSH`：64-byte client/server hello，BLAKE2b keyed MAC 认证；client nonce 在重试期间保持不变。
- `TKSR`：32-byte header、XChaCha20-Poly1305 ciphertext 和 16-byte tag；方向、session epoch 和
  单调 packet number 均纳入认证，64-packet replay window 拒绝重复或过旧记录。
- `TKF1`：30-byte header、payload 和 16-byte keyed MAC；Reed–Solomon 的 data/parity dimensions
  属于 wire contract，任一端不一致即 fail fast。
- KCP conversation 为 `low32(epoch) XOR high32(epoch)`，结果为零时归一为一。

这套格式被明确命名为 v1，而不是“自动安全 KCP”。后续格式必须使用新 mode/version，不能在
相同配置值下改变 KDF、nonce、header、FEC 或重放语义。PSK 只在 init 时复制到 endpoint/session
拥有的存储，close/destroy 和初始化失败路径都显式 wipe；错误与日志不得包含密钥材料。

安全 endpoint 不在认证前调用应用 `on_admit`。未知 peer 的 client hello 先以 endpoint PSK 做
无状态 MAC 校验；认证失败静默拒绝，不占 session slot，也不产生反射响应。通过校验后才进入
有界 session 表。此规则不能完全消除被窃取 hello 的 UDP 源地址重放；增加 cookie 会改变旧 wire
protocol，因此留给新的协议版本处理。

## 安全 KCP 所有权、状态与容量协议

`cnet_secure_kcp` 组合一个 `cnet_kcp`、一个 secure state 和一个 Reed–Solomon state，全部由同一
poll owner 驱动，不创建线程。公开状态机为：

```text
client: INITIAL -> HANDSHAKING -> ESTABLISHED -> DESTROYED
server: INITIAL -> WAITING     -> ESTABLISHED -> DESTROYED
```

- client `start(now)` 成功接纳首个 hello 后进入 HANDSHAKING；`update(now)` 按
  `handshake_retry_ms` 重发同一 hello。server 只响应通过 PSK 认证的 hello。
- `send` 仅在 ESTABLISHED 接纳；之前返回 `SALTS_EBUSY`。一次成功接纳由 KCP 拥有 payload copy，
  不表示 UDP/FEC 已发送或对端已收到。
- input datagram 只借用到函数返回。完整应用 message 的 view 只在 receive callback 内有效。
- output callback 每次接收一个完整 wire datagram，必须在返回前复制或同步接纳；失败保留在
  session 中并由当前/下一次 update 返回，KCP 状态等待后续重传。
- FEC encode storage 与 `receive_group_count` 个 decode group 在 init 时按硬上限预分配。正常
  data/parity path 不增长；恢复丢失 shard 的临时矩阵分配只发生在 loss path，失败返回
  `SALTS_ENOMEM`，不会交付半恢复 payload。
- 每组常驻接收预算为
  `(data_shards + parity_shards) * (max_payload_size + 2)` bytes，乘加使用 checked arithmetic；
  全 session 上限为 64 MiB。group 槽按 `group_id % receive_group_count` 复用，复用即使旧 borrowed
  view 失效，因此 view 不得跨 callback 保存。
- FEC data frame 先交给 AEAD/replay 校验，再进入 KCP；parity 只有达到 `data_shards` 个 authenticated
  shard 后才恢复。MAC/AEAD/epoch/direction/replay 任一失败均不推进 KCP 状态。
- destroy 要求停止 input/update，依次释放 KCP、FEC storage，最后 wipe PSK、派生 key、nonce、
  epoch 和 replay window。

统一 endpoint 中，显式 `session_open(peer, 0)` 创建 client role；认证后的未知入站 hello 创建
server role。安全会话在握手期间使用 `(peer, 0)` 作为内部索引，建立后 conversation 仅作为只读
session info 暴露；FEC wire frame 不携带 KCP conversation，因此路由事实源始终是 peer/session，
不维护第二份可独立推进的 conversation map。

## 统一门面的语义边界

`cnet_packet_send()` 在 UDP 与 KCP 下统一表示“payload 已复制并被有界队列接纳”，不表示对端已
收到。UDP 的一次应用消息对应一个 wire datagram；KCP 的一次应用消息可能对应多个发送、ACK 与
重传，因此门面不会虚构逐消息 ACK callback。异步 UDP 终态失败和 KCP output admission 失败统一
通过 session-scoped `on_error` 报告。

未知入站 `(peer, conv)` 只有在 `on_admit` 明确返回 `SALTS_OK` 后才创建 session；容量满时返回并
报告 `SALTS_ENOBUFS`，不扩容、不淘汰已有 session。session close 后 slot 可复用，但 generation
递增，旧 handle 返回 `SALTS_ENOENT`。

## 复杂度与性能验证

UDP send admission 和 completion 回收均为 O(1)，不在热路径分配。KCP 的协议复杂度由上游 KCP
实现决定；CNet wrapper 的 admission 与 callback drain 不引入按历史消息数增长的扫描。

验证覆盖：容量为零、send slot 饱和、borrow/copy 边界、IPv4/IPv6 peer round-trip、receive
demand、cancel/drain、KCP 分片、乱序/丢包重传、错误 output、segment HWM 和 C++ header 编译。
性能验证在既有 `cnet_io_benchmark` 上增加 bound UDP receive/send 场景后再设置回归阈值；功能实现
阶段不预先宣称吞吐收益。
