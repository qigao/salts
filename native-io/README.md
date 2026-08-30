# NativeIO

NativeIO 是 TurboUtils 根目录下的原生 I/O 操作层。它只负责把有界操作提交给明确选择的 OS backend，并把终态完成批量交还给调用者；它不拥有 Actor、Reactor、CFlow Graph 或用户 socket。

## 架构决策

原生 backend 留在 CFlow 会让 OS 完成状态依赖 Actor mailbox 和 acknowledge，无法单独测量或复用。放入 Platform 则会把现有 readiness 原语与网络操作、payload 生命周期混成一个职责。独立根模块保持以下单向依赖：

```text
CFlow / Actor / future adapters
              |
              v
         NativeIO operations
              |
              v
      Platform errors and ABI
```

当前公开版本提供 Windows IOCP、Linux epoll/io_uring，以及 64 位 macOS/BSD kqueue driver；均支持 TCP recv/send 和 UDP recv_from/send_to。Linux epoll 与 macOS/BSD kqueue 还支持非阻塞 connected byte pipe；IOCP named pipe 与 io_uring pipe 仍显式返回 `TURBO_ENOTSUP`。工厂只初始化调用方明确选择的 backend，不做隐式 fallback。不满足平台/位宽要求时显式返回 `TURBO_ENOTSUP`。现有 CFlow API 暂不迁移，因此本模块可独立验证和回滚，不改变已有用户行为。

## 数据与状态协议

- 数据单元：一个 `turbo_io_operation` 对应恰好一个 terminal `turbo_io_completion`。
- 事实源：backend 固定 endpoint/request 槽位；上层只持有带 generation 的句柄。
- 所有权：backend 借用 socket；成功 submit 后借用 payload，直到 observe 返回对应 completion。
- 拓扑：一个 backend 由一个 owner 线程调用。模块不创建线程、不加锁、不内置跨线程队列。
- 容量：endpoint、request 和 completion batch 均在 init 时固定；满额返回 `TURBO_ENOBUFS`。
- 顺序：每个 endpoint 的 read lane 与 write lane 分别按 FIFO 向内核发起操作，lane 之间不排序。request handle 与 `user_data` 用于关联；不同 endpoint 的 completion 顺序由内核决定。
- 取消：cancel 只请求取消。IOCP 的 `ERROR_OPERATION_ABORTED`、io_uring 的 `-ECANCELED` 和 readiness 队列中尚未执行的请求进入 CANCELLED；已经完成的请求不会被改写成取消。
- 关闭：`close admission -> cancel/drain -> close native sockets -> release endpoints -> destroy`。
- 等待：`timeout_ms == 0` 为 poll，`UINT32_MAX` 为无限等待，其余值为相对毫秒 deadline；无终态返回 `TURBO_ETIMEDOUT` 且 count 为零。

请求槽位状态机为：

```text
FREE --submit accepted--> PENDING --observe terminal--> FREE(next generation)
                              |
                              +--cancel request--------+
```

每次成功 submit 恰好产生一个可观察终态。submit 原生失败在返回前回滚到 FREE，不产生 completion。destroy 在 admission 未关闭、请求未 drain 或 endpoint 未释放时返回 `TURBO_EBUSY`，并保留所有权供调用者修复。

## 性能边界

初始化之后，submit/observe 不分配内存。endpoint 与 request 都通过预分配 free stack 以 O(1) 获取：

- IOCP：submit 直接调用 `WSARecv`/`WSASend`，observe 直接读取 completion port。
- epoll/kqueue：submit 先以单次非阻塞 syscall 尝试；仅在 would-block 时进入每 endpoint 的 FIFO lane，并由 owner 在 observe 中直接等待 readiness 和继续 syscall。
- io_uring：每个 endpoint 的 read/write lane 各保持至多一个内核 in-flight SQE，其余已接受描述符保留在固定 request 槽位中；observe drain CQ 后推进 lane。ring 由模块映射，但没有 worker、mutex、callback、payload copy 或跨线程 mailbox。

readiness 的 kernel interest 是请求 lane 推导出的镜像，不是第二份业务状态。endpoint/request/terminal storage 和 native event batch 均有固定上限。

## 最小示例

```c
#include <turbo/native_io.h>

turbo_io_backend backend = {0};
turbo_io_backend_config config = {
#if defined(_WIN32)
    TURBO_IO_BACKEND_IOCP,
#elif defined(__linux__)
    TURBO_IO_BACKEND_EPOLL, /* 或 TURBO_IO_BACKEND_IO_URING */
#else
    TURBO_IO_BACKEND_KQUEUE,
#endif
    16u, 64u, 16u};

int status = turbo_io_backend_init(&backend, &config);
if (status != 0)
  return status;

/* Attach an already-created socket, submit operations, then call
   turbo_io_backend_observe() from this same owner thread. Windows sockets must
   have been created for overlapped I/O. */
```

完整可运行的 TCP/UDP loopback 用法位于 `tests/native_io_test.c`。

`native_io_benchmark` 只比较同模型的原生基线：Windows 为 raw IOCP，Linux 分别为 raw epoll 与 raw io_uring，Apple/BSD 为 raw kqueue。输出按 backend 和 TCP/UDP 拆分的 payload 延迟、吞吐及 submit/observe 阶段表。TCP 覆盖 1/4/8/16/32/64 KiB；Windows/Linux UDP 覆盖到 32 KiB，因 IPv4 UDP 单 datagram 无法承载 64 KiB payload 而明确省略该行；kqueue UDP 止于 8 KiB，以符合 macOS 默认单 datagram 上限，不通过应用层拆包改变测试口径。

`native_io_pipe_benchmark` 在当前支持 pipe 的 Linux epoll 与 macOS/BSD kqueue 上比较 raw POSIX pipe 调用和 NativeIO。每个样本执行 256 次单向 transfer，覆盖 1/4/8/16/32/64 KiB；应用 payload 每次只计一次，不把读端和写端重复计算为两倍流量。fixture、buffer、descriptor 与 backend 初始化位于计时区外，输出独立的 p50/p95 延迟、吞吐以及 raw syscall、NativeIO submit/observe 阶段表。Windows 和 Linux io_uring 在对应 pipe backend 实现前不生成伪基线。
