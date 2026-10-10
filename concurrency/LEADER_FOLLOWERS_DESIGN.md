# Leader/Followers CPU 调度设计

状态：已实现 opt-in Concurrency CPU 后端、正式生命周期测试和小基准；
FlowMQ 应用结果通道仍是消费侧设计，尚未接入其运行时。本文以 `v2.3.0-rc.5` 的源码
`d04b3c909a5f1ac49883f36dcaa7b57de71627cb` 为基线。用户选择的消费场景是
FlowMQ 可直接使用的独立 CPU 任务调度器。本文不属于 rc.5 发布内容。

## 目标与现有证据

为应用层独立 CPU 工作提供显式创建、容量固定、可关闭的 Leader/Followers（LF）
线程池。首个消费场景是 FlowMQ 应用对完整消息执行校验、业务转换或其他独立计算，
然后将结果交回原 I/O lane。具体业务 callback 由消费工程提供。

现有事实：

- [`Salts::Concurrency`](CMakeLists.txt) 已导出独立线程池，且只依赖
  `Salts::Platform`，不需要 CFlow、CNet 或新的外部库。
- [`thread_pool.h`](include/salts/thread_pool.h) 已定义复制 task descriptor、
  run/cancel/finalize、FULL、shutdown policy 和同池 callback 禁止同步等待的协议。
- [`thread_pool.c`](src/thread_pool.c) 使用有界 Disruptor，复制 descriptor 后释放
  queue slot，再在锁外执行 callback。现有竞争消费后端没有显式 leader 身份。
- [`LF benchmark`](../cflow/benchmarks/cflow_ace_throughput_benchmark.c) 证明了
  处理前交接、合并唤醒和 batch 1/32 的测试局部协议。它复制完整 payload，不能证明
  本实现的小 descriptor + borrowed payload 后端有相同收益。
- [平台 LF 测试](../platform/tests/platform_ace_leader_followers_test.c) 和
  [CMeta 契约测试](../cmeta/tests/cmeta_ace_leader_followers_contract_cases.h)
  是原型证据；[覆盖清单](../cmeta/ACE_PATTERN_COVERAGE.md) 仍标记 LF 未生产资格化。
- FlowMQ 当前 `flowmq/include/flowmq_owner.h` 明确规定固定线程驱动 owner，
  `flowmq_owner_poll()` 没有跨线程 application-command wakeup 契约。
  `docs/FLOWMQ_LANE_SCALING.md` 的现行生产选择是固定 lane 直接 send/recv/progress。

本方案在应用计算边界组合 Half-Sync/Half-Async 和 LF。LF worker 不操作 FlowMQ
socket、CNet client/listener、NativeIO request、TLS 状态或协议状态机。

## 模块选择与兼容性

选择在 `Salts::Concurrency` 的现有 opaque `cmeta_threadpool_t` 内增加可选 LF
后端。复用 task descriptor、终态 callback、错误码和 Disruptor，而不是导出 benchmark
中的 CFlow mailbox 调度器。

| 候选 | 判断 |
| --- | --- |
| 保持现有普通线程池 | 必须作为基线和默认行为保留；如果 LF 无实际优势，继续使用它 |
| Concurrency 可选 LF 后端 | 选用；应用可单独链接，复用线程池契约，依赖方向稳定 |
| 新的 CFlow executor | 留给需要 CFlow 的消费方；不作为 FlowMQ 直接使用的前提 |
| 多个 LF worker 轮流推进同一 CNet owner | 不采用；违反固定 progress owner 和连接亲和契约 |
| 另建 LF 库、队列或通用 scheduler 框架 | 现有 Concurrency/Platform 足够，不增加平行框架 |

旧 `cmeta_threadpool_config_t` 和 stats 布局保持不变，旧 create 函数继续创建原后端。
增加独立的新配置与创建入口，避免老调用方传入较短 struct 时发生 ABI 越界读取。
新入口见当前源码 [`thread_pool.h`](include/salts/thread_pool.h)，已发布的 rc.5 SDK 不含它：

```c
typedef struct cmeta_threadpool_lf_config {
    size_t struct_size;
    uint32_t version;          /* 第一版严格为 1 */
    int num_threads;           /* > 0；不隐式选择 CPU 数 */
    size_t queue_capacity;    /* > 0；排队 descriptor 上限 */
    size_t max_batch;         /* 1..32；应用显式选择 */
} cmeta_threadpool_lf_config_t;

int cmeta_threadpool_create_leader_followers(
    const cmeta_threadpool_lf_config_t *config,
    cmeta_threadpool_t **out_pool);
```

