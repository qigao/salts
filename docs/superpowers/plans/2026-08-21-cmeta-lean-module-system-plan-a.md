# CMeta Lean Module-System Plan A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Migrate the CFlow semantic/PublicProof tree (M1–M6) to Lean 4.30 modules with explicit public semantic API, private proof machinery, hard PublicProof isolation, and unchanged C witness semantics.

**Architecture:** Convert the CFlow semantic spine bottom-up. Semantic carriers/functions required to state/use public proofs are public; proof plumbing is private. Until an identified legacy consumer can use `import all`, only the audited `TEMP-MODULE-BRIDGE(M<n>)` declarations/re-exports/exposures may remain visible. `EndToEnd` becomes private proof implementation, `Semantics` re-exports curated semantic scopes, and `PublicProof` remains the stable six-theorem facade. The legacy `CMeta.lean` remains the full-build root throughout Plan A. M6 converts the generated/conformance closure, removes every bridge, and proves client isolation.

**Tech Stack:** Lean 4.30.0, Lake, C11/CMake, GCC/Clang, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md`

**Execution amendment:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-plan-a-amendment.md`

## Global Constraints

- Do not change CMeta/CFlow runtime semantics, operator semantics, or theorem statements except visibility/import qualification forced by modules.
- Do not create duplicate/opaque carriers. Existing `CType`, `Callable`, `TypedGraph`, `SurfaceZip`, `FusedMap`, `ExecProgram`, `PackedVec`, plan/runtime structures remain authoritative.
- Do not convert `formal/CMeta.lean` to `module` and do not create `CMeta.InternalChecks`; both are Plan B.
- Never enable `allowImportAll`, `backward.privateInPublic`, or `backward.proofsInPublic`.
- Use `public import all X` when both re-exported public semantics and package-private proof/body access are required; `import all X` for private package implementation only; `public import X` for semantic re-export only.
- Every temporary bridge must carry `TEMP-MODULE-BRIDGE(M<n>): <consumer>` and be removed at the named phase.
- Authorized temporary body exposures only: `HArgs.one`, `Callable.ofUnary`, `Callable.invoke1` through M6; `MapChain.check` through M4; `PlanProgram.compile`, `PlanWellTyped` through M5.
- Generated Lean snapshots may change only by module framing. Data after the framing prefix must remain byte-for-byte identical.
- Commit only GREEN states. Local commits inside a phase are allowed; push only phase checkpoints.

## Bridge Removal Ledger

| Bridge | Remove |
|---|---|
| `dispatch_sound`, `dispatch_policy_sound` public | Task 6 / M2 |
| `TypedOp.step_exact`, `Pipeline.check_steps` public | Task 8 / M3 |
| `TypedGraph.check_stages` public | Task 11 / M5 |
| `TypedRelation.check_erase` public | Task 13 / M6 |
| `MapChain.check_signatures` public | Task 9 / M4 |
| `FusedMap.type_preserved` public | Task 11 / M5 |
| `SurfaceZip.lowering_preserves_type` public | Task 13 / M6 |
| `duplicate_idempotent_elimination_sound` public | Task 13 / M6 |
| `PlanNode.check_erase`, `PlanProgram.compile_well_typed` public | Task 10 / M4 |
| `ExecProgram.runtime_execution_exact`, `result_type_safe`, `compiled_plan_well_typed` public | Task 11 / M5 |
| `Cardinality` public re-export of `Execution` | Task 11 / M5 |
| `RuntimeConformance` public re-export of `Execution` | Task 13 / M6 |
| exposed bodies `HArgs.one`, `Callable.ofUnary`, `Callable.invoke1` | Task 13 / M6 |
| exposed body `MapChain.check` | Task 9 / M4 |
| exposed bodies `PlanProgram.compile`, `PlanWellTyped` | Task 11 / M5 |

---

## Task 1 — M1a: Calculus + migration conformance harness

**Files:** create `formal/CMeta/ModuleMigrationConformance.lean`; modify `formal/CMeta/Calculus.lean`, `formal/CMeta.lean`.

