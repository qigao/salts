# CFlow Windowed I/O Source Design

## 背景与性能边界

现有 `cflow_source_from_io_actor()` 把一个 downstream value demand 映射为一个
Actor request，并固定使用 request、command、Executor 与 typed result 容量 1。
`cflow_io_source_owner_run_ready(max_steps)` 已经能在一次 driver credit 内批量推进
Actor、Executor 与 acknowledge；真实 IOCP A/B 也表明容量 1 的 Disruptor 命令槽
不是瓶颈。剩余可被批量化的边界是：一次 positive demand 只能准备一个 operation，
因而多个彼此独立的 I/O 无法共享 wake、driver 与 Runtime pump 成本。

本设计只服务独立 I/O 的吞吐场景。它不声称改善必须串行的单次 socket exchange
延迟，也不改变旧构造器的严格一请求语义。生产实现只有在有界 synthetic control
benchmark 相对容量 1 的相同工作量提升至少 30%，且功能、关闭和内存验证通过时才
保留；否则撤回 windowed 生产 API 与实现。

## 决策

1. `cflow_resume_ctx` 增加只读 `downstream_demand` 快照。Runtime 在每次调用
   Source 或 continuation `resume()` 前写入当前尚未满足的 downstream-value demand。
   零表示直接调用者没有提供快照；所有既有 Source 继续忽略该字段。
2. 保留 `cflow_source_from_io_actor()` 的公开行为和容量 1；它调用同一内部实现并
   传入 window capacity 1。
3. 新增显式 opt-in 构造器：

   ```c
   enum { CFLOW_IO_SOURCE_MAX_WINDOW = 64u };

   int cflow_source_from_io_actor_windowed(
       cflow_source *out,
       cflow_io_source_owner *owner,
       const cflow_io_source_config *config,
       size_t window_capacity);
   ```

   `window_capacity` 必须在 `[1, CFLOW_IO_SOURCE_MAX_WINDOW]`；1 与旧构造器语义
   相同。64 与 Runtime pump quantum 对齐，限制单次 resume 的 prepare 工作量和
   线性扫描成本。
4. 新增只读 window 观测：

   ```c
   typedef struct cflow_io_source_window_stats {
       size_t capacity;
       size_t occupied;
       size_t demand_reserved;
       size_t results_ready;
       size_t peak_occupied;
   } cflow_io_source_window_stats;

   bool cflow_io_source_owner_get_window_stats(
       const cflow_io_source_owner *owner,
       cflow_io_source_window_stats *out);
   ```

   旧容量 1 owner 同样可查询，避免维护第二套统计事实源。

这是一次 CFlow Runtime v2 ABI 扩展：源码中的零初始化与只初始化 scheduler 的
位置初始化保持兼容，但独立编译的 `cflow_resume_ctx` 使用者必须与新库一起重建。
不提供布局探测或旧 ABI fallback。

## Demand 语义

`downstream_demand` 是调用 `resume()` 之前的只读饱和快照，不是 Source-item
demand。Windowed Source 计算：

```text
effective_demand = ctx.downstream_demand != 0 ? ctx.downstream_demand : 1
target = min(effective_demand, window_capacity)
new_operations = target - demand_reserved
```

`demand_reserved` 只统计已接受但尚未向 Runtime 发出 value 的 operation。发出一个
value 后由 Runtime 消耗一个 demand，下一次 resume 会收到更新后的快照。filter
可能丢弃 Source item；此时 demand 不下降，下一次 resume 可以继续补充 window。
Source 不保存、递增或递减 Runtime demand，因此 Runtime 仍是唯一事实源。

一次 resume 最多调用 `prepare` `window_capacity` 次。零快照只允许单步准备，保证
直接调用旧 Source 的测试和自定义 Runtime 不会意外预取。

## 数据、所有权与容量协议

Window 中每个固定 entry 包含：

