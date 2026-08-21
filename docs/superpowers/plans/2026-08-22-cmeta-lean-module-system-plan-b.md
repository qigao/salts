# CMeta Lean Module-System Plan B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete M7–M8 of the Lean 4.30 module migration by isolating the Producer/replay/backend/registry proof tree behind `CMeta.LanguageSpec`, then make `CMeta` a true two-entry public root backed by a private full-build `CMeta.InternalChecks` closure.

**Architecture:** Continue the proven Plan A frontier-first pattern. M7 converts the remaining legacy Producer/replay/backend/registry and related generated/conformance files bottom-up, with public visibility only for semantic declarations required by the existing `CMeta.Producer.LanguageSpec` carrier/judgment/rule facade; all proof engineering remains private and is consumed internally through `import all`. M8 creates `InternalChecks`, converts the root, and proves that `import CMeta` exposes only `CMeta.LanguageSpec` and `CMeta.PublicProof` public scopes while still kernel-checking the complete internal tree.

**Tech Stack:** Lean 4.30.0, Lake, C11/CMake, GCC, Clang, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-lean-module-system-migration-design.md`

## Global Constraints

- Do not change CMeta/CFlow runtime semantics, replay semantics, registry semantics, selection semantics, or theorem statements except visibility/import qualification forced by modules.
- Do not introduce a second semantic model or wrapper copies of existing carriers.
- `CMeta.Producer.LanguageSpec`'s existing carrier/judgment/rule facade is the target public surface for the Producer/replay tree.
- `CMeta.PublicProof` remains unchanged as the CFlow proof facade.
- Never enable Lake `allowImportAll`, `backward.privateInPublic`, or `backward.proofsInPublic`.
- Use `public import` only for intentional downstream semantic re-export, `import all` only for package-private proof/body access, and plain `import` for non-reexported semantic dependencies.
- Do not add blanket `public section` outside the stable `LanguageSpec` facade or use `@[expose]` to solve proof-body dependencies.
- Generated Lean snapshots may change only by required module framing; C-derived semantic payload after the framing prefix must remain byte-for-byte identical.
- The legacy `formal/CMeta.lean` remains the full-build root until M7 has a complete module closure.
- Every M7/M8 phase follows inspected RED → minimal GREEN → full exact-head GCC/Clang verification.
- Commit only GREEN official checkpoints. RED evidence may live only on a temporary verification branch/PR.

---

## Task 1 — M7a: Producer/FmtArgs leaf modules and second-tree visibility harness

**Files:**
- Create: `formal/CMeta/LanguageModuleMigrationConformance.lean`
- Modify: `formal/CMeta/Producer.lean`
- Modify: `formal/CMeta/FmtArgs.lean`
- Modify: `formal/CMeta.lean`

**Interfaces:**
- `Producer` remains implementation support for replay proofs; none of its helper theorems becomes part of the stable LanguageSpec facade.
- `FmtArgs` remains an internal semantic/conformance model and is not re-exported by `LanguageSpec`.
- The new conformance file becomes the authoritative M7 visibility harness and is converted to a module in Task 7 after its entire import frontier is moduleized.

- [ ] **Step 1: Write the RED visibility contract**

Create the initial legacy-compatible harness:

```lean
import CMeta.Producer
import CMeta.FmtArgs

#check CMeta.Producer.replay
#check CMeta.Producer.append
#check CMeta.Producer.count
#check CMeta.Producer.storage
#check CMeta.Producer.storageCount
#check CMeta.Producer.canRead

assert_not_exists CMeta.Producer.replay_append
assert_not_exists CMeta.Producer.count_eq_length
assert_not_exists CMeta.Producer.storage_count_eq_count
assert_not_exists CMeta.Producer.canRead_iff

#check CMeta.FmtArgs.Slot
#check CMeta.FmtArgs.legacyStorage
#check CMeta.FmtArgs.normalizedStorage
#check CMeta.FmtArgs.observe
#check CMeta.FmtArgs.legacyDispatch
#check CMeta.FmtArgs.argCountFromStorage
#check CMeta.FmtArgs.canReadRealArg

