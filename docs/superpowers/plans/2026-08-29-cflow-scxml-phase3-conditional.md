# CFlow SCXML Phase 3 Conditional Executable Content Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute null-data-model SCXML `if`/`elseif`/`else` blocks using exact action-time `In(id)` configuration semantics without duplicating Statechart instance state.

**Architecture:** Add a source-compatible contextual Statechart executable binding that exposes a borrowed read-only active-state query over the runtime's bounded action-time working configuration. Compile structured conditional blocks in `TurboUtils::CFlowScxml`, resolve every `In(id)` at admission, and interpret only the first matching partition through the existing transactional action/raise boundary.

**Tech Stack:** C11, CMeta descriptors, CFlow Statechart IR/runtime, TurboUtils XML parser, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-conditional-design.md`

## Global Constraints

- Obtain explicit approval before modifying the public Statechart callback/binding contract.
- Preserve legacy executable callback signatures and three-field source initializers.
- Keep `cflow_statechart_instance` as the sole mutable active-configuration fact source.
- Expose only a call-scoped read-only query; no borrowed configuration pointer may escape.
- Preserve bounded storage, checked arithmetic, fail-fast admission, deterministic document order, and transactional microstep rollback.
- Support only Appendix B.1 null-data-model `In(id)`; do not add a generic expression evaluator or accessible system variables.
- Keep transition `cond` outside this atomic increment.

---

### Task 1: Specify contextual native executable observations with RED tests

**Files:**
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `cflow/include/cflow/statechart_runtime.h`

**Interfaces:**
- Consumes: legacy `cflow_statechart_executable_fn`, instance-owned configuration, action phases.
- Produces: contextual callback type and call-scoped `is_active` query contract.

- [x] **Step 1: Add failing source-compatibility and binding-shape tests**

Keep one legacy three-field initializer and add contextual rows covering exactly
one callback, neither callback, and both callbacks. Assert only the exactly-one
forms pass runtime binding admission.

- [x] **Step 2: Build the focused native target and verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test --parallel
ctest --preset win-release-user -R ^cflow_statechart_runtime_test$ --output-on-failure
```

Expected: compilation fails because the contextual public types/field do not
exist.

- [x] **Step 3: Add the documented public types and append the binding field**

Declare the call-scoped state query, contextual argument struct, and contextual
callback. Append the new callback after the existing binding fields and update
binding validation to require exactly one callback kind.

- [x] **Step 4: Build and verify the admission tests are GREEN**

- [x] **Step 5: Commit the contextual binding contract**

```bash
git add cflow/include/cflow/statechart_runtime.h cflow/tests/cflow_statechart_runtime_test.c
git commit -m "feat: add contextual statechart executables"
```

### Task 2: Implement exact action-time active-configuration queries

