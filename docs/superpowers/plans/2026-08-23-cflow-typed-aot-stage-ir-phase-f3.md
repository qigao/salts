# CFlow Typed AOT Stage IR Phase F-3 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an inspectable typed Stage IR generated from the Direct schema and an executable exact-equivalence check against Graph semantics without changing the Direct hot loop.

**Architecture:** `cflow/direct.h` owns the public Stage IR contract and schema replay because the same compile-time tokens generate IR, Surface Graph and typed evaluation. `cflow/src/direct.c` owns control-plane validation and Graph matching. Existing Graph normalization remains the only Surface-to-primitive lowering source.

**Tech Stack:** ISO C11, CMeta callable/type metadata, CFlow Graph normalization, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-typed-aot-stage-ir-phase-f3-design.md`

## Global Constraints

- `StaticTarget` provenance is declared only by a compile-time schema producer; runtime code must not infer it from an erased function pointer.
- Direct evaluation remains single-threaded, allocation-free, callback-dispatch-free and bounded by caller-provided output capacity.
- Stage IR contains at most 16 immutable rows and borrows static descriptors/callables; it owns no storage.
- Graph matching is control-plane work, borrows the source Graph and transactionally owns/destroys only a temporary normalized Graph.
- Unsupported or contradictory dispatch fails explicitly; no Plan/Kernel fallback is added.

---

### Task 1: Lock the Stage IR contract with failing tests

**Files:**
- Modify: `cflow/tests/cflow_direct_test.c`

**Interfaces:**
- Consumes: existing `CFlowDirectTestSteps` and generated Direct pipeline.
- Produces: tests requiring `cflow_aot_dispatch`, `cflow_aot_stage_ir`, `cflow_aot_pipeline_ir`, `cflow_aot_equivalence_witness`, `<name>_ir`, validation, inline eligibility and Graph matching.

- [x] **Step 1: Add stable-value and generated-IR assertions**

Add `_Static_assert` checks for dispatch values `0`, `1`, `2` and a test which checks the generated pipeline has exactly Filter/Map/Map rows, exact semantic type continuity, static target names, and three `StaticTarget` dispatches.

- [x] **Step 2: Add exact Graph-equivalence behavior**

Build the generated Surface Graph, require a successful match with `source_graph_version == stream.graph.version` and `matched_stage_count == 3`, then build a graph using `cflow_direct_plus_one` in place of the expected map and require mismatch with a zero witness.

- [x] **Step 3: Add dispatch-consistency behavior**

Construct one canonical-raw row from `cflow_direct_square`, one adapter row from `cflow_direct_trap_map`, and one contradictory canonical-raw row from the adapter. Require the first two to validate and the contradictory row to fail.

- [x] **Step 4: Run the focused target and verify RED**

Run the configured MSVC Release target `cflow_direct_test`. Expected: compilation fails because the Stage IR types, generated accessor and functions do not exist.

### Task 2: Implement Stage IR validation and Graph matching

**Files:**
- Modify: `cflow/include/cflow/direct.h`
- Create: `cflow/src/direct.c`
- Modify: `cflow/CMakeLists.txt`

**Interfaces:**
- Consumes: `cmeta_callable_bind`, `cmeta_callable_same`, `cflow_graph_normalize`, semantic type comparison and linear Graph accessors.
- Produces: `cflow_aot_pipeline_ir_validate`, `cflow_aot_pipeline_ir_inline_eligible`, and `cflow_aot_pipeline_ir_match_graph`.

- [x] **Step 1: Declare the stable public representation**

Define explicit dispatch values, immutable borrowed stage/pipeline records, the committed witness record and public boolean APIs with optional static error strings.

- [x] **Step 2: Implement representation and dispatch validation**

Validate non-empty bounded stage arrays, exact input/output type continuity, Filter/Map signatures, value contracts, trivial lifecycle traits, and dispatch-specific capture/canonical/static-name requirements. Clear output errors on success.

- [x] **Step 3: Implement exact Graph matching**

Normalize the source Graph into zero-state temporary storage, require one exact linear root path containing Source followed by every Stage IR row, compare types and `cmeta_callable_same`, reject extra nodes/edges/branches, commit the witness only on success, and destroy temporary storage on every path.

- [x] **Step 4: Add the implementation to `turbo_cflow`**

Add `src/direct.c` to the existing library source list without changing target dependencies or installation rules.

- [x] **Step 5: Build and run the focused test to verify GREEN**

Build and run `cflow_direct_test`; expect every new and existing Direct test to pass.

### Task 3: Generate Stage IR from the Direct schema

**Files:**
- Modify: `cflow/include/cflow/direct.h`
- Modify: `cflow/tests/cflow_direct_test.c`

**Interfaces:**
- Consumes: `(kind, input_type, output_type, callable)` rows from `CFlowDirectSteps`.
- Produces: translation-unit-static `<name>_aot_stages`, `<name>_aot_ir_value`, and `<name>_ir()` while preserving existing builder/evaluator names.

- [x] **Step 1: Replay each schema row into immutable IR**

Generate Filter/Map kind, `StaticTarget`, `CMETA_TYPEOF` descriptors, a borrowed callable address and the stringized callable token for every row.

- [x] **Step 2: Keep generated eligibility unrolled and cross-check the IR**

Keep the existing schema-expanded eligibility path outside the item loop and
require tests to agree with `cflow_aot_pipeline_ir_inline_eligible(<name>_ir(),
NULL)`. A generic out-of-line IR traversal on every evaluator call is prohibited
because the F-3 benchmark demonstrated a regression above 10%.

- [x] **Step 3: Run Direct, pipeline and C++ header tests**

Build and run `cflow_direct_test`, `cflow_pipeline_test` and `cflow_header_cpp_test`; expect identical existing observable behavior plus the new IR assertions.

### Task 4: Verify compatibility, resources and performance

**Files:**
- Modify: `docs/superpowers/specs/2026-08-23-cflow-typed-aot-stage-ir-phase-f3-design.md` only if measured results add factual data.

**Interfaces:**
- Consumes: configured MSVC and Clang Release trees and `cflow_direct_benchmark`.
- Produces: reproducible correctness and performance evidence.

- [x] **Step 1: Run the CMeta/CFlow MSVC Release matrix**

Build all existing CMeta/CFlow test targets and run `ctest --preset win-release-user -R "^(cmeta|cflow)_.*test$"`; require zero failures.

- [x] **Step 2: Run the configured Clang Release matrix**

Build the same targets in the existing Clang Release tree and run its matching CTest filter; require zero failures.

- [x] **Step 3: Re-run the Direct benchmark**

Run at least five paired Release samples per compiler. Compare Direct and Plan medians with the F-2B recorded medians; investigate or revert any throughput regression greater than 10%.

- [x] **Step 4: Verify resource invariants**

Confirm Direct still allocates zero bytes in its evaluator and existing Plan selection/intermediate/result allocation assertions remain exact. Run `git diff --check`.

### Task 5: Review, commit and update the Draft PR

**Files:**
- Modify: this plan's checkbox state and measured-result section in the design document.

**Interfaces:**
- Consumes: the complete diff and fresh verification outputs.
- Produces: one reviewable commit pushed to `feat/cflow-direct-executor-phase-f2` and Draft PR #35.

- [x] **Step 1: Self-review public API, ownership and mismatch paths**

Check stable enum values, C/C++ header compatibility, static-lifetime borrowing, witness transactionality, exact Graph topology checks and absence of fallback.

- [ ] **Step 2: Commit the verified change**

Stage only F-3 files and commit with `feat(cflow): add typed AOT stage IR`.

- [ ] **Step 3: Push the existing Draft PR branch**

Push `feat/cflow-direct-executor-phase-f2` to `origin` and confirm the worktree is clean and synchronized.
