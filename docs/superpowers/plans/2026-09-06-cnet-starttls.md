# CNet Same-Connection TLS Upgrade Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 为 CNet 增加有界、fail-closed 的同连接 TCP 到 TLS 升级能力，解除 SaltsNet 邮件协议迁移对 CoroNet 的依赖。

**Architecture:** 公开 client 负责同步准入和记录级互斥，固定大小升级命令把已保留的 TLS context 交给现有 shard owner。owner 在原 transport/session 上切换到既有 TLS state/pump，沿用 deadline、NativeIO、事件队列和 terminal 清理路径，不建立第二连接或第二状态源。

**Tech Stack:** C11、CNet、NativeIO、BoringSSL OpenSSL-compat API、CMake Presets、TinyTest。

**Spec:** `docs/superpowers/specs/2026-09-06-cnet-starttls-design.md`

**Global Constraints:** 先写失败测试；所有容量有硬上限；配置同步消费；握手失败关闭且不降级；保持已有 `tls://` 与 accept-TLS 行为；修改后用 release preset 执行定向及相邻回归。

### Task 1: 固化状态与命令契约

**Files:**
- Modify: `cnet/tests/cnet_session_test.c`
- Modify: `cnet/tests/cnet_command_test.c`
- Modify: `cnet/src/cnet_session.c`
- Modify: `cnet/src/cnet_command.h`
- Modify: `cnet/src/cnet_command.c`
- Modify: `cnet/src/cnet_owner.h`

1. 写 `OPEN -> PROTOCOL_HANDSHAKING -> OPEN` 的失败测试。
2. 写 TLS upgrade 固定 payload 的命令复制/校验失败测试。
3. 运行两组测试确认 RED。
4. 最小实现状态迁移和命令类型，运行测试确认 GREEN。

### Task 2: 固化公开准入语义

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/src/cnet_shards.h`
- Modify: `cnet/src/cnet_shards.c`
- Modify: `cnet/tests/cnet_api_test.c`

1. 写参数、TLS-disabled、非 TCP、未连接、重复升级、pending send/receive/close 的失败测试。
2. 增加 versioned client upgrade options、client/server API 和 handshaking 状态。
3. 在 record 中维护 pending receive 与 upgrade 状态，成功准入后立即关闭新收发。
4. shard 只允许 OPEN session 接受 upgrade，并转移 context 引用。
5. 运行 API 测试确认 GREEN。

### Task 3: 在 owner 原地切换 TLS

**Files:**
- Modify: `cnet/src/cnet_event.h`
- Modify: `cnet/src/cnet_event.c`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/tests/cnet_owner_test.c`
- Modify: `cnet/tests/cnet_tls_test.c`

1. 写 loopback 双端先明文、后原地 TLS 的失败测试。
2. owner 校验 session quiescence，发布 handshaking，然后复用 `cnet_owner_start_tls()`。
3. 统一 context 在成功转移、命令拒绝、握手失败、close/stop 路径的释放。
4. 覆盖证书校验、升级后收发、静默 peer 超时和不降级。
5. 运行 owner/TLS/API 回归确认 GREEN。

### Task 4: 文档、格式与安装验证

**Files:**
- Modify: `cnet/README.md`
- Modify: `cnet/include/cnet/cnet.h`

1. 记录准入、回调顺序、所有权、backpressure 与 fail-closed 行为。
2. 对改动 C/C++ 文件运行仓库格式化检查。
3. 运行 CNet 定向和相邻 CTest。
4. 运行 `git diff --check` 并审查 diff。
5. 用 `install-win-release-user` 安装到 preset 指定的 Salts release SDK。
