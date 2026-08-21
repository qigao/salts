# CMeta Lean Module-System Plan A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the CFlow semantic/PublicProof tree (M1–M6) to Lean 4.30 modules with explicit public semantic API, private proof machinery, hard PublicProof isolation, and unchanged C witness semantics.

**Architecture:** Convert the CFlow semantic spine bottom-up. Semantic carriers/functions required to state and use the public proofs are explicitly public; proof plumbing is private. While an identified legacy consumer cannot yet use `import all`, use only the audited `TEMP-MODULE-BRIDGE(M<n>)` theorem/re-export/exposure bridges from the execution amendment, then remove each at its last-consumer phase. `EndToEnd` becomes a private theorem implementation layer; `Semantics` re-exports only semantic public scopes; `PublicProof` is the stable six-theorem facade. The legacy `CMeta.lean` remains the full-build root throughout Plan A. M6 moduleizes the CFlow generated/conformance closure, removes every remaining bridge, and proves hard client isolation.

**Tech Stack:** Lean 4.30.0, Lake, C11/CMake, GCC/Clang, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md`

**Execution amendment:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-plan-a-amendment.md`

## Global Constraints

- Do not change CMeta/CFlow runtime semantics, operator semantics, or proof statements except visibility/import qualification required by the module system.
- Do not introduce duplicate/opaque semantic carriers. Existing `CType`, `Callable`, `TypedGraph`, `SurfaceZip`, `FusedMap`, `ExecProgram`, `PackedVec`, plan/runtime structures remain authoritative.
- Do not convert `formal/CMeta.lean` to `module` in Plan A. Do not create final `CMeta.InternalChecks`; both belong to Plan B.
- Do not enable Lake `allowImportAll`.
- Do not set `backward.privateInPublic` or `backward.proofsInPublic`.
- Use `public import all X` when the current module intentionally re-exports `X`'s public semantic API and also needs `X`'s private proof/body scope. Use `import all X` for private package implementation access only. Use `public import X` for public re-export without private access.
- Every temporary compatibility declaration/import/body exposure must carry `TEMP-MODULE-BRIDGE(M<n>): <consumer>` and be removed at the named phase.
- The only authorized temporary `@[expose]` bridges are those listed in the execution amendment: `HArgs.one`, `Callable.ofUnary`, `Callable.invoke1` through M6; `MapChain.check` through M4; `PlanProgram.compile` and `PlanWellTyped` through M5.
- No migration-only `@[expose]` may remain at final Plan A exact head.
- Generated Lean snapshots may change only by module-system source framing. Their semantic payload after the framing prefix must remain byte-for-byte identical to the previous committed source.
- Keep the existing proof-placeholder and arity-specific callable guards green.
- Every RED must be inspected and must fail for the intended visibility/module reason, not a typo or missing unrelated import.
- Commit only GREEN states. Tasks inside a phase may be committed locally; push at phase checkpoints so CI is not intentionally fed a known-red intermediate commit.

## Bridge Removal Ledger

The executor must update this ledger mentally/source comments during work and prove it empty in Task 13.

| Bridge | Created | Remove |
|---|---|---|
| `dispatch_sound`, `dispatch_policy_sound` public | M1 | M2 / Task 6 |
| `TypedOp.step_exact`, `Pipeline.check_steps` public | M2 | M3 / Task 8 |
| `TypedGraph.check_stages` public | M2 | M5 / Task 11 |
| `TypedRelation.check_erase` public | M2 | M6 / Task 13 |
| `MapChain.check_signatures` public | M3 | M4 / Task 9 |
| `FusedMap.type_preserved` public | M3 | M5 / Task 11 |
| `SurfaceZip.lowering_preserves_type` public | M3 | M6 / Task 13 |
| `duplicate_idempotent_elimination_sound` public | M3 | M6 / Task 13 |
| `PlanNode.check_erase`, `PlanProgram.compile_well_typed` public within M4 | Task 9 | Task 10 |
| `ExecProgram.runtime_execution_exact`, `result_type_safe`, `compiled_plan_well_typed` public | M4 | M5 / Task 11 |
| `Cardinality` public re-export of `Execution` | M4 | M5 / Task 11 |
| `@[expose] HArgs.one`, `Callable.ofUnary`, `Callable.invoke1` | M1 | M6 / Task 13 |
| `@[expose] MapChain.check` | M3 | M4 / Task 9 |
| `@[expose] PlanProgram.compile`, `PlanWellTyped` | M4 | M5 / Task 11 |

---

## Task 1 — M1a: Convert `Calculus` and create the migration conformance harness

**Files**

- Create: `formal/CMeta/ModuleMigrationConformance.lean`
- Modify: `formal/CMeta/Calculus.lean`
- Modify: `formal/CMeta.lean`

**Consumes:** `Std`.

**Produces public semantic API:** `product`, `CoreExpr`, `CoreExpr.eval`, `CoreExpr.cardinality`, `ppRepeat`, `replay`.

**Must become private:** `product_length`, `CoreExpr.eval_length_eq_cardinality`, `map_cardinality`, `append_cardinality`, `product_cardinality`, `ppRepeat_length`, `ppRepeat_index_domain`, `ppRepeat_indices_unique`, `replay_length`, `replay_zip`.

- [ ] **RED:** create `formal/CMeta/ModuleMigrationConformance.lean` as a legacy source so it can test the pre-conversion file:

```lean
import CMeta.Calculus

#check CMeta.product
#check CMeta.CoreExpr
#check CMeta.CoreExpr.eval
#check CMeta.ppRepeat
#check CMeta.replay

assert_not_exists CMeta.product_length
assert_not_exists CMeta.CoreExpr.eval_length_eq_cardinality
assert_not_exists CMeta.replay_zip
#check_assertions
```

- [ ] Import `CMeta.ModuleMigrationConformance` from the legacy `formal/CMeta.lean` root.
- [ ] Run:

```bash
cd formal
lake env lean CMeta/ModuleMigrationConformance.lean
```

Expected RED: at least one `assert_not_exists` fails because legacy `CMeta.Calculus` exports those theorem names.

- [ ] **GREEN:** add the module header to `Calculus.lean`:

```lean
module
import Std
```

Mark exactly these declarations public:

