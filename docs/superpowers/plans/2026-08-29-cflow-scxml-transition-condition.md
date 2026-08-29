# CFlow SCXML Transition Condition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute SCXML null-data-model transition `cond="In(id)"` through native Statechart guards that query the published active configuration.

**Architecture:** Extend native guard bindings with an optional contextual callback sharing the existing call-scoped active-state query type. Resolve SCXML state names during compilation, emit bounded program-owned guard rows/users, and pass those bindings into the unchanged deterministic selection pipeline.

**Tech Stack:** C11, CMeta descriptors, CFlow Statechart IR/runtime, TurboUtils XML parser, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-transition-condition-design.md`

## Global Constraints

- Preserve the legacy guard callback signature and three-field source initializers.
- Treat the approved guard-binding row-size change as a public ABI break requiring consumer recompilation.
- Keep `cflow_statechart_instance` as the only active-configuration fact source.
- Expose only a call-scoped query over the immutable published selection configuration.
- Support only the null grammar `In(id)`; do not add a generic evaluator or fallback.
- Reject conditions on initial/history default transitions.
- Keep every generated row, allocation product, and lookup bounded and checked.
- Preserve selection priority, conflict filtering, error latching, and program/instance ownership rules.

---

### Task 1: Add the contextual native guard contract

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`

**Interfaces:**
- Consumes: `cflow_statechart_guard_fn`, `cflow_statechart_is_active_fn`, `cflow_statechart_guard_binding`.
- Produces: `cflow_statechart_guard_context`, `cflow_statechart_contextual_guard_fn`, appended `contextual_fn` binding field.

- [x] **Step 1: Write RED binding-shape tests**

Add one legacy `{id, fn, user}` initializer and contextual rows using:

```c
static bool contextual_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error);
```

Assert legacy-only and contextual-only rows initialize successfully, while
rows with neither or both callback kinds return
`CFLOW_STATECHART_RUNTIME_BINDING_MISMATCH`.

- [x] **Step 2: Build the focused target and verify RED**

Run under `VsDevCmd.bat`:

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test --parallel
```

Expected: compilation fails because the contextual guard public types and
field do not exist.

- [x] **Step 3: Add the minimal public declarations and XOR admission**

Append `contextual_fn` after `user`, document borrowed lifetime and event
semantics, and change binding admission to:

```c
(binding->fn == NULL) != (binding->contextual_fn == NULL)
```

- [x] **Step 4: Rebuild and run the focused admission tests until GREEN**

```powershell
ctest --preset win-release-user -R "^cflow_statechart_runtime_test$" --output-on-failure
```

### Task 2: Query published configuration from contextual guards

**Files:**
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md`

**Interfaces:**
- Consumes: normalized state lookup and `configurations[impl->published].bits`.
- Produces: contextual invocation with event/extended-state parity and a borrowed `is_active` query.

- [x] **Step 1: Write RED behavioral tests**

Use a real contextual guard to assert active and inactive real states, unknown
and pseudo IDs, the non-NULL Event for Event triggers, and NULL Event for
eventless/completion triggers. Keep one legacy failure test to catch accidental
error-contract changes.

- [x] **Step 2: Run focused tests and verify RED**

Expected: contextual-only binding admission succeeds after Task 1, but
selection cannot yet invoke it with a configuration query.

- [x] **Step 3: Implement the minimal published-configuration query**

Create a call-scoped query object containing the immutable IR and published
bitset. In `guard_enabled`, invoke `contextual_fn` when present; otherwise call
the unchanged legacy callback. Do not allocate, copy configuration, or retain
the query.

- [x] **Step 4: Run native Statechart tests until GREEN**

```powershell
ctest --preset win-release-user -R "^(cflow_statechart_runtime_test|cflow_statechart_test|cflow_statechart_hierarchy_adapter_test)$" --output-on-failure
```

### Task 3: Compile and expose SCXML guard rows

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Consumes: `parse_null_in_condition`, `resolve_condition_state`, contextual guard API.
- Produces: guarded native transitions and `cflow_scxml_program_guard_bindings()`.