**Public:** `product`, `CoreExpr`, `CoreExpr.eval`, `CoreExpr.cardinality`, `ppRepeat`, `replay`.

**Private:** `product_length`, `CoreExpr.eval_length_eq_cardinality`, `map_cardinality`, `append_cardinality`, `product_cardinality`, `ppRepeat_length`, `ppRepeat_index_domain`, `ppRepeat_indices_unique`, `replay_length`, `replay_zip`.

- [ ] RED: create:

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

Import `CMeta.ModuleMigrationConformance` from legacy `formal/CMeta.lean`.

- [ ] Run `cd formal && lake env lean CMeta/ModuleMigrationConformance.lean`; confirm RED is the visibility assertion against legacy Calculus.
- [ ] GREEN: `Calculus.lean` begins:

```lean
module
import Std
```

Add `public` only to the six public declarations above; theorem bodies remain unchanged/private; no `@[expose]`.

- [ ] Run:

```bash
cd formal
lake env lean CMeta/Calculus.lean
lake env lean CMeta/ModuleMigrationConformance.lean
```

- [ ] Commit locally: `refactor(formal): moduleize calculus surface`. Do not push before Task 5.

---

## Task 2 — M1b: Traits

**Files:** `formal/CMeta/Traits.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

**Header:**

```lean
module
import CMeta.Calculus
```

Do not public-re-export Calculus.

**Public:** `CType`, `Signature`, `Traits`, `Traits.inferUnary`, `SignaturePolicy`, `policyAllows`.

**Private:** `Traits.type_unique`, `Traits.inferUnary_of_known`, `Traits.inferUnary_unique`, `policyAllows_iff`.

- [ ] RED: add `import CMeta.Traits`, `#check` every public name above, then:

```lean
assert_not_exists CMeta.Traits.type_unique
assert_not_exists CMeta.Traits.inferUnary_of_known
assert_not_exists CMeta.Traits.inferUnary_unique
assert_not_exists CMeta.policyAllows_iff
```

Keep one final `#check_assertions`; run the conformance file and inspect the intended legacy-export failure.

- [ ] GREEN: add the header and public modifiers exactly as listed; do not change bodies.
- [ ] Run focused `lake env lean CMeta/Traits.lean` and migration conformance.
- [ ] Commit locally: `refactor(formal): moduleize trait semantics`. Do not push before Task 5.

---

## Task 3 — M1c: Callable + audited M6 body bridge

**Files:** `formal/CMeta/Callable.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

**Header:**

```lean
module
public import CMeta.Traits
```

**Public:** `CType.denote`, `HArgs`, `HArgs.one`, `HArgs.two`, `HArgs.append`, `HArgs.snoc`, `Callable`, `Callable.ofUnary`, `Callable.ofBinary`, `Callable.invoke`, `Callable.invoke1`, `Callable.invoke2`, `Callable.unaryBackendSignature`, `Callable.binaryBackendSignature`, `Callable.compose`, `Generator`, `Generator.signature`, `CallableDesc`, `eraseValue`, `eraseGenerator`.

**Private:** `Callable.compose_beta`, `Generator.signature_exact`, `eraseValue_unary`, `eraseValue_binary`, `eraseGenerator_preserves_signature`.

- [ ] RED: import Callable, `#check` representative public carriers/functions, then assert all five private theorem names absent. Verify the assertions fail against legacy Callable.
- [ ] GREEN: add module/public modifiers without body changes.
- [ ] On the existing declarations `HArgs.one`, `Callable.ofUnary`, and `Callable.invoke1`, add `@[expose] public` and immediately preceding comment:

```text
TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_lists_equal
```

Do not expose any other Callable/HArgs definition.

- [ ] Focused GREEN:

```bash
cd formal
lake env lean CMeta/Callable.lean
lake env lean CMeta/ModuleMigrationConformance.lean
```

- [ ] Commit locally: `refactor(formal): moduleize callable semantics`. Do not push before Task 5.

---

## Task 4 — M1d: Lambda

