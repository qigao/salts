# CFlow SCXML CMeta Public Admission Implementation Plan

> **For Codex:** Execute this plan inline with test-driven development and verify each checkpoint before moving on.

**Goal:** Publicly admit `datamodel="cmeta"` for bounded transition conditions and per-session CMeta initial state while preserving every existing null-model API and behavior.

**Architecture:** Add explicit versioned CMeta compile/session option structs beside the existing null entry points. The immutable program borrows the root `cmeta_data_desc` and owns compiled QueryVM guard programs; each session passes its caller-owned initial object to the native Statechart, which immediately copy-constructs its authoritative managed state. Executable-content CMeta conditions, assignment, iteration, and system bindings remain fail-fast unsupported in this increment.

**Tech Stack:** C11, CMeta descriptors and lifecycle traits, CFlow Statechart runtime, private bounded QueryVM expression programs, TinyTest, CMake Presets.

---

### Task 1: Lock the additive public contract with failing tests

**Files:**
- Create: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`
- Modify: `cflow-scxml/tests/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`

- [x] Add a reflected trivial CMeta root fixture and a `datamodel="cmeta"` transition condition.
- [x] Assert compile options ABI/size validation, exact model selection, and unchanged legacy rejection.
- [x] Assert the legacy session initializer rejects CMeta programs and the CMeta initializer copies caller state.
- [x] Assert true/false scalar guards select deterministically and `In("id")` composes with reflected fields.
- [x] Assert CMeta executable `<if>` remains explicitly unsupported.
- [x] Build the focused test and capture the expected compile failure before implementation.

### Task 2: Add public CMeta compile/session boundaries

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/src/cmeta_expr.h`

- [x] Add versioned compile options with the borrowed root descriptor and bounded expression limits.
- [x] Add versioned session options with a call-scoped borrowed initial object.
- [x] Route legacy compile/session calls through null-only internal helpers without changing null behavior.
- [x] Validate provider ABI, descriptor shape, storage type, and exact document model before allocations.

### Task 3: Own compiled CMeta guards in the immutable program

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Compile transition `cond` attributes once after state-name resolution.
- [x] Store null `In(id)` guards and CMeta expression programs in one tagged program-owned guard row.
- [x] Evaluate CMeta guards against the native runtime's borrowed authoritative state and configuration query.
- [x] Destroy every partially or fully compiled expression exactly once on build failure or program destruction.
- [x] Map expression syntax/type/limit/allocation failures to deterministic SCXML diagnostics.

### Task 4: Export the public dependency and verify consumers

**Files:**
- Modify: `cflow-scxml/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

- [x] Promote `TurboUtils::CMeta` to the `TurboUtils::CFlowScxml` public link contract because the public header exposes `cmeta_data_desc`.
- [x] Exercise the versioned structs through the installed-package consumer.
- [x] Document ownership, supported expressions, explicit exclusions, and migration compatibility.

### Task 5: Verify the slice

- [x] Run the new focused public CMeta SCXML test.
- [x] Run the private expression test, main SCXML regression test, and native Statechart runtime tests.
- [x] Run the configured Release test preset or the narrowest equivalent preset documented by this repository.
- [x] Inspect `git diff --check`, the final diff, and CodeGraph affected tests; record any platform work that remains CI-only.
