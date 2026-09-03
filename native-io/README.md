# NativeIO

NativeIO 是 Salts 根目录下的原生 I/O 操作层。它只负责把有界操作提交给明确选择的 OS backend，并把终态完成批量交还给调用者；它不拥有 Actor、Reactor、CFlow Graph 或用户 socket。

## 架构决策

原生 backend 留在 CFlow 会让 OS 完成状态依赖 Actor mailbox 和 acknowledge，无法单独测量或复用。放入 Platform 则会把现有 readiness 原语与网络操作、payload 生命周期混成一个职责。独立根模块保持以下单向依赖：

```text
CFlow / Actor / future adapters
              |
              v
         NativeIO operations
          /             \
         v               v
Coroutine pool       Platform errors/ABI
         |
         v
 vendor/minicoro
```

当前公开版本提供 Windows IOCP、Linux epoll/io_uring，以及 64 位 macOS/BSD kqueue driver；均支持 TCP connect/recv/send 和 UDP recv_from/send_to。Windows IOCP 支持 overlapped byte-mode named pipe，Linux epoll 与 macOS/BSD kqueue 支持非阻塞 connected byte pipe；io_uring pipe 仍显式返回 `SALTS_ENOTSUP`。工厂只初始化调用方明确选择的 backend，不做隐式 fallback。不满足平台/位宽要求时显式返回 `SALTS_ENOTSUP`。CFlow Actor 与 Reactive 可直接依赖 NativeIO；NativeIO 本身不依赖或拥有 CFlow/CNet 状态。

## 数据与状态协议

- 数据单元：一个 `native_io_operation` 对应恰好一个 terminal `native_io_completion`。
- 事实源：backend 固定 endpoint/request 槽位；上层只持有带 generation 的句柄。
- endpoint 类型：attach 时从 `SO_TYPE` 记录 stream/datagram，分别只接受 TCP/UDP operation；byte pipe 单独记录。IPv4/IPv6 是地址维度，不扩张 endpoint 类型，UDP 地址族仍由 native `sockaddr` 表达。
- 所有权：backend 借用 socket；成功 submit 后借用 payload，直到 observe 返回对应 completion。
- 拓扑：除 `native_io_backend_wake()` 外，一个 backend 只由一个 owner 线程调用。一个 owner 可在同一 backend 上驱动最多 `endpoint_capacity` 个 TCP/UDP/Pipe endpoint；模块不创建线程、不内置任务队列。需要多核扩展时由上层创建多个 backend 并分片 endpoint，不能让多个线程并发驱动同一 backend。wake 是唯一允许从生产者线程调用的合并式控制边。
- 容量：endpoint、request 和 completion batch 均在 init 时固定；满额返回 `SALTS_ENOBUFS`。
- 顺序：每个 endpoint 的 read lane 与 write lane 分别按 FIFO 向内核发起操作，lane 之间不排序。request handle 与 `user_data` 用于关联；不同 endpoint 的 completion 顺序由内核决定。
- 连接：`TCP_CONNECT` 独占尚未连接 stream endpoint 的 admission，重复 connect 返回 `SALTS_EALREADY`，连接终态被 observe 前提交 recv/send 返回 `SALTS_EBUSY`。readiness backend 要求该 socket 已由调用方设为 nonblocking；NativeIO 不改变其模式。
- 取消：cancel 只请求取消。IOCP 的 `ERROR_OPERATION_ABORTED`、io_uring 的 `-ECANCELED` 和 readiness 队列中尚未执行的请求进入 CANCELLED；已经完成的请求不会被改写成取消。
- 关闭：`close admission -> cancel/drain -> close native sockets -> release endpoints -> destroy`。
- 等待：`timeout_ms == 0` 为 poll，`UINT32_MAX` 为无限等待，其余值为相对毫秒 deadline；无终态返回 `SALTS_ETIMEDOUT` 且 count 为零。
- 唤醒：生产者先发布上层命令，再调用 wake。多个并发 wake 合并成一个有界 OS 控制信号；纯控制唤醒使 observe 返回 `SALTS_OK` 且 count 为零，不伪造 completion。close/destroy 前必须先停止 wake 调用者。

### Coroutine owner 路径

`native_io_backend_spawn_coroutine()` 提供同一 owner 线程上的可选结构化路径。entry 立即运行到返回或 `native_io_coroutine_await()`；await 成功提交后挂起，只有匹配的 terminal completion 被 owner observe 后才恢复。一次 observe 会先完成整批 terminal 的 request 归属解析，再恢复其中的 coroutine；因此恢复后的 entry 可以立即再次 await，不会复用仍被该批后续 packet 引用的 request 槽位。NativeIO 不使用 CoroNet context、TLS current-loop、隐式线程或第二套 request 状态机；request 槽位仍是唯一 I/O 事实源，coroutine 只保存执行位置。

coroutine task 数与 request 共用同一硬容量。frame 由 `Salts::Coroutine` 的有界池延迟创建并复用；池满返回 `SALTS_ENOBUFS`。取消 task 只转发为 request cancel，frame 必须等 `CANCELLED`/其他竞态终态被 observe 后才释放。带 ABI 版本与结构大小的 `native_io_coroutine_stats` 公开 `capacity`、`active` 与 `retained_frames`，但不改变既有 `native_io_backend_stats` 布局，也不暴露 minicoro handle。

