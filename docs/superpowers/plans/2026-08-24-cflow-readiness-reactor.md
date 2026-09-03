# CFlow Bounded Readiness Reactor Implementation Plan
> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为 CFlow WAIT/waker 增加有界、代际安全、可 quiescent close 的 readiness reactor adapter，并交付首个 Linux epoll 生产后端。

**Architecture:** Platform 预分配 registration slots 并拥有 native epoll/eventfd/thread；CFlow move 一个 opaque registration，负责 `read -> WOULD_BLOCK -> arm -> wake`。旧 raw readiness factory 保留。首个 native backend 由 `SALTS_ENABLE_EPOLL_READINESS` 显式启用，其他 host 明确 `SALTS_ENOTSUP`。

**Tech Stack:** ISO C11、Salts Platform thread/error primitives、Linux epoll/eventfd、CFlow Source/WAIT/waker、TinyTest、CMake Presets、GitHub Actions。

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-readiness-reactor-design.md`

## Global Constraints

- 不修改现有 `cflow_waker`、`cflow_waitable` 或旧 readiness factory ABI。
- registration/rearm 热路径不分配、不扩容；容量满返回 `SALTS_ENOBUFS`。
- 不提供 poll/select、定时扫描、thread-per-handle 或静默 fallback。
- Platform callback 和 CFlow waker 一律在锁外调用；callback 内 reactor
  shutdown 立即返回 `SALTS_EBUSY`，不等待自身 delivery 或 backend thread。
- 每个成功 register 必须恰好由 close 释放；slot 仅在 native unwatch 和 callback
  quiescence 后复用。
- `.codegraph/`、build tree 和 vcpkg installed tree 不提交。
- 每个 Task 采用 RED -> GREEN -> focused regression；提交前使用
  `superpowers:verification-before-completion`。

---

## Task 1: 固定 Platform 契约并实现有界 fake-driver state engine

**Files:**

- Create: `platform/include/salts/readiness.h`
- Create: `platform/tests/readiness_contract_suite.h`
- Create: `platform/tests/readiness_contract_suite.c`
- Create: `platform/tests/platform_readiness_fake_test.c`
- Create: `platform/src/readiness_internal.h`
- Create: `platform/src/readiness.c`
- Create: `platform/tests/readiness_fake_backend.h`
- Create: `platform/tests/readiness_fake_backend.c`
- Modify: `platform/tests/platform_header_cpp_test.cpp`
- Modify: `platform/tests/CMakeLists.txt`
- Modify: `platform/CMakeLists.txt`
- Modify: `CMakeOptions.cmake`

- [ ] **Step 1: 写公共 header compile/runtime RED tests**

  固定 opaque reactor/registration、event flags、config、stats、callback 和精确
  `int` lifecycle API。C++ header test 验证 `extern "C"` surface。

- [ ] **Step 2: 写共享 contract suite**

  suite 以 fixture factory 运行 register/arm/wake/rearm/unarm/close、full、
  duplicate、stale、resource reuse、backend error、shutdown 和 callback-close
  场景。fake fixture 必须确定性控制 dispatch，不使用 sleep。

- [ ] **Step 3: 运行 RED**

  Windows：

  ```powershell
  cmake --build --preset win-release-user --target platform_readiness_fake_test platform_header_cpp_test
  ```

  Expected: header/API 或 target 尚不存在而失败。

- [ ] **Step 4: 添加公共声明、CMake target 和 unsupported-host 行为**

  添加完整公共声明、测试 target 和 feature option 校验；不加入占位成功返回。
  未支持 native backend 的 factory 明确返回 `SALTS_ENOTSUP`。

- [ ] **Step 5: 实现 checked init-time storage**

  预分配 slot table 与 backend event batch；验证 zero、overflow、
  `UINT32_MAX` token index ceiling。所有失败保持输出 handle 为 empty。

- [ ] **Step 6: 实现 generation/state transitions**

  状态拆成 lifecycle (`FREE/OPEN/CLOSING/RETIRED`)、interest
  (`IDLE/ARMING/ARMED/UNARMING`)、delivery (`IDLE/CALLBACK`)、terminal
  (`NONE/RESERVED/DELIVERING`) 和 control (`NONE/REGISTER/ARM/UNARM/CLOSE`)。
  token 编码 index+generation；dispatch 仅在 generation 匹配且 interest 为
  `ARMED` 时提取 callback，并在 callback 之前消耗该 one-shot interest。

- [ ] **Step 7: 实现 close/quiescence 与 callback reentrancy**

  外部 unarm/close 等待 callback delivery、arm waiter 与 API borrow 归零；
  callback 内 close 与 reactor shutdown 按精确契约 fail fast。不得持锁调用
  backend 或 user callback。

- [ ] **Step 8: 实现统计与 backend fatal fan-out**

  统计只从 slot 主事实源派生；fatal error 向每个 ARMED slot 交付一次并关闭
  admission。

- [ ] **Step 9: 运行 fake suite GREEN 和相邻 Platform tests**

  ```powershell
  cmake --build --preset win-release-user --target platform_readiness_fake_test platform_clock_test platform_thread_test platform_header_cpp_test
  ctest --preset win-release-user -R "^platform_(readiness_fake|clock|thread|header_cpp)_test$" --output-on-failure
  ```

  Expected: 4/4 pass。

- [ ] **Step 10: Commit**

  ```powershell
  git add platform CMakeOptions.cmake
  git commit -m "feat(platform): add bounded readiness state engine"
  ```

## Task 2: 建模并证明 readiness 局部 safety 性质

**Files:**

- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Readiness.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Readiness.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/Readiness.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

- [x] **Step 1: 建模正交 slot facts 与纯决策**

  对 lifecycle/interest/delivery/terminal/control、bounded generation、arm 与
  shutdown admission decision 建模；只建模本期状态协议，不引入通信语义。

- [x] **Step 2: 建模 transition 与 finite trace**

  覆盖 register/arm/dispatch/unarm/close 的 commit/rollback、terminal fan-out、
  orphan retry、reclaim、API borrow 与 arm waiter 计数。

- [x] **Step 3: 证明局部 safety lemmas**

  证明每个 transition 及任意有限 trace 保持 slot invariant，one-shot 和
  terminal dispatch 互斥，callback self-rearm/shutdown fail-fast，reclaim 要求
  quiescence，generation 不回绕。

- [x] **Step 4: 证明独立 handle admission 性质**

  证明 closed gate 阻止新 control，旧 entrant 阻止 register publication，
  successful close 恢复真正 zero state，满计数仅失败且不意外关 gate。
  Slot/admission/generation/reactor gate 暂不合并为完整联合模型，不声称证明
  C 内存模型或 backend 实现。

- [x] **Step 5: 构建与审计**

  ```powershell
  lake build CMetaCFlowCalculus PhaseATests
  lake test
  rg.exe -n "\\b(sorry|axiom|admit|unsafe)\\b" CMetaCFlowCalculus/CFlow/Readiness.lean CMetaCFlowCalculus/Proofs/Readiness.lean Test/PhaseATests/Readiness.lean
  ```

## Task 3: 交付 Linux epoll native backend

**Files:**

- Create: `platform/src/readiness_epoll.c`
- Create: `platform/tests/platform_readiness_epoll_test.c`
- Modify: `platform/src/readiness_internal.h`
- Modify: `platform/CMakeLists.txt`
- Modify: `platform/tests/CMakeLists.txt`
- Modify: `CMakeUserPresets.json`
- Modify: `.github/workflows/cmeta.yml`

- [ ] **Step 1: 写 Linux native RED tests**

  使用 nonblocking pipe/socketpair 覆盖 readable、hangup、WOULD_BLOCK rearm、
  unarm、close、fd-number reuse、capacity full、backend shutdown。把 shared
  contract suite 的同一组 fixture assertions 运行在 epoll backend 上。

- [ ] **Step 2: 实现 epoll/eventfd reactor thread**

  使用 level-triggered `EPOLLONESHOT`。`epoll_event.data.u64` 保存 generation
  token；eventfd token 保留为 0。`EINTR` 重试，其他 `epoll_wait` 错误走 fatal
  fan-out。register/arm/close 路径不分配。

- [ ] **Step 3: 固定 feature option 与 unsupported host**

  Linux Release user preset 显式 `SALTS_ENABLE_EPOLL_READINESS=ON`。非 Linux
  若显式打开则 configure fail；关闭时 native init 返回 `SALTS_ENOTSUP`，fake
  suite 仍构建运行。

- [ ] **Step 4: Linux native GREEN**

  ```bash
  cmake --fresh --preset linux-release-user
  cmake --build --preset linux-release-user --target platform_readiness_epoll_test
  ctest --preset linux-release-user -R '^platform_readiness_(fake|epoll)_test$' --output-on-failure
  ```

  Expected: 2/2 pass，且 CMake cache 显示 option ON。

- [ ] **Step 5: Commit**

  ```bash
  git add platform CMakeUserPresets.json .github/workflows/cmeta.yml
  git commit -m "feat(platform): add epoll readiness backend"
  ```

## Task 4: 实现 CFlow registration Source adapter

**Files:**

- Create: `cflow/include/cflow/readiness.h`
- Create: `cflow/src/readiness.c`
- Create: `cflow/tests/cflow_readiness_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

