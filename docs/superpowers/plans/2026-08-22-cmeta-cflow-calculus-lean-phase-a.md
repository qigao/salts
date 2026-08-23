# CMeta–CFlow Calculus Lean Phase A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 建立独立、可由 Lean 机检的 Phase A 核心模型，并证明 retain/suspend 与 move/readability 的基础所有权不变量。

**Architecture:** 在 `formal/cmeta_cflow_calculus/` 下建立无第三方依赖的 Lean 4/Lake 工程。CMeta 层定义类型、capability、effect lattice、property set 与语义环境 `Γ`；Ownership 层以 token-indexed context 作为 binding 状态的唯一事实源；CFlow 层以输入类型、输出类型和 stream/terminal 模式索引 Flow term，使非法算子链接在构造时不可表达。

**Tech Stack:** Lean 4.33.1、Lake 5、Lean `Std`/核心库、elaboration-time test library。

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`

## Global Constraints

- Phase A 仅覆盖 `Types / Effects / Ownership / Flow Syntax`。
- 不修改 `cmeta/src`、`cmeta/include`、`cflow/src`、`cflow/include`、`platform/`、`concurrency/`、`turbostl/` 或 `utils/`。
- 不使用 `axiom`、`sorry`、`admit` 或第三方证明依赖。
- `Relation / Zip / Distinct / Sorted / Window / Merge / Parallel / Reactive` 不进入 v1 Flow syntax。
- `Source / WAIT / Demand / Terminal` small-step semantics、R1–R15 与 Cost 推导留给 Phase B–E。
- 保留现有程序行为；本阶段只增加 formal proof 产物与 formal 专用配置。
- 不创建 Git commit；用户未要求提交历史变更。

---

### Task 1: 独立 Lake 工程与可失败测试入口

**Files:**
- Create: `formal/cmeta_cflow_calculus/lean-toolchain`
- Create: `formal/cmeta_cflow_calculus/lakefile.toml`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: Lean toolchain `leanprover/lean4:v4.33.1`。
- Produces: `lake build` 的 `CMetaCFlowCalculus` library target 与 `lake test` 的 `PhaseATests` library driver。

- [ ] **Step 1: 添加 Lake 配置和测试入口**

  `lakefile.toml` 声明主 library 与 test library；测试先 import 尚不存在的 Phase A 模块，以证明测试入口确实能失败。

- [ ] **Step 2: 运行 RED 测试**

  Run: `lake test`

  Expected: FAIL，错误指向缺失的 `CMetaCFlowCalculus.CMeta.*` / `CFlow.Syntax` 模块，而不是 Lake 配置错误。

### Task 2: CMeta Types、Effects、Properties 与 Γ

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/Types.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/Effects.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/Properties.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/Environment.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: 无第三方依赖。
- Produces: `Ty`、`TypeCapability`、`Effect`、`Effect.le`、`Effect.join`、`Property`、`PropertySet`、`UnaryCallable`、`BinaryCallable`、`SourceDecl`、`CollectorDecl`、`Env`。

- [ ] **Step 1: 写 effect lattice 与 Γ 的行为测试**

  以 `example` / `#guard` 验证 `PURE ⊑ READ_ONLY ⊑ STATEFUL ⊑ EXTERNAL`、`join` 的上界/交换/结合性质，以及 Γ 中 callable/source/collector judgement 的精确类型。

- [ ] **Step 2: 运行测试并确认 RED**

  Run: `lake test`

  Expected: FAIL，缺少 Phase A 类型与定理。

- [ ] **Step 3: 实现最小 CMeta 模型与证明**

  `Effect.join` 取 lattice 中较大的 effect；properties 用正保证集合表达；`Env` 保存 type capability 与声明 judgement，不把 C runtime bitset 当作 calculus 定义。

- [ ] **Step 4: 运行测试并确认 GREEN**

  Run: `lake test`

  Expected: PASS，且没有 `sorry`/`axiom`。

