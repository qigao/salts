# CFlow Typed Machine IR Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a transactionally validated, immutable typed Machine IR with Lean small-step semantics and checkable C/Lean schema alignment.

**Architecture:** A borrowed `cflow_machine_definition` is normalized into exact-sized owned arrays and published only after complete validation. Lean owns the finite schema manifest and defines the deterministic Event-driven evaluator; C consumes a generated enum header and exposes read-only canonical queries. Runtime scheduling and callback binding remain in issue #64.

**Tech Stack:** C11, CMeta descriptors/effects/properties, TinyTest, CMake Presets, Lean 4.33.1, Lake.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-machine-ir-design.md`

## Global Constraints

- Existing Graph, Plan, Event/Mailbox, and runtime behavior remains unchanged.
- Machine construction is transactional: failure leaves an empty output.
- Stable non-zero IDs are semantic identities; array position and descriptor address are not.
- Build-time arrays are copied; CMeta descriptors remain borrowed through destroy.
- Machine adds no scheduler, runtime instance, callback invocation, serialization, or fallback.
- All allocation arithmetic is checked before input traversal or allocation.
- Generated C schema text is owned by Lean and checked in CI.

---

### Task 1: Lean-owned finite Machine schema

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/MachineSchema.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/MachineSchemaHeader.lean`
- Create: `formal/cmeta_cflow_calculus/CFlowMachineSchemaGen.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/MachineSchema.lean`
- Create: `cflow/include/cflow/generated/machine_schema.h`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`
- Modify: `formal/cmeta_cflow_calculus/lakefile.toml`

**Interfaces:**
- Consumes: Lean `List`, deterministic string rendering, existing generator CLI convention.
- Produces: `machineSchemaVersion`, `stateKindRows`, `actionObservationRows`, `MachineSchemaHeader.render`, and `cflow-machine-schema-gen --stdout|--write|--check`.

- [x] **Step 1: Write the failing Lean manifest tests**

Add literal expectations for schema version `1`, three state rows, three action-observation rows, unique C enum values, and exact rendered count/version macros. The break caught is a stale, duplicated, or reordered schema fact source.

- [x] **Step 2: Run the tests and observe the missing-module failure**

Run: `lake env lean Test/PhaseATests/MachineSchema.lean`  
Expected: FAIL because `MachineSchema` and `MachineSchemaHeader` do not exist.

- [x] **Step 3: Implement the finite manifest, renderer, and generator CLI**

Define rows with Lean names, C names, and explicit values. Validate non-empty/unique names and values before rendering `CFLOW_MACHINE_SCHEMA_VERSION`, row replay macros, and count macros. Follow the exact error/write/check behavior of the existing CMeta and operator-policy generators.

- [x] **Step 4: Generate the header and run focused Lean checks**

Run:

```powershell
lake exe cflow-machine-schema-gen --write ../../cflow/include/cflow/generated/machine_schema.h
lake exe cflow-machine-schema-gen --check ../../cflow/include/cflow/generated/machine_schema.h
lake env lean Test/PhaseATests/MachineSchema.lean
```

Expected: all commands exit `0`.

- [x] **Step 5: Commit the schema fact source**

```text
git add formal/cmeta_cflow_calculus cflow/include/cflow/generated/machine_schema.h
git commit -m "formal(cflow): define machine schema manifest"
```

### Task 2: Transactional immutable C Machine IR

**Files:**
- Create: `cflow/include/cflow/machine.h`
- Create: `cflow/src/machine.c`
- Create: `cflow/tests/cflow_machine_test.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `cflow_event_type`, `cmeta_type_desc_valid`, `cmeta_type_equal`, CMeta effects/properties, and generated Machine schema rows.
- Produces: declaration structs, `cflow_machine_status`, `cflow_machine_build`, `cflow_machine_destroy`, count/initial-state queries, and canonical `*_at` row queries.

- [x] **Step 1: Write the first failing public-contract tests**

Create a valid two-state Machine fixture and assert successful build, exact initial state, canonical sorted state/transition rows, copied arrays, and error-capable action admission. Add an empty-Machine test and a transactional-failure test asserting `machine.impl == NULL`. The breaks caught are partial publication, input aliasing, and missing finite-domain rejection.

- [x] **Step 2: Configure/build the target and observe the missing API failure**

