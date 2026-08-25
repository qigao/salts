# CFlow 显式 POSIX poll Backend 设计

## 背景与目标

父追踪 Issue #100 要求先说明 portable `poll` backend 的用途，再决定是否实现。当前
Platform readiness 已有 Linux epoll 和 macOS kqueue backend，CFlow 在其上复用同一个
bounded socket-lane adapter；没有 epoll/kqueue 的 POSIX 系统只能得到
`TURBO_ENOTSUP`。本设计增加一个显式选择的 `poll` backend，服务于小规模 descriptor
集合、兼容性验证和参考语义，不把它包装成可扩展 native backend，也不在其他 backend
初始化失败时自动降级。

关联子 Issue：#103。

## 仓库事实与外部契约

- `platform/src/readiness.c` 已集中实现 registration 容量、one-shot arm、continuation
  rearm、generation、terminal fanout、callback quiescence 和 shutdown 状态机。
- `cflow/src/io_native_readiness.c` 已集中实现 TCP/UDP、accept/connect、read/write lane、
  Actor completion、取消和 socket identity；它不依赖 epoll/kqueue 特有数据。
- epoll/kqueue 只实现 `turbo_readiness_backend_ops`，因此 poll 应作为第三个 Platform
  backend，而不是复制 CFlow adapter。
- POSIX `poll()` 对传入数组逐项检查，`timeout == -1` 可无限等待；listening socket 在
 连接可接受时读就绪，非阻塞 connect 完成时写就绪。regular file 永远读写就绪，因此
  本期 CFlow 仍只声明 socket 能力，不把 regular file readiness 冒充异步文件 I/O。
- POSIX pipe 是 FIFO 控制通道；控制端设为 nonblocking，单字节写入可合并，`EAGAIN`
  表示已有 wake evidence，不是状态丢失。

一手资料：

