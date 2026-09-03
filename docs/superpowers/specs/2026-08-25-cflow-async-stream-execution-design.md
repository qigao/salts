# CFlow 异步 Stream 执行设计

日期：2026-08-25

## 背景与范围

现有 `cflow_scheduler_worker_init()` 已通过 `salts_threadpool` 提供并发调度，`cflow_run` 也保证同一 Run 的 pump 串行化；但 `cflow_eval_stream()` / `cflow_eval_collect()` 固定创建测试 scheduler，因此 Container Stream terminal 无法使用调用方已有的 worker scheduler。

本次增加一个异步 collection terminal。它把一条已绑定 Range 的 `cflow_stream` 提交给调用方提供的并发 scheduler，并返回可等待、可取消、可查询、可销毁的执行句柄。

本次不增加逐元素或逐 operator 并行，不改变 filter/map/reduce 的顺序、错误或生命周期语义，也不增加第二套线程池或队列。

## 选择的接口

新增公开头文件 `cflow/stream_execution.h`：

```c
typedef struct cflow_stream_execution {
    void *impl;
} cflow_stream_execution;

typedef enum cflow_stream_execution_state {
    CFLOW_STREAM_EXECUTION_ZERO = 0,
    CFLOW_STREAM_EXECUTION_RUNNING,
    CFLOW_STREAM_EXECUTION_COMPLETED,
    CFLOW_STREAM_EXECUTION_FAILED,
    CFLOW_STREAM_EXECUTION_CANCELLED
} cflow_stream_execution_state;

typedef enum cflow_stream_execution_status {
    CFLOW_STREAM_EXECUTION_OK = 0,
    CFLOW_STREAM_EXECUTION_INVALID_ARGUMENT,
    CFLOW_STREAM_EXECUTION_INVALID_SCHEDULER,
    CFLOW_STREAM_EXECUTION_STREAM_REJECTED,
    CFLOW_STREAM_EXECUTION_COLLECTOR_REJECTED,
    CFLOW_STREAM_EXECUTION_GRAPH_REJECTED,
    CFLOW_STREAM_EXECUTION_SOURCE_REJECTED,
    CFLOW_STREAM_EXECUTION_RUN_REJECTED,
    CFLOW_STREAM_EXECUTION_DEMAND_REJECTED,
    CFLOW_STREAM_EXECUTION_ALLOCATION_FAILED,
    CFLOW_STREAM_EXECUTION_ALREADY_STARTED,
    CFLOW_STREAM_EXECUTION_WOULD_BLOCK,
    CFLOW_STREAM_EXECUTION_TERMINATED
} cflow_stream_execution_status;

typedef struct cflow_stream_execution_snapshot {
    cflow_stream_execution_state state;
    cmeta_status collector_status;
    size_t count;
    const char *error;
} cflow_stream_execution_snapshot;

cflow_stream_execution_status cflow_stream_execution_start(
    cflow_stream_execution *execution,
    const cflow_stream *stream,
    cflow_scheduler *scheduler,
    cmeta_collector collector);
cflow_stream_execution_status cflow_stream_execution_cancel(
    cflow_stream_execution *execution);
cflow_stream_execution_status cflow_stream_execution_wait(
    cflow_stream_execution *execution);
bool cflow_stream_execution_get_snapshot(
    const cflow_stream_execution *execution,
    cflow_stream_execution_snapshot *out);
cflow_stream_execution_status cflow_stream_execution_destroy(
    cflow_stream_execution *execution);
```

Container 只增加薄包装和 typed collector 宏，不另建执行引擎。

