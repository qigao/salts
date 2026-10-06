# Coroutine

底层协程、复用池和执行器分别使用 `<coro.h>`、`<coro_pool.h>` 和
`<coro_executor.h>`，类型及函数统一使用 `coro_` 前缀，池和执行器宏使用
`CORO_` 前缀。此次重命名会改变头文件路径与 C 链接符号；下游须更新引用并
重新编译。结构布局、所有权、错误码与执行协议保持原有约定，CMake 链接入口仍为
`Salts::Coroutine`。

## CMeta facade

应用协程包含 `<cmeta/coroutine.h>` 并链接 `Salts::Coroutine`。该模块安装 facade
头文件并导出 target；CMeta core 无需依赖 Coroutine。`cmeta_yield/cmeta_wait*`
直接复用 `coro_executor_*`，执行器仍拥有 frame、shard、wait slot 与 wake
queue。minicoro 头文件和 executor 私有结构不属于应用接口。

| 入口 | 输入、结果与约束 |
| --- | --- |
| `cmeta_yield()` | 在当前协程的 owner shard 主动让出执行；协程外返回 `SALTS_EINVAL`。 |
| `cmeta_wait_begin(&wait)` | 预留当前协程唯一的有界 slot；已有 reservation 返回 `SALTS_EBUSY`，容量不足返回 `SALTS_ENOBUFS`。失败清零输出。 |
| `cmeta_wait(wait, &status)` | 由原协程消费结果，尚未完成则挂起；提前完成不会丢失。函数返回 `SALTS_OK` 表示协议成功，`status` 才是外部操作结果。 |
| `cmeta_wait_for(wait, timeout_ms, &status)` | 正数超时从本次调用起算；超时返回 `SALTS_OK` 且 `status == SALTS_ETIMEDOUT`，并消费 token。零超时返回 `SALTS_EINVAL`，保留 reservation。 |
| `cmeta_wait_abort(wait)` | 外部操作未成功 admission 时由原协程撤销 reservation；成功使 token 失效。若完成已先到达，返回 `SALTS_EALREADY`，仍须调用 wait 消费。 |
| `cmeta_wait_complete(executor, wait, status)` | 任意线程发布一次完成；恢复仍由 owner shard 执行。重复完成返回 `SALTS_EALREADY`；已消费、已 abort 或 task 已返回的 token 返回 `SALTS_ENOENT`。 |
| `cmeta_current_executor()` / `cmeta_current_shard(executor)` | 查询当前 worker context；executor shard 线程外分别返回 `NULL` / `SIZE_MAX`。worker context 不代表当前正在运行可挂起的协程。 |

wait handle 是可复制的借用同步 token，不拥有 payload。错误 executor、错误协程或
shard 的调用返回 `SALTS_EINVAL`，旧 generation 返回 `SALTS_ENOENT`。wait 的非空
结果输出在入口清零；空输出指针返回 `SALTS_EINVAL`。同一个 slot 复用后，旧 token
不能完成、撤销或消费新的 reservation。

调用顺序为：预留 slot → 提交外部操作 → 等待并消费；外部 admission 失败时用
abort 收尾。已 admission 的操作，其 payload 与资源仍归外部 owner 管理。
**等待超时只结束同步等待，不会取消外部操作，也不能证明借用资源已无人访问。**
外部 owner 必须保留资源，直到 terminal completion 或显式取消协议证明 quiescent；
迟到的 token 完成会被拒绝。`shutdown` 关闭新 task admission，但仍接受已接收 wait
的完成。所有完成调用者必须停止后才能 `destroy`，不可把失效 token 当作 executor
销毁后仍可调用的许可。

完整、可编译的使用示例直接见正式测试
[cmeta_coroutine_facade_test.c](tests/cmeta_coroutine_facade_test.c)：外部线程投递、
确定性的提前完成、非零 owner shard 挂起恢复、timeout、abort 与单 slot generation
复用。[C++ 头文件测试](tests/coro_executor_header_cpp_test.cpp) 合并 executor
默认配置、值初始化与全部 facade 入口检查；两个应用测试均不包含 minicoro 或
executor 私有头文件。

本地验证沿用 user presets，例如 Windows 的 `VsDevCmd.bat` 环境下：

```powershell
cmake --preset win-dev-user
cmake --build --preset win-dev-user --target cmeta_coroutine_facade_test coro_executor_test coro_executor_header_cpp_test
ctest --preset win-dev-user --output-on-failure -R '^(cmeta_coroutine|coro_executor)'
```

`Salts::Coroutine` 是 `vendor/minicoro/minicoro.h` 的唯一编译封装，提供低层 coroutine primitive、单 owner 有界 frame pool，以及可选的多 shard Executor。它不依赖 CFlow、NativeIO 或 CNet；Executor 只复用 `Salts::Concurrency` 的线程池与 Disruptor，不把网络状态带入 coroutine core。