若 entry 未经 `native_io_coroutine_await()` 直接挂起，则违反 NativeIO coroutine 协议：spawn/resume 返回 `SALTS_EPROTO`，该 suspended frame 被销毁，task slot 立即归还，不把 backend 留在无法 drain 的活动状态。

```text
spawn -> RUNNING -> await/submit -> SUSPENDED
                                   |
                      OS terminal completion
                                   |
                                   v
                              RUNNING -> returned -> pooled
```

请求槽位状态机为：

```text
FREE --submit accepted--> PENDING --observe terminal--> FREE(next generation)
                              |
                              +--cancel request--------+
```

每次成功 submit 恰好产生一个可观察终态。submit 原生失败在返回前回滚到 FREE，不产生 completion。destroy 在 admission 未关闭、请求未 drain 或 endpoint 未释放时返回 `SALTS_EBUSY`，并保留所有权供调用者修复。

## 性能边界

若 byte pipe 的首要目标是最低框架延迟或最高吞吐，并且调用方能够自行管理
operation、completion、容量与关闭顺序，应直接使用 NativeIO Pipe 接口。
Actor 与 CFlow Reactive 适用于需要状态隔离、确认协议、demand 或 Graph 操作的
场景；它们提供额外语义，也会引入相应控制面成本。不要仅为传输数据而把
NativeIO Pipe 强制包装成 Actor 或 Reactive。

direct backend 初始化时预分配 endpoint/request/native event storage，之后 direct submit/observe 不分配内存。coroutine owner 的 task free stack、request 路由表与 completion batch 在首次 spawn 时一次性延迟创建；frame 随后按同一硬上限延迟创建并复用：

- IOCP：connect 使用 `ConnectEx`，socket 数据 submit 直接调用 `WSARecv`/`WSASend`，named-pipe submit 直接调用 overlapped `ReadFile`/`WriteFile`，observe 统一读取 completion port。
- epoll/kqueue：connect 使用 nonblocking `connect` 与 `SO_ERROR`；其余 submit 先以单次非阻塞 syscall 尝试，仅在 would-block 时进入每 endpoint 的 FIFO lane，并由 owner 在 observe 中直接等待 readiness 和继续 syscall。
- io_uring：connect 使用 `IORING_OP_CONNECT`。每个 endpoint 的 read/write lane 各保持至多一个内核 in-flight SQE，其余已接受描述符保留在固定 request 槽位中；observe drain CQ 后推进 lane。ring 由模块映射，但没有 worker、mutex、callback、payload copy 或跨线程 mailbox。

readiness 的 kernel interest 是请求 lane 推导出的镜像，不是第二份业务状态。endpoint/request/terminal storage 和 native event batch 均有固定上限。

从未 spawn coroutine 或当前没有活动 coroutine 时，`native_io_backend_observe()` 直接进入所选 OS driver，不经过 completion 路由缓冲或 coroutine context switch。纯 direct 使用也不会创建 coroutine owner/pool。因此 direct benchmark 仍测量原生 NativeIO 路径；只有显式 spawn coroutine 的调用方承担 owner storage 与 suspend/resume 成本。

## 最小示例

```c
#include <salts/native_io.h>

native_io_backend backend = {0};
native_io_backend_config config = {
#if defined(_WIN32)
    NATIVE_IO_BACKEND_IOCP,
#elif defined(__linux__)
    NATIVE_IO_BACKEND_EPOLL, /* 或 NATIVE_IO_BACKEND_IO_URING */
#else
    NATIVE_IO_BACKEND_KQUEUE,
#endif
    16u, 64u, 16u};

int status = native_io_backend_init(&backend, &config);
if (status != 0)
  return status;

/* Attach an already-created socket, submit operations, then call
   native_io_backend_observe() from this same owner thread. Windows sockets must
   have been created for overlapped I/O. */
```

完整可运行的 TCP/UDP loopback 用法位于 `tests/native_io_test.c`。

网络性能比较位于 CNet 的 `cnet_io_benchmark`，由依赖 NativeIO 的上层 target 统一比较 libuv、NativeIO 与 CNet，避免 NativeIO 反向依赖 CNet。libuv 只链接 benchmark executable，不进入 NativeIO 的公开依赖或生产链接面。

`native_io_pipe_benchmark` 在 Windows IOCP 上比较 raw overlapped named-pipe completion 与 NativeIO，在 Linux epoll 和 macOS/BSD kqueue 上比较 raw POSIX pipe 调用与 NativeIO。每个样本执行 256 次单向 transfer，覆盖 1/4/8/16/32/64 KiB；应用 payload 每次只计一次，不把读端和写端重复计算为两倍流量。fixture、buffer、handle/descriptor 与 backend 初始化位于计时区外，输出独立的 p50/p95 延迟、吞吐以及 raw submit、NativeIO submit/observe 阶段表。Linux io_uring 在对应 pipe backend 实现前不生成伪基线。
