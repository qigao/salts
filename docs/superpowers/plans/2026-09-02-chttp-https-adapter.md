# CHTTP HTTPS Adapter Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** 复用 CNet TLS 为 CHTTP HTTP/1.1 客户端、keep-alive 连接池和服务端提供 fail-closed HTTPS。

**Architecture:** CNet 提供初始化期构造的不可变客户端 TLS profile；CHTTP 用引用计数 profile 表达连接池安全域，服务端拥有一个 CNet TLS server context。HTTP parser、路由、中间件与 Session 不感知加密传输。

**Tech Stack:** ISO C11、CNet、OpenSSL（仅经 CNet）、llhttp、TinyTest、CMake Presets、MSVC/Ninja。

**Spec:** `docs/superpowers/specs/2026-09-02-chttp-https-adapter-design.md`

---

### Task 1: 用失败测试固定 CNet 可复用 TLS profile 契约

**Files:**
- Modify: `cnet/tests/cnet_tls_test.c`
- Modify: `cnet/tests/cnet_api_test.c`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Steps:**

1. 添加 profile init/destroy 参数、重复生命周期与 ALPN/证书配置校验测试。
2. 添加 `cnet_connect_options.tls` 与 `tls_client` 互斥、非 TLS URI 拒绝测试。
3. 添加真实 listener/client 测试：connect 接纳后销毁 public profile wrapper，TLS 握手和数据收发仍成功。
4. 构建 `cnet_tls_test cnet_api_test cnet_header_cpp_test`，确认编译因缺失公开 API 失败（RED）。
5. 只实现使该契约通过的 CNet profile API与 connect 分支。
6. 重跑三个测试（GREEN）。

### Task 2: 用失败测试固定 CHTTP TLS profile 与连接池键

**Files:**
- Create: `chttp/tests/chttp_tls_test.c`
- Modify: `chttp/tests/CMakeLists.txt`
- Modify: `chttp/tests/chttp_api_test.c`
- Modify: `chttp/tests/chttp_header_cpp_test.cpp`

**Steps:**

1. 添加 self-signed localhost 证书 fixture，仅写入 TinyTest 临时文件。
2. 添加 profile 生命周期、只允许 H1 ALPN、plain URI + profile 拒绝测试。
3. 添加同一 profile 两次顺序 GET 只产生一个 accepted connection 的测试。
4. 添加两个独立 profile 即使配置相同也产生两个 TLS pool 安全域的测试。
5. 构建相关测试并确认缺失 API/行为导致失败（RED）。
6. 实现 `chttp_tls_profile`、slot retain/release、`tls://` 接纳和三元池键。
7. 将同步 `chttp_options.tls` 原样映射到高级请求选项。
8. 重跑相关测试（GREEN）。

### Task 3: 用失败测试固定 CHTTP HTTPS server 生命周期

**Files:**
- Modify: `chttp/tests/chttp_tls_test.c`
- Modify: `chttp/tests/chttp_server_test.c`

**Steps:**

1. 添加 server TLS config 初始化失败、TLS buffer/handshake timeout 缺失和 H2 ALPN 拒绝测试。
2. 添加 server/client HTTPS 端到端路由测试；在 `chttp_server_init()` 后删除证书临时文件，证明 server context 不借用配置。
3. 覆盖中间件与 Session 至少各一条 HTTPS 请求路径。
4. 运行测试确认 server 尚走明文 accept 而失败（RED）。
5. 在 `chttp_server_impl` 中拥有 CNet TLS server context，TLS 模式使用 `cnet_listener_accept_tls()`。
6. 保证所有 init/start/stop/destroy 失败路径恰好释放一次 context。
7. 重跑 server 与 TLS tests（GREEN）。

### Task 4: 同步公开文档与安装消费面

**Files:**
- Modify: `cnet/README.md`
- Modify: `chttp/README.md`
- Modify: `docs/CHTTP_CNET_PROTOCOL_TODO.md`
- Modify: `book/chapters/13-networking.md`（若实际章节路径不同，先用 `fd.exe`/`rg.exe` 定位）
- Modify: `tests/install_consumer/consumer.c`
- Modify: `tests/install_consumer/consumer.cpp`

**Steps:**

1. 文档说明 `tls://`、profile 生命周期、连接池三元键、H1-only ALPN、server TLS/mTLS 和错误边界。
2. 把 HTTPS 从“未接入”清单移至已完成；保留 H2、WebSocket Upgrade、S3、H3 的真实状态。
3. 书稿按当前 API 更新，不描述历史改名或实验开关。
4. installed C/C++ consumer 编译引用新增公开结构与生命周期 API。

### Task 5: 格式化、回归与交付检查

**Steps:**

1. 对修改的 C/C++ 文件运行仓库 `clang-format`。
2. 构建并运行 `cnet_*`、`chttp_*`、`crpc_*` focused tests。
3. 运行 `verify_installed_package`。
4. 运行 Release 全量 build 与 229+ CTest。
5. 如 Release 通过，使用 `win-dev-user` 对新 TLS tests 跑 ASan focused 验证。
6. 运行 `codegraph sync .`、`git diff --check`、旧限制文字检索和 `git status --short`。
7. 进行最终 code review；修复发现后重跑受影响验证，才准备 commit/PR。