assert_not_exists CMeta.FmtArgs.legacy_normalized_observational_equivalence
assert_not_exists CMeta.FmtArgs.legacy_dispatch_normalizes
assert_not_exists CMeta.FmtArgs.normalized_guard_implies_physical_bound
```

Import `CMeta.LanguageModuleMigrationConformance` from the legacy root while M7 is in progress.

- [ ] **Step 2: Run RED and inspect the failure**

Run:

```bash
cd formal
lake env lean CMeta/LanguageModuleMigrationConformance.lean
```

Expected: failure only because the listed legacy proof names are still ordinary-import visible. Do not proceed on an import/path error.

- [ ] **Step 3: Moduleize `Producer` with the smallest semantic surface**

Start the file with:

```lean
module
import Std
```

Mark only the six operational definitions public:

```text
Producer.replay
Producer.append
Producer.count
Producer.storage
Producer.storageCount
Producer.canRead
```

Leave every theorem private/default. Do not add `@[expose]`.

- [ ] **Step 4: Moduleize `FmtArgs` without re-exporting Calculus**

Use:

```lean
module
import CMeta.Calculus
```

Mark the representation/observation operations public so direct FmtArgs conformance can state its semantic contract:

```text
FmtArgs.Slot
FmtArgs.legacyStorage
FmtArgs.normalizedStorage
FmtArgs.observe
FmtArgs.legacyDispatch
FmtArgs.argCountFromStorage
FmtArgs.canReadRealArg
```

Leave all equivalence/bounds theorems private/default. Do not public-import `Calculus`.

- [ ] **Step 5: Verify GREEN**

Run:

```bash
cd formal
lake env lean CMeta/Producer.lean
lake env lean CMeta/FmtArgs.lean
lake env lean CMeta/LanguageModuleMigrationConformance.lean
lake build --wfail
```

Expected: all pass; normal imports see the operational API but not proof plumbing.

- [ ] **Step 6: Commit GREEN**

```bash
git add -- formal/CMeta/Producer.lean formal/CMeta/FmtArgs.lean \
  formal/CMeta/LanguageModuleMigrationConformance.lean formal/CMeta.lean
git commit -m "refactor(formal): moduleize producer foundation"
```

---

## Task 2 — M7b: Nested replay semantics and applicability boundary

**Files:**
- Modify: `formal/CMeta/NestedReplay.lean`
- Modify: `formal/CMeta/NestedReplayLowering.lean`
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`

**Interfaces:**
- Public semantic carriers consumed later: `ReplayBackendCapability`, `ReplayIR`, `ReplayIR.sameProducerDepth`.
- Public lowering operations retained because backend-plan and LanguageSpec semantics use them: `lowerSameProducerDepth`, `LoweredReplayIR`, `lowerReplayIR`.
- List-level replay theorems and applicability proof lemmas remain private.

- [ ] **Step 1: Extend RED assertions**

Add positive checks for:

```lean
#check CMeta.Producer.nestedReplay
#check CMeta.Producer.ReplayBackendCapability
#check CMeta.Producer.ReplayBackendCapability.supportsSameProducerDepth
#check CMeta.Producer.ReplayIR
#check CMeta.Producer.ReplayIR.sameProducerDepth
#check CMeta.Producer.lowerSameProducerDepth
#check CMeta.Producer.LoweredReplayIR
#check CMeta.Producer.lowerReplayIR
```

Add negative checks:

```lean
assert_not_exists CMeta.Producer.nestedReplay_length
assert_not_exists CMeta.Producer.nestedReplay_count
assert_not_exists CMeta.Producer.lowerSameProducerDepth_iff
assert_not_exists CMeta.Producer.lowerReplayIR_isSome_iff
assert_not_exists CMeta.Producer.lowerReplayIR_progress
```

Run the harness and confirm those legacy theorems make it RED.

- [ ] **Step 2: Moduleize `NestedReplay`**

Use:

```lean
module
import CMeta.Producer
```

Make `Producer.nestedReplay` public; keep its proof family private. Do not re-export `Producer`.

- [ ] **Step 3: Moduleize `NestedReplayLowering`**

Use:

```lean
module
import CMeta.NestedReplay
```

Make public:

```text
ReplayBackendCapability
ReplayBackendCapability.supportsSameProducerDepth
ReplayIR
ReplayIR.sameProducerDepth
lowerSameProducerDepth
LoweredReplayIR
lowerReplayIR
```

