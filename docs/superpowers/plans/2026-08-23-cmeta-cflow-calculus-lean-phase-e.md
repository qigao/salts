# CMeta-CFlow Calculus Lean Phase E Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Formalize the v1 symbolic Cost domain and derive Direct / Plan / Kernel / Batch / Parallel Reduce execution paths without claiming measured runtime improvements.

**Architecture:** `CFlow/Cost.lean` owns the ten-dimensional symbolic vector, Pareto comparison, profile-weighted extraction, path facts, and reference cost formulas. `Proofs/Cost.lean` proves vector laws, mutually safe path admission, and cost comparisons while reusing Phase D execution refinements for semantic preservation. `Test/PhaseATests/PhaseE.lean` provides executable witnesses and negative boundaries.

**Tech Stack:** Lean 4, Lake, repository-local `CMetaCFlowCalculus` modules, no third-party Lean dependencies.

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`

## Global Constraints

- Cost is exactly the v1 ten-dimensional symbolic vector: allocations, allocated bytes, copies, copied bytes, callback dispatches, atomic operations, scheduler hops, wakeups, syscalls, and memory passes.
- Pareto dominance means pointwise less-than-or-equal cost plus at least one strictly smaller dimension.
- Profile weights may choose between semantically legal alternatives but never discharge rewrite soundness or execution eligibility.
- Direct requires a well-typed, linear, sync-closed segment and synchronous Kernel support.
- Plan requires a well-typed static segment with no dynamic wait, synchronous Kernel support, and failure of Direct eligibility.
- Kernel requires an async trigger and the matching Kernel capabilities.
- Batch cost improvement is proved only when the supplied batch count strictly reduces callback dispatches.
- Parallel Reduce may be Pareto-incomparable with sequential Reduce; profile-weighted preference requires explicit weights and does not imply semantic validity.
- Phase D `ExecutionRefines` remains the semantic fact source for Batch and Parallel Reduce.
- No wall-clock percentage, scheduler optimality, C runtime conformance, production C/C++ modification, `axiom`, `sorry`, or `admit` is allowed.
- The staged `CMakeUserPresets.json` change remains untouched.

---

### Task 1: Symbolic Cost vector and comparison

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Cost.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Cost.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseE.lean`

**Interfaces:**
- Consumes: `KernelCapabilities` and Phase D execution vocabulary.
- Produces: `Cost`, `CostDimension`, `Cost.get`, `Cost.pointwiseLE`, `Cost.ParetoDominates`, `CostWeights`, `Cost.weighted`, and `Cost.add`.

- [x] **Step 1: Write failing vector and comparison tests**

  Import `CMetaCFlowCalculus.CFlow.Cost` and `CMetaCFlowCalculus.Proofs.Cost`. Define literal vectors showing `⟨0, …, scheduler_hops := 0, …⟩` dominates the same vector with one scheduler hop, prove equality is not strict dominance, and calculate one weighted score from literal weights.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: fail because the Cost modules and names do not exist.

- [x] **Step 3: Implement the minimal Cost domain**

  Define all ten `Nat` fields, an exhaustive `CostDimension`, projection by dimension, component-wise addition, pointwise order, strict Pareto dominance, the ten non-negative profile weights, and the weighted dot product. Prove reflexivity/transitivity of pointwise order, addition monotonicity, strict dominance irreflexivity, and that a positive added delta yields dominance.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: pass with literal vector and weighted-score witnesses.

### Task 2: Direct / Plan / Kernel path derivation

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseE.lean`

**Interfaces:**
- Consumes: `Env`, `KernelCapabilities`, `SegmentFacts`, and `SyncClosed`.
- Produces: `ExecutionFacts`, `EfficiencyPath`, `DirectEligible`, `PlanEligible`, `KernelTrigger`, `KernelSupports`, `KernelRequired`, and `PathDerivation`.

- [x] **Step 1: Add failing path witnesses and negative boundaries**

  Derive Direct for a well-typed linear sync-closed segment under `SYNC_EXECUTION`; derive Plan for a static non-linear segment with no dynamic wait; derive Kernel for WAIT under `WAITABLE`. Reject Direct for a waiting segment, reject Kernel when WAITABLE is absent, and prove a Direct-eligible segment cannot carry a Kernel trigger.

- [x] **Step 2: Run focused RED**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: fail on missing path facts and derivation constructors.

- [x] **Step 3: Implement path judgments and separation proofs**

  Encode Direct, Plan, and Kernel as distinct propositions and an indexed derivation relation. Keep structural triggers separate from Kernel support. Prove Direct excludes every Kernel trigger and expose constructor theorems for each path.

- [x] **Step 4: Run focused GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: pass for all positive and negative path cases.

### Task 3: Reference costs for Direct / Plan / Kernel

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseE.lean`

