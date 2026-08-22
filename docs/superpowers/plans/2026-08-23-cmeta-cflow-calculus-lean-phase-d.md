# CMeta-CFlow Calculus Lean Phase D Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Define the R1–R15 theorem catalogue in Lean, prove R1–R10 semantic rewrites, and encode R11–R14 execution refinements plus R15 async-promotion well-formedness with all specification premises explicit.

**Architecture:** `CFlow/Rewrite.lean` owns rule classification, a pure stream-result denotation, callable interpretations, bounded skip arithmetic, ordered reduction trees, execution variants, eligibility facts, and the async-boundary judgement. `Proofs/Rewrite.lean` proves the semantic rules and common refinement preservation theorem; `Test/PhaseATests/PhaseD.lean` supplies concrete witnesses and negative premise checks for every R1–R15 rule.

**Tech Stack:** Lean 4.33.1, Lake, polymorphic list semantics, typed CMeta callable evidence, inductive reduction trees and execution-refinement relations

**Spec:** `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md`

## Global Constraints

- R1–R10 are semantic rewrites and must prove denotational equality.
- R11–R14 are execution refinements and must not be exposed as unconditional semantic rewrite constructors.
- R15 is an execution well-formedness rule and must use the authoritative ownership context plus complete live roots.
- `PURE`, `TOTAL`, `ASSOCIATIVE`, identity, always-true, and order-independence declarations are trusted premises; Lean proves consequences from explicit semantic laws and does not claim to inspect C callback bodies.
- R4 evaluates the first predicate before the second predicate in the fused predicate.
- R5 removes element traversal while preserving source-construction effects, completion/cancellation outcome, and ownership safety.
- R8 uses overflow-free `satAdd max a b = min max (a + b)` and requires the stream length to be bounded by `max`.
- R10 changes parenthesization only; it preserves encounter order and grants no automatic parallel execution.
- R11 requires `PURE`, `TOTAL`, `ASSOCIATIVE`, `NO_ALIAS`, `PARALLEL_EXECUTOR`, `SPLITTABLE_SOURCE`, and an explicit order-safety premise.
- R12 requires an exact source size and collector reserve support.
- R13 requires no WAIT, executor boundary, or async buffer.
- R14 requires a linear segment, contiguous source, `TRIVIAL_COPY`, no WAIT, and `BATCH_EXECUTION`.
- Map/Filter reorder, predicate reorder, Map across executor boundaries, Buffer/WAIT removal, unconditional parallelization, reduce permutation, and effectful FlatMap reassociation are not admitted.
- Symbolic Cost comparison remains Phase E; Phase D makes no wall-clock or cost-decrease claim.
- No `axiom`, `sorry`, `admit`, third-party Lean dependency, or production C/C++ modification is allowed.
- The staged `CMakeUserPresets.json` change remains untouched.

---

### Task 1: Rule taxonomy and semantic vocabulary

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Rewrite.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseD.lean`

**Interfaces:**
- Consumes: `CMeta.Env`, `UnaryCallable`, `BinaryCallable`, `CollectorDecl`, `Effect`, `Property`, `TypeCapability`, `KernelCapabilities`, and ownership definitions from Phases A–C.
- Produces: `RuleId`, `RuleClass`, `ruleClass`, `SemanticRuleId`, `ExecutionRefinementRuleId`, `WellFormednessRuleId`, `StreamOutcome`, `StreamResult`, `UnaryMeaning`, `PredicateMeaning`, `BinaryMeaning`, `satAdd`, `ReductionTree`, `ReductionForest`, `ExecutionForm`, and `ExecutionVariant`.

- [x] **Step 1: Write the failing taxonomy and state-shape tests**

  Import `CMetaCFlowCalculus.CFlow.Rewrite`; verify literal classifications `R1/R10 = semanticRewrite`, `R11/R14 = executionRefinement`, and `R15 = wellFormedness`. Construct a stream result with construction effects, ordered values, `.done`, and `ownershipSafe = true`, plus typed unary/predicate/binary meanings.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`

  Expected: FAIL because the Rewrite module does not exist.

- [x] **Step 3: Implement the minimal taxonomy and vocabulary**

  Define separate finite rule-id types so an execution refinement cannot inhabit `SemanticRuleId`. Define stream operators as record updates that preserve construction effects, outcome, and ownership safety. Define `ReductionTree.flatten/eval` and `ReductionForest.flatten/evalFrom` without permutation. Define execution variants with an explicit form and shared semantic result.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake build CMetaCFlowCalculus.CFlow.Rewrite` followed by `lake env lean Test/PhaseATests/PhaseD.lean`.

  Expected: PASS for taxonomy and vocabulary witnesses.

### Task 2: R1–R9 stream rewrite theorems

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Rewrite.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseD.lean`

**Interfaces:**
- Consumes: `StreamResult` operators, callable meanings, CMeta effect/property evidence, and `satAdd`.
- Produces: `r1_map_identity`, `r2_map_fusion`, `r3_filter_true`, `r4_filter_fusion`, `r5_limit_zero`, `r6_nested_limit`, `r7_skip_zero`, `r8_nested_skip`, and `r9_limit_map`.