低层 API 提供显式 coroutine 生命周期、cooperative yield/resume、bounded pool 和通用 scheduler。`coro_pool_t` 本身不增加锁或线程：create/acquire/release/destroy 由同一 owner 执行。`coro_executor_t` 在它之上建立固定 shard；每个 worker 独占一个 scheduler、一个 pool、一个有界 MPSC task queue 和一个有界 completion wake queue，用户线程只提交复制后的 descriptor，运行中的 frame 不跨 shard 迁移。

Executor 的 admission 与终态协议是：

- `submit_to` 显式保持连接或 session affinity，普通 `submit` round-robin 分片；
- `try_submit*` 在 shard queue 满时返回 `SALTS_ENOBUFS`，阻塞提交只等待 queue admission；
- 成功 admission 恰好执行 `run` 或 `cancel`，随后执行可选 `finalize`；拒绝不调用 callback；
- `shutdown` 关闭 admission 并 drain 已接收的有限 cooperative coroutine；
- `coro_executor_yield()` 主动让出当前 shard；`coro_yield()` 仍可作为低层等价入口；
- `await_begin` 为当前 frame 预留一个 generation-checked slot，外部操作提交失败时调用 `await_abort`，成功后调用 `await` 挂起；
- 任意完成线程通过 `await_complete(executor, handle, status)` 发布一次 terminal wake；它不会在调用线程直接 resume，而由原 shard owner 消费 wake queue 后恢复 frame；
- completion 可以先于 `await` 到达，也可以在 `shutdown` 关闭 task admission 后到达。重复完成返回 `SALTS_EALREADY`，已消费或已 abort 的 handle 返回 `SALTS_ENOENT`。

await slot 只保存“哪个 frame 等待、是否已有完成、完成状态是什么”；NativeIO request slot、readiness registration 或其他异步子系统仍是操作进度与 terminal result 的事实源。一个 frame 同时最多持有一个 await。调用顺序是：`await_begin` → 提交外部操作 → `await`；若提交失败则以 `await_abort` 收尾。外部 operation owner 必须保证每次成功提交最终恰好调用一次 `await_complete`，并在 `destroy` 前停止所有 completion caller。`shutdown` 无法替一个通用外部操作合成取消终态，因此遗失 completion 的 await 会按契约阻止 drain。

默认每 shard 最多保留 64 个 frame。按 minicoro 默认 128 KiB stack 与 1 KiB storage 计算，硬上限约为每 worker 8.1 MiB，尚未计入 frame metadata 和 alignment；64 位平台上的 1024-entry task queue 约持有 32 KiB descriptor，completion queue 则按 frame 上限向上取 2 的幂，因此每个 active await 最多占一个 wake entry。达到历史峰值的 frame 会被 pool 保留复用，因此长驻进程应按 `worker_count × max_capacity × (stack_size + storage_size)` 配置预算，而不是把默认值视为无成本。


## Internal batch POCs

Executor 内部保留两个只供测试/benchmark 使用的 batch POC：

- producer-side range admission：一次 mutex/claim/commit/signal 接收一组同 shard task；
- consumer-side dequeue batching：一次 mutex/release/broadcast 取出一组连续 FIFO task。

它们来自 #663/#665/#666 的性能分解，不是公开 API。实验结论是：

- owner-local / coarse one-way handoff 的收益远大于 executor 微优化；
- producer range admission 在合成 admission workload 中有明显独立收益；
- consumer dequeue batching 只有较小的二级收益，通常在 batch 16–32 已接近平台；
- 当前 production 中没有天然一次生成同 owner N 个 Coroutine Executor task 的 consumer。NativeIO Sharded 的语义边界仍是一次一个 routed task；CFlow 使用自己的 executor；CNet 普通 data plane 保持 owner-local。

因此 `coro_executor_try_submit_batch_to_internal()`、
`coro_executor_set_dequeue_batch_limit_internal()` 和相关常量只存在于
`coroutine/src/` 的 private header。它们不安装、不出现在
`coro_executor.h`，NativeIO/CNet/CFlow production 源码也不得直接依赖。
只有出现具有原生 range semantics 的真实 consumer，并有 paired end-to-end
evidence，才重新讨论 productization。

NativeIO 现有 `native_io_coroutine_await()` 仍由 backend 的单 owner 在 terminal completion 到达后恢复其私有 frame。通用 Executor 的 await token 为未来 adapter 提供跨线程完成投递 primitive，但不改变 NativeIO 当前“submit/observe 由 backend owner 推进”的约束；真正接入时，adapter 仍需把 NativeIO completion 映射到 token，不能让 token 成为第二份 I/O 结果。

依赖方向固定为：

```text
CFlow / CNet -> NativeIO -> Coroutine primitive -> vendor/minicoro

Application adapter -> Coroutine Executor -> Concurrency -> Platform
```

`cflow/minicoro` 适配目标已删除。CFlow 的 Graph/Resumable 不拥有 coroutine frame；需要异步 I/O 的 Actor、Reactive 或 CNet 层通过 NativeIO 的 operation/completion 或 coroutine owner API 工作。
