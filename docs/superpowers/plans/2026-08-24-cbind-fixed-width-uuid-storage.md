# CBind Fixed-Width Integer and UUID Storage Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development for every behavior change and superpowers:verification-before-completion before commit.

**Goal:** 让 TurboUtils CBind 安全解码 descriptor-driven 8/16/32/64 位整数，并提供可供 TurboParser 使用的 fixed-width/UUID header-local CMeta metadata。

**Architecture:** CBind 继续拥有格式无关 scalar conversion；CMeta descriptor 是 native storage 事实源。整数在 preflight 验证 width/size/alignment，在 64-bit token 域校验后经 exact-width temporary + `memcpy` 提交。UUID 作为 owned STRING adapter 同步解析 bounded slice 到固定 16-byte value。

**Tech Stack:** C11、C++17 header consumer、TurboUtils CMeta/CSerde/CBind/Core、TinyTest、CMake user presets、MSVC Release。

**Spec:** `docs/superpowers/specs/2026-08-24-cbind-fixed-width-uuid-storage.md`

## Global constraints

- 不改变 CBind public ABI、status enum、CSerde token protocol 或 legacy scalar 行为。
- 不修改 `vendor/`，不提交 `.codegraph/`，不修改原始 dirty TurboUtils worktree。
- 所有失败在写 destination 前判定；descriptor 错误在消费 input 前 fail fast。
- 不给 CBind 添加 Core 依赖；UUID adapter 保持 header-local、无分配、bounded。
- production code 前必须存在能够观察 missing behavior 的 RED test。

## Task 1: Fixed-width integer RED

**Files:**

- Modify: `cbind/tests/cbind_scalar_decode_test.c`

1. 定义测试专用、语义 identity 独立的 8/16/32/64 signed/unsigned type/data descriptors；期望值使用 `<stdint.h>` literal limits，不复用 production helper。
2. 添加 table/scenario tests，覆盖 min/max、signed/unsigned token crossover、negative-to-unsigned、one-past narrow ranges、integral float、fractional/NAN/INFINITY、destination unchanged。
3. 构造 wrong bits、size、alignment、storage kind descriptors，断言 `CBIND_INVALID_SHAPE` 且 reader 未消费。
4. 保留并扩展 legacy `int/long/size_t` assertions。
5. 构建并运行：

   ```powershell
   cmake --build --preset win-release-user --target cbind_scalar_decode_test
   ctest --preset win-release-user -R '^cbind_scalar_decode_test$' --output-on-failure
   ```

   预期 RED：合法 noncanonical fixed-width descriptor 当前返回 `CBIND_UNSUPPORTED`。

## Task 2: Fixed-width integer GREEN

**Files:**

- Modify: `cbind/src/scalar.c`

1. 把 canonical integer identity admission 改为 descriptor-driven validation：`CHAR_BIT == 8`、bits 集合、integer kind、exact size、exact alignment。
2. 让 integer zero/read/reset 按 bits 使用 exact `intN_t/uintN_t` temporary 和 `memcpy`，不越界。
3. 把 signed/unsigned token conversion 按 bits dispatch；先校验，再提交。保留 float strictness 与 status mapping。
4. 运行 Task 1 focused command，预期 GREEN；再运行 CBind 相邻测试：

   ```powershell
   ctest --preset win-release-user -R '^cbind_' --output-on-failure
   ```

## Task 3: Header-local fixed-width metadata and UUID RED

**Files:**

- Modify: `utils/tests/test_turbo_cmeta_data.c`
- Modify: `utils/tests/test_turbo_cmeta_data_cpp.cpp`

1. 先引用期望公开名称并验证 fixed-width type/data descriptor 的 kind、bits、size、align、semantic identity。
2. 添加 UUID tests：16-byte storage，lower/uppercase canonical slice，无 NUL 终止，错误长度/hyphen/hex，max-buffer，occupied destination，失败恢复全零，restore 幂等。
3. C++ test 编译期验证 metadata immutable、UUID size 和 fixed-width surface。
4. 构建：

   ```powershell
   cmake --build --preset win-release-user --target test_turbo_cmeta_data test_turbo_cmeta_data_cpp
   ```

   预期 RED：新 metadata symbol 尚不存在，测试 target 编译失败。

