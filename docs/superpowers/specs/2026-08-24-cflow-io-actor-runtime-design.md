# CFlow IO Actor Runtime Design

## 背景与边界

已合并的 Lean 模型把跨平台异步 IO 拆成 `BoundedMpsc`、`Executor` 和
`Actor` 三个状态机。本实现只完成平台无关的 C runtime refinement：请求所有权、
有界命令邮箱、backend 提交/取消、完成信用、Executor 投递、确认释放和关闭协议。

Linux epoll、未来的 kqueue、IOCP 与 io_uring 都位于 backend strategy 之后。本期
不新增 native backend，也不把 completion backend 伪装成 readiness backend。现有
`turbo_readiness_reactor` 和 `cflow_source_from_reactor_registration` 保持不变。

## 架构决策

新增公开 opaque handle `cflow_io_actor`，归属 `TurboUtils::CFlow`：

```text
concurrent callers
  | try_submit / try_cancel
  v
bounded Disruptor mailbox (logical MPSC, one driver consumer)
  | run_one / run_ready
  v
cflow_io_backend_ops (Reactor or Proactor strategy)
  | cflow_io_actor_complete (reserved request slot)
  v
existing cflow_executor
  | completion callback
  v
acknowledge -> release operation ownership
```

不新建线程池或 executor。调用方注入现有 `cflow_executor`；手动、串行和并发能力
沿用其既有行为。Actor driver 是显式驱动的单消费者，便于嵌入 reactor thread、
dedicated actor thread 或测试循环。

## 数据、事实源与所有权

- 数据单元：命令 `{submit|cancel, request_id}`、固定 request slot、完成值。
- 请求表是 operation、lease、phase 和 completion 的唯一事实源。
- `cflow_io_operation` 是 move-only token。`try_submit` 成功会清空调用方 token；
  失败不修改 token。其 `release` callback 在成功 acknowledge 后恰好调用一次。
- `lease_id` 与 `request_id` 都必须非零。活跃请求之间 lease 唯一；request ID
  单调递增且不回绕。
- backend 和 executor 只借用 Actor、operation payload 和 callback 配置，直至
  Actor quiescent destroy。
- Executor task 只携带稳定 request slot；用户 completion callback 返回后，task
  提交 `DELIVERED` 且不再接触 slot。若同时发出 wake，task 保留 callback credit
  直至 wake 返回；最后递减 credit 并解锁后不再接触 Actor。slot 只会在调用方
  确认后复用，Actor 只会在全部 injected callback 返回后 quiescent。

## 并发拓扑与线性化

- 命令发布：MPSC；一个 Actor driver 消费，FIFO。READY 请求按单调 request ID
  选择，因此 request slot 乱序释放和复用不会改变 backend submission 顺序。
- request 状态由 Actor gate 串行化。backend completion 可从任意 backend thread
  进入，但只写该请求预留的 completion credit。
- Executor delivery callback 可并发执行；只在 gate 下完成
  `dispatchQueued -> dispatchRunning -> delivered`，用户 callback 在锁外执行。
- backend submit/cancel、用户 completion/release、wake callback 和 Executor post
  均不在 Actor gate 内调用。
- wake 是可合并的边沿通知。driver 活跃期间产生的通知记入 pending handshake，
  driver 清除 active 时再次发出，避免退出窗口 lost wake；调用方仍应把 wake 当作
  调度提示而不是每次状态迁移一条消息。
- submit 的线性化点是 request slot 保留和命令 publish 在同一 gate 临界区内完成。
- backend completion 的线性化点是 pending request slot 首次写入 terminal result。

## 容量、内存与背压

配置包含正数 `request_capacity` 和 `command_capacity`：

```text
active_requests <= request_capacity
queued_commands <= command_capacity
```

init 一次性分配 request table 和 Disruptor ring。ring 物理容量向上取 2 的幂，
逻辑 command depth 仍严格受配置值约束。数据路径不扩容。

- request 满、command 满、lease 冲突和 closed 都是可区分 submit 结果；失败保持
  operation token 和 Actor 请求状态不变。
