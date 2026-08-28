# CFlow Advanced Socket Message Design

## 状态与范围

本设计解决 #132，只决定当前 native socket 数据层是否接纳 vectored TCP、UDP
ancillary metadata 与 UDP batching。本阶段不修改公开 C API，不改变既有 operation 数值、
结构体布局、错误码、backend 选择或 Actor 状态机。

DNS、TLS、bind/listen、multicast、socket option policy、message framing 与跨进程通信不在
本设计范围内。

## 证据

### 仓库事实

- `cflow_io_native_operation` 当前以单个 `buffer + length` 表达 TCP/UDP payload，长度统一
  限制到 `UINT32_MAX`；UDP 另借用 address storage。
- `cflow_io_actor` 的事实源是固定容量 request table。一个成功接纳的 request 最终产生
  一次权威 terminal completion，cancel 只是请求，不是 terminal evidence。
- epoll、kqueue 与 poll 共用 readiness adapter；IOCP 与 io_uring 使用固定 completion
  record。所有 backend 都禁止隐式 fallback 和数据面无界分配。
- 当前 completion 只表达 kind、bytes 与 error；没有 payload truncation、control
  truncation、metadata-present 或每个 datagram 的结果数组。

### 平台事实

- Windows `WSARecv`/`WSASend`、`WSARecvFrom`/`WSASendTo` 和 message variants 接受
  `WSABUF` 数组，能够表达 scatter/gather。Overlapped completion 返回本次传输的总字节数。
- POSIX `recvmsg`/`sendmsg` 使用 `msghdr.msg_iov`，按 iovec 顺序填充或发送；stream
  operation 可以短完成，零字节 TCP receive 表示 orderly shutdown。
- io_uring 提供 `RECVMSG`/`SENDMSG`。普通 request 仍可保持一次 SQE 对一次 terminal CQE；
  multishot receive 则允许一次 SQE 产生多个 CQE，并依赖 provided-buffer protocol。
- Windows 与 POSIX 都通过 control message 返回 ancillary data，但具体 option、类型、结构体、
  address-family 规则和 timestamp clock domain 不同。Payload truncation 与 control
  truncation也是两个独立条件。
- Linux `recvmmsg`/`sendmmsg` 是 Linux-specific batching；Windows 和 Apple 没有与它相同的
  portable completion、partial-batch 和 cancellation contract。

## 决策

| 候选 | 决策 | 原因 |
|---|---|---|
| Vectored TCP receive/send | 接受为后续独立加法式 API | 所有目标 backend 都有一请求 scatter/gather 原语，可保留短完成、EOF、取消与一次 terminal completion |
| Raw ancillary/control buffer | 拒绝进入 portable CFlow API | 泄漏平台 ABI、对齐、socket option 和 control-message 类型，且无法稳定表达截断与字段存在性 |
| UDP packet info | 当前拒绝；满足 reopen gate 后可设计 normalized profile | 需要调用方先配置 IPv4/IPv6 socket option，当前 native operation 层不拥有该配置事实 |
| UDP ECN | 当前拒绝；与 packet info 共用 reopen gate | enablement 和 dual-stack 规则跨平台不同，不能从一次 operation 推导配置是否有效 |
| UDP receive timestamp | 拒绝 portable core | software/hardware、clock domain、精度、receive/transmit queue 语义不一致，单个整数时间戳会丢失关键语义 |
| UDP batch operation | 拒绝 portable request model | 会把一个 request 拆成多个 datagram terminal，破坏当前 Actor credit、cancel 和 acknowledge 不变量 |
| Backend 内部 batching | 仅允许为不可观测优化 | 必须保持每个 request 的独立 terminal/cancel/order；只有 profiling 证明瓶颈后才可实现 |

## Vectored TCP 的后续公开契约

Vectored TCP 不追加字段到 `cflow_io_native_operation`，也不复用既有 enum。公开结构体没有
size/version 字段，直接扩展其布局会改变 ABI。后续实现应新增独立 typed operation 与 Actor
ops，同时复用同一 backend、socket identity、lane 和 completion state machine。

建议接口形状如下；这是后续实现约束，不是本设计 PR 新增的公开声明：

```c
#define CFLOW_IO_NATIVE_VECTOR_MAX 16u

typedef struct cflow_io_native_buffer_span {
    void *data;
    size_t length;
} cflow_io_native_buffer_span;

typedef enum cflow_io_native_vector_operation_kind {
    CFLOW_IO_NATIVE_TCP_RECV_VECTOR = 0,
    CFLOW_IO_NATIVE_TCP_SEND_VECTOR
} cflow_io_native_vector_operation_kind;

typedef struct cflow_io_native_vector_operation {
    cflow_io_native_vector_operation_kind kind;
    uintptr_t socket;
    const cflow_io_native_buffer_span *buffers;
    size_t buffer_count;
} cflow_io_native_vector_operation;
```

后续 API 必须提供独立 capability query 和 Actor backend ops。所有 backend 都支持后才可将
该 operation 声明为 portable；否则 capability query 必须按 backend/operation 明确返回
unsupported，禁止循环调用 scalar operation 作为 fallback。

