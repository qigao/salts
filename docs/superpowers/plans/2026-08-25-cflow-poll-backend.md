# CFlow Explicit POSIX poll Backend Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 增加显式、bounded、无 fallback 的 POSIX poll Platform/CFlow native I/O backend。

**Architecture:** Platform 新增 backend kind selector 和单 worker poll backend，复用已有 readiness state engine 的 registration、generation、terminal 与 shutdown 语义。CFlow 把新 kind 映射到同一个 readiness socket-lane adapter，因此 TCP、UDP、accept/connect、取消和 Actor ownership 不复制状态机。

**Tech Stack:** C11、POSIX poll/pipe/fcntl、Salts Platform threads/readiness、CFlow I/O Actor、TinyTest、CMake Presets、GitHub Actions。

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-poll-backend-design.md`

## Global Constraints

- 旧 Platform 默认 initializer 与旧 CFlow backend enum 数值保持不变。
- poll 只在 POSIX 编译，显式选择不支持的 kind 返回 `SALTS_ENOTSUP`，绝不 fallback。
- Platform records、worker 快照和 CFlow lanes 都固定容量；数据面不 realloc。
- caller 始终拥有 descriptor；backend 只借用，shutdown/destroy 只释放自有控制资源。
- token/generation 是 stale snapshot、rearm、close 与 descriptor reuse 的唯一权威 identity。
- 每个生产改动先有能因缺少该行为而失败的真实测试。

---

### Task 1: Additive Platform and CFlow selector contract

**Files:**
- Modify: `platform/include/salts/readiness.h`
- Modify: `platform/src/readiness.c`
- Modify: `platform/src/readiness_internal.h`
- Modify: `platform/tests/platform_readiness_unsupported_test.c`
- Modify: `platform/tests/platform_header_cpp_test.cpp`
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/src/io_native.c`
- Modify: `cflow/src/io_native_internal.h`
- Modify: `cflow/tests/cflow_io_native_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Produces: `salts_readiness_backend_kind`, `salts_readiness_backend_supported()`, `salts_readiness_reactor_init_kind()`, `CFLOW_IO_NATIVE_POLL`。
- Preserves: `salts_readiness_reactor_init()` 的平台默认选择。

- [ ] **Step 1: 写 selector 与 unsupported 的失败测试**

在 Windows 测试中断言 poll compile-time unsupported 且显式 init 返回
`SALTS_ENOTSUP`、清空非零 reactor；在 C/C++ header test 中引用两个 additive enum。

- [ ] **Step 2: 运行 focused build 并记录 RED**

```powershell
cmake --build --preset win-release-user --target platform_readiness_unsupported_test platform_header_cpp_test cflow_io_native_test cflow_header_cpp_test
```

预期：缺少 enum/function 声明导致编译失败，而不是环境或链接错误。

- [ ] **Step 3: 实现最小 selector**

`salts_readiness_reactor_init_kind()` 对已编译的 epoll/kqueue 调现有 factory，其他 kind
返回 `SALTS_ENOTSUP`；default initializer 调用同一 selector。CFlow 追加 enum、supported
分支和 readiness init case，但 poll 尚未编译时保持 unsupported。

- [ ] **Step 4: 运行 focused tests GREEN 并提交 selector slice**

```text
git commit -m "feat(platform): add explicit readiness backend selection"
```

---

### Task 2: Bounded POSIX poll Platform backend

**Files:**
- Create: `platform/src/readiness_poll.c`
- Create: `platform/tests/platform_readiness_poll_test.c`
- Modify: `platform/src/readiness_internal.h`
- Modify: `platform/CMakeLists.txt`
- Modify: `platform/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `salts_readiness_backend_ops` 与 `salts_readiness_reactor_init_backend()`。
- Produces: internal `salts_readiness_poll_init()`；固定 records/snapshot/control-pipe worker。

- [ ] **Step 1: 写 real pipe/socket poll contract tests**

复用 `readiness_backend_contract.c`，factory 必须通过
`salts_readiness_reactor_init_kind(..., SALTS_READINESS_BACKEND_POLL)` 初始化；额外覆盖
write readiness、POLLHUP/POLLNVAL 映射、descriptor close/reuse 和 blocked callback shutdown。

- [ ] **Step 2: 在 POSIX CI/host 构建中记录 RED**

