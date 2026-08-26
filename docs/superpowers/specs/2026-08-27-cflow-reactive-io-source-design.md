# CFlow Reactive I/O Source Design

## 背景

CFlow 当前有两条已验证但彼此独立的异步路径：

- `cflow_run` / `cflow_subscription` 以 downstream-value demand 驱动
  `cflow_source`，并通过 `WAIT -> arm -> wake -> resume` 表达暂停与恢复；
- `cflow_io_actor` 以有界 MPSC 命令、单 driver、backend completion、Executor
  delivery 与 acknowledge 表达原生 I/O 生命周期。

`cflow_source_from_reactor_registration()` 已能把 readiness resource 接入 Run，
但 Actor、native backend 和 file facade 的 completion 不会自动受
`cflow_run_request(n)` 控制。本设计新增一个薄 Adapter，把一个 Actor request
映射为一个 Source item，同时保留两个子系统原有事实源和状态机。

## 决策

新增公开 `cflow/io_source.h`：`cflow_source_from_io_actor()` 创建一个 typed
Source 和一个外部 owner。Adapter 内部拥有一个 request-capacity 为 1 的
`cflow_io_actor` 与一个 capacity 为 1 的 manual Executor；它借用 backend strategy
和调用方 context。

```text
positive Run demand
  -> Source.resume
  -> prepare one move-only operation
  -> Actor.try_submit
  -> driver wake
  -> owner_run_ready
  -> backend submit/completion
  -> Actor Executor delivery
  -> encode one copied typed value
  -> Source waker
  -> Run resumes and emits
```

completion backend 不会被伪装成 readiness backend。Adapter 只转换 demand、
completion 与生命周期边界，不修改 Actor、Runtime 或 native backend。

## 公开接口

```c
typedef struct cflow_io_source_owner {
    void *impl;
} cflow_io_source_owner;

typedef enum cflow_io_source_prepare_status {
    CFLOW_IO_SOURCE_PREPARE_OPERATION = 0,
    CFLOW_IO_SOURCE_PREPARE_DONE,
    CFLOW_IO_SOURCE_PREPARE_ERROR
} cflow_io_source_prepare_status;

typedef cflow_io_source_prepare_status (*cflow_io_source_prepare_fn)(
    void *user,
    cflow_io_operation *operation,
    const char **error);

typedef cflow_read_status (*cflow_io_source_encode_fn)(
    void *user,
    cflow_io_request_id request_id,
    cflow_io_lease_id lease_id,
    void *operation_user,
    const cflow_io_completion *completion,
    void *out_value,
    const char **error);

typedef struct cflow_io_source_config {
    const char *name;
    const cmeta_type_desc *type;
    cflow_io_backend_ops backend;
    void *backend_user;
    cflow_io_source_prepare_fn prepare;
    cflow_io_source_encode_fn encode;
    void *user;
    cflow_io_wake_fn drive;
    void *drive_user;
} cflow_io_source_config;

typedef struct cflow_io_source_stats {
    cflow_io_actor_stats actor;
    bool source_live;
    bool request_active;
    bool result_ready;
    bool close_requested;
} cflow_io_source_stats;

int cflow_source_from_io_actor(
    cflow_source *out,
    cflow_io_source_owner *owner,
    const cflow_io_source_config *config);

int cflow_io_source_owner_run_ready(
    cflow_io_source_owner *owner,
    size_t max_steps,
    size_t *progressed);

bool cflow_io_source_owner_is_quiescent(
    const cflow_io_source_owner *owner);

bool cflow_io_source_owner_get_stats(
    const cflow_io_source_owner *owner,
    cflow_io_source_stats *out);

int cflow_io_source_owner_close(cflow_io_source_owner *owner);
```

`cflow_source_from_io_actor()` 是 additive API。`out` 和 `owner` 必须为 zero state；
失败保持二者为 zero state。`name`、backend、backend context、prepare/encode/drive
context 和回调依赖均借用至 owner 成功关闭。

## 数据与所有权协议

| 项目 | 契约 |
|---|---|
| 数据单元 | 一个 move-only `cflow_io_operation` 对应零或一个 typed Source value |
| 事实源 | Actor request slot 保存 I/O 阶段；Adapter 单完成槽保存已编码输出 |
| operation 所有权 | `prepare` 返回 OPERATION 后移交 Adapter；Actor 接受后再 move 给 Actor；任何拒绝由 Adapter 调用 `release` |
| output 所有权 | encoder 在 completion callback 内写入 Adapter 独占槽；Source resume 复制到 Runtime 空 storage |
| output 类型 | 必须具有 `TRIVIAL_COPY | TRIVIAL_DESTROY`，避免跨 callback 保存非平凡对象 |
| backend 所有权 | Adapter 借用 `backend_user`，owner close 成功前必须保持有效 |
| 容量 | Actor request 1、manual Executor 1、typed completion slot 1；运行期间不扩容 |
| 背压 | 没有 positive demand 不调用 prepare；一个 request 未完成并确认前不准备下一 operation |
| 顺序 | 严格串行，Source emission 顺序等于 operation preparation 顺序 |