## 数据与并发协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 一个 Range 元素；其生命周期仍由 CFlow value traits 管理 |
| 主事实源 | `cflow_run` 是执行进度的事实源；句柄状态只记录 terminal 结果 |
| 句柄拥有 | 规范化 Graph、Run、Collector 状态副本、同步原语、错误文本 |
| 句柄借用 | scheduler、Range 的底层容器、Collector 的 context/输出对象 |
| 借用截止 | 上述借用必须保持有效直到 `cflow_stream_execution_destroy()` 返回 |
| 线程拓扑 | 一个外部 control owner；worker scheduler 可含多个 worker，但单 Run pump 串行 |
| 顺序 | 与现有解释执行完全相同；不并行调用同一 Collector |
| 容量/背压 | 沿用 scheduler ready/timer capacity 和 Collector limit；不隐式扩容 |
| 满额行为 | scheduler admission 或 Collector capacity 失败即 FAILED/拒绝，不 fallback |
| 关闭 | cancel/destroy 在外部线程同步关闭 Run；返回后不再访问借用对象 |
| 可观测性 | snapshot 暴露状态、Collector status、已接受数量与首个执行错误 |

公开线程安全边界：`get_snapshot()` 可与 worker 执行并发；`cancel()`、`wait()`、`destroy()` 是 control-plane 操作，由同一个外部 owner 串行调用。它们不得从本执行的 Range、operator 或 Collector callback 内调用；`cancel/wait/destroy` 在该场景返回 `WOULD_BLOCK`，避免自等待或释放活动栈。句柄必须用 `{0}` 初始化。

## 状态机

```text
ZERO --start/admitted--> RUNNING --done+commit--> COMPLETED
                              |--runtime/collector error--> FAILED
                              `--external cancel+close+abort--> CANCELLED
```

- `start()` 的所有 admission 阶段都 fail fast。失败会完整释放临时 Graph、Source、Run 和 Collector 事务，并把句柄恢复为 ZERO；调用方可修正输入后重试。
- `RUNNING -> COMPLETED/FAILED/CANCELLED` 只允许首次 terminal 决定结果。
- Collector `begin/accept/finish/abort` 恰好遵守其事务状态机；成功只发布 committed 输出，任何失败或取消均 abort。
- `cancel()` 若与自然完成竞争，以同步 `cflow_run_close()` 观察到的自然 terminal 为准；若自然 terminal 未发生，则发布 CANCELLED。
- `destroy()` 对 ZERO 幂等；对 RUNNING 等价于同步 cancel 后释放；对 terminal 状态关闭并释放 Run。

## 错误语义

- 参数、非并发 scheduler、无效 Stream、类型不匹配、生命周期 trait 缺失、Graph/Source/Run admission、scheduler demand rejection 都返回可区分 status。
- 一旦 `start()` 返回 OK，执行结果只通过 snapshot terminal state 表达。
- Collector 失败保留准确 `cmeta_status`；CFlow runtime 错误保留首个错误文本。
- 不记录重复错误日志，错误由调用 terminal 的边界消费。

## 架构影响与兼容性

- CFlow 新增 terminal facade，复用 `cflow_run`、`cflow_scheduler`、Range Source 和 `cmeta_collector`，依赖方向不变。
- Container 继续只是 CFlow 的用户入口；它不拥有 scheduler，也不依赖底层 threadpool 类型。
- 现有同步 API 和用户可见行为不变。新 API 是增量兼容改动。
- 同一 scheduler 可承载多个独立 Stream execution；这提供 pipeline 级并发。单 pipeline 内仍保持确定性顺序。

## 验证范围

1. worker scheduler 上异步 filter/map 收集成功并 commit；调用线程可 wait。
2. 同一 scheduler 上多个独立 execution 均完成，结果互不污染。
3. Collector limit 失败会 abort、输出保持零态、snapshot 为 FAILED。
4. 外部 cancel 同步关闭活动 Run，并且 Collector 只 abort 一次。
5. callback 内 wait/cancel/destroy 返回 WOULD_BLOCK。
6. 非并发 scheduler、无效 Stream、Collector 类型不匹配均在 admission 阶段拒绝且句柄保持 ZERO。
7. Container typed collector 包装可直接启动异步执行。
8. C11、C++ 聚合头、安装/消费与现有完整 CTest 回归通过。

## 后续工作

逐元素并行需要 operator 可并行性、顺序恢复、分区、合并、取消传播与资源预算的独立契约，应作为后续设计；本接口不提前承诺该语义。