## Task 4: Header-local metadata and UUID GREEN

**Files:**

- Modify: `utils/include/turbo_cmeta_data.h`

1. 包含 `turbo_uuid.h`、`limits.h`、`stdint.h`、`string.h`，添加 C/C++ portable static assertions。
2. 定义八组 header-local type identity/type/integer shape/data descriptors，stable id 使用 `turbo.<ctype>` / `turbo.<ctype>.data`。
3. 定义 UUID stable type、owned STRING shape、buffer ops/data descriptor。
4. UUID assign 只按 `(data,size)` fixed indices 解析 local temporary；成功才写 destination。失败由 CMeta wrapper restore 全零。
5. 运行 Task 3 focused targets/CTest，预期 GREEN。

## Task 5: Verification, self-review and commit

1. 重新 configure 并完整构建：

   ```powershell
   cmake --fresh --preset win-release-user
   cmake --build --preset win-release-user
   ```

2. 运行 focused、相邻与全量：

   ```powershell
   ctest --preset win-release-user -R '^(cbind_|cmeta_|test_turbo_cmeta_data)' --output-on-failure
   ctest --preset win-release-user --output-on-failure
   cmake --build --preset win-release-user --target verify_installed_package
   ```

3. 检查 dependency closure、`git diff --check`、`git status --short`，确认没有 vendor、
   `.codegraph`、build 或 SDD report 被 staged。
4. 逐条对照 spec/brief，执行 mutation-oriented self-review：错误 width/alignment、越界
   conversion、UUID size/hyphen/hex/max/occupied/rollback 任一保护被删除时至少一项测试失败。
5. 提交 Task 1 branch；不 push、不建 PR。把 base/commit SHA、RED/verification evidence、
   ownership notes 与 remaining risks 写入 controller 指定 implementer report。

## Reviewer fix: UUID adapter provenance

1. 用独立 C peer TU 实例化 UUID metadata；C/C++ consumer tests 必须接受该 provenance，
   且能用其 data descriptor 完成真实 UUID assignment。
2. 复制 canonical provenance/data/ops，分别只替换 `is_zero`、`assign`、
   `restore_zero`，先观察缺少 UUID-specific admission surface 的 RED。
3. 在 `turbo_cmeta_data.h` 增加 UUID 专用 size-prefixed adapter descriptor、v1 ABI 与
   header-local self validator；consumer validator 在完整 prefix 得到 size 证明前不得读取
   extension 字段。
4. 保留全部既有 UUID/fixed-width 名称与 generic CMeta ABI，不改变 tstr/vstr，不给 CBind
   增加 Core 依赖。
5. 重新执行 focused、相邻、全量 Release CTest、installed-package consumers、dependency
   closure、CodeGraph/diff/vendor checks，并以独立 fix commit 交付。

## Reviewer fix round 2: candidate-discoverable UUID provenance

1. peer TU 只返回 `const cmeta_data_desc *`；C/C++ tests 只用 candidate 调用 admission，先
   观察现有二参数、带外 record API 无法编译的 RED。
2. 把 UUID-specific v1 provenance 作为 `candidate->buffer_ops` 可达的 size-prefixed tail；
   generic base 保持 offset zero，所有 tail read 必须先由 `base.struct_size` 证明。
3. provider-TU callback 接受 canonical cross-TU candidate 与 intact deep copy，并逐项拒绝
   copied provenance 下的 `is_zero`、`assign`、`restore_zero` replacement。
4. 删除不可从 candidate 获得的二参数 API；以 C/C++ compile assertions 验证
   `turbo_uuid_cmeta_buffer_ops` macro facade 仍保留 const base lvalue、address 与 field/sizeof
   source usage。
5. 保持 generic records/ABI、tstr/vstr、CBind 依赖不变；重跑 focused、相邻、全量、安装
   consumer 与静态范围检查，用新的独立 fix commit 交付。