Keep `ReplayIR.activeMultiplicity`, `ReplayIR.sameProducerDepthAux` private as they already are, and keep all lowering proof theorems private/default.

- [ ] **Step 4: Verify and commit**

Run focused Lean files, the M7 harness, then `lake build --wfail`. Commit:

```bash
git add -- formal/CMeta/NestedReplay.lean formal/CMeta/NestedReplayLowering.lean \
  formal/CMeta/LanguageModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize replay semantics"
```

---

## Task 3 — M7c: Preprocessor backend identity and finite-map registry core

**Files:**
- Modify: `formal/CMeta/PreprocessorBackend.lean`
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`

**Interfaces:**
- Produces all backend/registry carriers required by existing `LanguageSpec` aliases and rule signatures.
- Registry mutation proof engineering stays private; later mutation/equivalence modules use `import all CMeta.PreprocessorBackend` when they need those bodies/theorems.

- [ ] **Step 1: Add RED public/private checks**

Positive-check:

```text
CompilerFamily
CompilerFamily.tag
LanguageMode
LanguageMode.standardValue
BackendKey
BackendQuery
PreprocessorBackend
PreprocessorBackend.key
PreprocessorBackend.replayCapability
PreprocessorBackend.requiresDeferred
PreprocessorBackend.IsReplayCertified
CertifiedPreprocessorBackend
CertifiedPreprocessorBackend.key
CertifiedPreprocessorBackend.replayCapability
CertifiedPreprocessorBackend.matchesQuery
CertifiedPreprocessorBackend.supportsReplay
PreprocessorBackendRegistry
PreprocessorBackendRegistry.lookup
PreprocessorBackendRegistry.supportingCandidates
```

Negative-check representative proof plumbing:

```lean
assert_not_exists CMeta.Producer.CertifiedPreprocessorBackend.compilerVersionPositive
assert_not_exists CMeta.Producer.CertifiedPreprocessorBackend.certifiedDepthPositive
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.mem_supportingCandidates_iff
```

Also assert representative list-level mutation helpers remain absent after conversion; use their existing exact names such as `lookupEntries_none_of_not_mem` and `target_not_mem_removeEntries`.

- [ ] **Step 2: Moduleize backend core**

Use:

```lean
module
import CMeta.NestedReplayLowering
```

Add `public` only to the semantic carriers/projections/lookup/discovery operations listed above plus the existing registry mutation operations whose result types are consumed by later public registry semantics. Keep certification facts, membership lemmas, list implementation helpers, uniqueness proofs, and mutation preservation proofs private/default.

- [ ] **Step 3: Give downstream proof modules internal access explicitly**

When a later M7 module currently depends on a newly private backend theorem/body, replace accidental transitive access with:

```lean
import all CMeta.PreprocessorBackend
```

Do not solve this by making the theorem public.

- [ ] **Step 4: Verify and commit**

Run `PreprocessorBackend.lean`, the M7 harness, and `lake build --wfail`. Commit:

```bash
git add -- formal/CMeta/PreprocessorBackend.lean formal/CMeta/LanguageModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize backend registry core"
```

---

## Task 4 — M7d: Canonical backend plan and selection policy

**Files:**
- Modify: `formal/CMeta/NestedReplayBackendPlan.lean`
- Modify: `formal/CMeta/PreprocessorBackendSelection.lean`
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`

**Interfaces:**
- `ReplayBackendPlan` and `lowerReplayBackendPlan` are the dynamic-plan semantics used by `LanguageSpec`.
- `BackendSelectionPolicy`, `WellFormedSelectionPolicy`, and `PreprocessorBackendRegistry.selectSupporting` are the public selection semantics used by `LanguageSpec`.
- Correctness theorems used only to implement LanguageSpec rules remain private and are reached there with `import all`.

- [ ] **Step 1: Extend RED contract**

Positive-check:

```text
ReplayExpansionPlan
ReplayExpansionPlan.respectsActiveProducers
ReplayExpansionPlan.fromIR
ReplayExpansionPlan.strategyTrace
ReplayBackendPlan
ReplayBackendPlan.fromIR
lowerReplayBackendPlan
PreprocessorBackendRegistry.resolveReplay
BackendPreference
BackendSelectionPolicy
BackendSelectionPolicy.thenBy
BackendSelectionPolicy.preferGreaterCertifiedDepth
BackendSelectionPolicy.preferNewerVersion
BackendSelectionPolicy.choose
BackendSelectionPolicy.select
BackendSelectionRank
WellFormedSelectionPolicy
BackendSelectionPolicy.replayRank
BackendSelectionPolicy.replayWellFormed
PreprocessorBackendRegistry.selectSupporting
```

