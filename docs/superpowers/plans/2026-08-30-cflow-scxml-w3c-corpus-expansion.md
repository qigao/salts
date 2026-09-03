# CFlow SCXML W3C Corpus Expansion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the W3C-derived SCXML regression corpus from two to seven mandatory assertions while preserving each selected upstream test's observable semantics.

**Architecture:** Keep the existing fixture-driven black-box harness and native Statechart runtime as the execution boundary. Add self-driving, null-datamodel fixtures derived from the official W3C IR `.txml` sources; transform only the `conf:` result vocabulary, unsupported wildcard failure observers, and unrelated timeout safety nets. No production API or runtime behavior changes are in scope.

**Tech Stack:** C11, CFlow Statechart runtime, Salts SCXML compiler, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`

## Global Constraints

- Use the W3C SCXML 1.0 Implementation Report and linked `.txml` files as the semantic fact source.
- Preserve the selected normative assertion and its pass/fail observation path in every local fixture.
- Replace `conf:pass` and `conf:fail` only with top-level `final` states named `pass` and `fail`.
- Replace unsupported wildcard failure observers only with the finite exact events that distinguish incorrect ordering for that fixture.
- Remove timeout `send` elements only when they are unrelated liveness guards and the local harness already detects failure to reach `pass` through `done == false`.
- Keep all fixtures self-driving; do not add sleeps, external I/O, invoke support, datamodel-specific expressions, or `_event` dependencies.
- Do not claim full W3C conformance and do not close the corpus checkbox in issue #122 from this increment alone.

---

### Task 1: Add RED coverage for the selected mandatory assertions

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

**Assertions:**
- W3C 144: raised internal events retain FIFO order.
- W3C 377: multiple `onexit` handlers execute in document order.
- W3C 416: entering a compound state's final child generates `done.state.<id>`.
- W3C 417: completing every region generates the parallel state's completion event.
- W3C 419: an enabled eventless transition is selected before a queued internal event.

- [x] **Step 1: Register one named TinyTest per new fixture**

Keep assertion identifiers and behavioral descriptions visible in the suite so a failing fixture maps directly to the upstream IR row.

- [x] **Step 2: Build and run the focused test to verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_w3c_conformance_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_w3c_conformance_test$ --output-on-failure
```

Expected: the executable builds, then the new cases fail because their fixture files do not exist.

### Task 2: Add the W3C-derived fixtures one assertion at a time

**Files:**
- Create: `cflow-scxml/tests/w3c/test144.scxml`
- Create: `cflow-scxml/tests/w3c/test377.scxml`
- Create: `cflow-scxml/tests/w3c/test416.scxml`
- Create: `cflow-scxml/tests/w3c/test417.scxml`
- Create: `cflow-scxml/tests/w3c/test419.scxml`

- [x] **Step 1: Add test 144 with exact reverse-order failure edges**

Preserve the two `raise` operations and two-state observation path. Replace each wildcard failure edge with the only wrong event at that point.

- [x] **Step 2: Add test 377 with exact reverse-order failure edges**

Preserve the two separate `onexit` handlers and eventless exit transition. Observe `event1`, then `event2` after the target state is entered.

- [x] **Step 3: Add tests 416 and 417 without unrelated timeout sends**

Preserve the compound and parallel completion structures and their exact `done.state.*` transitions. Let the harness's `done` assertion detect a missing completion event.

- [x] **Step 4: Add test 419 with the queued internal event as the failure witness**

Preserve `raise internalEvent` and the eventless transition to `pass`. Replace the wildcard transition with an exact `internalEvent` transition to `fail`; omit the external `send`, because the queued internal event is sufficient to distinguish eventless precedence.

- [x] **Step 5: Build and run the focused test to verify GREEN**

### Task 3: Record provenance and review the test-only impact

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`
- Modify: `docs/superpowers/plans/2026-08-30-cflow-scxml-w3c-corpus-expansion.md`

- [x] **Step 1: Extend the provenance table with direct W3C source links**

Record the preserved assertion and the exact local transformation for every fixture. State explicitly that the suite remains a selected regression corpus, not a conformance certification.

- [x] **Step 2: Run focused and adjacent SCXML regressions**

```powershell
ctest --preset win-release-user -R "^cflow_scxml(_w3c_conformance)?_test$" --output-on-failure
```

- [x] **Step 3: Run the complete Release test suite**

```powershell
ctest --preset win-release-user --output-on-failure
```

- [x] **Step 4: Refresh CodeGraph and inspect the final diff**

Confirm the change remains test/documentation-only, `.codegraph/` is untracked and ignored, and no upstream license or provenance information is lost.

- [x] **Step 5: Commit the atomic corpus expansion**

```powershell
git add cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c cflow-scxml/tests/w3c docs/superpowers/plans/2026-08-30-cflow-scxml-w3c-corpus-expansion.md
git commit -m "test(scxml): expand W3C regression corpus"
```
