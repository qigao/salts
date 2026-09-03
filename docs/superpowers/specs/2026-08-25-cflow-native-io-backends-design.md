# CFlow Native IO Backends Design

## 背景与决策

已实现的 `cflow_io_actor` 固化了有界 mailbox、请求完成信用、Executor 投递和确认释放；
它刻意把 OS I/O 放在 `cflow_io_backend_ops` 之后。本期增加四个明确选择、无 fallback 的
socket backend：Linux epoll、Apple kqueue、Windows IOCP 和 Linux io_uring。

四者不能统一成 readiness：epoll/kqueue 是 Reactor，IOCP/io_uring 是 Proactor。
统一边界因此是 Actor backend，而不是把四者都改造成 `salts_readiness_reactor`。
epoll 复用现有 Platform readiness；kqueue 以相同内部 contract 加入 Platform；
IOCP/io_uring 则直接实现 Actor completion backend。既有 `salts_readiness_arm()` one-shot
语义和 CFlow Source 适配保持不变；Platform 另增 callback-return continuation arm，供
readiness native adapter 在 callback 返回后由状态引擎提交 rearm。

```text
cflow_io_actor
  | cflow_io_backend_ops (统一 submit/cancel)
  v
cflow_io_native_backend (显式 backend kind)
  |-- epoll/kqueue: Platform one-shot readiness -> nonblocking syscall retry
  `-- IOCP/io_uring: submit native async request -> consume completion
  |
  `-> cflow_io_actor_complete (唯一 terminal evidence)
```

## 公开接口与兼容性

新增 `cflow/io_native.h`，不修改既有 `io_actor.h` 结构：

- opaque `cflow_io_native_backend`；
- `cflow_io_native_backend_kind`：EPOLL、KQUEUE、IOCP、IO_URING；
- `cflow_io_native_operation`：TCP recv/send 与 UDP recvfrom/sendto；
- init、Actor ops、stats、closed-socket identity release、shutdown、destroy。

连接建立、监听和 socket 配置属于控制面，不在本期异步 operation 集合。公开的四种
operation 在所有已支持 backend 上都完整实现；不公开 accept/connect 占位接口。
显式选择当前构建或 OS 不支持的 backend 返回 `SALTS_ENOTSUP`，不得自动转 epoll、
线程池或阻塞 I/O。

地址通过 caller-owned native `sockaddr` byte storage 传递。TCP operation 不使用地址；
UDP send 输入 `address_length`，UDP recv 输入 `address_capacity` 并在完成前写回
`address_length`。长度统一限制到 `UINT32_MAX`，使 Windows `WSABUF` 与 POSIX 具有
同一可验证上界。

## 数据、事实源与所有权

- Actor request table 仍是 operation/lease/completion 的业务事实源。
- Native backend 的固定 record table 只是派生的 OS in-flight 索引，主键为非零
  `request_id`；completion 接受后 record 立即可复用。
- `cflow_io_native_operation` 与 buffer/address 均由 Actor operation token 的调用方拥有。
  submit 接受后，到 Actor completion callback 返回前不得释放或修改 send buffer、
  socket 或 address；recv buffer/address 仅由 backend 写入。
- Backend 不关闭 caller socket。epoll/kqueue 为每个已知 socket identity 至多持有 read、
  write 两条 lane；每条 lane 各有一个 duplicate descriptor、一个 Platform registration
  和一个固定容量 intrusive FIFO。duplicate 在 `forget_socket` 或 shutdown 时关闭，不转移
  原 socket 所有权；
  socket syscall 逐次使用 `MSG_DONTWAIT`，不要求也不修改 caller 的 `O_NONBLOCK`。
  Linux/Apple send 路径逐次使用 `MSG_NOSIGNAL`，避免修改 socket option，也避免对端关闭
  把进程级 `SIGPIPE` 变成隐藏的控制流。
- Windows socket 与 IOCP 的关联在 handle 存活期间不可撤销；backend 因而维护
  `request_capacity` 大小的固定关联表。caller 在 socket 已关闭且相关请求归零后调用
  `cflow_io_native_backend_forget_socket()`，显式释放旧 handle identity，避免把 Windows
  后续复用的数值误认为仍关联当前 IOCP。
- readiness caller 同样必须在 socket 关闭且该 identity 的请求归零后调用
  `forget_socket`；否则 bounded socket table 会保留旧 identity，并可能把复用后的同数值 fd
  误认作旧 socket。
- Platform reactor worker 与 Proactor worker 借用 Actor completion handle，直至该
  backend 的 active count 为零；readiness adapter 不再创建 controller worker。

## 并发、状态机与线性化

Actor driver 串行调用 submit/cancel；backend worker 可并发调用
`cflow_io_actor_complete()`。epoll/kqueue 使用 Platform reactor worker 产生 one-shot
callback，直接在对应 read/write lane 上执行正常 I/O 有界批次 syscall 与 Actor completion；若仍
would-block，callback 返回 `REARM + 固定 lane interest`，由 Platform 在 callback 返回后
串行提交。IOCP/io_uring 各有一个 completion worker。