**Files:** `formal/CMeta/Lambda.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

**Header:**

```lean
module
public import all CMeta.Callable
```

**Public:** `Lambda`, `Lambda.invoke`, `Lambda.asCallable`, `anonymous`, `bindLast`.

**Private:** `Lambda.beta`, `Lambda.erasure_semantics`, `Lambda.erasure_signature_unary`, `Lambda.erasure_signature_binary`, `anonymous_beta`, `bindLast_beta`, `lambda_bind_same_shape`.

- [ ] RED: import Lambda, positive-check every public name, assert all seven proof names absent, verify intended failure.
- [ ] GREEN: moduleize, keep only semantic declarations public. `public import all` is intentional so Lambda proof bodies can unfold Callable privately.
- [ ] Run focused Lambda + migration conformance.
- [ ] Commit locally: `refactor(formal): moduleize lambda semantics`. Do not push before Task 5.

---

## Task 5 — M1e: Dispatch + M1 checkpoint

**Files:** `formal/CMeta/Dispatch.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

Replace current Lambda import with:

```lean
module
public import all CMeta.Traits
```

**Public semantic:** `Operator`, `DispatchRule`, `dispatch`, `OperatorPolicy`, `RulesRespectPolicy`, `composeSignature`, `inferAndAllow`.

**Private now:** `inferAndAllow_known`.

**M2 theorem bridges:** existing `dispatch_sound` and `dispatch_policy_sound` declarations receive `public` without signature/body edits. Add respectively:

```text
TEMP-MODULE-BRIDGE(M2): legacy Flow.ResolvedStep.dispatch_exact
TEMP-MODULE-BRIDGE(M2): legacy Flow.ResolvedStep.policy_safe
```

- [ ] RED: positive-check semantic names; `assert_not_exists CMeta.inferAndAllow_known`; verify legacy failure.
- [ ] GREEN: apply exact header/public/private/bridge rules.
- [ ] Run:

```bash
cd formal
lake env lean CMeta/Dispatch.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Audit:

```bash
! git grep -nE 'backward\.(privateInPublic|proofsInPublic)|allowImportAll' -- formal
```

- [ ] Commit `refactor(formal): moduleize dispatch semantics`, push the M1 stack, and require exact-head GCC/Clang `Lean proofs` success before M2.

---

## Task 6 — M2a: Flow; consume Dispatch bridges

**Files:** `formal/CMeta/Flow.lean`, `formal/CMeta/Dispatch.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

**Header:**

```lean
module
public import all CMeta.Dispatch
```

**Public semantic:** `TypedOp`, `TypedOp.operator`, `TypedOp.signature`, `stepType`, `Pipeline`, `Pipeline.steps`, `Pipeline.length`, `checkPipeline`, `cflowBuiltInPolicy`, `ResolvedStep`, `TargetSignatureUnique`, `WellFormedDispatch`.

**Private now:** `TypedOp.progress`, `TypedOp.output_unique`, `Pipeline.steps_length`, `ResolvedStep.dispatch_exact`, `ResolvedStep.policy_safe`, `ResolvedStep.target_signature_safe`, `ResolvedStep.cannot_target_incompatible`.

**M3 bridges:** existing `TypedOp.step_exact` and `Pipeline.check_steps` declarations get public visibility plus:

```text
TEMP-MODULE-BRIDGE(M3): legacy Optimize.canonicalizeMapLike_preserves_type
TEMP-MODULE-BRIDGE(M3): legacy Lowering.SurfaceZip.lowering_preserves_type
```

- [ ] RED: positive-check the semantic list; assert the immediately-private proof names above absent; verify legacy failure.
- [ ] GREEN: moduleize Flow; remove `public` and M2 comments from Dispatch `dispatch_sound` / `dispatch_policy_sound`.
- [ ] Add:

```lean
assert_not_exists CMeta.dispatch_sound
assert_not_exists CMeta.dispatch_policy_sound
```

- [ ] Run Flow, migration conformance, then `lake build --wfail`.
- [ ] Commit/push `refactor(formal): moduleize flow semantics`; verify exact-head GCC/Clang success.

---

## Task 7 — M2b: Graph + M2 checkpoint