```text
product
CoreExpr
CoreExpr.eval
CoreExpr.cardinality
ppRepeat
replay
```

Leave all listed proof theorems unmarked/private. Do not add `@[expose]`.

- [ ] Run focused GREEN:

```bash
cd formal
lake env lean CMeta/ModuleMigrationConformance.lean
lake env lean CMeta/Calculus.lean
```

- [ ] Inspect diff to confirm no theorem body/semantic body changed except visibility/header tokens.
- [ ] Commit locally:

```bash
git add -- formal/CMeta/Calculus.lean formal/CMeta/ModuleMigrationConformance.lean formal/CMeta.lean
git commit -m "refactor(formal): moduleize calculus surface"
```

Do not push until Task 5 M1 checkpoint.

---

## Task 2 — M1b: Convert `Traits`

**Files**

- Modify: `formal/CMeta/Traits.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Consumes:** public `Calculus` module only; Traits itself does not re-export Calculus.

**Produces public semantic API:** `CType`, `Signature`, `Traits`, `Traits.inferUnary`, `SignaturePolicy`, `policyAllows`.

**Must become private:** `Traits.type_unique`, `Traits.inferUnary_of_known`, `Traits.inferUnary_unique`, `policyAllows_iff`.

- [ ] **RED:** extend migration conformance imports and checks:

```lean
import CMeta.Calculus
import CMeta.Traits

#check CMeta.CType
#check CMeta.Signature
#check CMeta.Traits
#check CMeta.Traits.inferUnary
#check CMeta.SignaturePolicy
#check CMeta.policyAllows

assert_not_exists CMeta.Traits.type_unique
assert_not_exists CMeta.Traits.inferUnary_of_known
assert_not_exists CMeta.Traits.inferUnary_unique
assert_not_exists CMeta.policyAllows_iff
```

Keep Task 1 assertions in the same file and keep one final `#check_assertions`.

- [ ] Run:

```bash
cd formal
lake env lean CMeta/ModuleMigrationConformance.lean
```

Expected RED: the new Traits `assert_not_exists` checks fail against legacy Traits.

- [ ] **GREEN:** make `Traits.lean` begin:

```lean
module
import CMeta.Calculus
```

Use explicit `public` declarations/public sections only around the six semantic names listed above. Proof theorems remain private. Do not public-import Calculus.

- [ ] Run focused GREEN:

```bash
cd formal
lake env lean CMeta/Traits.lean
lake env lean CMeta/ModuleMigrationConformance.lean
```

- [ ] Commit locally:

```bash
git add -- formal/CMeta/Traits.lean formal/CMeta/ModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize trait semantics"
```

Do not push until Task 5.

---

## Task 3 — M1c: Convert `Callable` with the audited legacy-conformance exposure bridge

**Files**

- Modify: `formal/CMeta/Callable.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Consumes/re-exports:** `CMeta.Traits` public semantics.

**Produces public semantic API:** `CType.denote`, `HArgs`, `HArgs.one`, `HArgs.two`, `HArgs.append`, `HArgs.snoc`, `Callable`, `Callable.ofUnary`, `Callable.ofBinary`, `Callable.invoke`, `Callable.invoke1`, `Callable.invoke2`, `Callable.unaryBackendSignature`, `Callable.binaryBackendSignature`, `Callable.compose`, `Generator`, `Generator.signature`, `CallableDesc`, `eraseValue`, `eraseGenerator`.

**Must become private:** `Callable.compose_beta`, `Generator.signature_exact`, `eraseValue_unary`, `eraseValue_binary`, `eraseGenerator_preserves_signature`.

- [ ] **RED:** add:

```lean
import CMeta.Callable

#check CMeta.HArgs
#check CMeta.Callable
#check CMeta.Callable.ofUnary
#check CMeta.Callable.invoke1
#check CMeta.Generator
#check CMeta.CallableDesc
#check CMeta.eraseValue
#check CMeta.eraseGenerator

assert_not_exists CMeta.Callable.compose_beta
assert_not_exists CMeta.Generator.signature_exact
assert_not_exists CMeta.eraseValue_unary
assert_not_exists CMeta.eraseValue_binary
assert_not_exists CMeta.eraseGenerator_preserves_signature
```

Run the conformance file and verify the new assertions fail against legacy Callable.

- [ ] **GREEN:** start Callable with:

```lean
module
public import CMeta.Traits
```

Make the listed semantic names public. Keep proof theorems private.

- [ ] Add exactly these temporary body bridges, each with the exact source marker comment:

```text
TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_lists_equal
```

on:

```lean
@[expose] public def HArgs.one ...
@[expose] public def Callable.ofUnary ...
@[expose] public def Callable.invoke1 ...
```

Do not expose `ofBinary`, `invoke2`, or other definitions unless the execution amendment is first updated with an audited consumer.

- [ ] Run:

```bash
cd formal
lake env lean CMeta/Callable.lean
lake env lean CMeta/ModuleMigrationConformance.lean
```

- [ ] Commit locally:

```bash
git add -- formal/CMeta/Callable.lean formal/CMeta/ModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize callable semantics"
```

Do not push until Task 5.

---

## Task 4 — M1d: Convert `Lambda`

**Files**

- Modify: `formal/CMeta/Lambda.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Consumes/re-exports:** Callable public semantics and private bodies for Lambda proof implementation.

**Produces public semantic API:** `Lambda`, `Lambda.invoke`, `Lambda.asCallable`, `anonymous`, `bindLast`.

**Must become private:** `Lambda.beta`, `Lambda.erasure_semantics`, `Lambda.erasure_signature_unary`, `Lambda.erasure_signature_binary`, `anonymous_beta`, `bindLast_beta`, `lambda_bind_same_shape`.

- [ ] **RED:** add:

```lean
import CMeta.Lambda

#check CMeta.Lambda
#check CMeta.Lambda.invoke
#check CMeta.Lambda.asCallable
#check CMeta.anonymous
#check CMeta.bindLast

assert_not_exists CMeta.Lambda.beta
assert_not_exists CMeta.Lambda.erasure_semantics
assert_not_exists CMeta.Lambda.erasure_signature_unary
assert_not_exists CMeta.Lambda.erasure_signature_binary
assert_not_exists CMeta.anonymous_beta
assert_not_exists CMeta.bindLast_beta
assert_not_exists CMeta.lambda_bind_same_shape
```