- [POSIX poll](https://pubs.opengroup.org/onlinepubs/9799919799/functions/poll.html)
- [POSIX pipe/pipe2](https://pubs.opengroup.org/onlinepubs/9799919799/functions/pipe.html)
- [POSIX nonblocking socket I/O](https://pubs.opengroup.org/onlinepubs/000095399/functions/xsh_chap02_10.html)

## 公开接口

Platform 增加显式 backend selector，同时保留现有默认初始化语义：

```c
typedef enum turbo_readiness_backend_kind {
  TURBO_READINESS_BACKEND_EPOLL = 1,
  TURBO_READINESS_BACKEND_KQUEUE,
  TURBO_READINESS_BACKEND_POLL
} turbo_readiness_backend_kind;

bool turbo_readiness_backend_supported(turbo_readiness_backend_kind kind);

int turbo_readiness_reactor_init_kind(
    turbo_readiness_reactor *reactor,
    const turbo_readiness_config *config,
    turbo_readiness_backend_kind kind);
```

`turbo_readiness_reactor_init()` 继续选择当前平台原有默认值：Linux 构建启用 epoll 时选
epoll，macOS 选 kqueue，其他没有默认 backend 的平台返回 `TURBO_ENOTSUP`。显式 kind
不 fallback；未编译的 kind 直接返回 `TURBO_ENOTSUP` 并清空 reactor handle。

CFlow 在现有 enum 末尾追加 `CFLOW_IO_NATIVE_POLL`，保持旧数值稳定。CFlow 的
`backend_supported()` 只报告编译时能力；`backend_init()` 把 epoll/kqueue/poll 精确映射
到 Platform selector。

## 数据与所有权协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 一个 registration record：borrowed fd、registration token、arm token、interest 与 active/armed 状态 |
| 事实源 | backend mutex 下的固定 `records[registration_capacity]`；worker 的 pollfd/token 数组只是每轮快照 |
| 所有权 | caller 始终拥有 fd；Platform 从 register 成功借用到 close 成功；backend 拥有 worker、control pipe 和固定数组 |
| 生命周期 | 快照只在一次 `poll()` 与该轮 dispatch 内有效；每次控制变更通过 token/generation 使旧快照失效 |
| 拓扑 | 多控制线程可调用 Platform API；一个 poll worker 消费快照并串行发起 native dispatch |
| 顺序 | 每个 registration one-shot；CFlow 每 socket 每方向 FIFO 由既有 lane 保证，不承诺跨 descriptor 全局顺序 |
| 容量 | registration、快照和事件处理均固定上限；数据面不 realloc |
| 背压 | Platform 满返回 `TURBO_ENOBUFS`；CFlow request 满保持既有 `TURBO_EBUSY`/Actor failed completion |
| 失败 | 控制 hook 失败必须回滚 record；worker `poll()` 失败一次 terminalize reactor |
| 关闭 | 关闭 admission，terminal fanout，设置 stopping，写 control pipe，join worker，再允许 destroy |
| 观测 | 复用 Platform/CFlow stats；benchmark 单独报告 backend=`poll`，不设跨主机性能阈值 |

## 固定容量与计算

设 Platform registration 容量为 `C`、每轮最多 dispatch 为 `B`：

```text
1 <= C <= UINT32_MAX - 1
1 <= B <= C + 1
poll_snapshot_slots = C + 1        # slot 0 是 control pipe read end
record_bytes = C * sizeof(poll_record)
snapshot_bytes = (C + 1) *
                 (sizeof(struct pollfd) + 2 * sizeof(uint64_t))
```

所有 `C + 1` 和乘法在分配前做 checked arithmetic。CFlow readiness 仍使用
`C = 2 * request_capacity`，对应每个 live socket 最多一个 read lane 与一个 write lane。
`poll()` 每轮构造/扫描 O(C)；`B` 只限制一轮 callback 数，未处理的 level readiness 留到
下一轮，不丢弃也不扩容。

## Poll worker 与控制事务

worker 每轮在 backend mutex 下等待 `controls_pending == 0`，再把 active+armed record
拷贝到 worker-owned 快照，加入 control read fd，然后解锁并调用 `poll(..., -1)`。控制
API 不在锁内执行 pipe I/O；其顺序为：

```text
lock -> validate -> save previous -> stage desired -> ++controls_pending -> unlock
     -> nonblocking wake byte
     -> lock -> commit or restore -> --controls_pending -> broadcast -> unlock
```

Platform 的 control gate 会把这个 hook 触发的 dispatch 串行到 hook commit/rollback 之后；
backend 的 `controls_pending` 又阻止 worker 在 staged 窗口构造新快照。因此 wake 失败可恢复
previous record，成功则提交 desired record，不会暴露半提交状态。control pipe 满返回
`EAGAIN` 时视为成功，因为至少一个未消费 wake 已存在。worker 看见 control fd 后 drain 到
`EAGAIN`，检查 stopping，再重建快照。Linux 使用
`pipe2(O_NONBLOCK | O_CLOEXEC)`；其他 POSIX 使用 `pipe()` 后在 worker 启动前立即用
`fcntl()` 设置 `O_NONBLOCK` 与 `FD_CLOEXEC`，任一步失败都关闭两端并终止初始化。

普通 fd 事件在 dispatch 前再次锁定并核对：record active、registration token、armed、
arm token 全部匹配。只有匹配项才把 `armed` 改为 false 并调用
`turbo_readiness_backend_dispatch_generation()`；旧快照、unarm、close、rearm 与 fd 数值
复用都不能命中新 generation。

## 事件与错误语义

- `POLLIN`/`POLLPRI` -> `READ`；`POLLOUT` -> `WRITE`。
- `POLLERR`/`POLLNVAL` -> `ERROR`；`POLLHUP` -> `HANGUP`。
- worker `poll()` 的 `EINTR` 重试；其他错误以负 errno 调用一次
  `turbo_readiness_backend_fail()`。
- CFlow 只把 generic readiness 当作再次执行 nonblocking socket syscall 的证据；
  terminal syscall/`SO_ERROR` 仍是 operation completion 的事实源。
- poll backend 不改变 caller socket flags；CFlow 继续 duplicate descriptor，并要求 caller
  socket 本身满足 readiness operation 的 nonblocking 前置条件。

## 构建、兼容性与回滚

- Platform 在 POSIX 构建编译 poll；Linux 可同时编译 epoll，macOS 可同时编译 kqueue。
- Windows 只获得 additive enum/function 声明；显式 poll 返回 `TURBO_ENOTSUP`。
- CFlow 旧 backend enum 数值、默认 Platform initializer、错误码和 operation ABI 不变。
- 新公开函数需要 C/C++ header test；消费者需要重新链接，不需要数据迁移。
- 回滚可删除 poll source、selector 的 additive 分支、CFlow enum、测试/CI/doc 条目；既有
  epoll/kqueue/io_uring/IOCP 路径无需迁移。

## 验证范围

1. RED：公开 Platform/CFlow selector 尚不存在时，header/unsupported/native tests 编译失败。
2. Platform poll contract：invalid/full、read/write、one-shot、continuation、unarm、close、
   shutdown terminal、inflight callback join、descriptor reuse 与 stale snapshot。
3. CFlow shared contract：TCP、UDP、accept/connect、bidirectional lanes、queued cancel、
   request-slot reuse、socket forget、caller flag preservation。
4. benchmark：`CFLOW_NETWORK_BACKEND=poll` 精确报告 poll；新增 Linux poll CI 行，使用与
   其他 backend 相同的 bounded workload，只发布观测数据。
5. Windows Release 全量回归；PR CI 在 Linux/macOS 编译并运行 poll，Windows 验证
   unsupported 行为和公开 header。