### Task 3: Ownership Calculus 与基础定理

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CMeta/Ownership.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Ownership.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: `Env.hasCapability`、`TypeCapability.copy`、`TypeCapability.move`、`Ty`。
- Produces: `Ownership`、`Value`、`PackedValue`、`BindingState`、`OwnershipContext`、`ContextReadable`、`SuspendSafe`、`retain`、`move`、retain/move post-context 证明。

- [ ] **Step 1: 写 retain/suspend 与 move/readability 证明测试**

  用一个具备 `COPY`/`MOVE` capability 的 `User` 类型构造 Γ；验证 borrowed binding 可 retain 为 owned、retained singleton 可 suspension、borrowed singleton 不可 suspension、move 后 stale value reference 不可读且不可 suspension。

- [ ] **Step 2: 运行测试并确认 RED**

  Run: `lake test`

  Expected: FAIL，缺少 ownership 构造或定理。

- [ ] **Step 3: 实现最小 ownership 模型与无公理证明**

  `SuspendSafe context live` 对每个 live token 查询 authoritative context 并要求当前状态为 owned；`retain` 需要 Γ 的 `COPY` 证据，`move` 需要 Γ 的 `MOVE` 证据；proof 文件证明 post-context 边界定理。

- [ ] **Step 4: 运行测试并确认 GREEN**

  Run: `lake test`

  Expected: PASS。

### Task 4: 类型索引 Flow Syntax

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Syntax.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: Γ-indexed `UnaryCallable` / `BinaryCallable` / `SourceDecl` / `CollectorDecl`。
- Produces: `FlowMode.stream`、`FlowMode.terminal` 与 `Flow Γ input output mode` 的 `source/map/filter/flatMap/limit/skip/reduce/collect` 构造器。

- [ ] **Step 1: 写完整 grammar 与类型链测试**

  构造 `Source(User) → Filter(User→Bool) → Map(User→Name) → Limit(100) → Collect(NameList)`，并验证其类型为 `Flow Γ User NameList .terminal`；另构造 `FlatMap`、`Skip`、`Reduce` 以覆盖全部 v1 grammar，并用 `#guard_msgs` 验证七种 terminal 后继构造均被拒绝。

- [ ] **Step 2: 运行测试并确认 RED**

  Run: `lake test`

  Expected: FAIL，缺少 Flow 构造器或索引约束。

- [ ] **Step 3: 实现最小索引语法**

  所有非 terminal 算子只接受 `.stream` 输入；`collect` 是唯一产生 `.terminal` 的构造器，因此 Collect 后继续 Map 在 Lean 类型层不可表达。

- [ ] **Step 4: 运行测试并确认 GREEN**

  Run: `lake test`

  Expected: PASS，八种 v1 term 均被 elaboration test 覆盖。

### Task 5: 完整验证与范围审计

**Files:**
- Verify: `formal/cmeta_cflow_calculus/**`
- Verify: repository diff

**Interfaces:**
- Consumes: Task 1–4 的所有模块与测试。
- Produces: 可复验构建输出、禁用项扫描、生产目录未修改证据。

- [ ] **Step 1: 运行 formal 测试和完整 build**

  Run: `lake test`

  Run: `lake build`

  Expected: 两者 exit 0。

- [ ] **Step 2: 扫描未完成证明与越界语法**

  Run: `rg.exe -n "\\b(sorry|axiom|admit)\\b|Relation|Zip|Distinct|Sorted|Window|Merge|Parallel|Reactive" formal/cmeta_cflow_calculus`

  Expected: 不出现未完成证明；排除语法只允许出现在明确的测试/说明上下文，否则删除。

- [ ] **Step 3: 审计 Git diff 范围**

  Run: `git status --short`

  Run: `git diff --stat`

  Expected: 本任务新增内容只在 `formal/` 与本计划文件；既有 staged `CMakeUserPresets.json` 保持原样。
