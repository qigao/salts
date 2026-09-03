# Coroutine

`Salts::Coroutine` 是 `vendor/minicoro/minicoro.h` 的唯一编译封装，提供低层 coroutine primitive、单 owner 有界 frame pool，以及可选的多 shard Executor。它不依赖 CFlow、NativeIO 或 CNet；Executor 只复用 `Salts::Concurrency` 的线程池与 Disruptor，不把网络状态带入 coroutine core。

低层 API 提供显式 coroutine 生命周期、cooperative yield/resume、bounded pool 和通用 scheduler。`salts_coro_pool_t` 本身不增加锁或线程：create/acquire/release/destroy 由同一 owner 执行。`salts_coro_executor_t` 在它之上建立固定 shard；每个 worker 独占一个 scheduler、一个 pool、一个有界 MPSC task queue 和一个有界 completion wake queue，用户线程只提交复制后的 descriptor，运行中的 frame 不跨 shard 迁移。

Executor 的 admission 与终态协议是：

- `submit_to` 显式保持连接或 session affinity，普通 `submit` round-robin 分片；
- `try_submit*` 在 shard queue 满时返回 `SALTS_ENOBUFS`，阻塞提交只等待 queue admission；
- 成功 admission 恰好执行 `run` 或 `cancel`，随后执行可选 `finalize`；拒绝不调用 callback；
- `shutdown` 关闭 admission 并 drain 已接收的有限 cooperative coroutine；
- `salts_coro_executor_yield()` 主动让出当前 shard；`coro_yield()` 仍可作为低层等价入口；
- `await_begin` 为当前 frame 预留一个 generation-checked slot，外部操作提交失败时调用 `await_abort`，成功后调用 `await` 挂起；
- 任意完成线程通过 `await_complete(executor, handle, status)` 发布一次 terminal wake；它不会在调用线程直接 resume，而由原 shard owner 消费 wake queue 后恢复 frame；
- completion 可以先于 `await` 到达，也可以在 `shutdown` 关闭 task admission 后到达。重复完成返回 `SALTS_EALREADY`，已消费或已 abort 的 handle 返回 `SALTS_ENOENT`。

await slot 只保存“哪个 frame 等待、是否已有完成、完成状态是什么”；NativeIO request slot、readiness registration 或其他异步子系统仍是操作进度与 terminal result 的事实源。一个 frame 同时最多持有一个 await。调用顺序是：`await_begin` → 提交外部操作 → `await`；若提交失败则以 `await_abort` 收尾。外部 operation owner 必须保证每次成功提交最终恰好调用一次 `await_complete`，并在 `destroy` 前停止所有 completion caller。`shutdown` 无法替一个通用外部操作合成取消终态，因此遗失 completion 的 await 会按契约阻止 drain。

默认每 shard 最多保留 64 个 frame。按 minicoro 默认 128 KiB stack 与 1 KiB storage 计算，硬上限约为每 worker 8.1 MiB，尚未计入 frame metadata 和 alignment；64 位平台上的 1024-entry task queue 约持有 32 KiB descriptor，completion queue 则按 frame 上限向上取 2 的幂，因此每个 active await 最多占一个 wake entry。达到历史峰值的 frame 会被 pool 保留复用，因此长驻进程应按 `worker_count × max_capacity × (stack_size + storage_size)` 配置预算，而不是把默认值视为无成本。

NativeIO 现有 `native_io_coroutine_await()` 仍由 backend 的单 owner 在 terminal completion 到达后恢复其私有 frame。通用 Executor 的 await token 为未来 adapter 提供跨线程完成投递 primitive，但不改变 NativeIO 当前“submit/observe 由 backend owner 推进”的约束；真正接入时，adapter 仍需把 NativeIO completion 映射到 token，不能让 token 成为第二份 I/O 结果。

依赖方向固定为：

```text
CFlow / CNet -> NativeIO -> Coroutine primitive -> vendor/minicoro

Application adapter -> Coroutine Executor -> Concurrency -> Platform
```

`cflow/minicoro` 适配目标已删除。CFlow 的 Graph/Resumable 不拥有 coroutine frame；需要异步 I/O 的 Actor、Reactive 或 CNet 层通过 NativeIO 的 operation/completion 或 coroutine owner API 工作。