调用方将 `*out_pool` 初始化为 NULL。非法配置、版本、大小或乘加溢出返回
`SALTS_EINVAL`，资源不足返回 `SALTS_ENOMEM`；失败保持输出 NULL。
初始化完整成功才发布 handle，部分线程创建失败需关闭、唤醒并 join 已创建线程后回滚。
成功后使用已有 try_submit_task、shutdown_with_policy、wait_status 和 destroy。
公开 ABI 检查保留旧 config/stats 的布局与旧创建行为；新 config/stats 严格校验
struct_size/version，失败不改输出，跨版本不能混用头文件与静态库。

## 线程、队列与事实源

`P` 个并发 producer，`W` 个固定 worker；逻辑拓扑为 MPSC 接纳和轮换的单一 dequeue
owner。handler 可以并行；FIFO 只保证 descriptor 的领取顺序，不保证 handler 开始或
完成顺序。需要协议顺序的计算留在 lane，或使用显式有界结果排序窗口。

LF monitor mutex 保护生命周期、唯一 leader ID、worker 状态、queue depth、任务状态
和结算计数。Disruptor 存储 authoritative queued descriptor；depth 是随成功 publish /
claim 在同一临界区更新的派生计数，不用并发 size/empty 查询决定正确性。
LF 后端不再同时维护另一套独立 park/dispatch 谓词。

worker 状态为 `ELIGIBLE / WAITING / HANDLING / EXITED`；WAITING 仍可被选为 leader。
任何时刻 leader 是一个 eligible worker 或 NONE，HANDLING worker 不能同时是 leader。
所有 worker 都在忙时允许 NONE，最先完成者重新进入 eligible 集合并执行选举。

Disruptor 的 producer claim、写 descriptor、publish 都在 monitor 内完成，
避免预留槽位与实际发布之间留下不可见缺口。逻辑容量 Q 与实际 ring 容量
`next_power_of_two(Q)` 分开，前者严格决定 FULL。成功 claim 后必须 publish 一次。
消费者将 descriptor 复制到 worker 预分配 scratch 后按领取顺序 release slot，
后续 callback 只访问 scratch/arg，绝不持有 slot view。

## 接纳与 leader 交接

非阻塞接纳的线性化点是 monitor 内完成 descriptor publish 和 accepted 计数提交。
shutdown 使用同一 monitor 关闭接纳，因此竞争提交只能完整接受或完整拒绝。

```text
try_submit(task):
    校验 task.run；加锁
    若非 OPEN，返回 ESHUTDOWN
    若 queued == Q，返回 ENOBUFS
    claim -> 复制 descriptor -> publish
    accepted++，queued++
    若没有 leader，选 eligible worker
    若从空变为非空且 leader 正在等待，signal 该 leader
    解锁；返回 OK

worker:
    加锁；将自己置为 eligible；必要时选举
    循环检查 lifecycle、leader == self、queued > 0
    谓词不满足时发布 waiting，再执行 cond_wait 原子解锁等待
    leader 按 FIFO 领取最多 B 条，复制后释放 ring slots
    queued -= n，claimed += n；自己进入 HANDLING
    清除自己的 leader 身份，选出下一位 eligible worker
    有剩余工作且新 leader 正在等待时 signal
    解锁；逐条执行下面的任务终态协议
    加锁；重新 eligible；必要时选举；循环
```

选举采用按上次 leader 后一个 ID 开始的 round-robin，复杂度 O(W)。领取 O(B)，
复制成本 O(B * sizeof(task descriptor))；handler 成本不在 monitor 内。
每 worker 一条 condition，普通接纳只唤醒选定 leader，关闭才 broadcast。
在同一 mutex 下发布 waiting 并循环验证谓词，覆盖 signal-before-wait、伪唤醒、
空队列和最后一批不足 B 的情况。不得等凑满 batch，也不以轮询 sleep 代替 LF 等待。

batch=1/32 已有生命周期测试和基准；配置接受 1..32，batch=4 用于确定性的
私有批次取消测试。batch=8 的专项性能资格化仍待开展。
B 只是每次最多领取的数量，不表示一次处理所有任务，也不承诺优于小 batch。
批量领取可能扩大延迟和不公平度，应用长任务应优先用 B=1。

## 所有权、终态与取消

复用已有 `cmeta_threadpool_task_t`：复制的是函数/arg descriptor，不是 arg 内容。
拒绝接纳不调用任何 callback，调用方继续拥有并清理 arg。
成功后 arg 地址、payload、callback 代码和 provider context 保持有效，直到
finalize 返回；没有 finalize 时保持到 run/cancel 返回。