- [x] **Step 1: Write RED compiler and accessor tests**

Compile a transition with `cond='In(active)'`; assert success, one native guard,
the transition's nonzero guard ID, and one borrowed binding. Assert invalid
accessor arguments leave sentinels unchanged. Add malformed, quoted, unknown,
pseudo-state, and initial/history-default condition cases with literal expected
statuses and attribute locations.

- [x] **Step 2: Configure SCXML ON, build, and verify RED**

```powershell
cmake --fresh --preset win-release-user -DCFLOW_ENABLE_SCXML=ON
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R "^cflow_scxml_test$" --output-on-failure
```

Expected: valid transition conditions still return
`CFLOW_SCXML_UNSUPPORTED_FEATURE` and the accessor is undefined.

- [x] **Step 3: Add bounded guard counts, rows, users, and ownership**

Count one guard for each lowered conditioned transition row. Allocate
`cflow_statechart_guard`, `cflow_statechart_guard_binding`, and immutable
state-ID user rows with checked products; set each transition guard ID; publish
declarations through `cflow_statechart_definition`; transfer bindings/users to
the owning program and free them on every cleanup path.

- [x] **Step 4: Add strict condition admission and accessor implementation**

Reuse the exact null parser. Reject condition-bearing pseudo defaults, resolve
only declared real states, and return program-owned rows through
`cflow_scxml_program_guard_bindings()` without modifying outputs on invalid
arguments.

- [x] **Step 5: Run compiler/accessor tests until GREEN**

```powershell
ctest --preset win-release-user -R "^cflow_scxml_test$" --output-on-failure
```

### Task 4: Verify transition selection semantics and packaging

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `cflow/README.md`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Consumes: SCXML program statechart, initial value, executable bindings, and guard bindings.
- Produces: observable true/false selection behavior and installed-consumer coverage.

- [x] **Step 1: Add end-to-end selection regressions**

Exercise true and false Event conditions, false child-to-ancestor fallback,
eventless stabilization, completion selection, parallel `In(region)`, and
multi-event row expansion. Configure real instances with both program binding
views and assert active configurations or terminal completion.

- [x] **Step 2: Run focused integration tests after the lower-level GREEN cycles**

Expected: every condition path is exercised through real generated bindings;
omitting those bindings remains covered by native binding-mismatch tests.

- [x] **Step 3: Complete minimal wiring and documentation**

Pass guard rows/count into instance configs in tests and installed consumer.
Document exact `In(id)` syntax, borrowed program lifetime, selection-time
configuration, forbidden pseudo defaults, and required binary relink.

- [x] **Step 4: Run focused and adjacent GREEN verification**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test cflow_statechart_runtime_test --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_runtime_test|cflow_statechart_test|cflow_statechart_hierarchy_adapter_test)$" --output-on-failure
```

### Task 5: Package, full regression, and delivery

**Files:**
- Modify: `docs/superpowers/plans/2026-08-29-cflow-scxml-transition-condition.md`
- Inspect: all changed native runtime, SCXML, tests, docs, and package-consumer files.

**Interfaces:**
- Consumes: completed source tree and existing package verification targets.
- Produces: reproducible Release evidence and a review-ready branch.

- [x] **Step 1: Run enabled and disabled package verification**

Use the repository's existing install/package targets with SCXML ON and OFF;
verify the installed consumer compiles against the public contextual guard and
SCXML binding accessor.

- [x] **Step 2: Run complete MSVC Release verification**

```powershell
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
```

- [x] **Step 3: Sync CodeGraph and inspect affected callers/diff**

Confirm no generic expression engine, configuration mirror, unbounded storage,
foreign XML dependency, unrelated edit, placeholder, or `.codegraph` artifact
entered the branch.

- [x] **Step 4: Mark plan steps complete and create focused commits**

Commit native contextual guards separately from SCXML lowering/documentation so
each review boundary remains testable.

- [x] **Step 5: Use verification and branch-finishing skills for handoff**

Do not claim completion, create a PR, merge, or update issue #122 until local
evidence and the selected integration action permit it.