- 一个稳定 lease id，值为 `entry_index + 1`；
- Actor request id 与 submission/completion/ack 状态；
- 一个预初始化的 `cflow_value_slot`；
- completion-delivery sequence、`cflow_read_status` 与稳定错误指针。

| 项目 | 契约 |
|---|---|
| 数据单元 | 一个 move-only operation 对应零或一个 typed Source value |
| 事实源 | Actor slot 是 I/O 生命周期事实源；entry 只保存 demand reservation 与已编码结果 |
| operation 所有权 | `prepare` 成功后由 Adapter 暂持；Actor ACCEPTED 后转移；任何拒绝由 Adapter release 一次 |
| result 所有权 | encoder 写入 entry 独占 slot；Source resume 复制到 Runtime 空 storage 后 reset |
| 容量 | Actor request/command、manual Executor、entry/result 都使用同一 `window_capacity` |
| 背压 | 满时停止 prepare；不扩容、不重试、不丢弃、不覆盖 |
| payload | 仍由 operation/backend 约束；Adapter 不复制 caller payload |

构造期使用 checked arithmetic 验证 entry 数组以及每个 typed slot 的
`type->size + type->align - 1`。运行期不分配 Adapter entry、Actor slot、Executor job
或 result storage。

常驻预算至少为：

```text
window_capacity * sizeof(adapter_entry)
+ window_capacity * aligned_typed_value_bytes
+ Actor request slots
+ round_up_pow2(window_capacity) * Actor command entry bytes
+ window_capacity * manual Executor job bytes
+ retained operation/backend payload budget
```

最后一项仍由调用方配置和观测；window capacity 不能替代 payload 内存上限。

## 顺序与终态

Windowed Source 明确定义为 **authoritative completion delivery order**，不是 operation
preparation order。Actor 的 manual Executor 是单 consumer；每次 encoder 完成时在
Adapter gate 下分配单调 delivery sequence。Source 总是取最小 ready sequence。

- `VALUE`：按 delivery sequence 发出。
- `VALUE_AND_DONE`：先发出该 value，再终止；关闭 Actor admission，取消并 drain
  尚未 delivery 的 request，丢弃该终态之后产生的结果。
- `DONE`：在该 delivery sequence 终止，不发值，并执行相同 drain。
- `ERROR`、非法 encoder 状态：在该 delivery sequence 失败，关闭并 drain 其余请求。
- `prepare DONE`：停止新增 operation；已经接受的结果按 completion order 发出，
  `demand_reserved == 0` 后 Source DONE。
- `prepare ERROR` 或无效 operation：立即 Source ERROR，关闭 Actor，并 drain 已接受请求。

一旦记录 terminal delivery sequence，后续 completion 只进入 Actor delivered/ack drain，
不再调用 encoder，也不发布 value。已经拥有更小 sequence 的 ready value 先发出；因为
sequence 在 delivery 时分配，不存在“较小但尚未 delivery”的 entry。

## 并发状态机

拓扑精确为：

- 一个 Runtime pump consumer 调用 Source resume/prepare；
- `cflow_io_actor_try_submit()` 保持 MPSC-safe，但 Adapter 自身只有上述单 producer；
- backend completion 可从任意线程进入 Actor；
- manual Executor 可由一个 owner driver 消费，encoder 在该 driver 上串行执行；
- owner driver 仍是 single-driver，并发或重入返回 `SALTS_EBUSY`；
- Source waker 与外部 drive callback 始终在 Adapter gate 外执行。

每个 entry 的公开可观察状态迁移为：

```text
FREE
  -> PREPARING
  -> SUBMITTED
  -> ENCODING
  -> RESULT_READY
  -> RESULT_EMITTED
  -> ACKNOWLEDGED
  -> FREE

RESULT_READY -> ACKNOWLEDGED -> RESULT_EMITTED -> FREE
任何已接受状态 -> CLOSE_DRAIN -> ACKNOWLEDGED -> FREE
```

