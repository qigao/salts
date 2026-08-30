# CFlow SCXML CMeta Executable Conditionals Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Execute inline with `superpowers:executing-plans`; steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Admit and execute CMeta `<if>`, `<elseif>`, and `<else>` partitions with compiled bounded conditions and W3C `error.execution` behavior.

**Architecture:** Reuse the existing private CMeta QueryVM condition compiler for each non-unconditional branch. Each private `scxml_branch` owns either one null-model state id or one CMeta expression program; ordinary onentry, onexit, and transition executable blocks evaluate CMeta conditions against staged state. Evaluation failures enqueue `error.execution`, treat that condition as false, and continue selecting the first true partition. CMeta conditions inside invocation `<finalize>` remain a separate increment because that hook owns a different published-state and error-enqueue boundary.

**Tech Stack:** C11, CFlow Statechart executable bindings, CMeta/QueryVM, TinyTest, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

## Global Constraints

- Preserve the public SCXML compile/session ABI and the null data-model behavior.
- Compile every condition once; runtime execution performs no parsing.
- Branch expression programs own bounded compiler storage and are destroyed exactly once on every build failure and program destruction path.
- A conditional evaluation error is false plus one internal `error.execution`; failure to enqueue that event is fatal.
- CMeta `<if>` inside `<finalize>` continues to fail admission until its published-state error protocol has dedicated coverage.
- Do not commit or push this dirty worktree during this increment.

---

### Task 1: Specify CMeta executable conditional behavior

**Files:**
- Test: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

**Interfaces:**
- Consumes: `cflow_scxml_compile_cmeta()`, `cflow_scxml_session_init_cmeta()`, existing scalar `<assign>` and system-variable evaluation.
- Produces: public behavioral coverage for first-true partition selection, staged-state sequencing, nesting, and invalid conditions.

- [x] **Step 1: Replace the temporary rejection test with a successful first-true partition test**

  Compile a CMeta document whose on-entry block assigns a scalar, evaluates `<if>` and `<elseif>` in document order, and proves only the first true branch changes the staged state.

- [x] **Step 2: Add nested and system-variable coverage**

  Use `_name`/`_sessionid` in an outer condition and a reflected scalar in a nested condition; assert the session reaches the expected final state.

- [x] **Step 3: Add admission failure coverage**

  Compile missing, empty, non-boolean, and syntactically invalid `cond` attributes; assert stable public status, location, and no published program.

- [x] **Step 4: Run the focused test and record RED**

  Build and run `cflow_scxml_cmeta_test`; expected failure is the current `CMeta executable conditions are not supported yet` admission result.

### Task 2: Compile and own CMeta branch programs

**Files:**
- Modify: `cflow-scxml/src/scxml.c`

**Interfaces:**
- Consumes: `cflow_scxml_cmeta_expr_compile()` and the existing XML entity decoder/state resolver.
- Produces: one owned `cflow_scxml_cmeta_expr_program` per CMeta `<if>`/`<elseif>` condition.

- [x] **Step 1: Generalize the existing CMeta condition compiler helper**

  Compile directly into a caller-provided expression program, then keep the transition-guard helper as a thin owner adapter.

- [x] **Step 2: Admit required non-empty CMeta conditions**

  Preserve null-model `In(id)` validation; CMeta conditions use the expression compiler and its existing configured limits.

- [x] **Step 3: Emit branch conditions by data model**

  Store a null state id for null-model branches, a CMeta program for CMeta branches, and neither for `<else>`.

- [x] **Step 4: Add branch destruction to all ownership exits**

  Destroy every initialized CMeta branch program before freeing branch storage during build rollback, failed program assembly, and normal program destruction.

### Task 3: Evaluate transactional branch programs with W3C error semantics

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

**Interfaces:**
- Consumes: `cflow_scxml_cmeta_expr_evaluate_with_system()`, staged/published state, active-configuration callbacks, and internal-event enqueue callbacks.
- Produces: deterministic first-true selection in ordinary executable blocks, with conditional errors converted to false plus `error.execution`.

- [x] **Step 1: Evaluate ordinary CMeta branches against staged state**

  This makes earlier assignments in the same executable block visible while preserving the outer block transaction.

- [x] **Step 2: Preserve the separate finalize boundary**

  Reject CMeta conditionals while analyzing `<finalize>`; retain the existing null-model finalize path unchanged.

- [x] **Step 3: Enqueue condition evaluation failures and continue selection**

  Treat the failed condition as false, try later `<elseif>`/`<else>` partitions, and fail only if the bounded internal queue cannot accept `error.execution`.

- [x] **Step 4: Run the focused test to GREEN**

  Run `cflow_scxml_cmeta_test` and confirm all CMeta admission/runtime cases pass.

### Task 4: Document and verify

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

**Interfaces:**
- Consumes: completed implementation and tests.
- Produces: an accurate supported-subset statement and reproducible verification record.

- [x] **Step 1: Update the CMeta executable-content boundary**

  Document compiled `<if>/<elseif>/<else>` in ordinary transactional blocks, first-true ordering, staged-state visibility, system strings, and conditional-error behavior; keep CMeta finalize conditions, `<foreach>`, and container expressions explicitly unsupported.

- [x] **Step 2: Run focused Debug ASan tests**

  Run `cflow_scxml_cmeta_test`, `cflow_scxml_cmeta_expr_test`, and `cflow_scxml_test` through `win-dev-user`.

- [x] **Step 3: Run Release adjacent regression**

  Build through `win-release-user` and run the registered CTest suite.

- [x] **Step 4: Run repository consistency checks**

  Run `codegraph sync .` and `git diff --check`; record exact results without committing or pushing.

## Verification Record

- RED: `cflow_scxml_cmeta_test` retained 7 passing cases and produced the 4 expected admission failures before implementation.
- Focused GREEN: `cflow_scxml_cmeta_test` passed 1/1 through `win-dev-user`.
- Debug ASan adjacent regression: `cflow_scxml_test`, `cflow_scxml_cmeta_expr_test`, and `cflow_scxml_cmeta_test` passed 3/3 through `win-dev-user`.
- Release regression: the registered `win-release-user` CTest suite passed 199/199.
- Repository consistency: `codegraph sync .` indexed 2 changed files, and `git diff --check` completed without diagnostics.
