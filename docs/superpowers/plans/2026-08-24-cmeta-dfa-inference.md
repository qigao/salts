# CMeta DFA Inference Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 用合法 CMeta 行宏声明有限关系，构建有界 C DFA，并在 CFlow plan compile 阶段完成 opcode 推导和预解码。

**Architecture:** CMeta 提供无隐式分配的 relation/trie-DFA API；调用方拥有 workspace。CFlow 从一份规则行列表同时生成编译期 ValueFunction 和运行期 relation，并只在 admission 使用 DFA。

**Tech Stack:** C11、CMeta 宏、TinyTest、CMake Presets、Lean 4

**Spec:** `docs/superpowers/specs/2026-08-24-cmeta-dfa-inference-design.md`

## Global Constraints

- 输入仍是 C/CMeta 语法，不增加独立 DSL。
- arity 固定为 1–3，缺失规则和歧义规则 fail fast。
- CMeta 不依赖 CFlow、TurboParser、re2c 或 Lemon。
- CFlow executor 热路径不查询 DFA。
- 生产实现之前必须观察对应测试因能力缺失而失败。

---

### Task 1: CMeta relation/DFA public contract

**Files:**
- Create: `cmeta/tests/cmeta_infer_test.c`
- Modify: `cmeta/tests/CMakeLists.txt`
- Create: `cmeta/include/cmeta/infer.h`
- Create: `cmeta/src/infer.c`
- Modify: `cmeta/include/cmeta/meta.h`
- Modify: `cmeta/CMakeLists.txt`

**Interfaces:**
- Produces: `InferenceRules1/2/3`, `cmeta_infer_dfa_init`, `cmeta_infer_dfa_build`, `cmeta_infer_dfa_eval`, `cmeta_infer_status_string`.
- Consumes: `CMETA_PP_FOR_EACH_A` and ordinary caller-provided arrays.

- [x] Add TinyTest cases with literal expected results for shared prefixes, missing rules, wrong arity, insufficient capacity, duplicate rows and conflicting rows.
- [x] Register/build `cmeta_infer_test` and confirm RED because `cmeta/infer.h` is absent.
- [x] Add the minimal header declarations and macro row projections.
- [x] Implement trie insertion, ambiguity detection, transition sorting and binary-search evaluation.
- [x] Build/run `cmeta_infer_test` and confirm GREEN.
- [x] Add C++17 public-header compile use and rerun `cmeta_header_cpp_test`.

### Task 2: CFlow admission inference

**Files:**
- Modify: `cflow/src/plan_compile.c`
- Modify: `cflow/include/cflow/plan.h`
- Modify: `cflow/tests/cflow_pipeline_test.c`

**Interfaces:**
- Consumes: CMeta inference API from Task 1.
- Produces: DFA-derived `cflow_plan_opcode` and `cflow_plan_compile_stats.inference_queries`.

- [x] Add a pipeline assertion that two emitted instructions report two inference queries; confirm RED because the stats field is absent.
- [x] Declare one `CFLOW_PLAN_INFERENCE_ROWS` source and project it through both `ValueFunction3` and `InferenceRules3`.
- [x] Replace `opcode_for` with relation evaluation using `(op, output, cardinality)`.
- [x] Build the DFA once per capability/compile operation, propagate build/query errors without fallback, and increment the new stat only for emitted instructions.
- [x] Run pipeline tests and confirm existing interpreted/compiled results and predecoded callback checks remain GREEN.

### Task 3: Lean DFA semantics

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/FiniteCompute.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/FiniteCompute.lean`

**Interfaces:**
- Produces: `FiniteDfa`, `dfaStep`, `dfaRun`, transition/accept soundness theorems.
- Consumes: existing `FiniteRelation` and `lookup_some_mem`.

- [x] Add focused examples for accepted and missing symbol paths and confirm RED on absent DFA definitions.
- [x] Implement explicit DFA evaluation and prove successful step/accept membership.
- [x] Run `lake test` and `lake build`; scan for `sorry|admit|axiom`.

### Task 4: Documentation and verification

**Files:**
- Modify: `cmeta/LANGUAGE_REFERENCE.md`
- Modify: `cmeta/README.md`
- Modify: `CMakeUserPresets.json`
- Modify: `.gitignore`

**Interfaces:**
- Documents public ownership, capacity, failure and executor boundaries.
- Restores the repository-mandated versioned preset entry.

- [x] Document inference declarations, workspace ownership, complexity and a complete runnable example.
- [x] Run focused CMeta/CFlow targets and CTest filters.
- [x] Run all CMeta/CFlow CTest tests, then full release build/test if focused verification is clean.
- [x] Run Lean verification, `git diff --check`, CodeGraph affected analysis and inspect the complete diff.