**Files:** `formal/CMeta/Graph.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

**Header:**

```lean
module
public import all CMeta.Flow
public import all CMeta.Callable
```

**Public semantic:** `RelationResult`, `TypedBranches`, `TypedBranches.erase`, `checkBranches`, `ErasedRelation`, `TypedRelation`, `TypedRelation.erase`, `checkRelation`, `ErasedStage`, `TypedGraph`, `TypedGraph.stages`, `checkGraph`.

`checkBranchTail`, `checkBranchTail_typed`, `TypedBranches.check_erase`, `TypedRelation.progress`, `TypedRelation.output_unique`, `TypedGraph.progress`, `TypedGraph.output_unique` are private.

**Bridges:** existing `TypedRelation.check_erase` gets public + `TEMP-MODULE-BRIDGE(M6): legacy StructuredConformance.typed_relation_valid`; existing `TypedGraph.check_stages` gets public + `TEMP-MODULE-BRIDGE(M5): legacy EndToEnd.structured_graph_type_safe`.

- [ ] RED: positive-check public semantic names; assert the private graph proof names above absent; verify legacy failure.
- [ ] GREEN: apply exact header/public/private/bridge rules.
- [ ] Run Graph + migration conformance + `lake build --wfail`.
- [ ] Commit/push `refactor(formal): moduleize graph semantics`; require exact-head GCC/Clang success.

---

## Task 8 — M3: Optimize + Lowering; remove M3 Flow bridges

**Files:** `formal/CMeta/Optimize.lean`, `formal/CMeta/Lowering.lean`, `formal/CMeta/Flow.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

### Optimize RED/GREEN

Header:

```lean
module
public import all CMeta.Graph
import all CMeta.Flow
```

**Public semantic:** `MapChain`, `MapChain.run`, `MapChain.signatures`, `MapChain.check`, `FusedMap`, `canonicalizeMapLike`, `IdempotentEndomap`.

**Private now:** `MapChain.run_cons`, `canonicalizeMapLike_preserves_type`, `duplicate_idempotent_elimination_type`.

**Bridges:**

- existing `MapChain.check_signatures`: public + `TEMP-MODULE-BRIDGE(M4): legacy Plan.PlanNode.check_erase`;
- existing `FusedMap.type_preserved`: public + `TEMP-MODULE-BRIDGE(M5): legacy EndToEnd.fused_map_type_safe`;
- existing `duplicate_idempotent_elimination_sound`: public + `TEMP-MODULE-BRIDGE(M6): legacy OptimizerConformance.identity_duplicate_elimination_sound`;
- existing public `MapChain.check`: temporarily add `@[expose]` + `TEMP-MODULE-BRIDGE(M4): legacy Plan.PlanNode.check_erase unfolds MapChain.check`.

- [ ] RED: positive-check public semantic names; assert the three immediately-private proof names absent; verify failure.
- [ ] GREEN: apply exact module/bridge rules.

### Lowering RED/GREEN

Header:

```lean
module
public import all CMeta.Optimize
import all CMeta.Flow
import all CMeta.Callable
```

**Public semantic:** `SurfaceZip`, `ErasedInvokeRelation`, `checkInvokeRelation`, `SurfaceZip.lower`.

**Private now:** `SurfaceZip.lowering_progress`, `SurfaceZip.lowering_output_unique`.

Existing `SurfaceZip.lowering_preserves_type` is public bridge with `TEMP-MODULE-BRIDGE(M6): legacy EndToEnd and StructuredConformance`.

- [ ] RED: positive-check semantic names; assert the two private theorem names absent; verify failure.
- [ ] GREEN: moduleize and add the one bridge.

### Remove Flow bridges

- [ ] Remove public/markers from `TypedOp.step_exact` and `Pipeline.check_steps`; add both `assert_not_exists` checks.
- [ ] Run Optimize, Lowering, migration conformance, and `lake build --wfail`.
- [ ] Commit/push `refactor(formal): moduleize optimizer and lowering semantics`; require exact-head GCC/Clang success.

---

## Task 9 — M4a: Plan

