# CMeta Finite Compute Implementation Plan

> **执行约束：** 逐项使用 TDD；每个生产能力必须先出现可观察的编译或测试失败。

**Goal:** 在现有 CMeta C11 语法上增加有限类型函数、值函数、predicate/require 与 Schema 常量聚合，并用 Lean 建立有限关系证明模型。

**Architecture:** `cmeta/include/cmeta/compute.h` 是纯头文件公共表面，复用 `CMETA_PP_FOR_EACH` 生成 typedef/enum 查询项；`meta.h` 只聚合该表面。Lean 模块独立定义关系性质，不进入运行时。

**Spec:** `docs/superpowers/specs/2026-08-23-cmeta-finite-compute-design.md`

## Task 1: C11 public surface RED

**Files:**
- Create: `cmeta/tests/cmeta_compute_test.c`
- Modify: `cmeta/tests/CMakeLists.txt`

- [x] 写 TypeFunction1/2/3、ValueFunction1/2/3、Predicate/Require、SchemaCount/All/Any 和同名分片的成功用例。
- [x] 重新 configure 并构建 `cmeta_compute_test`，确认因公共宏缺失而失败。
- [x] 记录 RED 的首个缺失符号，不把语法错误误认成功。

## Task 2: Minimal compute header GREEN

**Files:**
- Create: `cmeta/include/cmeta/compute.h`
- Modify: `cmeta/include/cmeta/meta.h`

- [x] 实现带 arity 标记的生成标识符。
- [x] 用现有 `CMETA_PP_FOR_EACH` 实现 1/2/3 元类型与值函数声明。
- [x] 实现 Predicate、Satisfies 和跨 C/C++ 的 Require。
- [x] 实现 SchemaCount、SchemaAll、SchemaAny。
- [x] 构建并运行 `cmeta_compute_test`，确认 GREEN。

## Task 3: Negative compile contracts

**Files:**
- Create: `cmeta/tests/compile_fail/cmeta_compute_missing_type.c`
- Create: `cmeta/tests/compile_fail/cmeta_compute_conflicting_value.c`
- Create: `cmeta/tests/compile_fail/cmeta_compute_require.c`
- Modify: `cmeta/tests/CMakeLists.txt`

- [x] 先注册 expected-failure tests，确认没有检测机制时测试失败。
- [x] 复用仓库既有 compile-fail CMake 模式；若不存在，只增加 CMake `try_compile`/CTest helper，不写外部脚本。
- [x] 验证三个源文件都因预期合同失败，且不是 include/path 等无关原因。

## Task 4: C++17 compatibility

**Files:**
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

- [x] 先加入 C++17 `TypeEval*`、`ValueEval*` 和 `Require` 静态断言并确认 RED。
- [x] 修正公共头的语言兼容实现，确认 C++ test GREEN。

## Task 5: Lean finite relation model

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/FiniteCompute.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/FiniteCompute.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

- [x] 先写 functional、total-on、closed-over 与 lookup soundness witness，确认缺少模块时 RED。
- [x] 实现最小通用关系定义和证明，无 `sorry`/`admit`/`axiom`。
- [x] 运行 focused Lean test，再运行 `lake test` 与 `lake build`。

## Task 6: Documentation and verification

**Files:**
- Modify: `cmeta/README.md`
- Modify: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cmeta/C_META_CAPABILITIES.md`

- [x] 记录语法、1–16 行限制、单 token key、缺失映射 fail-fast、Schema 单列表达式约束和示例。
- [x] 运行 CMeta 相关 targets/CTest，再运行全仓库 build/test。
- [x] 运行 proof-escape、CodeGraph affected、`git diff --check` 和工作区状态检查。
- [x] 对完整 diff 做只读审查，修复所有 HIGH/MED 问题。