Verify RED against legacy Lambda.

- [ ] **GREEN:** use:

```lean
module
public import all CMeta.Callable
```

Make only the listed semantic declarations public. The combined `public import all` is intentional: downstream Lambda users receive Callable semantic API, while Lambda's private proofs can unfold Callable bodies without widening Callable's public proof names.

- [ ] Run focused GREEN and commit locally:

```bash
cd formal
lake env lean CMeta/Lambda.lean
lake env lean CMeta/ModuleMigrationConformance.lean
git add -- CMeta/Lambda.lean CMeta/ModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize lambda semantics"
```

Do not push until Task 5.

---

## Task 5 — M1e: Convert `Dispatch`, keep the two M2 theorem bridges, and take the M1 checkpoint

**Files**

- Modify: `formal/CMeta/Dispatch.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Consumes/re-exports:** Traits public semantics; private Traits bodies for dispatch proofs.

**Produces public semantic API:** `Operator`, `DispatchRule`, `dispatch`, `OperatorPolicy`, `RulesRespectPolicy`, `composeSignature`, `inferAndAllow`.

**Private now:** `inferAndAllow_known`.

**Temporary M2 public bridges:** `dispatch_sound`, `dispatch_policy_sound`, required by still-legacy `Flow.lean`.

- [ ] **RED:** add public semantic checks and only the proof assertion that can already be hidden:

```lean
import CMeta.Dispatch

#check CMeta.Operator
#check CMeta.DispatchRule
#check CMeta.dispatch
#check CMeta.OperatorPolicy
#check CMeta.RulesRespectPolicy
#check CMeta.composeSignature
#check CMeta.inferAndAllow

assert_not_exists CMeta.inferAndAllow_known
```

Do not assert absence of `dispatch_sound` or `dispatch_policy_sound` yet; they are audited M2 bridges.

- [ ] Verify RED against legacy Dispatch.

- [ ] **GREEN:** replace the accidental Lambda dependency with the exact module import needed by Dispatch:

```lean
module
public import all CMeta.Traits
```

Mark semantic declarations public. Keep `inferAndAllow_known` private.

Mark exactly these theorem bridges public, with comments:

```lean
-- TEMP-MODULE-BRIDGE(M2): legacy Flow.ResolvedStep.dispatch_exact
public theorem dispatch_sound ...

-- TEMP-MODULE-BRIDGE(M2): legacy Flow.ResolvedStep.policy_safe
public theorem dispatch_policy_sound ...
```

- [ ] Run focused checks, then full M1 Lean root gate:

```bash
cd formal
lake env lean CMeta/Dispatch.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

Expected: full legacy root is green because Flow can still consume the two explicit public bridge theorems.

- [ ] Audit M1 for blanket/backward escapes:

```bash
git grep -nE 'backward\.(privateInPublic|proofsInPublic)|allowImportAll' -- formal
```

Expected: no matches.

- [ ] Commit if not already committed and push the M1 GREEN stack to `leanv4`. Verify the triggered `Lean proofs` run reaches and passes `lake build --wfail` on both GCC and Clang before proceeding to M2.

Commit message for Dispatch task:

```text
refactor(formal): moduleize dispatch semantics
```

---

## Task 6 — M2a: Convert `Flow`, consume/remove Dispatch bridges, and create downstream bridges

**Files**

- Modify: `formal/CMeta/Flow.lean`
- Modify: `formal/CMeta/Dispatch.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Produces public semantic API:** `TypedOp`, `TypedOp.operator`, `TypedOp.signature`, `stepType`, `Pipeline`, `Pipeline.steps`, `Pipeline.length`, `checkPipeline`, `cflowBuiltInPolicy`, `ResolvedStep`, `TargetSignatureUnique`, `WellFormedDispatch`.

**Private now:** `TypedOp.progress`, `TypedOp.output_unique`, `Pipeline.steps_length`, `ResolvedStep.dispatch_exact`, `ResolvedStep.policy_safe`, `ResolvedStep.target_signature_safe`, `ResolvedStep.cannot_target_incompatible`.

**Temporary public bridges:** `TypedOp.step_exact` (remove M3), `Pipeline.check_steps` (remove M3).

- [ ] **RED:** before conversion, add Flow checks plus private assertions that do not conflict with known bridges:

```lean
import CMeta.Flow

#check CMeta.TypedOp
#check CMeta.stepType
#check CMeta.Pipeline
#check CMeta.checkPipeline
#check CMeta.cflowBuiltInPolicy
#check CMeta.ResolvedStep
#check CMeta.TargetSignatureUnique
#check CMeta.WellFormedDispatch

assert_not_exists CMeta.TypedOp.progress
assert_not_exists CMeta.TypedOp.output_unique
assert_not_exists CMeta.Pipeline.steps_length
assert_not_exists CMeta.ResolvedStep.target_signature_safe
assert_not_exists CMeta.ResolvedStep.cannot_target_incompatible
```

Verify RED.

- [ ] **GREEN:** use:

```lean
module
public import all CMeta.Dispatch
```

Make the semantic list public. Make the two known downstream bridge theorems public with exact markers:

```text
TEMP-MODULE-BRIDGE(M3): legacy Optimize.canonicalizeMapLike_preserves_type
TEMP-MODULE-BRIDGE(M3): legacy Lowering.SurfaceZip.lowering_preserves_type
```

for `TypedOp.step_exact` and `Pipeline.check_steps` respectively.

- [ ] Remove `public` and both `TEMP-MODULE-BRIDGE(M2)` comments from `dispatch_sound` and `dispatch_policy_sound`; Flow now sees them privately through `public import all CMeta.Dispatch`.

- [ ] Add these assertions to migration conformance:

```lean
assert_not_exists CMeta.dispatch_sound
assert_not_exists CMeta.dispatch_policy_sound
```

- [ ] Run:

```bash
cd formal
lake env lean CMeta/Flow.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit and push GREEN:

```text
refactor(formal): moduleize flow semantics
```

Verify exact-head GCC/Clang CI before Task 7.

---