```text
FREE -> SUBMITTING -> PENDING -> COMPLETING -> FREE
                     | cancel request
                     `-> native cancel/remove -> completion(CANCELLED)
```

- submit 线性化：固定 record 成功保留；满时返回 `SALTS_EBUSY`，不产生 native effect。
- terminal 线性化：record 首次从 PENDING 转为 COMPLETING；重复 native event/CQE 计为 stale。
- epoll/kqueue 在 nonblocking syscall 返回 EAGAIN/EWOULDBLOCK 时通过
  `salts_readiness_arm_continuation()` one-shot arm；事件到达后由 reactor callback 重试，
  仍 would-block 则返回 rearm。read/write lane 各自 FIFO，取消可从队列移除非队首请求，
  普通完成不越过同方向队首。
- readiness terminal error 无法安全 rearm；为保持 accepted request 的 exact-once terminal
  evidence，callback 会 drain 该 lane，最大工作量由 `request_capacity` 硬限制，而不是
  `completion_batch_capacity`。因此 `request_capacity` 同时是故障回调的延迟预算，配置时
  必须结合 reactor 上其他 registration 的可接受暂停时间。
- Apple kqueue 对纯 `ERROR/HANGUP` interest 使用带 `NOTE_LOWAT` 的内部 read filter；
  普通可读数据不会作为 READ 泄露给调用方，socket 无法继续接收时仍产生 EOF/error
  evidence。
- IOCP 的 `OVERLAPPED` 和 `WSABUF` 内嵌于固定 record，并保持到 GQCS completion。
- io_uring SQ 写入由 backend mutex 串行化，CQ 只由 worker 消费；每次槽位 claim 生成
  `(generation, record_index)` `user_data`，cancel 只匹配该 generation，避免迟到 cancel
  误伤复用槽位。cancel completion 使用零、shutdown NOP 使用保留值并由控制面消费。
- cancel 是 best effort，但若 backend 成功移除尚未执行的 readiness watch，则主动提交
  CANCELLED completion；Proactor cancel 后仍以 native CQE/IOCP packet 为 terminal evidence。

用户 callback、Actor callback、socket syscall、native wait、Executor 操作均不在 backend
mutex 内执行。

## 容量、背压与内存预算

配置 `request_capacity` 和 `completion_batch_capacity` 都必须为正，batch 不超过
`request_capacity`。batch 约束正常 I/O 事件；terminal drain 的硬上限是
`request_capacity`。初始化一次性分配 record 与 completion/event batch，数据面不扩容。

```text
active_native_requests <= request_capacity
live_readiness_socket_identities <= request_capacity
live_readiness_registrations <= 2 * request_capacity
live_iocp_socket_identities <= request_capacity
resident metadata = backend header
                  + request_capacity * backend_record_size
                  + readiness(request_capacity * socket_lane_record_size)
                  + IOCP(request_capacity * socket_identity_record_size)
                  + completion_batch_capacity * native_event_size