```text
QUEUED -> CLAIMED -> RUNNING -> FINALIZING -> COMPLETED
   |         |
   +---------+-> CANCELLING -> FINALIZING -> CANCELLED
```

每条 CLAIMED task 在 monitor 内选择 RUNNING 或 CANCELLING；这一步与 shutdown
线性化，且只允许一次选择。`CANCEL_PENDING` 取消 QUEUED 和尚未选择 RUNNING 的
CLAIMED task，包括 worker 私有 batch 剩余条目。已选择 RUNNING 的 task 正常完成，
不抢占线程、不将 cancel 请求冒充执行终态。

run/cancel/finalize 全部在锁外执行，TLS 标记整个 callback 链为同池执行上下文。
每个 accepted task 恰好执行 run 或 cancel 的一条路径，然后 finalize 一次（若非 NULL）。
cancel 为 NULL 表示没有取消 callback，但仍结算 CANCELLED 并调用 finalize。
finalize 返回后才释放 pool pending credit 和提交 terminal 计数。
pool 的 COMPLETED 表示 callback 生命周期完成，不表示业务计算或远端发送成功。

callback 必须正常返回；C++ adapter 在边界捕获异常并编码业务失败，不能让异常、
longjmp 或线程退出越过池的结算逻辑。不允许 callback 同步 wait/destroy 同一池；
同池阻塞提交遇到 FULL 返回 `SALTS_EBUSY`。非阻塞递归提交仍按 FULL/CLOSED 处理。
provider/插件 lease 由任务 envelope 显式持有；不要让裸 callback 指针隐式保活 DSO。
释放 lease 的 wrapper 位于宿主常驻代码中，必须等插件 run/cancel/cleanup 全部返回，
再释放 lease；正在执行的插件 finalize 不能自行释放最后一个保活自己的 lease。

## 容量与背压

Q 为 ring 中排队 descriptor 上限，W 为 worker 数，B 为 max_batch：

```text
queued <= Q
claimed + running + cancelling + finalizing <= W * B
pool_pending <= Q + W * B
descriptor_storage ~= next_power_of_two(Q) * sizeof(task)
                    + W * B * sizeof(task) + worker/control storage
```

所有乘加、ring 取整、线程数组分配和统计计数边界必须检查溢出。
与现有 `int pending()`、int64 counters 的范围相容；若无法表示配置上限则创建失败。
长期累计计数接近边界时关闭新接纳并完成已接纳任务，不静默 wrap。
池只约束 descriptor，不保证 arg/payload 的字节上限。

FlowMQ 消费适配层另设任务 envelope 固定池、输入/输出 backing budget、每 lane 的
outstanding credits。预算按实际被保活的 backing allocation 计费，不能仅按 slice
逻辑长度计费；小 slice pin 住大 backing 时同样可能耗尽内存。
每个 job 的输入、输出大小设硬上限，pool/reset/trim 在最后一个相关 lease 结束前禁止执行。

I/O lane 仅使用 try_submit_task，不等待 CPU 队列空间。FULL 时撤销准备的资源和
结果 credit，保留调用方 ownership，按应用既有接纳/重试策略报告背压。
不增加无界暂存、静默丢弃或改为让 I/O lane 临时执行昂贵计算。

## FlowMQ 回传协议

```text
固定 I/O lane：完整消息 -> 拥有型 job + 预留结果 credit -> try_submit
LF worker：读取不可变输入 -> 独立计算 -> 发布拥有型结果
原 I/O lane：取结果 -> 校验 generation/sequence -> 提交协议状态或发送 -> 释放 credit
```

每 lane 使用现有 Disruptor 的 MPSC worker queue 存小型结果 envelope，唯一 consumer
为该 lane。每个成功提交的 job 预先获得一条结果 credit；容量 R 满足
`outstanding_jobs <= R`，直到 owner 消费完成或取消结果才归还。
这样 task input slot 即使先释放，也不会无限增加未消费结果。
run/cancel 都准备一个 terminal result，宿主 finalize 统一发布一次；不能在取消时丢失结果。
task envelope 与 result envelope 使用分开的固定存储和释放义务。result 不借用 task 的
内存；发布成功后 worker 不再访问 result，owner 可立即消费并释放它。task/input cleanup
仍在 finalize 中完成，owner 不凭“结果已可见”提前释放 worker 尚在使用的 arg。
固定 task pool 的容量独立于结果 credit；结果被快速消费、credit 已归还但 finalize
尚未返回时，也只能从该 task pool 的空闲项接纳新任务。