- backend completion 使用已保留 request slot，不再竞争 command 容量。
- Executor full/closed 时 completion 留在 slot 中，后续 driver 可重试；没有
  fallback queue，也不丢弃结果。

`FULL` 可在 Executor 释放容量后重试；`CLOSED` 对同一 Executor 不可恢复，属于关闭
顺序错误。Executor 必须保持 OPEN，直到 Actor 所有 completion 已投递、确认且
`cflow_io_actor_destroy` 成功；随后才 shutdown/drain Executor。反向关闭会增加
`executor_rejected_closed` 并保持 Actor busy，不会静默释放 operation。

## 请求状态机

```text
ADMITTED -> READY -> BACKEND_PENDING
ADMITTED/READY + cancel -> COMPLETED(CANCELLED)
BACKEND_PENDING + cancel -> BACKEND_PENDING(cancel_requested)
BACKEND_PENDING + backend result -> COMPLETED(result)
COMPLETED + executor accepts -> DISPATCH_QUEUED
DISPATCH_QUEUED + callback starts -> DISPATCH_RUNNING
DISPATCH_RUNNING + callback returns -> DELIVERED
DELIVERED + acknowledge -> RELEASING -> FREE
```

backend cancel 的返回值不是 terminal completion。错误只进入统计，request 仍等待
authoritative native completion。backend submit 同步失败转换为 `FAILED(error)`；若
backend 已同步回调完成，后续 submit 返回值不得覆盖已记录 completion。

## 关闭与失败语义

Actor lifecycle 为 `RUNNING -> CLOSING`。close 关闭命令 admission，取消尚未提交
backend 的请求，并为 pending 请求设置 cancel request。关闭前已经发布的命令继续
FIFO drain。

quiescence 要求 command 为空、无活跃 request、无 Actor driver action、无 Actor
delivery callback。未确认的 delivered request、尚未返回的 backend operation 或
Executor backpressure 都会使 destroy 返回 `TURBO_EBUSY`，不得强制释放 lease。

completion callback 可由 concurrent Executor 对不同请求并发调用；wake 也可从
producer、backend completion、delivery 或 acknowledge 线程并发/重入。所有 context
由调用方同步，callback 参数只在调用期间借用。callback credit 覆盖 completion 与
wake 的完整执行期，因此 callback 内同步 destroy 返回 `TURBO_EBUSY`；应在返回后
另行调度销毁。

## 候选方案与取舍

1. 复用 `turbo_threadpool` 作为 command queue：拒绝。worker-pool 是 MPMC work
   stealing，不能表达单 Actor owner 和 FIFO command observation。
2. 手写 mutex FIFO：可行但重复已有 bounded ring。本实现选 Disruptor broadcast
   mode + 一个 consumer，并加 logical capacity/close gate。
3. completion 也进入 command queue：拒绝。command 满时会丢失 terminal result，
   违反 completion credit 证明。
4. 每个 OS 暴露不同 Actor API：拒绝。backend strategy 隔离 Reactor/Proactor
   差异，公共 ownership/backpressure/shutdown 语义保持一致。

## 兼容性、迁移与回滚

- 只新增 `cflow/io_actor.h`、实现和测试；不修改既有 vtable、错误码、Graph IR、
  配置或序列化格式。
- CFlow 已私有链接 Concurrency，因此 Disruptor 不扩散到公开 ABI。
- 用户可先用 fake backend 验证，再逐个接入 readiness/completion adapter。
- 回滚可删除新增文件和 CMake 条目，无数据迁移；现有 executor、Actor 和 readiness
  行为不受影响。

## 验证契约

- header/C++ compatibility；
- request/command 满、lease 冲突、closed 的事务性拒绝；
- MPSC submit 的唯一 ID、FIFO backend observation；
- cancel-before-submit 与 cancel-after-native-submit；
- stale/duplicate backend completion；
- Executor full 保留 completion 并可重试；
- callback start/finish 后才允许 acknowledge；
- close drain、unacknowledged busy destroy 和 terminal conservation；
- 现有 executor、Actor、readiness、Disruptor 相邻回归；
- Windows Release 与 dev/ASan preset，Lean aggregate build/test。
