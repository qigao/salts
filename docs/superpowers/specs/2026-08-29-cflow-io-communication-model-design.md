# CFlow I/O 通信模型设计

## 背景

现有 `cflow_io_actor` 同时维护 Actor 请求状态、消费命令、调用 native backend、
接收 completion 并投递 Executor。该结构保证了请求所有权、取消、关闭和 ack，
但把 Actor 模型与 readiness/completion I/O 执行模型绑定在同一状态机中。

本设计把“Actor 与 I/O driver 的通信协议”从具体执行模型中拆出，同时保持
`cflow_io_actor_init()`、`cflow_io_actor_try_submit()`、completion callback、ack 和
关闭行为兼容。第一阶段不改变现有 backend 的线程或调用时序。

## 决策

### 分层

```text
Actor / CFlow Source
        |
        | typed command/event contract
        v
readiness driver | completion driver | blocking driver
        |
        v
epoll/kqueue/poll | IOCP/io_uring | bounded blocking workers
```

Actor 与三类 driver 是独立模型：

- Actor 只拥有 mailbox、领域状态、请求关联、取消意图和消息处理顺序。
- readiness driver 拥有注册、interest mask、read/write lane 和 rearm。
- completion driver 拥有 native operation slot、in-flight 生命周期和 completion batch。
- blocking driver 拥有有界 job admission、worker execution 和 completion publication。

公共部分仅包含稳定 identity、终态结果、容量/关闭语义，不包含 epoll、kqueue、
IOCP、SQE 或 OVERLAPPED 状态。

### C 数据契约

新增 `<cflow/io_communication.h>`，提供以下互不混用的数据单元：

- `cflow_io_identity`：`endpoint_id + request_id + generation`。
- `cflow_io_endpoint_identity` 与 `cflow_io_control_command`：公共 endpoint close
  控制面，不借用 request/operation ID。
- `cflow_io_readiness_command/event`：watch/modify/unwatch 与 ready/error/closed。
- `cflow_io_completion_command/event`：submit/cancel 与
  completed/eof/cancelled/failed。
- `cflow_io_blocking_command/event`：execute/cancel 与
  completed/eof/cancelled/failed。

每类结构都有 fail-fast validation 函数。它们验证协议字段，不验证 native handle、
buffer slot 是否存在；后者属于具体 driver 的事实源。

### Completion callback adapter

`cflow_io_native_backend_communication_model()` 是 native backend kind 到通信模型的
唯一映射：epoll/kqueue/poll 属于 readiness，IOCP/io_uring 属于 completion。
映射与平台编译可用性无关；可用性仍由 `cflow_io_native_backend_supported()` 独立回答。

新增内部 `cflow_io_driver` bridge。`cflow_io_actor_init()` 把现有
`cflow_io_backend_ops` 明确建模为 completion callback driver：其 submit/cancel 请求
最终只能产生一个 terminal completion。Actor 实现只调用 driver，不再直接保存或
调用 public backend vtable。该 adapter 保留原来的同步调用、错误映射和 callback
reentry 行为，因此公开行为不变。

bridge 同时提供仅在 CFlow 实现内部可见的 kind-tagged adapter 初始化入口，使
readiness、completion、blocking adapter 可分别注入自己的 vtable 与事实源；当前
公开 Actor 配置在 Actor 边界走 completion callback adapter，避免改变初始化接口或
callback 时序。底层实现即使使用 readiness，向 Actor 暴露的仍是 terminal
completion，而不是 readiness 状态镜像。

后续迁移时：

- readiness backend 将在其 Reactor 边界使用 readiness command/event channel；
- IOCP/io_uring 将换成 completion channel；
- blocking socket 将使用独立 blocking channel，禁止作为失败 fallback。

## 数据与生命周期协议

### 数据单元与事实源

- identity 是跨队列关联键；`generation` 防止 slot/handle 复用后的陈旧事件。
- native operation slot 是 buffer、native context 和 partial offset 的唯一事实源。
- command/event 只携带稳定 ID、长度、状态和 flags，不携带跨线程裸 buffer 指针。
- Actor 请求状态与 driver native phase 分属不同事实源，不做双向镜像。

### 所有权与生命周期

```text
slot: FREE -> CLAIMED -> PUBLISHED -> OBSERVED -> RELEASED -> FREE
```

- 成功发布 command 后，producer 不再修改对应 descriptor。
- terminal event 被 Actor 观察并 ack 前，driver 不复用 operation/buffer slot。
- borrowed data view 不得越过 event ack、slot release 或 coroutine suspension。
- 每个成功 admission 恰好对应一个 terminal event；取消意图不是 terminal evidence。

### 线程拓扑

- readiness：Actor 侧可有多个 command producer；Reactor 是单一 command consumer；
  每个 connection 的 RX/TX data ring 是固定 SPSC。
- completion：command 是 MPSC；若 native backend 有多个 completion worker，event
  publication 是 MPSC；Actor endpoint 是单一 event consumer。
- blocking：command 是 MPSC；bounded worker pool 竞争消费；completion 是 MPSC，
  Actor endpoint 单消费。
- lifecycle 控制面要求 quiescent；destroy 不与 publish/observe 并发。