Capability query 只报告当前 build 的 backend/operation 原生映射。需要 host limit 的 backend
在 vector adapter 初始化时验证固定 16 段预算；host 不满足时该 vector adapter 以
`TURBO_ENOTSUP` 失败，既有 scalar backend 仍可使用。实现不能把 capability 缩成一个较小但
未公开的段数，也不能在 submit 时拆成多个 scalar request。单个 socket/handle 的有效性仍在
submit 边界检查，不由 compile-time capability query 代替。

### Admission 与容量

- `buffer_count` 范围是 `1..CFLOW_IO_NATIVE_VECTOR_MAX`。
- 16 是 CFlow 的固定 metadata budget，不是对 host `IOV_MAX` 的推断。初始化或 capability
  probe 必须确认 backend 能原生提交 16 段；不能满足时整个 vector capability 为 unsupported。
- 每段长度必须非零且 data 非空；总长度用 checked addition 计算，并限制到
  `UINT32_MAX`。
- backend 在 submit 内把 span descriptors 转换并复制到固定 request record。descriptor
  array 在 submit 返回后即可失效；payload storage 仍由调用方拥有并借用到 terminal
  callback 返回。
- 每个 request record 固定保留最多 16 个 `WSABUF` 或 `iovec`。不在数据面分配，也不因
  segment count 增长 record table。

### 所有权矩阵

| 对象 | 所有者 | 借用/有效期 | 终态行为 |
|---|---|---|---|
| vector operation 与 caller span array | caller | 只借用到 submit 返回；backend 在返回前校验并复制 descriptors | backend 不保留 caller descriptor 指针，也不释放它们 |
| receive payload spans | caller | 成功接纳后由 backend 独占写，直到 terminal callback 返回 | callback 返回后 caller 可读、复用或释放 |
| send payload spans | caller | 成功接纳后保持不可修改，直到 terminal callback 返回 | callback 返回后 caller 可修改、复用或释放 |
| converted `WSABUF`/`iovec` 与 native message header | backend request record | 从成功接纳保持到 native completion 被消费 | acknowledge/release request record 时回收固定 slot；无数据面 free |
| completion/result | Actor | 只借给 terminal callback；`bytes` 是可按值复制的总前缀长度 | callback 返回后 completion 指针失效，Actor 继续 acknowledge/release 流程 |
| socket | caller | backend 从接纳借用到 terminal callback 返回 | backend 不 close；quiescent 后由 caller close/forget |
| UDP address storage | 既有 scalar API 的 caller | 本设计不改变现有 `io_native.h` 契约 | vector TCP 不接收 address，也不新增 address result |
| ancillary/control buffer | 不存在于 portable API | 没有 caller borrow；backend 不向 callback 暴露 native control pointer | 若 reopen，只能在固定 request record 内解析为 normalized value result |

因此取消与失败只结束 backend 的借用，不转移 payload、socket 或 descriptor 所有权。未成功
接纳时 backend 不保留任何 caller 状态；成功接纳后恰好通过一次 terminal callback 归还所有
payload/socket borrows。

### 完成、顺序与所有权

- `bytes` 是逻辑串联 payload 前缀的总传输字节数；receive 按 segment 顺序填充，send 按
  segment 顺序消费。
- 成功允许 `0 < bytes <= total_length` 的短完成。TCP receive 的 `bytes == 0` 仍映射
  `CFLOW_IO_COMPLETION_EOF`；send 的零进展不是自动重试承诺。
- backend 只借用 payload，不拥有或释放 payload。receive 期间每段是 backend 独占写；
  send 期间每段不可修改。
- cancel、stale completion、socket forget 与 shutdown 完全复用 scalar TCP 的终态和顺序。
- readiness backend 使用单次 nonblocking `recvmsg`/`sendmsg`；IOCP 使用单次
  `WSARecv`/`WSASend`；io_uring 使用普通一次性 `RECVMSG`/`SENDMSG`。不得用 N 次 scalar
  syscall 伪装成一个 vector request。

```text
vector metadata upper bound
  = request_capacity
    * (16 * max(sizeof(WSABUF), sizeof(iovec)) + native message header)
```

这部分是初始化期固定 resident metadata，不包含 caller-owned payload bytes。

### Backend feasibility

| Backend | Native mapping | Completion model | Required retained state | Semantic boundary |
|---|---|---|---|---|
| IOCP | one overlapped `WSARecv` / `WSASend` with `WSABUF[1..16]` | one IOCP packet | `OVERLAPPED` and fixed converted spans in request record | total bytes may be a short prefix; zero-byte receive is EOF |
| io_uring | one ordinary `IORING_OP_RECVMSG` / `IORING_OP_SENDMSG` | one terminal CQE | fixed `iovec[16]` and `msghdr` in request record | do not use multishot or provided buffers in this API |
| epoll | one nonblocking `recvmsg` / `sendmsg` after readiness | one Actor terminal completion or one-shot rearm | fixed `iovec[16]` in retained lane request | `EAGAIN` rearms; no scalar loop |
| kqueue | same shared readiness adapter mapping as epoll | one Actor terminal completion or one-shot rearm | fixed `iovec[16]` in retained lane request | EOF/error remains kqueue readiness evidence |
| poll | same shared readiness adapter mapping as epoll | one Actor terminal completion or one-shot rearm | fixed `iovec[16]` in retained lane request | explicit O(registration capacity) readiness backend, never fallback |

