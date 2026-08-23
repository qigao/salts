# CFlow Plan Exact Cost Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove the exact source-level steps and bounded resources of the canonical raw `Filter -> Map -> Map` CFlow Plan path and make its storage-representation boundary explicit.

**Architecture:** Add a focused Plan execution model beside the stable ten-dimensional Cost model. Derive exact metrics from a validated workload, project those metrics to `Cost`, and make linear/tree/hash encodings equivalent only through a common canonical schedule and observation.

**Tech Stack:** Lean 4.33.1, Lake 5, repository-local CMeta-CFlow calculus, PR #44 Release benchmark evidence.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-plan-exact-cost-design.md`

## Global Constraints

- Do not change production C/C++ behavior or public APIs.
- Preserve the v1 ten-dimensional `Cost` structure.
- Do not claim Lean proves wall-clock time or the compiled C binary.
- Add no third-party Lean dependency, axiom, `sorry`, or `admit`.
- Use literal benchmark witnesses derived independently from the Lean functions.

---

### Task 1: Lock exact Plan metrics with a failing Phase G test

**Files:**
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseG.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: existing `Cost` and PR #44's `n=1024`, `k=512`, 8-byte map outputs.
- Produces: required names `FusedFilterMapMap`, `PlanEvalMetrics`, `rawPlanMetrics`, and exact literal witnesses.

- [x] **Step 1: Write the failing test**

  Define a literal workload and assert that `rawPlanMetrics` returns zero Graph
  queries, two instruction visits, three raw stage calls, `2048` user calls,
  `2560` element visits, three allocations, `8320` allocated bytes, `128`
  selection bytes, `4096` intermediate bytes, `4096` result bytes, `8192` peak
  live bytes, and three memory passes. Add empty-input and empty-selection
  literals so conditional allocation/pass behavior is observable.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseG.lean`

  Expected: fail because `CMetaCFlowCalculus.CFlow.PlanCost` and its declarations
  do not exist.

- [x] **Step 3: Record RED before production implementation**

  Keep the failure output in the task log; do not weaken literal expectations.

### Task 2: Implement the exact profile and prove formulas

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/PlanCost.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/PlanCost.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`

**Interfaces:**
- Consumes: `Cost`, natural-number arithmetic, the Phase G required names.
- Produces: `FusedFilterMapMap.Valid`, `selectionByteCount`, `rawPlanMetrics`, `PlanEvalMetrics.toCost`, and exact theorem projections for Graph queries, visits, calls, allocations, bytes, peak live memory, and passes.

- [x] **Step 1: Implement the minimal operational profile**

  Use guarded natural-number formulas. `n=0` returns zero metrics. For `n>0`,
  count one selection allocation, each non-empty map output allocation, three
  stage calls, `n+2*k` user calls, and `2*n+k` element visits. Compute peak
  live bytes as intermediate bytes plus the maximum of selection and result.

- [x] **Step 2: Prove the general non-empty theorem**

  Under validity, `0<n`, and `0<k`, prove zero Graph queries, exact calls and
  visits, three allocations, exact bytes, and three memory passes. Prove the
  projected `Cost` retains those structural coordinates.

- [x] **Step 3: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseG.lean`

  Expected: pass without warnings.

### Task 3: Prove storage representation independence and verify the branch

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/PlanCost.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/PlanCost.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseG.lean`

**Interfaces:**
- Consumes: exact Plan metrics and a canonical list of Plan stages.
- Produces: `PlanStorage`, `PlanEncoding`, `RepresentationEquivalent`, and `representation_equivalent_same_metrics`.

- [x] **Step 1: Add a failing representation-equivalence witness**

  Construct linear-tape, tree, and hash-indexed encodings with the same literal
  schedule and output observation; require the theorem to equate their metrics.

- [x] **Step 2: Run focused RED**

  Run: `lake env lean Test/PhaseATests/PhaseG.lean`

  Expected: fail because representation declarations are absent.

- [x] **Step 3: Implement the minimal representation contract**

  Store only the representation tag, canonical schedule, output observation,
  and workload. Define equivalence as schedule, observation, and workload
  equality; prove metrics equality by rewriting those facts.

- [x] **Step 4: Run complete verification**

  Run from `formal/cmeta_cflow_calculus`:

  ```text
  lake env lean Test/PhaseATests/PhaseG.lean
  lake test -v
  lake build -v
  ```

  Run from the repository root:

  ```text
  rg.exe -n "\b(sorry|admit|axiom)\b" formal/cmeta_cflow_calculus --glob "*.lean"
  git diff --check
  git status --short
  ```

  Require all Lean commands and `git diff --check` to exit zero; the proof
  escape scan must return no matches.