## Task 7 — M2b: Convert `Graph` and take the M2 checkpoint

**Files**

- Modify: `formal/CMeta/Graph.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Imports:**

```lean
module
public import all CMeta.Flow
public import all CMeta.Callable
```

**Produces public semantic API:** `RelationResult`, `TypedBranches`, `TypedBranches.erase`, `checkBranches`, `ErasedRelation`, `TypedRelation`, `TypedRelation.erase`, `checkRelation`, `ErasedStage`, `TypedGraph`, `TypedGraph.stages`, `checkGraph`.

`checkBranchTail` and `checkBranchTail_typed` stay private.

**Private now:** `TypedBranches.check_erase`, `TypedRelation.progress`, `TypedRelation.output_unique`, `TypedGraph.progress`, `TypedGraph.output_unique`.

**Temporary bridges:** `TypedRelation.check_erase` through M6; `TypedGraph.check_stages` through M5.

- [ ] **RED:** add Graph public checks and assertions for immediately private proof plumbing:

```lean
import CMeta.Graph

#check CMeta.RelationResult
#check CMeta.TypedBranches
#check CMeta.TypedRelation
#check CMeta.checkRelation
#check CMeta.TypedGraph
#check CMeta.checkGraph

assert_not_exists CMeta.TypedBranches.check_erase
assert_not_exists CMeta.TypedRelation.progress
assert_not_exists CMeta.TypedRelation.output_unique
assert_not_exists CMeta.TypedGraph.progress
assert_not_exists CMeta.TypedGraph.output_unique
```

Verify RED.

- [ ] **GREEN:** apply the two imports above, mark the semantic list public, and mark only these two proof bridges public:

```text
TEMP-MODULE-BRIDGE(M6): legacy StructuredConformance.typed_relation_valid
TEMP-MODULE-BRIDGE(M5): legacy EndToEnd.structured_graph_type_safe
```

on `TypedRelation.check_erase` and `TypedGraph.check_stages`.

- [ ] Run focused + full M2 gate:

```bash
cd formal
lake env lean CMeta/Graph.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit/push:

```text
refactor(formal): moduleize graph semantics
```

Verify exact-head GCC/Clang CI.

---

## Task 8 — M3: Convert `Optimize` and `Lowering`, then remove the M3 Flow bridges

**Files**

- Modify: `formal/CMeta/Optimize.lean`
- Modify: `formal/CMeta/Lowering.lean`
- Modify: `formal/CMeta/Flow.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

### Cycle A — Optimize

**Public semantic API:** `MapChain`, `MapChain.run`, `MapChain.signatures`, `MapChain.check`, `FusedMap`, `canonicalizeMapLike`, `IdempotentEndomap`.

**Private now:** `MapChain.run_cons`, `canonicalizeMapLike_preserves_type`, `duplicate_idempotent_elimination_type`.

**Temporary:** `MapChain.check_signatures` through M4, `FusedMap.type_preserved` through M5, `duplicate_idempotent_elimination_sound` through M6, and `@[expose] MapChain.check` through M4.

- [ ] RED checks:

```lean
import CMeta.Optimize
#check CMeta.MapChain
#check CMeta.MapChain.check
#check CMeta.FusedMap
#check CMeta.canonicalizeMapLike
#check CMeta.IdempotentEndomap
assert_not_exists CMeta.MapChain.run_cons
assert_not_exists CMeta.canonicalizeMapLike_preserves_type
assert_not_exists CMeta.duplicate_idempotent_elimination_type
```

- [ ] Convert Optimize with:

```lean
module
public import all CMeta.Graph
import all CMeta.Flow
```

- [ ] Add exact markers to the three theorem bridges and to the temporary exposed body:

```text
TEMP-MODULE-BRIDGE(M4): legacy Plan.PlanNode.check_erase
TEMP-MODULE-BRIDGE(M5): legacy EndToEnd.fused_map_type_safe
TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_duplicate_elimination_sound
TEMP-MODULE-BRIDGE(M4): legacy Plan.PlanNode.check_erase unfolds MapChain.check
```

### Cycle B — Lowering

**Public semantic API:** `SurfaceZip`, `ErasedInvokeRelation`, `checkInvokeRelation`, `SurfaceZip.lower`.

**Private now:** `SurfaceZip.lowering_progress`, `SurfaceZip.lowering_output_unique`.

**Temporary theorem bridge:** `SurfaceZip.lowering_preserves_type` through M6.

- [ ] RED checks:

```lean
import CMeta.Lowering
#check CMeta.SurfaceZip
#check CMeta.ErasedInvokeRelation
#check CMeta.checkInvokeRelation
#check CMeta.SurfaceZip.lower
assert_not_exists CMeta.SurfaceZip.lowering_progress
assert_not_exists CMeta.SurfaceZip.lowering_output_unique
```

- [ ] Convert Lowering with:

```lean
module
public import all CMeta.Optimize
import all CMeta.Flow
import all CMeta.Callable
```

- [ ] Keep only `SurfaceZip.lowering_preserves_type` public with:

```text
TEMP-MODULE-BRIDGE(M6): legacy EndToEnd and StructuredConformance
```

### Remove M3 Flow bridges

- [ ] Remove public visibility/markers from `TypedOp.step_exact` and `Pipeline.check_steps`; Optimize/Lowering now import Flow private scope.
- [ ] Add:

```lean
assert_not_exists CMeta.TypedOp.step_exact
assert_not_exists CMeta.Pipeline.check_steps
```

- [ ] Run full M3 gate:

```bash
cd formal
lake env lean CMeta/Optimize.lean
lake env lean CMeta/Lowering.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit/push:

```text
refactor(formal): moduleize optimizer and lowering semantics
```

Verify exact-head GCC/Clang CI.

---

## Task 9 — M4a: Convert `Plan` with within-phase bridges

**Files**

