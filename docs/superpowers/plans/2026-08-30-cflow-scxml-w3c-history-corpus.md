# CFlow SCXML W3C History Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add W3C-derived history assertions 579 and 580, including native support for an initial transition that targets sibling history.

**Architecture:** Keep the native Statechart instance as the sole owner of active configuration and stored history. Admit only the bounded `initial -> sibling history -> real descendant` chain, resolve stored/default configuration in the runtime, and execute eligible pseudo-transition content through the existing staged action transaction without new public or instance storage.

**Tech Stack:** C11, CFlow Statechart runtime, CFlow SCXML frontend, TurboParser XML, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-30-cflow-statechart-initial-history-chain-design.md`

## Global Constraints

- Preserve every existing accepted Statechart/SCXML behavior and ABI.
- Keep pseudo-states out of working and published active configurations.
- Resolve declared pseudo-state IDs in `In(id)` while preserving false runtime membership.
- Keep history, extended state, raised events, effects, and configuration in the existing transactional commit boundary.
- Do not add fallback parsing, interpreter paths, unbounded storage, or new dependencies.

---

### Task 1: Establish failing native and W3C regressions

**Files:**
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Create: `cflow-scxml/tests/w3c/test579.scxml`
- Create: `cflow-scxml/tests/w3c/test580.scxml`

**Interfaces:**
- Consumes: existing `history_fixture`, contextual executable bindings, and `check_w3c_fixture`.
- Produces: observable tests for the newly admitted native shape and W3C assertions 579/580.

- [x] **Step 1: Add a native initialization trace test**

Configure `HISTORY_PARENT_INITIAL` to target `HISTORY_SHALLOW`, bind actions to
the parent initial, shallow-history default, and child initial transitions, and
expect this literal trace:

```c
const int expected_trace[] = {
    microstep_trace_code(CFLOW_STATECHART_ACTION_INITIAL,
                         HISTORY_PARENT_INITIAL),
    microstep_trace_code(CFLOW_STATECHART_ACTION_HISTORY,
                         HISTORY_SHALLOW),
    microstep_trace_code(CFLOW_STATECHART_ACTION_INITIAL,
                         HISTORY_CHILD_INITIAL)};
```

- [x] **Step 2: Add W3C fixtures and named TinyTest cases**

Test 579 must emit `event1`, `event2`, and `event3` from parent entry, initial
content, and unset-history default content respectively, then re-enter the
parent and require `event1`, `event2`, and a leaf-entry witness with no second
`event3`. Test 580 must use literal `In(sh1)` guards at child, parent, exited,
and restored configurations, with every true result targeting `fail`.

- [x] **Step 3: Run RED verification**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test cflow_scxml_w3c_conformance_test --parallel
ctest --preset win-release-user -R "^(cflow_statechart_runtime_test|cflow_scxml_w3c_conformance_test)$" --output-on-failure
```

Expected: test 579 and the native regression fail because native validation
still returns `CFLOW_STATECHART_INVALID_INITIAL`; test 580 fails because
declared pseudo IDs are still rejected by `In(id)` resolution; all previously
registered cases pass.

### Task 2: Admit the bounded native pseudo chain

**Files:**
- Modify: `cflow/src/statechart.c`
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`

**Interfaces:**
- Consumes: `validate_default_transitions`, normalized parent and state-kind tables.
- Produces: acceptance of only `INITIAL -> sibling HISTORY_SHALLOW|HISTORY_DEEP`, plus declared pseudo-state `In(id)` queries that evaluate false.

- [x] **Step 1: Replace the blanket pseudo-target rejection**

Compute a literal `initial_history_target` predicate from source kind, target
kind, and equal parents. Keep every other pseudo target invalid and preserve the
existing real-descendant rule.

- [x] **Step 2: Run the native test and observe the runtime failure boundary**

The validator must pass while configuration construction remains RED. This
checkpoint separates admission from execution and prevents accidental broadening.

- [x] **Step 3: Admit declared pseudo-state `In(id)` queries**

Remove the real-state kind filter from both null and CMeta condition resolvers,
retain range/unknown checks, move the existing pseudo samples out of invalid
diagnostic tables, and require test 580 to compile before runtime chain support
is added.

### Task 3: Resolve and execute initial-to-history chains

**Files:**
- Modify: `cflow/src/statechart_runtime.c`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`
- Test: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

**Interfaces:**
- Consumes: `history_slots`, staged `history_counts`, default transition/target indices, existing pseudo action and transaction callbacks.
- Produces: startup and re-entry support with exact action order and unchanged rollback.

- [x] **Step 1: Resolve history targets during initial configuration**

When an initial default target is history, activate its real default target in
the zeroed startup history buffer while retaining the initial transition as the
owner's recorded pseudo action.

- [x] **Step 2: Resolve stored or default history during later default entry**

Pass an explicit `collect_default_action` flag through history restoration. An
ordinary history target collects its own action; an initial-to-history chain
records only the initial transition because chain execution owns the optional
history action.

- [x] **Step 3: Execute the bounded pseudo-action chain**

Add an internal helper with this behavior:

```c
run initial transition action;
if (initial target is history && staged history count == 0)
    run that history state's default transition action;
```

Use it in startup, pre-entry pseudo actions, and entry-interleaved pseudo actions.
Return the first existing runtime status without logging or fallback.

- [x] **Step 4: Run focused GREEN verification**

Run both focused test executables and require the new native trace plus fixtures
579/580 to pass without weakening any expected event or configuration check.

### Task 4: Record provenance and verify the repository

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`

**Interfaces:**
- Consumes: official W3C test579/test580 sources and the selected-corpus disclaimer.
- Produces: reviewable provenance and exact local transformation notes.

- [x] **Step 1: Document the two official sources and transformations**

State that 579 replaces the generator counter/timeout with two finite event
traces, and 580 replaces generator pass/fail operations with exact `In(sh1)`
guards and staged entry events. Keep the non-certification disclaimer.

- [x] **Step 2: Run layered verification**

```powershell
ctest --preset win-release-user -R "^(cflow_statechart_runtime_test|cflow_scxml_.*test)$" --output-on-failure
ctest --preset win-release-user --output-on-failure
git diff --check
```

- [x] **Step 3: Review compatibility and commit**

Confirm no public headers, structure sizes, capacity formulas, or unrelated
files changed; then commit the native fix, fixtures, tests, plan, and design as
one reviewable behavior change.