Negative-check:

```lean
assert_not_exists CMeta.Producer.ReplayExpansionPlan.fromIR_respects
assert_not_exists CMeta.Producer.lowerReplayIR_requirement
assert_not_exists CMeta.Producer.lowerReplayBackendPlan_eq_some_iff
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.resolveReplay_eq_some_iff
assert_not_exists CMeta.Producer.BackendSelectionPolicy.select_mem
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.selectSupporting_mem_candidates
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.selectSupporting_lowering_canonical
```

- [ ] **Step 2: Moduleize canonical plan layer**

Use:

```lean
module
import CMeta.NestedReplayLowering
import all CMeta.PreprocessorBackend
```

Make the plan/expansion carriers and operations in the positive list public. Keep all correctness theorems private/default.

- [ ] **Step 3: Moduleize selection layer**

Use:

```lean
module
import Init.Tactics
public import CMeta.NestedReplayBackendPlan
import all CMeta.NestedReplayBackendPlan
```

The two-import spelling is intentional: downstream needs the public backend-plan vocabulary while selection proofs need private plan/backend proof facts. Mark only the positive-list policy/rank/selection semantics public. Keep `select_mem`, rank algebra theorems, permutation proofs, and registry selection correctness theorems private/default.

- [ ] **Step 4: Verify and commit**

Run both focused files, the M7 harness, and the full Lake build. Commit:

```bash
git add -- formal/CMeta/NestedReplayBackendPlan.lean \
  formal/CMeta/PreprocessorBackendSelection.lean \
  formal/CMeta/LanguageModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize replay backend selection"
```

---

## Task 5 — M7e: Registry mutation/equivalence/substitutability/Setoid chain

**Files:**
- Modify: `formal/CMeta/PreprocessorBackendRegistryMutation.lean`
- Modify: `formal/CMeta/PreprocessorBackendRegistryEquivalence.lean`
- Modify: `formal/CMeta/PreprocessorBackendRegistrySubstitutability.lean`
- Modify: `formal/CMeta/PreprocessorBackendRegistrySetoid.lean`
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`

**Interfaces:**
- Preserve the existing semantic registry mutation operations and observational-equivalence relation needed by `LanguageSpec.Rule.remove_congr`, `insert_congr`, and `replace_congr`.
- Public lower-level theorem exposure is not required merely because a LanguageSpec wrapper theorem delegates to it; `LanguageSpec` will use package-private `import all`.

- [ ] **Step 1: Inventory exact semantic carriers before editing**

For each file, classify every declaration by the spec rule: public only if it occurs in an existing LanguageSpec public signature/body-reducible alias or is required to construct/use such a carrier; proof-only declarations remain private. Record the exact public list in `LanguageModuleMigrationConformance.lean` with positive `#check`s and add negative assertions for at least one proof theorem from each file.

- [ ] **Step 2: Produce an inspected RED**

Run:

```bash
cd formal
lake env lean CMeta/LanguageModuleMigrationConformance.lean
```

Expected: negative assertions fail on legacy theorem visibility, not on missing semantic names.

- [ ] **Step 3: Moduleize bottom-up**

Each file begins with `module`. Use `public import` only when its public semantic types are intentionally part of the next module's semantic surface; add a paired `import all` when its theorem bodies require private predecessor proof facts. Do not re-export conformance modules.

The final `PreprocessorBackendRegistrySetoid` module must expose the existing registry equivalence/Setoid semantic relation needed by the `≈` notation, while its congruence proof implementation remains private/default.

- [ ] **Step 4: Verify and commit**

Run each of the four files directly, then the M7 harness and `lake build --wfail`. Commit only after all are GREEN:

```bash
git add -- formal/CMeta/PreprocessorBackendRegistryMutation.lean \
  formal/CMeta/PreprocessorBackendRegistryEquivalence.lean \
  formal/CMeta/PreprocessorBackendRegistrySubstitutability.lean \
  formal/CMeta/PreprocessorBackendRegistrySetoid.lean \
  formal/CMeta/LanguageModuleMigrationConformance.lean
git commit -m "refactor(formal): moduleize registry equivalence semantics"
```