## Ancillary metadata 的拒绝边界与 reopen gate

Portable core 不接受 caller-owned raw control buffer、`cmsghdr`/`WSACMSGHDR`、native flag
或 platform-specific packet-info structure。这样会把第三方 ABI、alignment 与 socket
配置状态扩散到领域层，也无法让调用方区分“字段未启用”“平台不支持”“control buffer
被截断”。

只有同时满足以下条件才重开 normalized UDP metadata：

1. 有明确 consumer 同时需要至少 Windows、Linux、macOS 中两个平台，而不是预想需求。
2. 单独定义 socket configuration owner；它负责 IPv4/IPv6/dual-stack enablement，并可查询
   实际 capability，不把 `setsockopt` 隐藏在 receive operation 中。
3. 使用 normalized result + presence bits；payload truncation、control truncation 和未知
   control message 都有可区分结果。
4. packet info 精确定义 destination address 与 interface index；ECN 只接受 `0..3` codepoint；
   timestamp 明确 software/hardware、clock id、精度和转换责任。
5. 每个平台具有真实 socket parity、disabled-option、truncation、cancel、shutdown 与
   C/C++ header 测试。

## UDP batching 的拒绝边界与 reopen gate

当前 Actor 协议以 request 为 admission、completion credit、cancel 与 acknowledge 单元。
`recvmmsg`、multishot recvmsg 或等价机制会让一次提交产生多个 datagram 结果，必须另行定义
batch slot、每项状态、partial batch、buffer lease、关闭 drain 与取消终态。因此它不能新增为
现有 native operation kind，也不能以一个 completion 隐藏部分 datagram 失败。

只有 profile/benchmark 证明 UDP syscall 或 submission overhead 占代表性 workload 总耗时
至少 20%，且给出以下完整协议时才重开：

- hard-bounded batch size、payload budget 与 checked arithmetic；
- 每项 address/length/truncation/status 和 buffer lease；
- partial submit、partial completion、cancel、timeout、shutdown 与 acknowledge；
- IOCP、io_uring、readiness backend 的同语义映射或显式 capability split；
- scalar baseline、典型/峰值/饱和 benchmark 与 retained-memory evidence。

Backend 可在内部批量 drain 已独立 admission 的 requests，但优化不得改变 completion 数量、
顺序、错误、cancel 或回调可见性。

## 架构、兼容性与迁移影响

- 架构：保持 `Actor -> native backend -> authoritative completion`，不新增 Graph、Stream、
  Statechart 或通信层依赖。
- 状态归属：Actor request table 仍是唯一业务事实源；platform vector descriptors 只是固定
  record 内的派生 native state。
- 错误：现有 scalar API 不变；未来 vector shape 错误为 `TURBO_EINVAL`，unsupported 为
  `TURBO_ENOTSUP`，容量满仍为 `TURBO_EBUSY`。
- ABI：本设计 PR 无 ABI 变化；未来 vector 使用独立结构和 entry point，避免扩大现有公开
  struct。
- 性能：不宣称 vector 或 batching 有性能收益。后续 vector 实现先验证正确性；任何 hot-path
  收益必须由现有 network benchmark 的同 runner 对比支持。
- 回滚：删除本设计文档和 README 链接即可；没有数据、配置或运行时迁移。

## 后续验证矩阵

Vectored TCP 实现至少覆盖：1、2、16 segments；总长度边界与溢出；短 receive/send；跨
segment boundary；EOF；pending cancel；same-socket bidirectional lanes；capacity full；stale
identity；shutdown；Windows IOCP、Linux epoll/poll/io_uring、macOS kqueue/poll；C/C++ headers。

Ancillary 与 batching 在 reopen gate 满足前没有公开占位 API、内部 fallback 或未完成代码。

## 一手资料

- [Microsoft scatter/gather I/O](https://learn.microsoft.com/en-us/windows/win32/winsock/scatter-gather-i-o-2)
- [Microsoft `WSARecvMsg`](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_wsarecvmsg)
- [Microsoft `IP_PKTINFO`](https://learn.microsoft.com/en-us/windows/win32/winsock/ip-pktinfo)
- [Microsoft Winsock ECN](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ecn)
- [POSIX.1-2024 `sys/socket.h`](https://pubs.opengroup.org/onlinepubs/9799919799.2024edition/basedefs/sys_socket.h.html)
- [Linux `recvmsg(2)`](https://man7.org/linux/man-pages/man2/recvmsg.2.html)
- [liburing `io_uring_prep_recvmsg_multishot(3)`](https://man7.org/linux/man-pages/man3/io_uring_prep_recvmsg_multishot.3.html)
- [Linux timestamping](https://kernel.org/doc/html/latest/networking/timestamping.html)
- [Apple `recvmsg(2)`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/recv.2.html)
- [Apple `sendmsg(2)`](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/sendmsg.2.html)