结果 capacity 预留必须在 task admission 前成功；admission 失败回滚 credit/payload。
结果 queue 的发布使用同一套现有 claim/publish 协议。credit 保证有容量，不保证所有
较早 producer 已发布：较晚完成可以暂时不可见，owner 继续 drain，不据此重发结果。
worker 不阻塞等待原 lane 腾出结果空间。若 claim 在有效 credit 下仍报容量不足，
视为不变量失败并明确终止资格化/运行，停止新接纳；不能偷偷丢弃、扩容，
不能宣称正常 drain 成功或继续释放在途存储。它不是正常业务背压恢复路径。

envelope 存 lane 的应用 lifetime token、连接 slot/generation、job correlation 和
需要时的 message sequence。generation 失配时 owner 释放结果及 credit，
不将结果应用到复用的连接。lane/结果队列必须活到全部 outstanding job 回传并被回收，
LF worker 不能保存即将销毁的 socket 指针。

依赖顺序的操作不交给独立 LF task。确需乱序计算、顺序提交时，以 R credit 同时约束
owner 的 reorder window，失败/取消也发布占位终态以推进序列；不能引入无界 map。

首个集成由应用 loop 在有限 timeout 的 owner_poll 前后 drain 结果，明确配置最大轮询
等待时间。当前公开 owner_poll 不承诺 application-command wake，不能调用私有
benchmark wake 冒充公开能力。若需要低延迟 idle wake，须另行设计并验证公开 wake
契约后接入；本 LF 池本身不观察、路由或唤醒 NativeIO backend。

## 关闭、销毁与统计

pool：`OPEN -> CLOSING_DRAIN / CLOSING_CANCEL -> CLOSED`。第一次 shutdown 固定
policy；重复同 policy 幂等，换 policy 返回 `SALTS_EBUSY`。关闭时唤醒全部 worker、
阻塞 producer 和 idle waiter。DRAIN 完成所有 accepted task；CANCEL 走上述未开始
任务终态协议。monitor 不会在结算 callback 中持有。

`wait_status` 等待 pending 为零且全部 finalize 已返回；自身 callback 调用返回 EBUSY。
wait 是瞬时 idle 观察，不是仍开放接纳时的永久 barrier；整体关闭先 shutdown 再 wait。
destroy 前停止并 join 全部 producer/API 调用者，完成池 drain，join worker，最后释放
condition、mutex、Disruptor 和 scratch。destroy 不与其他访问并发。
不可返回的 callback 会阻止 drain；不以 timeout 强制释放仍在使用的存储。

FlowMQ 应用关闭顺序：停止产生新的 offload job -> 停止 task submitter -> 关闭 LF
接纳 -> 保持结果队列与 lane lifetime 活着 -> 继续 owner 回收终态 -> pool idle / worker
join -> outstanding credits 清零 -> 原线程完成 socket/owner stop、drain、destroy ->
最后释放 job pool、payload pool 和 provider lease。pool idle 不代表 owner 已消费结果。

精确 snapshot 在 monitor 下读取 authoritative 状态：

```text
accepted = queued + claimed + running + cancelling + finalizing + completed + cancelled
idle 后 accepted = completed + cancelled
```

已有 stats 字段保持定义；LF 专有 claimed/running/cancelling/finalizing、sleeping_workers、
leader_id、max_batch、handoffs/wake_signals/empty_wakes 使用版本化 get_lf_stats 查询。
peak_pending_tasks 复用旧查询；没有新增延迟直方图或逐事件日志。

## 风险、验证与交付阶段

| 风险 | 触发与最小控制 |
| --- | --- |
| HIGH：owner 亲和破坏 | worker 操作 socket/backend；仅传 immutable 输入与应用结果 |
| HIGH：返回通道死锁/泄漏 | 结果未预留容量或取消未回传；每 accepted job 持有一个 terminal credit |
| HIGH：提前回收 | pool idle 就销毁 lane/payload/provider；独立等待结果消费和 lease 归零 |
| HIGH：错误顺序提交 | 将领取 FIFO 当完成 FIFO；限制独立任务，必要时有界 sequence window |
| MED：monitor 竞争、batch 延迟 | producer/leader 集中操作；与原线程池比较 P99 与锁等待后再选择 B |
| MED：睡眠结果延迟 | 公开 owner 没有 command wake；明确有限 poll timeout，另行资格化 wake |

阶段一已实现 opt-in Concurrency LF backend。正式 thread_pool_lf_test 包含
sparse wake、强制阻塞第一 handler 后第二 handler 提前进入、1/2/4
workers、多 producer、FULL、shutdown 与提交竞争、claimed batch 取消和 finalize gate。
逐 ID 验证 run/cancel 二选一、finalize 一次、ownership 及所有 accepted 终态。