- Modify: `formal/CMeta/Plan.lean`
- Modify: `formal/CMeta/Optimize.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

**Public semantic API:** `PlanOpcode`, `ErasedPlanInst`, `PlanNode`, `PlanNode.erase`, `checkPlanInst`, `PlanProgram`, `PlanProgram.code`, `checkPlan`, `ErasedPlan`, `PlanProgram.compile`, `PlanWellTyped`.

**Private final:** `PlanNode.check_erase`, `transform_compiles_as_map`, `PlanProgram.check_code`, `PlanProgram.compile_well_typed`, `PlanProgram.compile_endpoints`, `PlanProgram.output_unique`.

- [ ] RED assertions for theorem names not needed by still-legacy Execution:

```lean
import CMeta.Plan
#check CMeta.PlanOpcode
#check CMeta.PlanNode
#check CMeta.PlanProgram
#check CMeta.PlanWellTyped
assert_not_exists CMeta.transform_compiles_as_map
assert_not_exists CMeta.PlanProgram.compile_endpoints
assert_not_exists CMeta.PlanProgram.output_unique
```

- [ ] Convert with:

```lean
module
public import all CMeta.Lowering
import all CMeta.Optimize
```

- [ ] Keep `PlanNode.check_erase` and `PlanProgram.compile_well_typed` public only until Task 10, both marked:

```text
TEMP-MODULE-BRIDGE(M4): legacy Execution; remove Task 10
```

- [ ] Mark `PlanProgram.compile` and `PlanWellTyped` with the exact M5 exposed-body marker and `@[expose] public` because legacy EndToEnd unfolds them:

```text
TEMP-MODULE-BRIDGE(M5): legacy EndToEnd unfolds compiled plan semantics
```

- [ ] Remove the M4 bridge/public theorem visibility from `MapChain.check_signatures` and remove `@[expose]` from `MapChain.check`; Plan now has `import all CMeta.Optimize`.
- [ ] Add `assert_not_exists CMeta.MapChain.check_signatures` to migration conformance.
- [ ] Run focused GREEN:

```bash
cd formal
lake env lean CMeta/Plan.lean
lake env lean CMeta/ModuleMigrationConformance.lean
```

- [ ] Commit locally, do not push until Task 10:

```text
refactor(formal): moduleize plan semantics
```

---

## Task 10 — M4b: Convert `Execution` and `Cardinality`, remove within-M4 Plan bridges, take M4 checkpoint

**Files**

- Modify: `formal/CMeta/Execution.lean`
- Modify: `formal/CMeta/Cardinality.lean`
- Modify: `formal/CMeta/Plan.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`

### Execution

**Public semantic API:** `ValueVec`, `PackedVec`, `CompletedGenerator`, `ExecInst`, `reduceValues`, `ExecInst.run`, `ExecInst.planNode`, `RuntimeInst`, `ExecInst.runtime`, `runRuntimeInst`, `ExecProgram`, `ExecProgram.run`, `ExecProgram.planProgram`, `ExecProgram.runtimeCode`, `runRuntimePlan`.

**Private now:** `ExecInst.map_length`, `ExecInst.reduce_length_le_one`, `ExecInst.planNode_checked`, `runRuntimeInst_output`, `ExecInst.runtime_exact`.

**Temporary M5 theorem bridges:** `ExecProgram.runtime_execution_exact`, `ExecProgram.result_type_safe`, `ExecProgram.compiled_plan_well_typed`.

- [ ] RED checks/assertions for the immediately private group.
- [ ] Convert with:

```lean
module
public import all CMeta.Plan
```

- [ ] Add exact M5 comments/public visibility to the three EndToEnd-consumed theorem bridges.
- [ ] Remove public visibility/markers from `PlanNode.check_erase` and `PlanProgram.compile_well_typed`; Execution now imports Plan private scope.
- [ ] Add assertions proving both Plan theorem bridges are hidden.

### Cardinality

Cardinality contributes no approved public semantic API in Plan A. Its own definitions/theorems remain private.

- [ ] Convert it with the temporary EndToEnd reachability bridge:

```lean
module
-- TEMP-MODULE-BRIDGE(M5): legacy EndToEnd reaches Execution through Cardinality
public import all CMeta.Execution
```

Do not mark `reduceCount` or any `ExecInst.*_cardinality` theorem public.

- [ ] Extend migration conformance with a direct `import CMeta.Cardinality` and verify:

```lean
assert_not_exists CMeta.ExecInst.filter_cardinality
assert_not_exists CMeta.ExecInst.map_cardinality
assert_not_exists CMeta.ExecInst.flatMap_cardinality
assert_not_exists CMeta.ExecInst.reduce_cardinality
```

Do not assert `ExecProgram` absence yet because the audited M5 re-export bridge intentionally exposes Execution semantics through Cardinality.

- [ ] Run M4 full gate:

```bash
cd formal
lake env lean CMeta/Execution.lean
lake env lean CMeta/Cardinality.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit/push the M4 GREEN stack and verify exact-head GCC/Clang CI:

```text
refactor(formal): moduleize execution and cardinality semantics
```

---

## Task 11 — M5: Convert `EndToEnd`, create `Semantics`, convert `PublicProof`, establish partial hard isolation, update CI guard

**Files**

- Modify: `formal/CMeta/EndToEnd.lean`
- Create: `formal/CMeta/Semantics.lean`
- Modify: `formal/CMeta/PublicProof.lean`
- Modify: `formal/CMeta/PublicProofConformance.lean`
- Create: `formal/CMeta/PublicProofIsolationConformance.lean`
- Modify: `formal/CMeta/ModuleMigrationConformance.lean`
- Modify: `formal/CMeta/Cardinality.lean`
- Modify: `formal/CMeta/Graph.lean`
- Modify: `formal/CMeta/Optimize.lean`
- Modify: `formal/CMeta/Execution.lean`
- Modify: `formal/CMeta/Plan.lean`
- Modify: `formal/CMeta.lean`
- Modify: `.github/workflows/lean.yml`

### RED — current facade still leaks internal names

- [ ] Create the initial client-style isolation test as legacy source first:

```lean
import CMeta.PublicProof

#check CMeta.PublicProof.structured_graph_type_safe
#check CMeta.PublicProof.zip_lowering_type_safe
#check CMeta.PublicProof.fused_map_type_safe
#check CMeta.PublicProof.direct_plan_exact
#check CMeta.PublicProof.runtime_output_type_safe
#check CMeta.PublicProof.static_checker_matches_runtime

assert_not_exists CMeta.EndToEnd.direct_plan_exact
assert_not_exists CMeta.TypedGraph.check_stages
assert_not_exists CMeta.FusedMap.type_preserved
assert_not_exists CMeta.ExecProgram.runtime_execution_exact
#check_assertions
```

