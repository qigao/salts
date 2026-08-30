# CFlow SCXML CMeta `_event.name` Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Expose the current event name as the bounded, read-only CMeta expression value `_event.name` during event-triggered SCXML transition guards and executable content.

**Architecture:** Keep the native `cflow_event_view` supplied to contextual callbacks as the event fact source. Resolve its numeric event ID through an immutable program-owned O(1) index at each callback boundary, then pass a call-scoped string view into the private CMeta expression evaluator. The index uses at most `max_events * sizeof(void *)` additional program storage (512 KiB when `max_events` is 65,536 on a 64-bit target). Do not retain borrowed event payloads, change the public Statechart ABI, or claim the unsupported SCXML `_event` fields and eventless-retention semantics.

**Tech Stack:** C11, CFlow Statechart runtime, CMeta descriptors, QueryVM, TinyTest, CMake Presets.

**Normative reference:** [W3C SCXML 1.0, system variables and event structure](https://www.w3.org/TR/2015/REC-scxml-20150901/#SystemVariables)

---

### Task 1: Specify evaluator syntax and failure boundaries with tests

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_expr_test.c`
- Modify: `cflow-scxml/src/cmeta_expr.h`
- Modify: `cflow-scxml/src/cmeta_expr.c`

- [x] Add a direct evaluator test that compiles and evaluates `_event.name == "go"` and the scalar value `_event.name` from a bounded call-scoped event-name view.
- [x] Add negative cases for an absent current event, bare `_event`, and unsupported `_event.type` so the partial profile fails explicitly.
- [x] Run `cmake --build --preset win-release-user --target cflow_scxml_cmeta_expr_test` and the focused CTest filter; confirm the new positive test fails before implementation.
- [x] Add the private `event_name` system view, parse only the exact `_event.name` field, and load it through the existing bounded string operand path.
- [x] Re-run the focused evaluator test and confirm it passes.

### Task 2: Bind immutable event names at SCXML callback boundaries

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Add public integration tests for an external event transition condition, an internal `<raise>` transition condition, and event-triggered executable content using `_event.name`.
- [x] Run the focused public CMeta test and confirm the new cases fail before implementation.
- [x] Retain an immutable program event-name ID index in compiled guard/block users.
- [x] Resolve `context->event->id` once per callback into a call-scoped copy of the base system values; fail fast if an event ID is not in the compiled program map.
- [x] Thread that same call-scoped view through assignment, foreach bodies, and nested conditional expression evaluation within one executable block.
- [x] Re-run both focused CMeta tests and confirm they pass.

### Task 3: Document the exact conformance boundary

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/tests/w3c/README.md`

- [x] Document that program/session bindings support `_event.name` only while native contextual callbacks carry an Event.
- [x] Explicitly list `type`, `sendid`, `origin`, `origintype`, `invokeid`, `data`, and retention across later eventless microsteps as unsupported; keep the Phase 3 system-variable umbrella open.
- [x] Confirm all `_event` claims in the repository match this boundary with `rg.exe`.

### Task 4: Verify and prepare the branch

**Files:**
- Verify all modified files.

- [x] Run focused build/tests for `cflow_scxml_cmeta_expr_test` and `cflow_scxml_cmeta_test`.
- [x] Run the full `win-release-user` build and CTest suite with `CFLOW_ENABLE_SCXML=ON`.
- [x] Inspect `git diff --check`, `git status --short`, and the final diff for accidental ABI or unrelated changes.
- [x] Commit the verified increment on `feat/cflow-scxml-cmeta-event` and prepare the PR without marking #122 complete.
