# CFlow SCXML Invoke Idlocation Implementation Plan

> Historical plan: V3 hook details below describe the implementation at that
> time. The current Statechart boundary is V4-only `on_host_transaction`; V1-V3
> types and prefix compatibility have been removed.

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task.

**Goal:** Implement issue #177 with stable-only, bounded, transactional CMeta `invoke/@idlocation`, dynamic invocation identity, and backward-compatible runtime hooks ABI v3.

**Spec:** `docs/superpowers/specs/2026-08-30-cflow-scxml-invoke-idlocation-design.md`

**Architecture:** Add a v3 stable transaction callback to the native runtime, reuse its double buffers/internal FIFO/effect journal, then lower idlocation into an owned CMeta location and keep each committed runtime ID in the bounded invocation row. Dynamic done reporting maps a live token to the finite internal done Event while exposing the actual row ID through SCXML metadata.

**Tech Stack:** C11, CMeta managed values, CFlow Statechart runtime, CFlow SCXML, TinyTest, CMake Presets/CTest.

## Global Constraints

- Machine published state is the sole mutable state fact source; never mutate a const published view or maintain a session state mirror.
- IDs are exactly `<owner-state-id>.<unsigned-decimal-token>`, use a session-local monotonic `uint64_t`, are bounded by `CFLOW_SCXML_EVENT_METADATA_CAPACITY`, and never wrap or truncate.
- Runtime hooks v1/v2 retain their historical accepted prefix sizes and behavior; v3 fields are read only when covered by `struct_size`.
- `on_stable` and `on_stable_transaction` are mutually exclusive for v3.
- State, staged internal Events, row transitions, and adapter effect tickets commit together or roll back together; adapter ticket commit occurs after Machine publication.
- Invocation adapter ABI v1/v2 remains unchanged.
- Tests are written and observed failing for the intended reason before implementation.
- Use only CMake presets for configure/build/test and run focused tests before the full suite.

---

### Task 1: Add native stable transaction hook ABI v3

**Files:**
- Modify: `cflow/include/cflow/statechart_instance.h`
- Modify: `cflow/src/statechart_instance.c`
- Modify: `cflow/tests/cflow_statechart_instance_test.c`

1. Add red TinyTest coverage for old v1/v2 prefix compatibility, valid/invalid v3 shapes, NOOP, COMMIT, FATAL, invalid staged work, effect commit/discard order, cancellation winner, and managed-state lifetime.
2. Append `CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V3`, the stable transaction result/context/callback types, and `on_stable_transaction` without changing existing field order.
3. Replace `sizeof(current-struct)` validation for old versions with fixed prefix-size validation and field-presence-safe copying.
4. Implement the stable transaction using existing staging buffers, `stage_internal_event`, `stage_external_effect`, `commit_*`, `discard_*`, and managed-value helpers. Publish under the instance lock; commit tickets after unlock.
5. Prove old v1/v2 paths do not add copies, version changes, events, or effects.
6. Build and run `cflow_statechart_runtime_test` through `win-release-user`; commit the task.

### Task 2: Compile and execute transactional invoke idlocation

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`
- Modify as required for public constants only: `cflow-scxml/include/cflow/scxml.h`

1. Add red admission tests for top-level/nested owned strings and every rejected location class in the spec, including id conflict and checked length/depth boundaries.
2. Add red runtime tests for stable entry, transient entry, re-entry uniqueness, parallel document order, v1/v2 adapter acceptance, recoverable rejection, fatal multi-invoke rollback, assignment failure, effect exhaustion, cancellation, and shutdown.
3. Extend invocation descriptors with the compiled owned-string location and checked runtime/done-name length budget. Keep a finite internal declaration/done Event key.
4. Extend bounded invocation rows and lifecycle reservations so dynamic ID/token/state transitions follow the spec. Use row ID for start, cancel, forward, returned metadata and lifecycle routing.
5. Replace the idlocation program's v2 stable start path with the v3 stable transaction callback. Generate `<owner>.<token>`, assign staged CMeta state, evaluate other arguments, prepare adapter tickets, stage row-transition effects/errors, and return NOOP/COMMIT/FATAL exactly as specified.
6. Preserve the existing v2 stable hook for programs without invoke idlocation and preserve literal/auto-ID behavior outside this feature.
7. Build and run `cflow_scxml_cmeta_test` and the adjacent `cflow_scxml_test`; commit the task.

### Task 3: Complete dynamic done identity, documentation, and verification

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`
- Modify: `cflow/README.md`

1. Add red tests for `cflow_scxml_session_report_invoke_done`: active token success, zero/stale/cancelled rejection, queued-done cancellation race, two invocation mappings, finalize exactly once, dynamic `_event.name`, and matching `_event.invokeid`.
2. Add the additive public done-report function and route it through existing tagged external admission without changing adapter structs.
3. Make Event observation synthesize bounded `done.invoke.<row-id>` for a matching dynamic completion and use the same row snapshot for `_event.invokeid`; retain numeric finite Event selection and preprocess lifecycle behavior.
4. Update `cflow/README.md` to document CMeta-only owned-string admission, stable transaction/publication ordering, ID form/exhaustion, adapter compatibility, done-report API, and the exact remaining prefix/wildcard limitation.
5. Run `git diff --check`, focused SCXML tests, all `cflow_scxml` tests, and `ctest --preset win-release-user --output-on-failure`; commit the task.