- [ ] Import it from legacy `formal/CMeta.lean` and run:

```bash
cd formal
lake env lean CMeta/PublicProofIsolationConformance.lean
```

Expected RED: internal names are visible through the legacy PublicProof import chain.

### GREEN — EndToEnd private implementation

- [ ] Convert `EndToEnd.lean` to:

```lean
module
import all CMeta.Cardinality
import all CMeta.Graph
import all CMeta.Lowering
import all CMeta.Optimize
import all CMeta.Plan
import all CMeta.Execution
```

Leave every `EndToEnd.*` theorem private, including `direct_plan_endpoints`.

### GREEN — remove M5 bridges

- [ ] `Cardinality.lean`: replace the public re-export bridge with only:

```lean
import all CMeta.Execution
```

- [ ] `Graph.lean`: remove public visibility/marker from `TypedGraph.check_stages`.
- [ ] `Optimize.lean`: remove public visibility/marker from `FusedMap.type_preserved`.
- [ ] `Execution.lean`: remove public visibility/markers from `runtime_execution_exact`, `result_type_safe`, `compiled_plan_well_typed`.
- [ ] `Plan.lean`: remove temporary `@[expose]` and M5 marker from `PlanProgram.compile` and `PlanWellTyped`; keep those declarations public but their bodies unexposed.
- [ ] Add the corresponding negative assertions to `ModuleMigrationConformance.lean`.

Do **not** remove the M6 bridges `TypedRelation.check_erase`, `SurfaceZip.lowering_preserves_type`, or `duplicate_idempotent_elimination_sound` yet.

### GREEN — Semantics and PublicProof

- [ ] Create `Semantics.lean` exactly as a no-declaration aggregator:

```lean
module
public import CMeta.Calculus
public import CMeta.Traits
public import CMeta.Callable
public import CMeta.Lambda
public import CMeta.Dispatch
public import CMeta.Flow
public import CMeta.Graph
public import CMeta.Optimize
public import CMeta.Lowering
public import CMeta.Plan
public import CMeta.Execution
```

- [ ] Convert `PublicProof.lean` header to:

```lean
module
public import CMeta.Semantics
import all CMeta.EndToEnd
```

Place the existing five aliases and six wrapper theorems inside `public section`; do not alter their statements/bodies except namespace qualification forced by the module elaborator.

- [ ] Convert `PublicProofConformance.lean` to:

```lean
module
import CMeta.PublicProof
```

Its conformance theorems remain private/default.

- [ ] Convert `ModuleMigrationConformance.lean` itself to `module`; all of its imports are now modules. Keep its accumulated positive/negative assertions.
- [ ] Convert `PublicProofIsolationConformance.lean` to `module` and keep the partial negative set above. Do not yet assert the three M6 bridges absent.

### Update workflow guard

- [ ] Replace the old single-import equality check with explicit checks:

```bash
grep -qx 'public import CMeta.Semantics' formal/CMeta/PublicProof.lean
grep -qx 'import all CMeta.EndToEnd' formal/CMeta/PublicProof.lean
grep -qx 'import CMeta.PublicProof' formal/CMeta/PublicProofConformance.lean
grep -qx 'import CMeta.PublicProof' formal/CMeta/PublicProofIsolationConformance.lean
```

Retain the forbidden-vocabulary scan for `PreprocessorBackend|Registry|NestedReplay|OptimizerTopology` over both facade/conformance files.

- [ ] Add a CI invocation before full Lake build:

```bash
cd formal
lake env lean CMeta/PublicProofIsolationConformance.lean
```

or equivalent workflow working-directory form.

### M5 verification

- [ ] Run:

```bash
cd formal
lake env lean CMeta/PublicProofConformance.lean
lake env lean CMeta/PublicProofIsolationConformance.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit/push:

```text
refactor(formal): enforce module public proof facade
```

Verify exact-head GCC/Clang CI. The three documented M6 proof bridges may still be public; every other M1–M5 bridge scheduled for removal must be gone.

---

## Task 12 — M6a: Moduleize base/plan generated snapshots and direct-plan conformance closure

**Files**

- Modify: `formal/cmeta_conformance_witness.c`
- Modify: `formal/CMeta/GeneratedC.lean`
- Modify: `formal/cmeta_plan_conformance_witness.c`
- Modify: `formal/CMeta/PlanGeneratedC.lean`
- Modify: `formal/CMeta/Conformance.lean`
- Modify: `formal/CMeta/PlanConformance.lean`
- Modify: `formal/CMeta/RuntimeConformance.lean`

### RED — generated source is still legacy

- [ ] Before changing C output, prepend `module` manually to a temporary working copy or change committed `GeneratedC.lean` first and run its importing conformance as a module. Verify the intended failure is that the module import chain reaches a legacy generated/conformance file. Do not commit this RED state.

### GREEN — generator framing

For each of these two C witnesses:

```text
formal/cmeta_conformance_witness.c
formal/cmeta_plan_conformance_witness.c
```

- [ ] At the exact point before the existing first emitted `import Std`, emit:

```c
fputs("module\n\n", stdout);
```

Do not change any later emitted witness data.

- [ ] Regenerate/update committed snapshots so they are exactly:

```text
module\n\n + previous committed source
```

for:

```text
formal/CMeta/GeneratedC.lean
formal/CMeta/PlanGeneratedC.lean
```

- [ ] Verify payload identity against the parent commit:

```bash
python - <<'PY'
from pathlib import Path
import subprocess
for path in ["formal/CMeta/GeneratedC.lean", "formal/CMeta/PlanGeneratedC.lean"]:
    old = subprocess.check_output(["git", "show", f"HEAD:{path}"], text=True)
    new = Path(path).read_text()
    assert new == "module\n\n" + old, path