Run:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cflow_machine_test'
```

Expected: FAIL because `cflow/machine.h` and the implementation do not exist.

- [x] **Step 3: Implement minimal owned construction and queries**

Use one private implementation with exact-sized arrays. Preflight every count/byte multiplication before reading arrays. Copy to temporary storage, sort by stable IDs and `(source,event,priority)`, validate, then publish once. `destroy` frees every owned array and zeros the public handle.

- [x] **Step 4: Run focused tests to green**

Run: `ctest --preset win-release-user -R "^cflow_machine_test$" --output-on-failure`  
Expected: PASS.

- [x] **Step 5: Add failing validation matrix tests**

Use literal fixtures for zero/duplicate IDs, unknown initial/source/target/Event/guard/action IDs, state/Event/action type mismatches, invalid guard contracts, invalid observation declarations, terminal outgoing edges, duplicate priority, unreachable states, and unused guard/action rows. Each `it` names one rejected invariant.

- [x] **Step 6: Implement the validation matrix and rerun**

Return the specific `cflow_machine_status` for each failure. Reachability uses a bounded traversal over the copied finite state array; no dynamic growth occurs after exact allocation. Run the focused target and CTest until all cases pass.

- [x] **Step 7: Add C++ inclusion coverage and commit**

Include `cflow/machine.h` in the existing C++ header test, add the new target to C11 properties, run both tests, then commit:

```text
git add cflow
git commit -m "feat(cflow): add transactional typed machine ir"
```

### Task 3: Machine small-step semantics and proofs

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/Machine.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/Machine.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/Machine.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Consumes: `Ty`, dependent `Value`, `Mailbox.TypedEvent`, and Machine schema kinds.
- Produces: typed declarations, `Machine.Valid`, `Config`, `MachineObservation`, `Trace`, deterministic priority selection, `step`, `SmallStep`, typing preservation, determinism, consumption, failure, and terminal-absorption theorems.

- [ ] **Step 1: Write failing executable semantic examples**

Construct literal Machines showing lowest enabled priority selection, no-transition error, action-success state commit, action-failure source-state preservation, Event consumption, DONE trace, and terminal absorption. Expected traces are literal lists, not computed by a duplicate evaluator.

- [ ] **Step 2: Run and observe missing semantics**

Run: `lake env lean Test/PhaseATests/Machine.lean`  
Expected: FAIL because `CFlow.Machine` does not exist.

- [ ] **Step 3: Implement minimal typed semantic definitions**

Define finite lookups, candidate filtering, lowest-priority selection, action outcome, observations, terminal mode, configuration, pure `step`, and `SmallStep` as one evaluator equality. A non-terminal call consumes the input exactly once; no-match and action failure enter error with the source state unchanged.

- [ ] **Step 4: Run semantic examples to green**

Run the focused Lean test and confirm every literal trace matches.

- [ ] **Step 5: Write theorem witnesses before proofs**

Add examples requiring `smallStep_deterministic`, `step_preserves_state_typing`, `step_preserves_event_typing`, `step_consumes_once`, `action_failure_preserves_state`, and `terminal_no_step`.

- [ ] **Step 6: Prove the admitted-fragment obligations**

Prove determinism from functional evaluation, typing preservation from validated transition references/type alignment, exact consumption by case analysis, and terminal absorption from the evaluator's terminal guard. Do not use `sorry` or axioms.

- [ ] **Step 7: Run focused and complete Lean suites, then commit**

Run:

```powershell
lake env lean Test/PhaseATests/Machine.lean
lake test
```

Expected: exit `0`, then commit:

```text
git add formal/cmeta_cflow_calculus
git commit -m "formal(cflow): prove machine transition semantics"
```

### Task 4: Alignment gate, documentation, and delivery verification

**Files:**
- Modify: `.github/workflows/cmeta-cflow-calculus.yml`
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-24-cflow-machine-ir-design.md`
- Modify: `docs/superpowers/plans/2026-08-24-cflow-machine-ir.md`

**Interfaces:**
- Consumes: completed generator, C Machine API/tests, and Lean proofs.
- Produces: CI stale-header gate, user-facing ownership/error example, and reproducible delivery evidence.

- [ ] **Step 1: Add the generator CI check and README example**

Run `lake exe cflow-machine-schema-gen --check ../../cflow/include/cflow/generated/machine_schema.h` next to existing generated checks. Document build/destroy ownership, descriptor lifetime, canonical queries, validation failures, and the explicit boundary that execution begins in #64.

- [ ] **Step 2: Run focused Release and ASan verification**

Run the Machine CTest plus C++ header test under `win-release-user`, then configure/build `win-dev-user` and run `cflow_machine_test` under ASan. Expected: all pass with no sanitizer report.

- [ ] **Step 3: Run complete regression and formal verification**

Run complete `ctest --preset win-release-user --output-on-failure`, `lake test`, and all three generated-header `--check` commands. Expected: zero failures.

- [ ] **Step 4: Inspect the final diff and commit delivery metadata**

Confirm `.codegraph/`, build outputs, and local generated caches are absent from the diff; mark completed plan steps and commit:

```text
git add .github cflow docs formal
git commit -m "docs(cflow): document typed machine ir delivery"
```

- [ ] **Step 5: Request independent review and prepare the stacked PR**

Review against issue #63 and this spec, fix every validated finding through a failing regression test, rerun verification, push `feat/cflow-machine-ir`, and create a PR based on `feat/cflow-typed-event-mailbox` that closes #63. Retarget to `master` after #73 merges.