固定容量 1 是该适配器的一对一语义，不是可调吞吐参数。需要并行预取的调用方继续
直接使用 `cflow_io_actor`；未来若增加 prefetch，必须另行定义 demand、乱序完成与
重排容量协议。

## 线程拓扑与唤醒

- Run scheduler 是唯一 Source resume consumer。
- operation preparation 发生在 Run pump 线程。
- Actor submission 是 MPSC-safe，但本 Adapter 每次只有一个 live request。
- `owner_run_ready()` 只有一个 driver，可由 reactor/event-loop 线程调用；并发或重入
  返回 `TURBO_EBUSY`。
- backend completion 可从任意 backend thread 进入 Actor。
- Adapter 的 completion encoder 运行在 owner driver 的 internal manual Executor。
- `drive` 是可合并边沿通知，表示 owner 可能可驱动；它必须调度
  `owner_run_ready()`，不得在回调内同步 close/destroy。
- Source waker 可在 owner driver 线程调用，并可同步触发 scheduler。

为了覆盖 completion-before-arm 竞态，waitable arm 在共享 gate 下安装 waker并重查
完成槽；若 completion 已发布，则在锁外立即 wake。重复 wake 可合并，不能丢失从
`SUBMITTED` 到 `RESULT_READY` 的边沿。

## 状态机

```text
IDLE
  -> PREPARING
  -> SUBMITTED
  -> RESULT_READY / TERMINAL_READY
  -> ACKNOWLEDGED
  -> IDLE / DONE / ERROR

任何非终态
  -> CLOSE_REQUESTED
  -> Actor cancel/terminal completion
  -> acknowledged + quiescent
  -> owner close
```

Source 可以在 Actor acknowledge 之前消费已复制 value。此时下一次 resume 返回 WAIT，
直到 owner driver 完成 acknowledge 并再次 wake；因此不会因 inline scheduler 重入而
在 request slot 释放前提交第二个 operation。

`prepare` 的 DONE 直接结束 Source，不创建 Actor request。ERROR 和 encoder ERROR
把稳定错误指针传播给 Run。encoder 返回 `CFLOW_READ_WOULD_BLOCK` 属于协议错误，
转换为明确的 Source ERROR；一个 authoritative completion 不允许重新等待。

## 关闭协议

1. Run cancel/close 调用 Source cancel，原子设置 close requested 并关闭 Actor admission。
2. 已提交的 native request 收到 best-effort cancel，但仍等待 authoritative completion。
3. Source destroy 清除 waker、标记 `source_live=false` 并释放 Source 引用；不销毁 backend。
4. 调用方继续响应 `drive` 并调用 `owner_run_ready()`，直到 completion delivery 与
   acknowledge 全部 drain。
5. `owner_close()` 在 Source 仍 live 或 Actor 未 quiescent 时返回 `TURBO_EBUSY`，不释放状态。
6. quiescent 后 owner close 销毁 Actor 和 manual Executor、清理 typed slot、清空 owner。
7. 调用方之后才能销毁 borrowed backend 与 callback context。

关闭不阻塞等待 native completion，也不静默丢弃 operation。Actor completion 始终是
唯一终态证据。

## 错误语义

- invalid output/owner/config/backend/descriptor/callback：`TURBO_EINVAL`；
- 内部 state、typed slot、Executor 或 Actor 分配失败：`TURBO_ENOMEM`；
- concurrent/reentrant driver 或过早 owner close：`TURBO_EBUSY`；
- Actor admission 的 FULL/CLOSED/LEASE/ID 异常转换为稳定 Source ERROR；
- prepare/encoder 用户错误通过其 `error` 指针传播；该字符串必须有效至 owner close；
- backend submit failure 保留 Actor 原有 `FAILED(error)` completion；
- 不提供 fallback backend、无界 queue、隐式 retry 或静默 drop。

## 兼容性与迁移

- 只新增 `io_source.h`、`io_source.c`、测试、聚合 include 与文档；现有 ABI 和行为不变。
- 直接 Actor/native/file 用户无需迁移。
- readiness Source 继续用于“read + arm readiness”的资源；本 Adapter 用于
  “prepare operation + authoritative completion”的 Reactor/Proactor backend。
- 回滚可删除新增文件和 CMake 条目，不涉及数据迁移。

## 验证范围

- 无 demand 时零 preparation/submit；
- 一单位 demand 最多一个 live request；
- synchronous completion-before-arm 无 lost wake；
- completion 编码为 VALUE、VALUE_AND_DONE、DONE、ERROR；
- inline scheduler 在 acknowledge 前重入时不重复提交；
- prepare DONE/ERROR 与 operation token 拒绝释放；
- cancel-before-submit、cancel-after-submit、authoritative completion drain；
- owner live/early close、single-driver、quiescence 和 repeated lifecycle；
- C11/C++17 aggregate header；
- `cflow_io_actor_test`、`cflow_io_native_test`、`cflow_io_file_test`、
  `cflow_runtime_test`、`cflow_readiness_test` 相邻回归；
- Windows Release 全量、install package consumer，并按可用环境补充 dev/ASan。