```

epoll/kqueue 每个已使用方向额外占一个 duplicate descriptor 和一个 Platform registration；
其 Platform registration 容量是 `2 * request_capacity`，event batch 仍使用配置值。IOCP
每个 request 占一个 `OVERLAPPED`；io_uring 的 SQ/CQ mmap 大小由 kernel 对请求容量取整后的
entries 决定。
满额返回 `SALTS_EBUSY`，由 Actor 转为 FAILED completion；不建立 fallback queue，
不无界分配，不覆盖旧请求。

## 错误与关闭协议

- 参数/operation shape 错误：`SALTS_EINVAL`。
- backend 容量满：`SALTS_EBUSY`。
- OS 错误：POSIX 返回负 errno，Windows 返回负 Win32/WSA code。
- TCP recv 零字节映射 EOF；UDP 零长度 datagram 是成功的 OK(0)。
- native cancellation 映射 CANCELLED；其他 terminal 错误映射 FAILED(error)。
- `shutdown` 首次关闭 backend admission；active 非零返回 `SALTS_EBUSY`，保留
  cancel/completion 能力。readiness active 为零时关闭所有 retained lane 后 shutdown/join
  Platform reactor；Proactor 则唤醒、join completion worker。
- `destroy` 只在 shutdown 成功后释放资源，否则 `SALTS_EBUSY`。

调用顺序是：Actor close → drive cancel/completion → Executor drain → acknowledge →
Actor destroy → native backend shutdown/destroy → Executor shutdown。该顺序保持原 Actor
证明与公开行为。

## 候选方案与取舍

1. 把 IOCP/io_uring 塞进 `salts_readiness_reactor`：拒绝，completion 不是 readiness，
   会丢失 buffer/OVERLAPPED 生命周期语义。
2. 每个平台暴露不同 Actor API：拒绝，会复制 admission、completion credit 与关闭协议。
3. 复制现有 epoll backend：拒绝。Platform 已有经过 generation/shutdown/race 测试的
   readiness contract；CFlow 只增加从 readiness 到 Actor completion 的薄适配。
4. 引入 libuv/liburing：本期拒绝。仓库没有该依赖，引入会改变部署与许可面；薄 native
   adapter 已能表达本期四种 operation。io_uring 仅在适配层封装 raw ABI。
5. 每 request 创建线程执行 blocking socket：拒绝，不是请求的 native model，也无法给出
   同等有界资源与取消语义。

## 双 Native TCP/UDP Echo 性能契约

原有 network benchmark 默认保持 `CFLOW_NETWORK_PEER=raw`：client 经过 CFlow native
backend/Actor/Executor，peer 由阻塞式 socket server thread 驱动，并继续覆盖 TCP/UDP、
latency/throughput 与 blocking/busy wait。`CFLOW_NETWORK_PEER=native` 支持 TCP 与 UDP，
两种协议都通过两个独立 native endpoint 完成 Echo，不隐式回退到 raw peer。

双 native fixture 有两个彼此独立的 endpoint owner。每个 endpoint 独占一个 native
backend、I/O Actor、manual Executor、completion probe 与 socket identity；fixture 只拥有
两者共同借用的 wake latch。benchmark thread 是 submit、pump、completion validation、
acknowledge 的唯一 owner；backend worker 只发布 completion 并唤醒 latch。每个 endpoint
最多存在一个 outstanding operation。TCP short send/recv 通过有界循环完成，零字节进展
视为 `SALTS_EIO`；UDP 以单个 datagram 为数据单元，send/recv 字节数不完整即失败。

每个传输方向先提交 receiver read，再提交匹配的 sender write，并共同 pump 两个 endpoint；
两端 completion 均 acknowledge 后再推进下一阶段。benchmark 的 heap operation wrapper
深拷贝 send address，并为 recv 内嵌独立 address storage；UDP receiver 在 completion callback
返回前把 native operation 发布的 source address 与 length 复制到 completion probe，server
随后使用该来源地址发送响应。地址缺失、越界、来源 endpoint 不匹配或 datagram 长度不完整
均为显式错误。完整
Echo 按 client→server、server→client 两个方向顺序执行，最后逐字节校验 payload。TCP 的
配对也避免 payload 超过当前 socket send window 时 sender 等 receiver、receiver 又尚未提交
的互等。报告中的
`application_bytes` 只计一次 payload，与 ogrenet Echo benchmark 的
`SetBytes(payload_size)` 约定一致；`wire_bytes = 2 * application_bytes` 只是 loopback 双向传输
估算。JSON schema 继续使用 `cflow-network-benchmark/v1`，兼容字段 `peer_mode` 与
`exchanges_per_second` 保持不变。CI 对 TCP 的 1 KiB、4 KiB、64 KiB 与 UDP 的 1 KiB、
4 KiB、8 KiB，分别在 blocking/busy 模式运行五个独立进程，形成 12 个协议/负载/等待
组合；记录 Echo/s、应用 MiB/s、P99、process CPU、RSS、errors、rejections 与 stale
completions。UDP CI 上限采用各目标均可运行的共同负载；实现仍允许 host 支持时显式请求
65,507 B。共享 runner 数据只作为可比较证据，不设性能阈值。Ubuntu 24.04 另有显式
`io_uring` 行；初始化不支持、策略拒绝或报告 backend 不匹配都使任务失败，不允许回退。

关闭顺序为：关闭原 socket → Actor close/drain/destroy → forget closed socket identity →
backend shutdown/destroy → Executor shutdown/destroy → shared wake latch destroy。成功报告要求
两个 endpoint 均无 cleanup、rejection 或 stale completion 错误。

## 迁移、回滚与验证

这是新增 API。现有 Actor、自定义 backend、readiness Source、数据格式和错误码不变。
调用方可先保持自定义 backend，再显式迁移到 native backend。回滚可移除新增 header/source/
CMake 条目，不需要数据迁移。

验证包括：Platform kqueue backend-neutral contract、公共 C/C++ header、
invalid/unsupported/full、真实 TCP/UDP loopback、短读写、
zero-length UDP、pending cancel、Actor/Executor 关闭顺序、backend stats、各 OS 编译与运行，
以及 io_actor/readiness/disruptor 相邻回归。

## 一手接口资料

- [Apple XNU `sys/event.h`](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/event.h)
- [Linux io_uring userspace API](https://www.kernel.org/doc/html/latest/userspace-api/io_uring.html)
- [Microsoft I/O completion ports](https://learn.microsoft.com/windows/win32/fileio/i-o-completion-ports)