### 容量、背压与错误

- queue、operation slot、buffer slot 和 worker job 都有独立硬上限。
- `FULL`、`CLOSED`、`INVALID_ARGUMENT`、`NOT_FOUND`、`STALE` 必须可区分。
- 满额不丢弃、不覆盖、不扩成无界容器；调用方保留未成功转移的所有权。
- backend 初始化失败不得自动切换 blocking driver。

### 关闭

```text
OPEN -> STOP_ADMISSION -> CANCEL/UNREGISTER -> DRAIN_EVENTS -> QUIESCENT -> DESTROYED
```

已接受 operation 必须完成或产生 cancelled/failed terminal event。关闭只禁止新
admission，不清空已发布 command/event。driver 和 Actor 分别达到 quiescent 后才能
释放共享 slot storage。

## 平台映射

### epoll/kqueue/poll

readiness driver 消费 watch/modify/unwatch，拥有 fd 注册和 interest；ready 后在 driver
线程执行 recv/send，直到 `EAGAIN` 或 batch budget，然后发布 data-ready/write-drained。

### IOCP/io_uring

completion driver 消费 submit/cancel，持有 OVERLAPPED/SQE 与 buffer slot，批量收割
完成并发布 terminal event。上层不模拟 readiness phase。

### blocking

blocking driver 把 execute command 投递到有界 worker pool，worker 执行阻塞 syscall
并发布 terminal event。它是显式选择的第三种模型，不是 readiness/completion fallback。

具体 C 端 adapter 复用 `cflow_executor` 的 worker 实现，并遵守以下协议：

- 数据单元是调用方拥有的 `cflow_io_blocking_job` 描述符；Actor 成功接纳后通过
  `cflow_io_operation` 持有其生命周期，driver 只在执行期间借用描述符。
- Actor request table 是 request identity、operation 所有权和 terminal delivery 的唯一
  事实源；driver 固定 slot table 只记录 queued/running job，不复制业务状态。
- driver 拥有 worker Executor、固定 slot table 和同步锁；slot 总数与 Executor queue
  capacity 都在初始化时确定，运行中不扩容。Actor request capacity 仍是整个调用链的
  总在途上限。
- submit 成功后，Executor 对 task descriptor 的 `run` 或 `cancel` 恰好调用一次，随后
  `finalize` 恰好调用一次；两条路径都必须向 Actor 发布唯一 terminal completion，
  `finalize` 才释放 driver slot。
- queued job 收到 cancel 后不进入用户回调并产生 `CANCELLED`；running job 的 cancel
  仅记录意图，不能跨平台强制中断已经进入的阻塞 syscall，实际 syscall 结果仍是唯一
  terminal 事实。需要可中断 I/O 时应选择 readiness/completion driver。
- driver close 停止接纳并以 `CANCEL_PENDING` 关闭其 Executor；调用顺序是 Actor close、
  driver close、持续 drive/deliver/ack、Actor destroy、driver destroy。driver destroy 在
  slot 或 worker 尚未 quiescent 时返回 `TURBO_EBUSY`，不会隐式等待或释放借用对象。
- driver 初始化、Executor admission 或 terminal publication 失败均原样暴露为错误或
  统计，不切换到其他 I/O 模型。

## 形式化模型

Lean 模型拆成：

- `IO/Communication.lean`：公共 identity、terminal event 和抽象 Contract。
- `IO/ReadinessDriver.lean`：watch/ready 状态与公共投影。
- `IO/CompletionDriver.lean`：submit/inflight/completed 状态与公共投影。
- `IO/BlockingDriver.lean`：queued/running/completed 状态与公共投影。
- `Proofs/IOCommunication.lean`：三个模型的 submit、complete、close refinement，
  identity 稳定、重复 terminal 拒绝、关闭拒绝新 command、pending 容量不变式。

证明不得使用 `sorry`、`admit` 或新增 `axiom`。证明范围是有限抽象状态机的 safety
与 refinement；不声称证明 OS kernel、C 原子内存序或 scheduler fairness。

## 兼容性、迁移与回滚

- 第一阶段只新增公开类型/validation API，并用内部 bridge 重构 Actor；现有结构大小、
  函数签名、错误码、callback 时序和 backend contract 不变。
- 后续每个 native backend 独立迁移；Actor completion adapter 只负责既有公开 callback
  契约，不作为 readiness/completion/blocking 初始化失败时的 fallback。
- 若某 backend 回归，可仅回退该 backend 的 channel adapter，公共通信契约和其他 driver
  不受影响。

## 验证

- TinyTest：结构 validation、模型不混用、completion callback adapter 调用与错误传播。
- 现有 `cflow_io_actor_test`、`cflow_io_source_test`、`cflow_io_native_test` 全部保持通过。
- Lean focused test、`lake build`、`lake test` 与 proof-placeholder 扫描。
- `git diff --check`，随后才运行网络 benchmark；语义测试与性能结论相互独立。
- 网络性能只按同轮、同协议、同 payload、同 window 的 Direct 结果计算 Actor 与
  Source 的延迟、吞吐、尾延迟和 CPU 比率。阶段 timing 只报告各自的绝对内部耗时，
  不作为模型间性能参照。
