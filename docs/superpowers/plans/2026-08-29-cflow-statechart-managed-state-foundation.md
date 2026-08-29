# CFlow Statechart Managed State Foundation Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task by task.

**Goal:** Admit lifecycle-managed CMeta extended state in the native Statechart transaction without changing trivial-state or Event payload behavior.

**Architecture:** Keep the published/alternate/scratch three-slot design, add explicit live-state tracking, and route every construction, replacement, rollback, and destroy through the existing `value_storage.h` lifecycle helpers. Managed state remains executor-owned; the legacy synchronous snapshot API stays byte-copy-only.

**Tech Stack:** C11, CMeta lifecycle traits, CFlow SerialExecutor, TinyTest, CMake Presets.

---

### Task 1: Specify managed state admission and ownership

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`

- [x] Replace the rejection regression with tests proving managed initial state is independently copied and destroyed.
- [x] Add a failing-copy test proving initialization publishes no instance and destroys every successfully constructed slot.
- [x] Run `cmake --build --preset win-dev-user --target cflow_statechart_runtime_test` and the target directly; confirm the new tests fail for the expected unsupported-type result.

### Task 2: Add lifecycle-aware transaction slots

**Files:**
- Modify: `cflow/src/statechart_runtime.c`
- Reuse: `cflow/src/value_storage.h`

- [x] Track liveness for the two state buffers and action scratch buffer.
- [x] Add small helpers for reset, copy-construct, action-output preparation, transaction finalization, and rollback.
- [x] Admit `cflow_value_type_supported(state_type)` while keeping Event payload admission on `cflow_value_storage_type_supported`.
- [x] Construct both initial state slots with rollback-safe cleanup and destroy all live slots from `instance_impl_free`.
- [x] Run the focused runtime target until Task 1 tests pass.

### Task 3: Prove action success and rollback ownership

**Files:**
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `cflow/src/statechart_runtime.c`

- [x] Add a two-action managed-state test that exercises both buffer directions and verifies the final value through action observation.
- [x] Add an action-failure test that verifies the published value survives and all staged resources are destroyed exactly once.
- [x] Make action invocation destroy only known-live outputs and require failed callbacks to leave output uninitialized.
- [x] Finalize the current action value into the staged slot before publication; reset all transaction slots on every failure/cancellation path.
- [x] Run the focused runtime target after each red/green step.

### Task 4: Preserve snapshot compatibility and document the boundary

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`

- [x] Add a test that managed state returns false from `copy_state` without writing output or returning a type descriptor.
- [x] Guard `copy_state` so it only performs byte copies for trivial state.
- [x] Document managed action-output and snapshot ownership rules; keep all existing trivial snapshot tests unchanged.

### Task 5: Verify the foundation and record impact

**Files:**
- Verify: `cflow/tests/cflow_statechart_runtime_test.c`
- Verify: `cflow-scxml/tests/cflow_scxml_test.c`

- [x] Run `cmake --build --preset win-dev-user --target cflow_statechart_runtime_test cflow_scxml_test`.
- [x] Run `ctest --preset win-dev-user -R "^(cflow_statechart_runtime_test|cflow_scxml_test)$" --output-on-failure`.
- [x] Run `codegraph affected -p . cflow/src/statechart_runtime.c cflow/include/cflow/statechart_runtime.h cflow/tests/cflow_statechart_runtime_test.c` and inspect reported callers/tests.
- [x] Review `git diff --check`, `git diff --stat`, and `git status --short` before claiming completion.
