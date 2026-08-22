# CFlow Calculus Conformance Phase F-1 实施计划

> **执行方式：** 在隔离 worktree 中按 `superpowers:executing-plans` 与 TinyTest 约定逐项执行；本阶段不改公开 API。

**目标：** 把 Lean Phase A–E 已证明的执行路径边界落成 C11 可执行契约，覆盖当前已有的 Plan 与 Kernel 路径，并明确 Direct / Batch / Parallel Reduce 尚未实现，防止把紧凑 Plan 错当成 Direct。

**架构映射：** `cflow_plan_compile_surface` + `cflow_plan_eval_array` 对应 Plan；`cflow_run` / `cflow_eval_array` 对应 Kernel。Direct 要求无间接 dispatch 的专用实现，当前代码不存在，因此 F-1 只建立“不误标”边界，不伪造 Direct 证明。等待型 Source 只能通过 Kernel 验证 `WAIT -> wake -> VALUE -> DONE`。

**技术栈：** C11、CMeta typed callable、CFlow Graph/Plan/Runtime、TinyTest、CMake Presets、MSVC + Ninja。

## 约束

- 以 `origin/refactor/execution-foundation` 为基线，使用 `test/cflow-calculus-conformance-phase-f1` 独立分支。
- 不修改 Lean 形式化分支，不修改用户暂存的 `CMakeUserPresets.json`。
- 不新增公开路径选择 API，不改变 Graph、Plan、Runtime 的所有权或错误语义。
- Plan 不得对不支持的 relation 拓扑隐式回退到 Kernel。
- WAIT、唤醒、需求与终止只能由可观察状态/输出证明，不使用源码文本断言。
- Batch 与 Parallel Reduce 留给后续独立 PR；F-1 不暴露未完成能力。

## Task 1：建立独立 conformance 测试目标

**文件：**

- 新增：`cflow/tests/cflow_calculus_conformance_test.c`
- 修改：`cflow/tests/CMakeLists.txt`

- [ ] 先在 CMake 注册 `cflow_calculus_conformance_test`，验证目标因测试文件尚不存在而 RED。
- [ ] 创建 TinyTest 文件，链接 `TurboUtils::CFlow`、`TurboUtils::Platform`、`TurboUtils::TinyTest` 与既有 `CFLOW_TEST_SUPPORT`。
- [ ] 重新配置，确认目标可构建且 CTest 能独立筛选。

## Task 2：证明 Plan 与 Kernel 的观察等价

**输入：** `int[6] = {1,2,3,4,5,6}`。

**Flow：** `filter(even) -> map(square) -> map(half)`。

- [ ] 用 `cflow_plan_compile_surface` 编译，断言得到两条 instruction、两个 map callback。
- [ ] 分别通过 `cflow_plan_eval_array`（Plan）与 `cflow_eval_array`（Kernel）执行。
- [ ] 用 `cflow_result_equal` 比较类型、数量与值，并断言结果为 `{2.0, 8.0, 18.0}`。
- [ ] 独立销毁两个结果、Plan 与 Stream，证明所有权边界不混用。

## Task 3：证明 WAIT Source 只能经 Kernel 保持观察序列

**输入：** `cflow_source_from_timer(count=1, interval_ticks=5)`。

- [ ] 创建 identity Graph、deterministic scheduler、记录型 sink 与 `cflow_run`。
- [ ] 请求一个下游值并运行 ready queue，断言尚无值/终止、outstanding demand 仍为 1，表明运行停在 WAIT。
- [ ] 推进 4 ticks，断言仍无可观察输出。
- [ ] 再推进 1 tick，断言依次观察到值 `0` 与 DONE，outstanding demand 变为 0。
- [ ] 关闭 Run，并销毁 scheduler 与 Graph。

## Task 4：证明不支持的 Plan 拒绝且不隐式回退

- [ ] 构造包含结构化 relation 的合法 Flow，并生成 normalized/optimized Graph。
- [ ] 断言 `cflow_plan_graph_supported` 为 false，`cflow_plan_compile` 为 false，且没有 Plan impl。
- [ ] 对同一语义单独走 Kernel 执行并验证输出，证明 Kernel 可用但不是 Plan 编译的 fallback。
- [ ] 销毁失败 Plan 与所有 Graph/Stream 资源。

## Task 5：验证与交付

- [ ] 构建并运行 `cflow_calculus_conformance_test`。
- [ ] 运行相邻回归：`cflow_graph_test`、`cflow_pipeline_test`、`cflow_runtime_test`、`cflow_execution_test`。
- [ ] 运行全部 `^cflow_` CTest。
- [ ] 检查 `git diff --check`、分支状态与主工作区暂存 blob 未变化。
- [ ] 提交并推送独立 Phase F-1 分支；创建以 `refactor/execution-foundation` 为 base 的 PR。

## 后续阶段

- Phase F-2：设计并实现真正的 Direct executor，建立无间接 dispatch 的可测量证据。
- Phase F-3：Batch path 与 allocation/copy cost 证据。
- Phase F-4：Parallel Reduce，包含结合律/纯度准入与调度等价性。