**Files:** `formal/CMeta/Plan.lean`, `formal/CMeta/Optimize.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

Header:

```lean
module
public import all CMeta.Lowering
import all CMeta.Optimize
```

**Public semantic:** `PlanOpcode`, `ErasedPlanInst`, `PlanNode`, `PlanNode.erase`, `checkPlanInst`, `PlanProgram`, `PlanProgram.code`, `checkPlan`, `ErasedPlan`, `PlanProgram.compile`, `PlanWellTyped`.

**Private final:** `PlanNode.check_erase`, `transform_compiles_as_map`, `PlanProgram.check_code`, `PlanProgram.compile_well_typed`, `PlanProgram.compile_endpoints`, `PlanProgram.output_unique`.

- [ ] RED: positive-check plan semantic names; assert `transform_compiles_as_map`, `compile_endpoints`, `output_unique` absent; verify failure.
- [ ] GREEN: moduleize.
- [ ] Existing `PlanNode.check_erase` and `PlanProgram.compile_well_typed` get public + `TEMP-MODULE-BRIDGE(M4): legacy Execution; remove Task 10`.
- [ ] Existing public `PlanProgram.compile` and `PlanWellTyped` get temporary `@[expose]` + `TEMP-MODULE-BRIDGE(M5): legacy EndToEnd unfolds compiled plan semantics`.
- [ ] Remove public/bridge from `MapChain.check_signatures`; remove temporary `@[expose]` from `MapChain.check`; assert `MapChain.check_signatures` absent.
- [ ] Run Plan + migration conformance focused GREEN.
- [ ] Commit locally `refactor(formal): moduleize plan semantics`; do not push until Task 10.

---

## Task 10 — M4b: Execution + Cardinality; M4 checkpoint

**Files:** `formal/CMeta/Execution.lean`, `formal/CMeta/Cardinality.lean`, `formal/CMeta/Plan.lean`, `formal/CMeta/ModuleMigrationConformance.lean`.

### Execution

Header:

```lean
module
public import all CMeta.Plan
```

**Public semantic:** `ValueVec`, `PackedVec`, `CompletedGenerator`, `ExecInst`, `reduceValues`, `ExecInst.run`, `ExecInst.planNode`, `RuntimeInst`, `ExecInst.runtime`, `runRuntimeInst`, `ExecProgram`, `ExecProgram.run`, `ExecProgram.planProgram`, `ExecProgram.runtimeCode`, `runRuntimePlan`.

**Private now:** `ExecInst.map_length`, `ExecInst.reduce_length_le_one`, `ExecInst.planNode_checked`, `runRuntimeInst_output`, `ExecInst.runtime_exact`.

**M5 bridges:** existing `ExecProgram.runtime_execution_exact`, `ExecProgram.result_type_safe`, `ExecProgram.compiled_plan_well_typed` get public plus exact `TEMP-MODULE-BRIDGE(M5): legacy EndToEnd` comments.

- [ ] RED: positive-check semantic names; assert immediately-private theorem names absent; verify legacy failure.
- [ ] GREEN: moduleize; remove public/markers from Plan `PlanNode.check_erase` and `PlanProgram.compile_well_typed`; assert both absent.

### Cardinality

Header during M4:

```lean
module
-- TEMP-MODULE-BRIDGE(M5): legacy EndToEnd reaches Execution through Cardinality
public import all CMeta.Execution
```

No `Cardinality` declaration is public; `reduceCount` and all cardinality theorems remain private.

- [ ] Directly import Cardinality in migration conformance and assert absent:

```lean
assert_not_exists CMeta.ExecInst.filter_cardinality
assert_not_exists CMeta.ExecInst.map_cardinality
assert_not_exists CMeta.ExecInst.flatMap_cardinality
assert_not_exists CMeta.ExecInst.reduce_cardinality
```

Do not assert `ExecProgram` absent before M5 because the re-export is deliberate.

- [ ] Run:

```bash
cd formal
lake env lean CMeta/Execution.lean
lake env lean CMeta/Cardinality.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

- [ ] Commit/push `refactor(formal): moduleize execution and cardinality semantics`; require exact-head GCC/Clang success.

---

## Task 11 — M5: EndToEnd + Semantics + PublicProof + partial isolation