print("module framing only: ok")
PY
```

Run this before committing; if Task 12 has prior local commits, use the exact pre-Task-12 SHA instead of `HEAD` and record it in the task notes.

### GREEN — conformance modules

- [ ] Convert:

`Conformance.lean`:

```lean
module
import all CMeta.EndToEnd
import all CMeta.GeneratedC
```

`PlanConformance.lean`:

```lean
module
import all CMeta.Conformance
import all CMeta.PlanGeneratedC
```

`RuntimeConformance.lean`:

```lean
module
import all CMeta.PlanConformance
import all CMeta.Execution
```

All declarations in these conformance files remain private/default; there is no public implementation-gate API in Plan A.

- [ ] Configure/build both formal presets and regenerate these two snapshots:

```bash
cmake --preset formal-linux-gcc
cmake --build --preset build-formal-linux-gcc --target cmeta_header_conformance_witness cmeta_plan_conformance_witness
build/formal-linux-gcc/bin/cmeta_header_conformance_witness > /tmp/GeneratedC.lean
build/formal-linux-gcc/bin/cmeta_plan_conformance_witness > /tmp/PlanGeneratedC.lean
diff -u formal/CMeta/GeneratedC.lean /tmp/GeneratedC.lean
diff -u formal/CMeta/PlanGeneratedC.lean /tmp/PlanGeneratedC.lean

cmake --preset formal-linux-clang
cmake --build --preset build-formal-linux-clang --target cmeta_header_conformance_witness cmeta_plan_conformance_witness
build/formal-linux-clang/bin/cmeta_header_conformance_witness > /tmp/GeneratedC.clang.lean
build/formal-linux-clang/bin/cmeta_plan_conformance_witness > /tmp/PlanGeneratedC.clang.lean
diff -u formal/CMeta/GeneratedC.lean /tmp/GeneratedC.clang.lean
diff -u formal/CMeta/PlanGeneratedC.lean /tmp/PlanGeneratedC.clang.lean
```

- [ ] Run `cd formal && lake build --wfail`.
- [ ] Commit locally:

```text
refactor(formal): moduleize direct conformance snapshots
```

Do not take final Plan A checkpoint until Task 13.

---

## Task 13 — M6b: Moduleize structured/optimizer generated closure, remove final bridges, strengthen isolation, full exact-head checkpoint

**Files**

Generated C witnesses:

- `formal/cmeta_structured_conformance_witness.c`
- `formal/cmeta_structured_policy_conformance_witness.c`
- `formal/cmeta_optimizer_conformance_witness.c`
- `formal/cmeta_optimizer_gating_conformance_witness.c`
- `formal/cmeta_optimizer_topology_conformance_witness.c`

Generated Lean snapshots:

- `formal/CMeta/StructuredGeneratedC.lean`
- `formal/CMeta/StructuredPolicyGeneratedC.lean`
- `formal/CMeta/OptimizerGeneratedC.lean`
- `formal/CMeta/OptimizerGatingGeneratedC.lean`
- `formal/CMeta/OptimizerTopologyGeneratedC.lean`

Conformance modules:

- `formal/CMeta/StructuredConformance.lean`
- `formal/CMeta/StructuredPolicyConformance.lean`
- `formal/CMeta/OptimizerConformance.lean`
- `formal/CMeta/OptimizerGatingConformance.lean`
- `formal/CMeta/OptimizerTopologyConformance.lean`

Bridge/source cleanup:

- `formal/CMeta/Callable.lean`
- `formal/CMeta/Graph.lean`
- `formal/CMeta/Optimize.lean`
- `formal/CMeta/Lowering.lean`
- `formal/CMeta/PublicProofIsolationConformance.lean`
- `formal/CMeta/ModuleMigrationConformance.lean`
- `.github/workflows/lean.yml`
- `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md`
- `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-plan-a-amendment.md`

### Cycle A — generated framing

- [ ] For all five C witnesses, emit exactly:

```c
fputs("module\n\n", stdout);
```

before the existing first generated `import Std` and change no semantic payload emission.

- [ ] Update all five committed generated files to exactly `module\n\n + previous source` and run the same Python payload-identity assertion from Task 12 across all five paths.

### Cycle B — conformance modules

Convert with these exact private implementation imports:

`StructuredConformance.lean`:

```lean
module
import all CMeta.RuntimeConformance
import all CMeta.StructuredGeneratedC
import all CMeta.Graph
import all CMeta.Lowering
```

`StructuredPolicyConformance.lean`:

```lean
module
import all CMeta.StructuredConformance
import all CMeta.StructuredPolicyGeneratedC
```

`OptimizerConformance.lean`:

```lean
module
import all CMeta.StructuredPolicyConformance
import all CMeta.OptimizerGeneratedC
import all CMeta.Optimize
import all CMeta.Callable
```

`OptimizerGatingConformance.lean`:

```lean
module
import all CMeta.OptimizerConformance
import all CMeta.OptimizerGatingGeneratedC
```

`OptimizerTopologyConformance.lean`:

```lean
module
import all CMeta.OptimizerGatingConformance
import all CMeta.OptimizerTopologyGeneratedC
```

All their declarations remain private/default.

### Remove final M6 bridges

- [ ] `Graph.lean`: remove public visibility/marker from `TypedRelation.check_erase`.
- [ ] `Lowering.lean`: remove public visibility/marker from `SurfaceZip.lowering_preserves_type`.
- [ ] `Optimize.lean`: remove public visibility/marker from `duplicate_idempotent_elimination_sound`.
- [ ] `Callable.lean`: remove temporary `@[expose]` and M6 markers from `HArgs.one`, `Callable.ofUnary`, `Callable.invoke1`; keep the names public, bodies unexposed.

### Strengthen hard PublicProof isolation

- [ ] Extend `PublicProofIsolationConformance.lean` so a module importing only `CMeta.PublicProof` positively checks all six public wrappers and negatively checks at least:

```lean
assert_not_exists CMeta.EndToEnd.direct_plan_exact
assert_not_exists CMeta.TypedGraph.check_stages
assert_not_exists CMeta.TypedRelation.check_erase
assert_not_exists CMeta.SurfaceZip.lowering_preserves_type
assert_not_exists CMeta.FusedMap.type_preserved
assert_not_exists CMeta.duplicate_idempotent_elimination_sound
assert_not_exists CMeta.ExecProgram.runtime_execution_exact
#check_assertions
```

This satisfies the required Graph, Lowering, Optimize, Execution, and EndToEnd coverage.

- [ ] Add the same final bridge names to `ModuleMigrationConformance.lean` where appropriate so direct module clients also prove proof-surface contraction.

### Snapshot and full local verification

Build the full witness list for GCC and Clang:

```text
cmeta_header_conformance_witness
cmeta_type_identity_conformance_witness
cmeta_descriptor_bridge_conformance_witness
cmeta_type_identity_multi_tu
cmeta_type_universe_probe
cmeta_traits_row_syntax_witness
cmeta_operator_row_syntax_witness
cmeta_fmt_args_simplification_witness
cmeta_producer_replay_witness
cmeta_nested_replay_deferred_witness
cmeta_plan_conformance_witness
cmeta_structured_conformance_witness
cmeta_structured_policy_conformance_witness
cmeta_optimizer_conformance_witness
cmeta_optimizer_gating_conformance_witness
cmeta_optimizer_topology_conformance_witness
```

- [ ] Run the workflow-equivalent generated snapshot comparisons for `GeneratedC`, descriptor/type identity, compiler-specific nested replay, Plan, Structured, StructuredPolicy, Optimizer, OptimizerGating, and OptimizerTopology. Every `diff -u` must be zero.
- [ ] Run all applicability binaries currently listed in `.github/workflows/lean.yml`.
- [ ] Run:

```bash
cd formal
lake env lean CMeta/PublicProofConformance.lean
lake env lean CMeta/PublicProofIsolationConformance.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

