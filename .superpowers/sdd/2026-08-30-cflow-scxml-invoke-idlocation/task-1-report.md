# Task 1 报告：native Statechart stable transaction hook ABI v3

## 状态

DONE

## 修改文件

- `cflow/include/cflow/statechart_runtime.h`
- `cflow/src/statechart_runtime.c`
- `cflow/tests/cflow_statechart_runtime_test.c`
- `.superpowers/sdd/2026-08-30-cflow-scxml-invoke-idlocation/task-1-report.md`

## 设计与所有权

- Machine 当前 `published` buffer 仍是唯一可变状态事实源。v3 hook 运行前，runtime 使用既有 `copy_staging_buffers` 把 configuration、history、completion 和 managed extended state copy-construct 到另一 buffer。
- `published_state` 是 callback-scoped 只读借用；`staged_state` 是 runtime 独占、已构造的可变副本。callback 不得保留指针，也不得 move、destroy 或替换 managed value 的所有权。
- `raise_internal` 复用既有有界 `stage_internal_event`，成功后 Event payload 由 runtime 复制持有；`stage_effect` 复用既有 move-only effect journal，成功 staging 后 ticket 所有权转移给 runtime。
- `NOOP` 销毁 staged managed state；若已经 stage Event/effect，则作为 hook contract violation 整体失败。`FATAL`、copy 失败、无效 ticket、journal 满及其他 contract violation 均清空 staged Event、按序 discard 已接受 ticket、销毁 staged state，并保持 published state/version 不变。
- `COMMIT` 在 instance lock 下重新检查 terminal/cancel winner，随后提交 internal FIFO、切换 `published` 并递增 publication/configuration version；解锁后才按 staging 顺序 commit effect tickets。取消先赢时 state/Event 全部回滚，ticket 按序 discard。
- ABI v1 固定前缀为 `offsetof(cflow_statechart_runtime_hooks, on_event)`；ABI v2 固定前缀为 `offsetof(cflow_statechart_runtime_hooks, on_stable_transaction)`；ABI v3 要求完整新结构。复制长度为 `min(struct_size, sizeof(runtime copy))`，并显式清零旧版本不可见尾字段，因此 v1/v2 不进入 transaction copy 路径。

## 红测命令与失败证据

命令：

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_statechart_runtime_test"
```

结果：exit code `1`。预期失败证据包括：

- `error C2061: syntax error: identifier 'cflow_statechart_stable_transaction_result'`
- `error C2039: 'on_stable_transaction': is not a member of 'cflow_statechart_runtime_hooks'`
- `error C2065: 'CFLOW_STATECHART_RUNTIME_HOOKS_ABI_V3': undeclared identifier`

失败原因是生产 API 尚无 v3 类型、版本常量和尾字段，不是测试拼写、链接或环境错误。

## 绿测命令与完整结果

Focused build + CTest：

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_statechart_runtime_test && ctest --preset win-release-user -R ^cflow_statechart_runtime_test$ --output-on-failure"
```

结果：build exit code `0`；`1/1` passed，`0` failed，CTest 总耗时 `0.16 sec`。

完整 preset build + CTest：

```powershell
cmd /c "call \"C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat\" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure"
```

结果：build exit code `0`；`198/198` passed，`0` failed，CTest 总耗时 `20.17 sec`。其中 `cflow_statechart_runtime_test`、`cflow_statechart_runtime_adapter_test` 与 `cflow_header_cpp_test` 均通过。

## Commit

- 实现与测试：`9a6d4727721ce006c41c5486a1b1fbf2003d05cc` (`feat(cflow): add stable transaction hooks ABI v3`)
- 本报告由紧随其后的文档 commit 提交；最终 SHA 记录在任务 handoff 中（Git commit 无法在其自身内容中自引用最终 SHA）。

## Self-review

- `HIGH` / `事实`：未发现。逐条检查所有 COMMIT/rollback/cancel 分支，已接受 ticket 在 count 清零后恰好执行一次 commit 或 discard；测试覆盖正常顺序、FATAL 顺序、journal 满与取消胜出。
- `MED` / `事实`：未发现。v1 short、v1 old-full、v2 old-size、v3 full、未知版本、v3 short、v3 双 stable hook 均有行为测试；managed state 覆盖 legacy 无额外 copy、v3 COMMIT 平衡与 stable copy failure 清理。
- `LOW` / `事实`：独立 reviewer 未运行，因为 Task brief 明确禁止派生任何子代理；已用 `git diff --check`、CodeGraph affected、focused CTest、public C++ header build 和完整 198-test preset 自审替代。
- Mutation check：删除 version increment 会破坏 NOOP/COMMIT version 断言；提前 commit ticket 会破坏 effect 对 published state 的观察；漏掉 rollback 会破坏 state/configuration/internal queue 与 discard trace 断言；读取旧 ABI 尾字段会破坏真实短 legacy struct 测试。

## Concerns

- 无功能性 concern。
- 流程性限制：按任务要求未派生独立 code-review 子代理；完整 self-review 与全量验证均已完成。