预期：explicit poll init 为 `SALTS_ENOTSUP` 或 test target 缺 source。

- [ ] **Step 3: 实现固定数组 worker 和 control pipe**

实现 register/arm/unarm/close/shutdown/destroy hooks。每次控制先 stage record 并增加
`controls_pending`，解锁后写 control pipe，再在锁内 commit/rollback 并唤醒等待 worker；
worker 只在 pending 为零时构造 snapshot。每次 arm 保存 registration/arm token，dispatch
前重新核对 generation。control write 的 `EAGAIN` 视为 coalesced success；shutdown wake、
等待 thread_exited、join。

- [ ] **Step 4: 运行 Platform focused contract GREEN 并提交 backend slice**

```text
git commit -m "feat(platform): add bounded POSIX poll readiness backend"
```

---

### Task 3: Route the shared CFlow native socket contract through poll

**Files:**
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/src/io_native_readiness.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: `CFLOW_IO_NATIVE_POLL` 和 Platform explicit selector。
- Produces: poll parity for TCP recv/send, UDP recv/send, TCP accept/connect and cancellation。

- [ ] **Step 1: 添加 shared backend helper 的 poll 调用并记录 RED**

POSIX test 必须运行 `native_check_backend(POLL)`，并覆盖 readiness worker ownership、
queued follower cancellation、socket-scoped forget；Linux 再覆盖 cancelled slot reuse。

- [ ] **Step 2: 显式映射 CFlow kind 到 Platform kind**

`cflow_io_native_readiness_init()` 用 switch 校验编译能力并调用
`salts_readiness_reactor_init_kind()`；CMake 在全部 POSIX 编译一次 shared adapter，并按平台
同时声明 poll+epoll 或 poll+kqueue。

- [ ] **Step 3: 运行 CFlow native/readiness 相邻测试 GREEN 并提交 integration slice**

```text
git commit -m "feat(cflow): route native socket IO through poll"
```

---

### Task 4: Benchmark, capability matrix, documentation and CI

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Modify: `.github/workflows/cflow-release-benchmarks.yml`
- Modify: `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: `CFLOW_NETWORK_BACKEND=poll`。
- Produces: JSON `backend="poll"` 与独立 Linux poll host artifact。

- [ ] **Step 1: 先写 benchmark selector 失败测试**

在已有 benchmark test suite 中断言文本 `poll` 映射到 exact enum/name；缺少分支时测试失败。

- [ ] **Step 2: 加入 selector、能力矩阵和 O(n) 限制说明**

文档明确 socket 操作支持、pipe/file/device 不属于本 Issue、poll 不是 fallback，并写出
`2 * request_capacity` registration 与 O(n) snapshot scan。

- [ ] **Step 3: 增加 Linux poll matrix row**

复制 Ubuntu 24.04 host 配置并把 `network_backend` 设为 `poll`。现有 exact backend report
校验保证 fallback 或错误报告直接失败；macOS native test 在同一 binary 内运行 poll contract。

- [ ] **Step 4: 运行 reduced benchmark 并提交 evidence slice**

```text
git commit -m "bench(cflow): track explicit poll backend evidence"
```

---

### Task 5: Verification, self-review and PR

**Files:**
- Review: all files changed from `origin/master`

**Interfaces:**
- Produces: reproducible local evidence, clean commits, linked PR closing #103。

- [ ] **Step 1: 运行 CodeGraph affected 与 diff review**

检查公开 ABI、default selector、hook rollback、锁内无 callback/I/O（control pipe 是有界
nonblocking control effect）、shutdown join、checked allocation 和 stale generation。

- [ ] **Step 2: 运行 focused、相邻与 Windows Release 全量测试**

Windows 必须保持 141+ tests 全绿并断言 poll unsupported；POSIX 行由 PR CI 运行真实 poll。

- [ ] **Step 3: 检查 source hygiene**

运行 `git diff --check`，确认无临时标记、占位实现或隐式 fallback。

- [ ] **Step 4: 推送并创建 PR**

PR 标题使用 `feat(cflow): add explicit portable poll backend`，正文包含 `Closes #103`、
父 Issue #100、所有权/容量/关闭摘要与验证证据。

- [ ] **Step 5: 监控 CI 到 terminal**

任何 Linux/macOS poll failure 视为真实回归；先写/收紧失败测试，再修实现，不把 poll job
改成可选 skip。