- [ ] **Step 1: 写 adapter ownership/API RED tests**

  验证构造成功 move registration，构造失败保持 caller ownership；拒绝 managed
  value；Source cancel/destroy 各执行一次 quiescent close 和 user close。

- [ ] **Step 2: 写 WAIT/wake RED tests**

  fake readiness 驱动 `WOULD_BLOCK -> WAIT -> wake -> VALUE -> rearm ->
  VALUE_AND_DONE`。覆盖同步 arm failure、backend error、duplicate/stale wake、
  close 与 inflight callback 并发。

- [ ] **Step 3: 实现 refcounted adapter state**

  owner reference 由 Source 持有，Platform callback 持临时 reference。最后一个
  reference 按 `registration close -> user close -> free` 顺序释放。waker 在锁外
  调用且同 generation 最多一次。

- [ ] **Step 4: 保留旧 API 并更新 umbrella/header tests**

  `cflow_source_from_readiness` 行为不变；`cflow/cflow.h` 导出新 header。
  `Salts::CFlow` 将 Platform 设为 PUBLIC dependency。

- [ ] **Step 5: Windows fake GREEN**

  ```powershell
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user --target cflow_readiness_test cflow_runtime_test cflow_header_cpp_test
  ctest --preset win-release-user -R "^cflow_(readiness|runtime|header_cpp)_test$" --output-on-failure
  ```

  Expected: 3/3 pass。