**Files:**
- Modify: `cflow/src/statechart_runtime.c`
- Modify: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md`

**Interfaces:**
- Consumes: normalized state-ID lookup, staged/published configuration bits,
  exit/entry order, initial/history default transition rows.
- Produces: call-scoped O(log states) `is_active` observations and W3C-ordered
  initial/history action interleaving.

- [x] **Step 1: Add failing exact observation trace tests**

Cover one exit span, selected transition content, nested entries, parallel
entries, initial default content, and history default content. Record both
action phase/order and `is_active` results so the test distinguishes partial
action-time configuration from the old/final configurations.

- [x] **Step 2: Run the focused test and verify semantic RED**

Expected: contextual callbacks are admitted but receive no valid query or
observe the old initial/history ordering.

- [x] **Step 3: Add checked working storage and query implementation**

Account for one working bitset and one state-indexed pseudo-transition lookup
in instance requirements. Initialize, rebuild, and clear them only on the
single-owner executor. Resolve query IDs through the immutable native IR and
bit-test the working configuration.

- [x] **Step 4: Interleave configuration mutation and action spans**

For exits, invoke the full state action span before clearing the state. For
entries, set the state, run its entry span, then run the selected initial or
history default transition span owned by that state. Use the same procedure for
initial stabilization and ordinary microsteps. Keep the final staged
configuration and commit point unchanged.

- [x] **Step 5: Add failure/storage regressions and verify GREEN**

Cover max-storage rejection, checked overflow, callback failure, bounded raise
failure, no partial publication, and legacy callback behavior.

- [x] **Step 6: Commit runtime configuration observations**

```bash
git add cflow/src/statechart_runtime.c cflow/tests/cflow_statechart_runtime_test.c docs/superpowers/specs/2026-08-27-cflow-statechart-phase1-design.md
git commit -m "fix: expose action-time statechart configuration"
```

### Task 3: Admit and lower null-data-model conditional blocks

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Create: `cflow-scxml/tests/fixtures/conditional_trace.scxml`
- Create: `cflow-scxml/tests/fixtures/conditional_trace.expected`

**Interfaces:**
- Consumes: contextual executable binding and program-owned executable block
  storage.
- Produces: structured `IF` branches with resolved `In(id)` predicates and
  nested ordered step spans.

- [x] **Step 1: Add failing first-true, else, no-match, and nested tests**

Use only `raise` inside partitions so outcomes are observed through the existing
bounded internal Event path. Include empty partitions and prove only the first
true partition executes.

- [x] **Step 2: Add failing phase/configuration fixture**

Exercise conditional blocks in `onexit`, ordinary transition content,
`onentry`, and initial/history default transition content. The expected trace
must distinguish the W3C incremental configuration at every point.

- [x] **Step 3: Run focused SCXML tests and verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: compilation returns `CFLOW_SCXML_UNSUPPORTED_FEATURE` for `if`.

- [x] **Step 4: Implement structure and null-condition admission**

Add `if`, `elseif`, and `else` element kinds; validate placement, cardinality,
empty marker content, and required attributes. Parse `In(id)` with bounded
whitespace handling, resolve a declared real-state ID, and diagnose malformed,
unknown, or pseudo-state arguments at the `cond` attribute.

- [x] **Step 5: Add checked structured block storage and contextual execution**

Count and allocate branch/step rows transactionally. Emit index-based nested
spans and evaluate them through a contextual SCXML callback. Copy the null
extended-state witness unchanged and stop immediately on bounded raise failure.

- [x] **Step 6: Verify focused GREEN and rollback semantics**

Add a selected multi-raise branch with insufficient internal capacity and
assert the original configuration/version, zero committed internal Events, and
the native queue-full status.

- [x] **Step 7: Commit conditional lowering**

```bash
git add cflow-scxml/src/scxml.c cflow-scxml/tests/cflow_scxml_test.c cflow-scxml/tests/fixtures
git commit -m "feat: execute SCXML conditional blocks"
```

### Task 4: Document, package-test, and verify the atomic feature

**Files:**
- Modify: `cflow/README.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-conditional-design.md`
- Modify: `docs/superpowers/plans/2026-08-29-cflow-scxml-phase3-conditional.md`

- [x] **Step 1: Document the supported grammar and borrowed context lifetime**

State that null conditions are exactly `In(id)`, conditional blocks may nest,
transition `cond` remains unsupported, system variables remain inaccessible,
and old binaries must relink because the binding row size changed.

- [x] **Step 2: Extend build-tree and installed-consumer checks**

Compile one legacy native binding and one SCXML conditional program when the
feature is enabled. Preserve the negative target/header assertions when SCXML
is disabled.

- [x] **Step 3: Run focused and adjacent regressions**

```powershell
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_runtime_test|cflow_statechart_test|cflow_statechart_hierarchy_adapter_test)$" --output-on-failure
cmake --build --preset win-release-user --target turbo_cflow cflow_scxml_test --parallel
```

- [x] **Step 4: Run package and full repository verification**

Use the repository's existing SCXML-enabled/disabled package tests, then:

```powershell
ctest --preset win-release-user --output-on-failure
```

- [x] **Step 5: Review the diff and update completed checkboxes**

Confirm no unrelated files, unbounded state, generic expression evaluator,
system-variable claim, or transition-condition claim entered the patch.

- [x] **Step 6: Commit documentation and verification**

```bash
git add cflow/README.md tests/install_consumer/consumer.c docs/superpowers
git commit -m "docs: describe SCXML conditional execution"
```

### Task 5: Review and integration

- [x] **Step 1: Run final verification from a clean branch state**
- [x] **Step 2: Request code review focused on ABI, ordering, ownership, and rollback**
- [ ] **Step 3: Push the branch and create a PR linked to issue #122**
- [ ] **Step 4: Wait for Linux/macOS/Windows CI and address reproducible failures**
- [ ] **Step 5: After merge only, mark `Implement conditional executable content` complete in #122**