- [x] **Step 1: Add failing concrete theorem-use tests**

  Use token transforms `+1` then `*2`, even/positive predicates, and a three-element stream. Check exact ordered results for map fusion and filter fusion; check that limit-zero retains construction effects/outcome/safety; check nested limit, skip-zero, saturated nested skip with `max = 3`, and limit/map commutation.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`

  Expected: FAIL because the R1–R9 theorem identifiers are absent.

- [x] **Step 3: Prove R1–R9 without proof escapes**

  Case-analyze `StreamResult` once per rule and use list induction for identity, filter-true, and ordered filter fusion. Prove saturated-drop equality by splitting `a + b <= max`; in the saturated branch use the bounded-length premise to show both drops are empty. Require `PURE` and `TOTAL` arguments on every callback-moving/fusing theorem even when list equality itself only consumes the callable interpretation law.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake build CMetaCFlowCalculus.Proofs.Rewrite` followed by `lake env lean Test/PhaseATests/PhaseD.lean`.

  Expected: PASS for R1–R9 witnesses and exact metadata preservation.

### Task 3: R10 reassociation and R11 parallel-reduce eligibility

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Rewrite.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Rewrite.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseD.lean`

**Interfaces:**
- Consumes: ordered `ReductionTree`, `ReductionForest`, `BinaryMeaning`, Γ type capabilities, K capabilities, and rule taxonomy.
- Produces: `r10_reduce_reassociation`, `ParallelOrder`, `OrderSafe`, `ParallelReducePremises`, and the `ExecutionRefines.r11` constructor.

- [x] **Step 1: Add failing ordered-tree and eligibility tests**

  Build the ordered tree `((1 + 2) + (3 + 4))`; prove it equals left reduction under addition. Construct an R11 refinement with pure/total/associative sum, `NO_ALIAS`, parallel executor, splittable source, and encounter-order preservation. Add negative witnesses showing missing parallel capability and a permutation-allowing policy without an order-independence law cannot satisfy the premises.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`

  Expected: FAIL because R10/R11 definitions or theorems are absent.

- [x] **Step 3: Prove ordered reassociation and encode parallel eligibility**

  Prove by tree induction that folding an initial accumulator across `tree.flatten` equals combining it with `tree.eval` under associativity. Lift the lemma to `ReductionForest`. Define R11 only as an execution-refinement constructor and require `OrderSafe`; `.preservesEncounterOrder` is trivially safe, while `.mayPermute` requires a commutativity/order-independence law.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`.

  Expected: PASS for R10 equality and all R11 positive/negative boundaries.

### Task 4: R12–R15 refinement and well-formedness rules

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Rewrite.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Rewrite.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests/PhaseD.lean`

**Interfaces:**
- Consumes: collector declarations, source/segment facts, Γ/K capabilities, `SuspendSafe`, and execution variants.
- Produces: `SourceShape`, `CollectorExecutionCapabilities`, `SegmentFacts`, `ExecutionRefines.r12`, `ExecutionRefines.r13`, `ExecutionRefines.r14`, `AsyncBoundary`, `AsyncPromotionWellFormed`, `execution_refinement_preserves_semantics`, and `r15_async_promotion_requires_owned`.

- [x] **Step 1: Add failing R12–R15 witnesses**

  Derive reserve-collect for `size = 3`, direct execution for a sync-closed segment, and batch execution for a linear/contiguous/trivially-copyable/no-wait segment under batch-capable `K`. Verify every refinement retains exactly the same `StreamResult`. Construct an owned live-root R15 witness and reject the same root under a borrowed context.

- [x] **Step 2: Run the focused test and verify RED**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`.

  Expected: FAIL because the R12–R15 constructors and preservation theorems are absent.

- [x] **Step 3: Implement the minimal refinement and async-boundary judgements**

  Make R12 inspect `SourceShape.size = some n` and `reserve = true`; make R13 inspect three false segment flags; make R14 inspect linear/contiguous/no-wait facts plus Γ/K capabilities. Share the same semantic result on both sides of every execution-refinement constructor and prove preservation by relation inversion. Define R15 as `SuspendSafe context liveRoots`, independent of which of the four async boundary tags triggered promotion.

- [x] **Step 4: Run the focused test and verify GREEN**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`.

  Expected: PASS for R12–R15 and all missing-premise rejection cases.

### Task 5: Root integration and complete verification

**Files:**
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Verify: `formal/cmeta_cflow_calculus/**`

**Interfaces:**
- Consumes: Phase D definitions, proofs, and tests.
- Produces: root imports and one Phase A/B/C/D test driver.

- [x] **Step 1: Add root and test-driver imports**

  Import `CFlow.Rewrite` and `Proofs.Rewrite` from the root library; import `PhaseATests.PhaseD` from the test driver.

- [x] **Step 2: Run focused and complete Lean verification**

  Run: `lake env lean Test/PhaseATests/PhaseD.lean`

  Run: `lake test -v`

  Run: `lake build -v`

  Expected: all commands exit 0 with Phases A–D elaborated.

- [x] **Step 3: Audit proof, classification, and scope boundaries**

  Run: `rg.exe -n "\b(sorry|admit|axiom)\b" CMetaCFlowCalculus Test`

  Run: `git diff --name-only -- cmeta cflow platform concurrency turbostl utils`

  Expected: no proof escapes and no production-directory changes. Confirm R1–R10, R11–R14, and R15 occupy only their designated rule-id types.

- [x] **Step 4: Confirm unrelated staged state is unchanged**

  Run: `git rev-parse ':CMakeUserPresets.json'`

  Expected: staged blob remains `268d3ff55fcd14b6d62ea44b104b8ecb58d54a02`.
