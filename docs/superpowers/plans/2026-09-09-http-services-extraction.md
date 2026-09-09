# HTTP Services 仓库迁移实施计划

> **For agentic workers:** 使用 superpowers:executing-plans 在当前会话逐项执行。用户已授权 CHTTP、S3、CRPC 一起迁移。

**Goal:** 在同级 `http-services` 创建独立 Git 仓库，移入三个模块并通过安装后的 Salts SDK 构建。

**Architecture:** HTTPServices → Salts 单向依赖。S3 与 CRPC 依赖 CHTTP；CNet、CFlow、NativeIO、解析器、序列化与通用 Crypto 留在 Salts。HTTP 私有 cjwt/turbo_crypto 随模块迁移。

**Tech Stack:** C11/C++17、CMake user presets、Ninja、vcpkg、TinyTest。

**Spec:** 用户在本次会话确认的三模块迁移方案；具体边界见下文。

## 约束与决策

- 保留头文件路径、C API、错误语义、内存所有权、进度 owner 和协议算法。
- 保留 `Salts::CHTTP`、`Salts::S3`、`Salts::CRPC` 目标名及库文件名/ABI；由 `find_package(HTTPServices)` 提供。Salts 不再提供这三个目标。
- 候选方案：只迁移 CHTTP/S3 会使保留的 CRPC 引入包循环；拆分 CRPC transport 会改变接口且增加迁移成本。本次选择三个模块整体迁移，避免运行时重构。
- HIGH 兼容性风险：旧消费端只查找 Salts 将缺少三个目标；必须增加 HTTPServices 包查找。旧 Salts SDK 含有这些目标时明确拒绝，避免同时装载两套实现。
- MED 构建风险：私有库、测试与安装检查依赖旧源码树路径；迁移专属依赖及检查，采用已安装 Salts 的公开目标验证。
- 状态归属与失败：只迁移源码/包归属，不迁移业务数据。先保存来源 commit、验证迁移内容，再验证两个包；失败保留可审查工作区，不发布半成品。
- 回滚：使用原 Salts commit 在独立 checkout 恢复旧构建；新仓库保留来源记录，避免覆盖工作区与安装目录。消费端切回旧包组合。
- 不新增网络协议或外部依赖。上游授权与本地修改记录随 vendored 源码保留。

## Task 1: 建立独立包与迁移源码

**Files:** 新仓库根 CMakeLists.txt、CMakePresets.json、CMakeUserPresets.json、cmake/、vcpkg.json、README.md；移动 chttp/、s3/、crpc/、vendor/cjwt/、vendor/turbo_crypto/。

- [x] 执行原树 `ctest --preset win-release-user -R "^(chttp_|s3_|crpc_)" --output-on-failure` 记录基线。
- [x] 校验绝对源/目标路径及目标不存在，创建本地 Git 仓库并记录来源 commit；移动上述目录。
- [x] 新包通过验证后的 SALTS_ROOT 查找 Salts；独立导出 HTTPServicesTargets，复用原 CMake helpers 与编译选项。
- [x] 提供 Windows/Linux Debug/Release user presets、显式安装入口及独立 vcpkg manifest。

## Task 2: 切断旧包边界并迁移安装验证

**Files:** Salts CMakeLists.txt、vendor/CMakeLists.txt、vcpkg.json、tests/install_consumer/、cmake/VerifyInstalledPackage.cmake、README.md、ARCHITECTURE.md；新仓库 tests/install_consumer/、cmake/VerifyInstalledPackage.cmake、README.md。

- [x] 移除 Salts 对三个模块及私有 crypto 的构建引用，移除 HTTP 专属 llhttp manifest 依赖。
- [x] 拆分现有 C/C++ 安装消费测试；Salts 保留 CNet 测试，新包保留 HTTP/S3/CRPC 测试。
- [x] 两个包分别验证安装导出；新包验证公开目标链接、运行及第三方私有依赖不泄漏。
- [x] 更新模块导航、当前架构、迁移步骤与相关设计文档归属。

## Task 3: 验证与交付

- [x] 使用 VsDevCmd 环境执行 Salts Release configure、安装和 verify_installed_package。
- [x] 使用 VsDevCmd 环境执行 HTTPServices Release configure、完整构建、完整 CTest、安装与安装消费验证。
- [x] 执行 Salts CNet 相邻回归，确认不依赖 HTTPServices。
- [x] 比较迁移前后生产源码及公开头文件；执行 git diff --check，核对新仓库不包含构建/索引产物。
- [x] 报告实际测试结果、迁移路径与未验证平台；保留可审查改动，不创建远程仓库或推送。

## 验证结果（2026-09-09，Windows x64 Release）

- 迁移前：42 项 CHTTP/S3/CRPC/JWT 测试通过。
- 迁移后：HTTPServices 完整构建成功，43 项测试通过（含 turbo_crypto）。
- HTTPServices 安装消费：禁用私有第三方包查找，重复 find_package 成功，5 项 C/C++ 程序编译运行通过。
- Salts：独立安装消费验证通过，22 项 CNet 回归通过；安装消费明确拒绝导出已迁出的三个目标。
- SALTS_ROOT 无效或内容不完整时明确失败；旧缓存 Salts_DIR 不能绕过指定根。
- 独立审查发现预加载 SDK A 后要求 SDK B 可能保留旧 targets；已复现并增加实际 imported artifact 根校验，自动负向回归通过，复核通过。
- 591 个模块/vendor 文件无遗漏；生产实现、头文件与测试源码保持不变，仅三个模块构建定义及两个 README 改动。
- 两个 SDK 已分别安装到 PKG_ROOT/salts/release 与 PKG_ROOT/http-services/release。
- Linux、Android、Debug/ASan 未在本次验证。