阶段二已加入 [`thread_pool_lf_benchmark`](benchmarks/thread_pool_lf_benchmark.c)：
16 B、1 KiB、64 KiB，生产者 1/4、worker 1/4，默认池和 LF batch=1/32，
共 36 组，每组 1,024 条、8 次采样和一次预热。线程创建、输入构造与预期 checksum
在计时外；计时包含有界提交、全量逐字节 checksum 和 finalize barrier。
逐 ID 验证每一轮 run/finalize 次数及最终 checksum。payload 为借用的不可变输入，
这不是 FlowMQ 网络吞吐或 copy bandwidth。最大输入 backing 为 64 MiB。
专项完整资格化才扩展 batch=4/8 和中间 payload：64 B、256 B、4 KiB、16 KiB、
worker=2，每组 2,048 条、16 次采样。分组验证 burst、paced sparse、
长短混合任务、消费暂停、结果队列接近容量、冷启动/反复创建与长期 wrap。
在同一总线程/核预算下比较 direct lane、原 Concurrency pool、LF、serial token。
报告吞吐、P50/P95/P99、CPU/消息、handoff/wake、拒绝率与实际 retained backing 峰值。
旧 benchmark 的平台倍数仅作研究证据，不作为新 API 或 FlowMQ 的收益承诺。

阶段三建立应用级 FlowMQ 正式 fixture，验证 generation stale、ordered result window、
cancel result、有限 poll timeout 和关闭全过程。执行 Windows/Linux/macOS CTest、
ASan/UBSan、TSan，安装 SDK 的 C11/C++17 小型消费验证；移动平台的构建和设备运行
分别报告。CI 使用完整 configure graph 和 CTest，不直接执行生成的 benchmark binary。

生产启用保持显式创建与应用 offload 开关。回滚停止新接纳，drain/reclaim 既有工作后
重建原线程池或关闭 offload；不得在任务尚未结算时切换执行后端或修改 owner。
## 使用与本地验证

应用链接 `Salts::Concurrency`，包含 `<salts/thread_pool.h>`，显式使用新 creator。
现有 submit/try_submit、shutdown、wait 和 destroy 均可使用；task descriptor 复制，
arg 不复制，应用为成功任务保持输入到 finalize 返回，失败则自行回收。
完整 C11/C++17 消费示例见 [installed consumer](tests/package_config/leader_followers/main.c)。
FlowMQ 无须引入 CFlow；它仍需实现上文的有界结果队列、credits 和 owner 回收。

本地 Windows MSVC Release：LF、旧线程池、C++ consumer、Disruptor、Actor 和
CFlow execution 共 6 个 CTest，重复 5 次通过。Debug ASan：LF、旧线程池、C++ consumer、
Disruptor、CFlow owner executor 和 DSO executor 共 6 个 CTest，重复 3 次通过。
36 组基准 Release 用时 7.25 s，110,952 个断言；Debug ASan 用时 16.20 s，均通过。
Release 的安装 SDK C11/C++17 消费 CTest 已通过（3.17 s），只链接 Salts::Concurrency。
这些是本地证据，跨平台/TSan/安装包 CI 与 FlowMQ 应用资格化分别记录，不能推定通过。

一次本地 Release 采样，P=4/W=4（msg/s）：

| 输入 | 默认池 | LF batch=1 | LF batch=32 |
| --- | ---: | ---: | ---: |
| 16 B | 1,285,645 | 142,577 | 2,149,623 |
| 1 KiB | 1,224,716 | 229,291 | 1,436,210 |
| 64 KiB | 55,344 | 51,643 | 55,940 |

这些短采样不提供 P99 或 CPU/消息结论。batch=1 的高频交接在短任务上代价明显，
LF 没有普遍优势；按应用测量选择 B，默认后端保持原实现。

重现：configure 加 `-DCONCURRENCY_BUILD_BENCHMARKS=ON`；按正式 preset build，
通过 `ctest --preset <preset> -LE "^$" -R "^thread_pool_lf_benchmark$" -V` 运行。
功能 CTest 为 thread_pool_lf_test、concurrency_header_cpp_test，以及 Release 的
thread_pool_lf_installed_test（独立 install/find_package/链接 C11/C++17 消费工程）。
CI 完整构建 configure graph，通过 CTest 执行小基准和 ASan+UBSan/TSan 生命周期检查。
当前源码没有升级 FlowMQ SDK、改动其 I/O owner 或发布新 Salts 版本。