---

## Task 6 — M7f: Make `LanguageSpec` the enforced public facade

**Files:**
- Modify: `formal/CMeta/LanguageSpec.lean`
- Modify: `formal/CMeta/LanguageSpecConformance.lean`
- Create: `formal/CMeta/LanguageSpecIsolationConformance.lean`
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`
- Modify: `formal/CMeta.lean`

**Interfaces:**
- Public facade remains exactly the current carrier aliases, judgments, and Rule declarations; no theorem is renamed or weakened.
- Client isolation proves implementation proof names are not visible through a normal `import CMeta.LanguageSpec`.

- [ ] **Step 1: Write the client isolation RED**

Create:

```lean
module
import CMeta.LanguageSpec

#check CMeta.Producer.LanguageSpec.IR
#check CMeta.Producer.LanguageSpec.Backend
#check CMeta.Producer.LanguageSpec.Query
#check CMeta.Producer.LanguageSpec.Registry
#check CMeta.Producer.LanguageSpec.Plan
#check CMeta.Producer.LanguageSpec.Certifiable
#check CMeta.Producer.LanguageSpec.Matches
#check CMeta.Producer.LanguageSpec.Supports
#check CMeta.Producer.LanguageSpec.Candidate
#check CMeta.Producer.LanguageSpec.LowersTo
#check CMeta.Producer.LanguageSpec.ResolvesTo
#check CMeta.Producer.LanguageSpec.Rule.cert_intro
#check CMeta.Producer.LanguageSpec.Rule.match_intro
#check CMeta.Producer.LanguageSpec.Rule.support_intro
#check CMeta.Producer.LanguageSpec.Rule.candidate_intro
#check CMeta.Producer.LanguageSpec.Rule.candidate_elim
#check CMeta.Producer.LanguageSpec.Rule.lower_intro
#check CMeta.Producer.LanguageSpec.Rule.lower_elim
#check CMeta.Producer.LanguageSpec.Rule.selection_elim
#check CMeta.Producer.LanguageSpec.Rule.selection_lower
#check CMeta.Producer.LanguageSpec.Rule.resolve_intro
#check CMeta.Producer.LanguageSpec.Rule.resolve_elim
#check CMeta.Producer.LanguageSpec.Rule.eq_refl
#check CMeta.Producer.LanguageSpec.Rule.eq_symm
#check CMeta.Producer.LanguageSpec.Rule.eq_trans
#check CMeta.Producer.LanguageSpec.Rule.candidates_congr
#check CMeta.Producer.LanguageSpec.Rule.selection_congr
#check CMeta.Producer.LanguageSpec.Rule.lowering_congr
#check CMeta.Producer.LanguageSpec.Rule.remove_congr
#check CMeta.Producer.LanguageSpec.Rule.insert_congr
#check CMeta.Producer.LanguageSpec.Rule.replace_congr

assert_not_exists CMeta.Producer.count_eq_length
assert_not_exists CMeta.Producer.nestedReplay_length
assert_not_exists CMeta.Producer.lowerReplayBackendPlan_eq_some_iff
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.mem_supportingCandidates_iff
assert_not_exists CMeta.Producer.BackendSelectionPolicy.select_mem
assert_not_exists CMeta.Producer.PreprocessorBackendRegistry.selectSupporting_mem_candidates
assert_not_exists CMeta.Producer.CLanguageSpecConformance.syntaxCarriers
```

The final assertion verifies that importing the public facade does not pull in its conformance proof module.

- [ ] **Step 2: Moduleize `LanguageSpec`**

Use explicit semantic re-exports rather than relying on transitive imports. The header must begin with `module` and public-import the public semantic modules required to construct/use the existing aliases and rule signatures; add private `import all` edges for implementation theorem bodies. The final imports must not include any `*Conformance` module.

Wrap the existing `CMeta.Producer.LanguageSpec` declarations in a `public section` (or add `public` individually) so the existing carrier/judgment/rule facade remains the supported surface exactly as it is today.

- [ ] **Step 3: Moduleize conformance privately**

`LanguageSpecConformance.lean` begins:

```lean
module
import CMeta.LanguageSpec
```

Its `CLanguageSpecConformance.*` theorem family remains private/default. If proof bodies need implementation theorem names that are intentionally hidden from the public facade, add only package-private `import all` dependencies to this conformance module.

- [ ] **Step 4: Verify facade and isolation**

Run:

```bash
cd formal
lake env lean CMeta/LanguageSpec.lean
lake env lean CMeta/LanguageSpecConformance.lean
lake env lean CMeta/LanguageSpecIsolationConformance.lean
lake env lean CMeta/LanguageModuleMigrationConformance.lean
lake build --wfail
```

- [ ] **Step 5: Commit**

```bash
git add -- formal/CMeta/LanguageSpec.lean \
  formal/CMeta/LanguageSpecConformance.lean \
  formal/CMeta/LanguageSpecIsolationConformance.lean \
  formal/CMeta/LanguageModuleMigrationConformance.lean formal/CMeta.lean
