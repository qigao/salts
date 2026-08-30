# CFlow SCXML CMeta Transactional Assign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Admit bounded CMeta `<assign location="..." expr="..."/>` executable steps whose mutations commit with the native Statechart transaction or roll back completely while raising `error.execution`.

**Architecture:** Keep the native Statechart state slot as the sole mutable fact source. A private `cmeta_assign` module compiles one reflected dotted location and one scalar value expression into immutable program-owned data; execution evaluates against and mutates only the callback's staged `out_state`. `scxml.c` remains the XML/IR adapter and maps recoverable assignment failures to a staged internal `error.execution` event.

**Tech Stack:** C11, CMeta data descriptors, existing bounded QueryVM expression evaluator, CFlow Statechart transactions, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

## Global Constraints

- Existing null-model documents and entry points remain unchanged; null-model `<assign>` still fails admission.
- The immutable SCXML program borrows the root schema and owns every compiled assignment until program destruction.
- Dotted reflected struct fields are admitted in this increment; sequence indices, maps, pointers, system-variable writes, arrays, and `<foreach>` remain fail-fast unsupported.
- Runtime evaluation is single-threaded on the instance SerialExecutor. The current staged object is the only writable object; program descriptors are immutable and shared.
- A block begins from a fully constructed copy. Successful steps mutate that copy. A recoverable assignment failure restores the block output to the input value, stages `error.execution`, and returns success so the native runtime can commit only the unchanged state plus the internal event.
- All source, path-depth, literal, and string sizes use the existing positive CMeta expression hard limits.

---

### Task 1: Lock assignment behavior with failing public tests

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

**Interfaces:**
- Consumes: `cflow_scxml_compile_cmeta()`, `cflow_scxml_session_init_cmeta()`.
- Produces: observable tests for ordered assignment, later guard visibility, rollback, and admission errors.

- [x] Add a reflected `count`/`source` fixture whose enum read callback can fail for one sentinel value.
- [x] Add an SCXML fixture that assigns `count=2`, then reaches final through a later `count == 2` transition condition.
- [x] Add an SCXML fixture that assigns `count=2`, fails while evaluating a second assignment, catches `error.execution`, and reaches final only when `count == 1` proves rollback.
- [x] Assert missing `location`/`expr`, unknown fields, read-only `_event` destinations, and null-model `<assign>` fail during compilation with stable status classes.
- [x] Build and run `cflow_scxml_cmeta_test`; record RED because `<assign>` is currently an unsupported element.

### Task 2: Generalize the private evaluator for scalar values

**Files:**
- Modify: `cflow-scxml/src/cmeta_expr.h`
- Modify: `cflow-scxml/src/cmeta_expr.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_expr_test.c`

**Interfaces:**
- Produces: `cflow_scxml_cmeta_expr_compile_value()`, `cflow_scxml_cmeta_expr_evaluate_value()`, `cflow_scxml_cmeta_expr_program_value_kind()` and a borrowed scalar `cflow_scxml_cmeta_expr_value`.
- Preserves: existing Boolean-only compile/evaluate entry points and their diagnostics.

- [x] Add RED tests for signed, unsigned, float, Boolean, string, enum, and reflected-location scalar results.
- [x] Split the shared parser into Boolean-condition and scalar-value admission modes; retain the compiled root value kind.
- [x] Add one shared evaluator that copies scalar numbers and returns call-scoped borrowed string views without allocation.
- [x] Keep existing condition wrappers strict: non-Boolean roots continue to fail condition compilation/evaluation.
- [x] Run private expression tests and the existing public guard test.

### Task 3: Compile and apply reflected assignment commands

**Files:**
- Create: `cflow-scxml/src/cmeta_assign.h`
- Create: `cflow-scxml/src/cmeta_assign.c`
- Modify: `cflow-scxml/CMakeLists.txt`
- Test: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

**Interfaces:**
- Produces: `cflow_scxml_cmeta_assign_compile()`, `cflow_scxml_cmeta_assign_apply()`, `cflow_scxml_cmeta_assign_program_destroy()`.
- Consumes: root `cmeta_data_desc`, state resolver, expression limits, compiled scalar value API.

- [x] Parse a non-empty dotted NCName path under `max_path_depth`; reject indices and leading `_` system locations explicitly.
- [x] Resolve and bounds-check each reflected field offset, retaining only borrowed descriptors plus one checked byte offset.
- [x] Validate destination/value compatibility at compile time for bool, integer, float, enum, and string destinations.
- [x] Apply numeric values with checked exact conversion and `memcpy` to avoid alignment violations.
- [x] Apply enum/string through CMeta provider operations; restore the staged destination to semantic zero before assignment and use a bounded temporary copy for potentially aliased strings.
- [x] Ensure every compile/apply failure has one deterministic diagnostic and every owned allocation has one cleanup path.
- [x] Run the focused public and private tests under the Debug/ASan preset.

### Task 4: Admit `<assign>` into SCXML executable IR

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

**Interfaces:**
- Consumes: private assignment program API.
- Produces: `SCXML_STEP_ASSIGN` rows backed by program-owned assignment programs.

- [x] Add `assign` element recognition and permit exactly `location` and `expr` attributes.
- [x] Count assignment rows/steps and reserve `error.execution` independently of Event I/O adapters.
- [x] Decode both XML attribute values through the existing bounded lexical decoder, then compile one immutable assignment program during emission.
- [x] Execute assignment steps against `context->out_state`; later steps observe earlier successful staged mutations.
- [x] On assignment failure, stage `error.execution`, restore the block output from `context->state`, and abort the remaining block without publishing partial mutations.
- [x] Destroy partially and fully emitted assignment programs exactly once on build failure or program destruction.
- [x] Verify null-model rejection and all existing executable-content regressions.

### Task 5: Document and verify the increment

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

- [x] Document the admitted dotted scalar assignment subset, ownership, exact conversion, rollback, and `error.execution` behavior.
- [x] Keep system variables, sequence indices, `<foreach>`, arrays, and structured CBind/CSerde values explicitly unsupported.
- [x] Run focused Debug/ASan tests, installed-package verification if the public surface changes, and Release full CTest.
- [x] Run `codegraph sync`, `codegraph affected`, `git diff --check`, and inspect the final status without committing or pushing.