### Final static audit

- [ ] Run:

```bash
git grep -n "allowImportAll" -- formal/lakefile.toml formal/CMeta.lean formal/CMeta || true
git grep -nE 'backward\.(privateInPublic|proofsInPublic)' -- formal || true
git grep -n '@\[expose\]' -- formal/CMeta || true
git grep -n 'TEMP-MODULE-BRIDGE' -- formal docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-plan-a-amendment.md || true
```

Expected in production Lean sources:

- no `allowImportAll`;
- no backward compatibility options;
- no migration-only `@[expose]`;
- no `TEMP-MODULE-BRIDGE` marker.

The amendment document itself intentionally contains the phrase `TEMP-MODULE-BRIDGE`; when auditing, distinguish documentation from production by also requiring:

```bash
! git grep -n 'TEMP-MODULE-BRIDGE' -- formal/CMeta
```

- [ ] Verify every Plan A module has a `module` header:

```bash
git grep -n '^module$' -- \
  formal/CMeta/Calculus.lean \
  formal/CMeta/Traits.lean \
  formal/CMeta/Callable.lean \
  formal/CMeta/Lambda.lean \
  formal/CMeta/Dispatch.lean \
  formal/CMeta/Flow.lean \
  formal/CMeta/Graph.lean \
  formal/CMeta/Optimize.lean \
  formal/CMeta/Lowering.lean \
  formal/CMeta/Plan.lean \
  formal/CMeta/Execution.lean \
  formal/CMeta/Cardinality.lean \
  formal/CMeta/EndToEnd.lean \
  formal/CMeta/Semantics.lean \
  formal/CMeta/PublicProof.lean \
  formal/CMeta/PublicProofConformance.lean \
  formal/CMeta/PublicProofIsolationConformance.lean \
  formal/CMeta/ModuleMigrationConformance.lean \
  formal/CMeta/GeneratedC.lean \
  formal/CMeta/PlanGeneratedC.lean \
  formal/CMeta/StructuredGeneratedC.lean \
  formal/CMeta/StructuredPolicyGeneratedC.lean \
  formal/CMeta/OptimizerGeneratedC.lean \
  formal/CMeta/OptimizerGatingGeneratedC.lean \
  formal/CMeta/OptimizerTopologyGeneratedC.lean \
  formal/CMeta/Conformance.lean \
  formal/CMeta/PlanConformance.lean \
  formal/CMeta/RuntimeConformance.lean \
  formal/CMeta/StructuredConformance.lean \
  formal/CMeta/StructuredPolicyConformance.lean \
  formal/CMeta/OptimizerConformance.lean \
  formal/CMeta/OptimizerGatingConformance.lean \
  formal/CMeta/OptimizerTopologyConformance.lean
```

Expected: one matching header per listed file.

- [ ] Confirm `formal/CMeta.lean` is still legacy (no top-level `module`) and still imports the full verification closure. Do not create `InternalChecks`.

### Documentation status and commit

- [ ] Only after all local verification passes, update the main design spec status to:

```text
Plan A implemented and exact-head verified; Plan B pending
```

and update the execution amendment status to the same Plan A completion state.

- [ ] Commit all final M6 work with scoped staging only:

```text
refactor(formal): close CFlow module proof boundary
```

### Exact-head GitHub verification

- [ ] Push `leanv4`.
- [ ] Fetch PR #3 and record exact `head_sha`.
- [ ] Fetch the `Lean proofs` run associated with exactly that SHA. Do not accept a successful run from an older head.
- [ ] Require `status=completed` and `conclusion=success`.
- [ ] Require both jobs:

```text
Lean 4 / kernel check (gcc)
Lean 4 / kernel check (clang)
```

completed/success.
- [ ] In both jobs require successful steps for:
  - Configure formal build
  - Build C conformance witnesses
  - Verify C/Lean conformance snapshots
  - Execute applicability probes
  - Reject proof placeholders
  - Reject arity-specific callable formal APIs
  - Enforce public proof facade boundary / isolation
  - Build and kernel-check Lean proofs
- [ ] Only after exact-head success declare Plan A complete. If CI fails before `lake build --wfail` due to transient Lean toolchain download/install infrastructure, rerun the failed job without changing code; judge code only from runs that reach the kernel build.

---

## Plan A Done Means

A downstream client can intentionally use:

```lean
import CMeta.PublicProof
```

and receives the curated semantic vocabulary plus the six stable `CMeta.PublicProof` theorems, while representative graph/lowering/optimizer/execution/EndToEnd proof plumbing is genuinely absent from that client's scope. The legacy root still kernel-checks every pre-existing formal proof and C-derived conformance path. Plan B may then migrate the independent Producer/Replay/Registry/LanguageSpec tree and perform the final `CMeta.InternalChecks` + root-module conversion.