**Interfaces:**
- Consumes: `PathDerivation`, `Cost`, and `Cost.ParetoDominates`.
- Produces: `Workload`, `directCost`, `planCost`, `kernelCost`, `pathCost`, `direct_cost_dominates_plan`, and `plan_cost_dominates_kernel`.

- [x] **Step 1: Add failing literal derivations**

  For three items and two operators, assert the exact Direct, Plan, and Kernel reference vectors. Prove Direct dominates Plan when the callback count is positive and Plan dominates Kernel because Kernel adds atomic, scheduler, and wakeup overhead.

- [x] **Step 2: Run focused RED**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: fail on missing workload and reference-cost definitions.

- [x] **Step 3: Implement reference formulas and proofs**

  Use one memory pass for a non-empty workload. Direct has no indirect callback dispatch; Plan has `items * operators` dispatches; Kernel retains Plan work and adds `items` atomic operations plus one scheduler hop and wakeup. Document these as calculus reference formulas, not measurements of the current C runtime.

- [x] **Step 4: Run focused GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: pass with exact vector calculations and Pareto proofs.

### Task 4: Batch and Parallel Reduce cost boundaries

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Cost.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseE.lean`

**Interfaces:**
- Consumes: Phase D `ExecutionRefines.r11`, `ExecutionRefines.r14`, and `execution_refinement_preserves_semantics`.
- Produces: `scalarCost`, `batchCost`, `sequentialReduceCost`, `parallelReduceCost`, `CostedRefinement`, `batch_cost_dominates_scalar`, `parallel_reduce_costs_incomparable`, and `costed_refinement_preserves_semantics`.

- [x] **Step 1: Add failing Batch and Parallel Reduce tests**

  Reuse the Phase D R14 and R11 witnesses. For eight items and two batches, prove Batch strictly lowers callback dispatches. Compare a sequential reduction with seven critical-path callbacks against a parallel reduction with three callbacks and two scheduler hops; prove neither vector Pareto-dominates the other, then show explicit profile weights prefer the parallel vector.

- [x] **Step 2: Run focused RED**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: fail on missing costed-refinement and comparison APIs.

- [x] **Step 3: Implement minimal costed refinement and boundary proofs**

  Pair an existing Phase D semantic refinement with before/after Cost values and an explicit Pareto or profile comparison supplied by the caller. Prove semantic equality solely from `ExecutionRefines`; prove Batch dominance from the strict callback-count premise; prove Parallel Reduce incomparability from fewer critical-path callbacks and positive scheduler hops.

- [x] **Step 4: Run focused GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseE.lean`

  Expected: pass while retaining the separation between semantic and cost premises.

### Task 5: Integration and verification

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Modify: `docs/superpowers/plans/2026-08-23-cmeta-cflow-calculus-lean-phase-e.md`

**Interfaces:**
- Consumes: all Phase E definitions, proofs, and tests.
- Produces: umbrella imports and a repeatable full verification checkpoint.

- [x] **Step 1: Add umbrella-import references**

  Import `PhaseATests.PhaseE` from the aggregate test module and import both Cost modules from `CMetaCFlowCalculus.lean` after their focused RED/GREEN cycles establish the module graph.

- [x] **Step 2: Run aggregate integration check**

  Run: `lake test`

  Expected: pass and prove that the complete Phase E module graph is reachable from both umbrella modules.

- [x] **Step 3: Complete imports and plan checklist**

  Add the production and proof imports, add the aggregate test import, check every completed plan step, and keep the staged preset untouched.

- [x] **Step 4: Run full verification**

  Run from `formal/cmeta_cflow_calculus`:

  ```text
  lake test
  lake build
  ```

  Then run repository-root checks:

  ```text
  git diff --check
  rg.exe -n "\b(sorry|admit|axiom)\b" formal/cmeta_cflow_calculus
  git diff --name-only -- cmeta cflow platform concurrency turbostl utils
  git rev-parse ':CMakeUserPresets.json'
  ```

  Expected: all Lean checks pass, no proof escape exists, no production directory changed, and the preset blob remains `268d3ff55fcd14b6d62ea44b104b8ecb58d54a02`.
