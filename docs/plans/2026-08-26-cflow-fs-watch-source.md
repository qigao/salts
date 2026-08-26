# CFlowFS Typed Watch Source Implementation Plan

> Issue: [#116](https://github.com/qigao/turbo-utils/issues/116)

**Goal:** 将原生文件监听的有界事件队列无轮询地适配为 typed `cflow_source`，供 Run、Graph 与 Actor 复用。

**Architecture:** `cflow_fs_watch` 保持低层公开 ABI 不变，只在 CFlowFS 库内增加“队列可读/后端终止”通知。新的 Source 适配器把借用的 watcher event 经调用方 encoder 写入一个 trivial CMeta value；Source 和外部 owner 各持一份共享状态引用，Source 销毁只请求关闭，owner 在 Source 已销毁且 watcher 静止后完成 drain/destroy。

**Tech Stack:** C11、CMeta type descriptor、CFlow Source/Waitable、Turbo mutex/atomic、TinyTest、CMake Presets。

---

## 状态与协议

- 事实源：`cflow_fs_watch` 的有界队列和 rescan 控制状态；适配器不维护第二份事件队列。
- 值所有权：watch callback 中的路径仍为借用；encoder 必须在回调内复制到 `out_value`，输出类型必须满足 trivial copy/destroy。
- 线程拓扑：原生 backend 是 producer；CFlow driver 是唯一 consumer；waker 可以从 backend 线程调用。
- 背压：沿用 watcher 的固定 `event_capacity`，满额进入现有 generation-safe `RESCAN_REQUIRED`，不扩容、不丢失恢复信号。
- 关闭顺序：停止 Source admission → Source destroy 请求 watcher close，并等待已取出的 waker 返回 → owner 重试 close/drain → backend done、队列空且 readiness 通知全部返回 → watcher destroy → 释放 Source/owner 状态。
- 错误语义：非法 descriptor/config 返回 `TURBO_EINVAL`/`TURBO_ENOTSUP`；encoder 失败成为 `CFLOW_STEP_ERROR`；并发 driver 或过早 owner close 返回 `TURBO_EBUSY`。

### Task 1: 锁定公开 Source 契约（RED）

**Files:**
- Create: `cflow-fs/tests/cflow_fs_watch_source_test.c`
- Modify: `cflow-fs/tests/CMakeLists.txt`

1. 写编译级与行为测试，声明 `cflow_fs_watch_source_owner`、config、encoder、open、owner close API。
2. 覆盖无效 descriptor/config，失败时调用者输出保持原值。
3. 通过 `win-release-user` 配置并构建目标，确认因 API 尚不存在而失败。

### Task 2: 实现最小 typed Source（GREEN）

**Files:**
- Create: `cflow-fs/include/cflow/fs_watch_source.h`
- Create: `cflow-fs/src/fs_watch_source.c`
- Modify: `cflow-fs/CMakeLists.txt`

1. 定义 encoder 回调和 additive public constructor/owner close API。
2. 实现 Source/Waitable、single-consumer event encoding、encoder error 和双引用生命周期。
3. 仅实现测试要求的契约，运行目标测试直到通过。

### Task 3: 消除 native publish 到 CFlow waker 的丢失唤醒（RED → GREEN）

**Files:**
- Modify: `cflow-fs/src/fs_watch_internal.h`
- Modify: `cflow-fs/src/fs_watch.c`
- Modify: `cflow-fs/src/fs_watch_source.c`
- Modify: `cflow-fs/tests/cflow_fs_watch_source_test.c`

1. 先写真实临时目录测试：Source 返回 WAIT 并 arm 后，文件创建必须触发一次 wake，无需调用 polling driver。
2. 确认测试因没有 native notification 而失败。
3. 增加私有 notified-open、队列 ready 查询与 out-of-lock 通知；成功 publish、首次 rescan marker 和 backend done 都通知。
4. Source arm 在共享锁内注册 waker，并重新检查 watcher readiness/termination；publisher 在 watcher 锁外提取并调用 waker，验证 before-arm / during-arm 两类竞态不会丢唤醒。

### Task 4: 验证执行与关闭协议（RED → GREEN）

**Files:**
- Modify: `cflow-fs/tests/cflow_fs_watch_source_test.c`
- Modify: `cflow-fs/src/fs_watch_source.c`

1. 写 `cflow_run` 行为测试，验证 typed event 经 Source 进入 sink，路径内容由 encoder 复制且在 callback 后仍有效。
2. 写 encoder failure 测试，验证 Run 以明确错误终止。
3. 写 owner ordering 测试：Source live 时 close 返回 `TURBO_EBUSY`；Source destroy 后 close 在尚未静止时返回 `TURBO_EBUSY`，最终 drain/destroy 成功并归零。
4. 写 cancel/wake 测试，验证 backend done 会唤醒等待中的 Source 并终止。

### Task 5: 文档、头文件兼容与验证

**Files:**
- Modify: `cflow-fs/README.md`
- Modify: `cflow-fs/tests/cflow_fs_header_cpp_test.cpp`

1. 文档化 encoder 借用边界、输出类型、容量/背压、Source/owner 关闭顺序和 Run 示例。
2. 增加 C++17 头文件 API 类型检查。
3. 依次运行：新测试目标、现有 watcher 测试、CFlow runtime/readiness 相关测试、CFlowFS 全部测试、Release 全量回归、install preset 与安装消费验证。
4. 用 CodeGraph affected/impact 和 `git diff --check` 复核影响面；完成 review 后提交、推送并创建关联 #116 的 PR。