**Files:** `formal/CMeta/EndToEnd.lean`, create `formal/CMeta/Semantics.lean`, modify `formal/CMeta/PublicProof.lean`, `formal/CMeta/PublicProofConformance.lean`, create `formal/CMeta/PublicProofIsolationConformance.lean`, modify `formal/CMeta/ModuleMigrationConformance.lean`, `formal/CMeta/Cardinality.lean`, `formal/CMeta/Graph.lean`, `formal/CMeta/Optimize.lean`, `formal/CMeta/Execution.lean`, `formal/CMeta/Plan.lean`, `formal/CMeta.lean`, `.github/workflows/lean.yml`.

### RED

Create client isolation source:

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

Import it in legacy `formal/CMeta.lean`. Run it; verify RED is current transitive visibility.

### EndToEnd GREEN

Header:

```lean
module
import all CMeta.Cardinality
import all CMeta.Graph
import all CMeta.Lowering
import all CMeta.Optimize
import all CMeta.Plan
import all CMeta.Execution
```

All seven `EndToEnd.*` theorems remain private/default.

### Remove M5 bridges

- [ ] Cardinality: replace public bridge with `import all CMeta.Execution`.
- [ ] Graph: make `TypedGraph.check_stages` private.
- [ ] Optimize: make `FusedMap.type_preserved` private.
- [ ] Execution: make the three `ExecProgram` M5 bridge theorems private.
- [ ] Plan: remove temporary `@[expose]` from `PlanProgram.compile` and `PlanWellTyped`; declarations stay public.
- [ ] Add corresponding negative assertions to migration conformance.
- [ ] Keep only M6 theorem bridges `TypedRelation.check_erase`, `SurfaceZip.lowering_preserves_type`, `duplicate_idempotent_elimination_sound`.

### Semantics/PublicProof GREEN

`Semantics.lean` exactly:

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

`PublicProof.lean` header:

```lean
module
public import CMeta.Semantics
import all CMeta.EndToEnd
```

Move the existing five aliases and six wrapper theorems into `public section` without statement/body changes.

`PublicProofConformance.lean`:

```lean
module
import CMeta.PublicProof
```

Convert `ModuleMigrationConformance.lean` and `PublicProofIsolationConformance.lean` themselves to modules; keep partial negative isolation until M6.

### CI guard update

Replace old exact single-import check with:

```bash
grep -qx 'public import CMeta.Semantics' formal/CMeta/PublicProof.lean
grep -qx 'import all CMeta.EndToEnd' formal/CMeta/PublicProof.lean
grep -qx 'import CMeta.PublicProof' formal/CMeta/PublicProofConformance.lean
grep -qx 'import CMeta.PublicProof' formal/CMeta/PublicProofIsolationConformance.lean
```

Retain forbidden-vocabulary scan for `PreprocessorBackend|Registry|NestedReplay|OptimizerTopology`. Add explicit CI execution of `lake env lean CMeta/PublicProofIsolationConformance.lean` before full Lake build.

- [ ] Run all three conformance files and `lake build --wfail`.
- [ ] Commit/push `refactor(formal): enforce module public proof facade`; require exact-head GCC/Clang success.

---

## Task 12 — M6a: Generated base/plan snapshots + direct-plan conformance closure

**Files:** `formal/cmeta_conformance_witness.c`, `formal/CMeta/GeneratedC.lean`, `formal/cmeta_plan_conformance_witness.c`, `formal/CMeta/PlanGeneratedC.lean`, `formal/CMeta/Conformance.lean`, `formal/CMeta/PlanConformance.lean`, `formal/CMeta/RuntimeConformance.lean`.

### RED

- [ ] Create a local RED by moduleizing one generated/conformance edge before its dependency; run the importing module and verify Lean rejects the legacy dependency. Revert the RED edit before GREEN. Do not commit RED.

### Generated framing GREEN

For both C witnesses, immediately before the existing first emitted `import Std`, add exactly:

```c
fputs("module\n\n", stdout);
```

Committed `GeneratedC.lean` and `PlanGeneratedC.lean` become exactly `module\n\n` plus their pre-Task-12 bytes.

- [ ] Record pre-Task-12 SHA:

```bash
PRE_M6A=$(git rev-parse HEAD)
```

- [ ] Verify framing-only identity:

```bash
python - "$PRE_M6A" <<'PY'
from pathlib import Path
import subprocess, sys
base = sys.argv[1]
for path in ["formal/CMeta/GeneratedC.lean", "formal/CMeta/PlanGeneratedC.lean"]:
    old = subprocess.check_output(["git", "show", f"{base}:{path}"], text=True)
    new = Path(path).read_text()
    assert new == "module\n\n" + old, path
print("module framing only: ok")
PY
```

### Conformance modules

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

`RuntimeConformance.lean` must temporarily re-export Execution semantics for still-legacy `StructuredConformance`:

```lean
module
import all CMeta.PlanConformance
-- TEMP-MODULE-BRIDGE(M6): legacy StructuredConformance needs CType/Callable/ValueVec semantics
public import all CMeta.Execution
```

All RuntimeConformance declarations remain private/default.

- [ ] Build both GCC/Clang witness targets `cmeta_header_conformance_witness` and `cmeta_plan_conformance_witness`, regenerate snapshots, and require zero `diff -u` against committed files.
- [ ] Run `cd formal && lake build --wfail`; legacy StructuredConformance must remain green through the explicit RuntimeConformance semantic re-export.
- [ ] Commit locally `refactor(formal): moduleize direct conformance snapshots`; do not final-push before Task 13.

---

## Task 13 — M6b: Structured/optimizer closure + final bridge removal + exact-head checkpoint

**Generated C witnesses:** `formal/cmeta_structured_conformance_witness.c`, `formal/cmeta_structured_policy_conformance_witness.c`, `formal/cmeta_optimizer_conformance_witness.c`, `formal/cmeta_optimizer_gating_conformance_witness.c`, `formal/cmeta_optimizer_topology_conformance_witness.c`.

**Generated Lean:** `formal/CMeta/StructuredGeneratedC.lean`, `StructuredPolicyGeneratedC.lean`, `OptimizerGeneratedC.lean`, `OptimizerGatingGeneratedC.lean`, `OptimizerTopologyGeneratedC.lean`.

**Conformance:** `formal/CMeta/StructuredConformance.lean`, `StructuredPolicyConformance.lean`, `OptimizerConformance.lean`, `OptimizerGatingConformance.lean`, `OptimizerTopologyConformance.lean`.

**Cleanup:** `formal/CMeta/RuntimeConformance.lean`, `Callable.lean`, `Graph.lean`, `Optimize.lean`, `Lowering.lean`, `PublicProofIsolationConformance.lean`, `ModuleMigrationConformance.lean`, `.github/workflows/lean.yml`, design spec and execution amendment status.

### Generated framing

- [ ] Record `PRE_M6B=$(git rev-parse HEAD)`.
- [ ] In all five C witnesses, emit `fputs("module\n\n", stdout);` before their existing first generated `import Std`.
- [ ] Prepend exactly `module\n\n` to each committed generated Lean source.
- [ ] Run the Task-12 Python framing-only assertion against `PRE_M6B` for all five files.

### Conformance conversion

Use exact imports:

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

All declarations remain private/default.

### Remove every final bridge

- [ ] RuntimeConformance: replace `public import all CMeta.Execution` with `import all CMeta.Execution`; remove marker.
- [ ] Graph: make `TypedRelation.check_erase` private/remove marker.
- [ ] Lowering: make `SurfaceZip.lowering_preserves_type` private/remove marker.
- [ ] Optimize: make `duplicate_idempotent_elimination_sound` private/remove marker.
- [ ] Callable: remove temporary `@[expose]` and M6 marker from `HArgs.one`, `Callable.ofUnary`, `Callable.invoke1`; names remain public with unexposed bodies.

### Full client isolation

`PublicProofIsolationConformance.lean`, importing only `CMeta.PublicProof`, must positively check all six wrapper theorems and include:

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

Add matching direct-module absence assertions to `ModuleMigrationConformance.lean` where useful.

### Full verification

- [ ] GCC and Clang build this exact target list:

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