Result emit 与 Actor acknowledge 可按任一顺序发生；只有两者都完成或该 result 被终态
discard 后 entry 才回到 FREE。lease id 在 FREE 前不得复用。所有成功 Actor claim/
submit 都严格走 completion、delivery、acknowledge、operation release 终态。

## 唤醒、公平性与复杂度

现有 lossless coalesced drive generation 保留。一次 `owner_run_ready(max_steps)` 仍只
计算实际 Actor transition、Executor task 和 acknowledge 数，不把 Source prepare 数
混入 `progressed`。

Source waker 在以下边沿被取出并锁外调用：首个 result ready、entry 在 emit 后完成
ack 而释放 window、prepare DONE 可终止、以及终态错误。多个 result ready 可合并为
一次 wake；Runtime pump 在自己的 64-unit quantum 内逐个 resume/value。

Adapter 查找最早 ready entry 和可 acknowledge entry 的最坏时间为 O(window_capacity)，
空间为 O(window_capacity * typed_value_size)。`CFLOW_IO_SOURCE_MAX_WINDOW == 64` 把
单次扫描和 prepare 工作限制在 Runtime quantum 同一数量级。

## 关闭与错误

关闭顺序保持旧协议：停止 Source admission，Actor close，等待 authoritative native
completion，Executor delivery，Adapter discard/emit 状态结算，Actor acknowledge，
Source destroy，最后 owner close。Owner close 在任一 occupied entry、callback credit、
driver credit、live Source 或非 quiescent Actor 存在时返回 `SALTS_EBUSY`。

新增错误边界：

- window 为 0 或大于 64：`SALTS_EINVAL`；
- checked allocation 失败：`SALTS_ENOMEM`；
- Actor FULL/LEASE/command FULL 在内部容量本应可接纳时：稳定 Source protocol error，
  不 fallback；
- completion lease 不指向对应 occupied entry、request id 不一致、重复 result 或非法
  状态迁移：稳定 Source protocol error，并保留 Actor stale-completion 统计。

## 兼容性、迁移与回滚

- 旧 constructor、默认容量、completion order（容量 1 时等同 preparation order）、
  callback 线程、错误与关闭语义保持不变。
- 新 windowed constructor 与 stats 是 additive API；只有 `cflow_resume_ctx` 布局要求
  消费者重建。
- README 必须把 windowed Source 标为 opt-in independent-I/O throughput 路径，不能
  替代需要 preparation-order 的流程。
- 若性能 gate、功能测试、ASan 或平台测试失败，撤回 `downstream_demand`、windowed
  API/实现/benchmark；旧容量 1 Adapter 保持当前 HEAD 行为，不保留隐藏 fallback。

## 验证矩阵

1. Runtime demand 快照：request 4 时 resume 依次看到 4、3、2、1；filter drop 后快照
   不下降；零 demand 不调用 resume。
2. 容量：0、1、2、64、65；Actor/Executor/result capacity 一致；构造失败保持 zero state。
3. Window fill：request N 在首次 WAIT 前接受 `min(N, capacity)`；没有 demand 不 prepare。
4. Completion：乱序 delivery 按 delivery order 发出；每个 request release/ack 一次。
5. 终态：prepare DONE/ERROR，encoder VALUE_AND_DONE/DONE/ERROR，终态后的 completion
   只 drain 不 emit。
6. 背压：window 满不 prepare；emit 但 ack 未完成不复用 lease；ack 后继续补窗。
7. 竞态：completion-before-submit-return、completion-before-arm、driver tail window、并发
   completion、reentrant drive、cancel/close drain。
8. 性能：容量 1、8、32 的相同 synthetic operation/value 数；报告 ns/value、ops/s、
   wake 次数、driver calls、peak occupied、拒绝与错误。8 或 32 至少一个相对容量 1
   提升 30%，且延迟不回退超过 10%、内存不超过按容量计算的 20% 额外偏差。
9. 回归：Source、Actor、Runtime、readiness、native/file、C++ header、全 CFlow Release，
   可用时补充 Windows ASan 与 install consumer。