git commit -m "refactor(formal): enforce language spec boundary"
```

---

## Task 7 — M7g: Convert the remaining Producer-side generated/conformance closure

**Files:**
- Modify every still-legacy `formal/CMeta/*.lean` imported by the legacy root or by a module reachable from `LanguageSpec`, including the existing NestedReplay applicability/conformance files, backend selection/registry conformance files, TypeIdentity/DescriptorBridge files and their generated snapshots where still legacy.
- Modify the corresponding C witness generators only when required to emit module framing.
- Modify: `formal/CMeta/LanguageModuleMigrationConformance.lean`

**Interfaces:**
- This task creates no new public API.
- Conformance theorem families remain private/default.
- Generated snapshots become modules with framing-only source changes and identical semantic payload.

- [ ] **Step 1: Compute the remaining legacy frontier**

From the exact Task-6 head, list every file reachable from the current `formal/CMeta.lean` imports and fail the task if any future `InternalChecks` dependency does not begin with `module`. This list is the task's closed conversion set; do not add unrelated files outside `formal/`.

- [ ] **Step 2: Convert ordinary conformance files**

For each conformance file, prepend `module`, use plain `import` for public semantic APIs, and use `import all` only for package-private proof dependencies. Keep all conformance declarations private/default.

- [ ] **Step 3: Convert generated snapshots with framing-only diffs**

For every generated Lean snapshot still legacy, update both the C emitter and committed snapshot so generated output begins with the required module framing. Preserve every generated semantic row/value after that prefix byte-for-byte.

For each generator, build and regenerate into the configured build tree and require:

```bash
diff -u formal/CMeta/<Snapshot>.lean build/<preset>/formal/generated/<Snapshot>.lean
```

Expected after the migration commit: zero diff.

- [ ] **Step 4: Convert the M7 harness itself to a module**

Once every dependency it imports is a module, change its header to:

```lean
module
```

Keep all direct `assert_not_exists` statements; do not add `#check_assertions` because `lake build --wfail` treats its successful summary warning as failure in Lean 4.30.

- [ ] **Step 5: Verify complete M7 closure**

Run the exact formal gates used by CI for both GCC and Clang, including C witnesses, generated snapshot zero-diff, applicability probes, placeholder/API guards, PublicProof isolation, LanguageSpec isolation, and:

```bash
cd formal
lake build --wfail
```

- [ ] **Step 6: Commit M7 closure**

Stage only the enumerated M7 closure files and corresponding witness emitters; commit:

```bash
git commit -m "refactor(formal): close producer module tree"
```

---

## Task 8 — M7 exact-head checkpoint

**Files:**
- Modify CI only if needed to add the new LanguageSpec isolation guard; do not rename the existing `Lean proofs` workflow/job identities.

- [ ] **Step 1: Audit forbidden escape hatches**

Run:

```bash
! git grep -nE 'backward\.(privateInPublic|proofsInPublic)|allowImportAll' -- formal
```

Expected: success with no matches.

- [ ] **Step 2: Audit no accidental public conformance surface**

The public `LanguageSpec` file must not import any `*Conformance` or generated snapshot module. `LanguageSpecIsolationConformance` must pass by importing only `CMeta.LanguageSpec`.

- [ ] **Step 3: Push the exact M7 head and require both CI matrix jobs GREEN**

Require the GitHub Actions `Lean proofs` run on the exact M7 commit to complete successfully for both GCC and Clang. Do not begin M8 from a locally green but unverified head.

---

## Task 9 — M8: `InternalChecks`, root conversion, and root isolation contract

**Files:**
- Create: `formal/CMeta/InternalChecks.lean`
- Create: `formal/CMeta/RootIsolationConformance.lean`
- Modify: `formal/CMeta.lean`
- Modify: `formal/lakefile.toml` only if the existing default-root declaration needs no-semantic-change module syntax adjustment.
- Modify CI only to add root-isolation verification if the default `lake build --wfail` does not already compile the new client file.

**Interfaces:**
- `CMeta.InternalChecks` owns build reachability only and exports no supported API.
- `CMeta` publicly re-exports exactly `CMeta.LanguageSpec` and `CMeta.PublicProof` and privately reaches all internal verification through `InternalChecks`.

- [ ] **Step 1: Write root client isolation RED**

Create:

```lean
module
import CMeta

#check CMeta.Producer.LanguageSpec.IR
#check CMeta.Producer.LanguageSpec.Rule.lower_intro
#check CMeta.PublicProof.StructuredGraph
#check CMeta.PublicProof.structured_graph_type_safe
#check CMeta.PublicProof.static_checker_matches_runtime

assert_not_exists CMeta.EndToEnd.direct_plan_exact
assert_not_exists CMeta.TypedGraph.check_stages
assert_not_exists CMeta.Producer.count_eq_length
assert_not_exists CMeta.Producer.lowerReplayBackendPlan_eq_some_iff
assert_not_exists CMeta.Producer.CLanguageSpecConformance.syntaxCarriers
assert_not_exists CMeta.ExecProgram.runtime_execution_exact
```

Before root conversion, this must fail because the legacy root leaks internal declarations.

- [ ] **Step 2: Create `InternalChecks` from the current legacy root closure**

Move the current internal build-reachability import list from `formal/CMeta.lean` into:

```lean
module

import CMeta.ModuleMigrationConformance
import CMeta.LanguageModuleMigrationConformance
import CMeta.LanguageSpecConformance
import CMeta.LanguageSpecIsolationConformance
import CMeta.PublicProofConformance
import CMeta.PublicProofIsolationConformance
-- plus every current conformance/generated/internal module required by the
-- exact pre-M8 root closure, one explicit import per module.
```

Do not use `public import` in `InternalChecks`. The file introduces no declarations.

- [ ] **Step 3: Convert the root to the two-entry contract**

Replace the legacy import list with exactly:

```lean
module
public import CMeta.LanguageSpec
public import CMeta.PublicProof
import CMeta.InternalChecks
```

Do not public-import `Semantics`, `Producer`, registry modules, conformance modules, generated modules, or `InternalChecks` directly from the root.

- [ ] **Step 4: Prove the build did not shrink**

Compare the pre-M8 root import closure with `InternalChecks`. Every internal/conformance/generated module previously kernel-checked by the root must remain reachable. Then run:

```bash
cd formal
lake env lean CMeta/RootIsolationConformance.lean
lake build --wfail
```

Expected: root client isolation passes and the complete internal build remains green.

- [ ] **Step 5: Run full GCC/Clang exact-head verification**

Run the same formal C witness generation, snapshot zero-diff, applicability, placeholder/API, PublicProof isolation, LanguageSpec isolation, and Lake gates on both compiler matrix lanes. Require exact-head GitHub Actions success.

- [ ] **Step 6: Final audit and commit**

Verify:

```bash
grep -qx 'module' formal/CMeta.lean
grep -qx 'public import CMeta.LanguageSpec' formal/CMeta.lean
grep -qx 'public import CMeta.PublicProof' formal/CMeta.lean
grep -qx 'import CMeta.InternalChecks' formal/CMeta.lean
! git grep -nE 'backward\.(privateInPublic|proofsInPublic)|allowImportAll' -- formal
```

Commit:

```bash
git add -- formal/CMeta.lean formal/CMeta/InternalChecks.lean \
  formal/CMeta/RootIsolationConformance.lean
# add CI/lake files only if actually modified
git commit -m "refactor(formal): enforce root module boundary"
```

The final exact-head CI must pass before Plan B is declared complete.