- [ ] Run workflow-equivalent snapshot diffs for GeneratedC, DescriptorBridge, TypeIdentity, compiler-specific NestedReplay, Plan, Structured, StructuredPolicy, Optimizer, OptimizerGating, OptimizerTopology; every diff must be zero.
- [ ] Execute all applicability binaries in current `.github/workflows/lean.yml`.
- [ ] Run:

```bash
cd formal
lake env lean CMeta/PublicProofConformance.lean
lake env lean CMeta/PublicProofIsolationConformance.lean
lake env lean CMeta/ModuleMigrationConformance.lean
lake build --wfail
```

### Static audit

Run:

```bash
! git grep -n "allowImportAll" -- formal/lakefile.toml formal/CMeta.lean formal/CMeta
! git grep -nE 'backward\.(privateInPublic|proofsInPublic)' -- formal
! git grep -n '@\[expose\]' -- formal/CMeta
! git grep -n 'TEMP-MODULE-BRIDGE' -- formal/CMeta
```

Expected: all four commands succeed because there are no matches.

Verify module headers:

```bash
git grep -n '^module$' -- \
  formal/CMeta/Calculus.lean formal/CMeta/Traits.lean formal/CMeta/Callable.lean \
  formal/CMeta/Lambda.lean formal/CMeta/Dispatch.lean formal/CMeta/Flow.lean \
  formal/CMeta/Graph.lean formal/CMeta/Optimize.lean formal/CMeta/Lowering.lean \
  formal/CMeta/Plan.lean formal/CMeta/Execution.lean formal/CMeta/Cardinality.lean \
  formal/CMeta/EndToEnd.lean formal/CMeta/Semantics.lean formal/CMeta/PublicProof.lean \
  formal/CMeta/PublicProofConformance.lean formal/CMeta/PublicProofIsolationConformance.lean \
  formal/CMeta/ModuleMigrationConformance.lean formal/CMeta/GeneratedC.lean \
  formal/CMeta/PlanGeneratedC.lean formal/CMeta/StructuredGeneratedC.lean \
  formal/CMeta/StructuredPolicyGeneratedC.lean formal/CMeta/OptimizerGeneratedC.lean \
  formal/CMeta/OptimizerGatingGeneratedC.lean formal/CMeta/OptimizerTopologyGeneratedC.lean \
  formal/CMeta/Conformance.lean formal/CMeta/PlanConformance.lean \
  formal/CMeta/RuntimeConformance.lean formal/CMeta/StructuredConformance.lean \
  formal/CMeta/StructuredPolicyConformance.lean formal/CMeta/OptimizerConformance.lean \
  formal/CMeta/OptimizerGatingConformance.lean formal/CMeta/OptimizerTopologyConformance.lean
```

Expected one top-level `module` match per listed file.

Confirm `formal/CMeta.lean` itself has no top-level `module`, still imports the full verification closure, and no `InternalChecks` exists.

### Documentation + exact head

- [ ] After local verification, set both design spec and execution amendment status to `Plan A implemented and exact-head verified; Plan B pending`.
- [ ] Commit final M6 work: `refactor(formal): close CFlow module proof boundary`.
- [ ] Push `leanv4`; fetch PR #3 exact `head_sha`.
- [ ] Fetch the `Lean proofs` run for exactly that SHA; require `completed/success`.
- [ ] Require both `Lean 4 / kernel check (gcc)` and `(clang)` completed/success, including Configure, C witnesses, snapshot verification, applicability, placeholder guard, callable/lambda guard, public proof boundary/isolation, and `Build and kernel-check Lean proofs`.
- [ ] If a job fails before kernel build solely on pinned Lean toolchain download/install infrastructure, rerun the failed job without code changes; only a run that reaches the kernel step is code evidence.

---

## Plan A Done Means

`import CMeta.PublicProof` provides the curated semantic vocabulary and six stable wrappers, while representative Graph/Lowering/Optimize/Execution/EndToEnd proof machinery is genuinely absent from client scope. The legacy root still kernel-checks the full existing formal stack and all real-C conformance snapshots. Plan B can then migrate Producer/Replay/Registry/LanguageSpec and perform final `InternalChecks` + root-module conversion.