- [ ] **Step 6: Commit**

  ```powershell
  git add cflow platform/CMakeLists.txt
  git commit -m "feat(cflow): adapt reactor registrations to WAIT wake"
  ```

## Task 5: 完成 race stress 与同步差分观察

**Files:**

- Modify: `platform/tests/platform_readiness_fake_test.c`
- Modify: `platform/tests/platform_readiness_epoll_test.c`
- Modify: `cflow/tests/cflow_readiness_test.c`

- [ ] **Step 1: 添加确定性 race barriers**

  用 mutex/condition barrier 固定 callback-entered、external-unarm、external-close
  顺序；验证 close 在 callback 返回前不释放资源，返回后无旧 wake。

- [ ] **Step 2: 添加 bounded stress**

  固定迭代数和 registration 数，循环 arm/cancel/rearm、slot reuse、reactor
  shutdown。每轮验证 `registered/armed/inflight` 最终为零；不以 sleep 判断正确性。

- [ ] **Step 3: 添加差分观察**

  用相同 int 序列分别运行同步 array Source 与 fake/native readiness Source，比较
  values、DONE、ERROR 和 outstanding demand。Linux native case 使用 pipe。

- [ ] **Step 4: 运行 Release 与开发 sanitizer**

  Windows fake：

  ```powershell
  cmake --build --preset win-release-user --target platform_readiness_fake_test cflow_readiness_test
  ctest --preset win-release-user -R "^(platform_readiness_fake_test|cflow_readiness_test)$" --repeat until-fail:20 --output-on-failure
  cmake --fresh --preset win-dev-user
  cmake --build --preset win-dev-user --target platform_readiness_fake_test cflow_readiness_test
  ctest --preset win-dev-user -R "^(platform_readiness_fake_test|cflow_readiness_test)$" --output-on-failure
  ```

  Linux native CI 同样对 fake/epoll/CFlow 三个测试执行重复 stress。

- [ ] **Step 5: Commit**

  ```powershell
  git add platform/tests cflow/tests
  git commit -m "test(cflow): stress readiness cancellation and reuse"
  ```

## Task 6: 文档、安装包与完整验证

**Files:**

- Modify: `cflow/README.md`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `CMakeLists.txt`
- Modify: `.github/workflows/cmeta.yml`
- Modify: `docs/superpowers/specs/2026-08-24-cflow-readiness-reactor-design.md`
- Modify: `docs/superpowers/plans/2026-08-24-cflow-readiness-reactor.md`

- [ ] **Step 1: 文档化 ownership 和 host matrix**

  写明 registration move、borrowed resource 失效点、callback thread、正常关闭
  顺序、callback 内 close、capacity formulas、错误码和 unsupported host。

- [ ] **Step 2: 扩展 installed consumer**

  安装后 consumer 编译 `<salts/readiness.h>` 和 `<cflow/readiness.h>`，链接
  `Salts::CFlow`，在 Linux 验证 epoll feature，在其他 host 验证 public
  surface 和 `SALTS_ENOTSUP`。

- [ ] **Step 3: 同步 CodeGraph 并检查影响面**

  ```powershell
  codegraph sync .
  codegraph affected -p . platform/include/salts/readiness.h platform/src/readiness.c platform/src/readiness_epoll.c cflow/include/cflow/readiness.h cflow/src/readiness.c
  git status --short
  ```

- [ ] **Step 4: Windows 全量验证**

  在 `VsDevCmd.bat`、`PROJECT_ROOT`、`VCPKG_ROOT` 环境中运行：

  ```powershell
  cmake --fresh --preset win-release-user
  cmake --build --preset win-release-user
  ctest --preset win-release-user --output-on-failure
  cmake --build --preset install-win-release-user
  cmake --build --preset win-release-user --target verify_installed_package
  ```

- [ ] **Step 5: Linux CI native 验证**

  要求 Linux Release job 证明 option ON、fake/native/CFlow stress 通过、全量
  CTest 通过、install 和 `verify_installed_package` 通过。Windows/macOS job
  证明 option OFF、fake/CFlow adapter 和 installed headers 通过。

- [ ] **Step 6: 更新计划证据并 Commit**

  只在本地与 CI 证据实际通过后勾选对应步骤，并记录命令、test count、CI run。

  ```powershell
  git add CMakeLists.txt cflow platform tests .github docs
  git commit -m "docs(cflow): complete readiness reactor verification"
  ```
