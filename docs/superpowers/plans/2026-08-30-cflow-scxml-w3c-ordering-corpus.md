# CFlow SCXML W3C Ordering Corpus Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Expand the selected W3C-derived SCXML regression corpus with mandatory ordering assertions 404, 405, 406, and 412 without broadening the public conformance claim.

**Architecture:** Keep the existing file-driven conformance harness as the only execution path. Add local null-datamodel transformations that preserve each upstream ordering witness, replace W3C generator-only pass/fail vocabulary with ordinary final states, replace wildcard failure transitions with the finite competing event set, and omit timeout sends whose only purpose is upstream liveness detection. No runtime or public API change is planned; a failing fixture is evidence of a runtime semantic gap and must be investigated rather than weakened.

**Tech Stack:** C11, CFlow Statechart runtime, CFlow SCXML frontend, TinyTest, CMake Presets, official W3C SCXML Implementation Report fixtures.

---

### Task 1: Add failing corpus registrations

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

- [x] Add one named TinyTest case for each missing local fixture: 404, 405, 406, and 412.
- [x] Build and run only `cflow_scxml_w3c_conformance_test`.
- [x] Confirm RED because the four fixture files cannot be read; do not accept compilation or runtime semantic failures as the intended RED signal.

### Task 2: Add minimal W3C-derived fixtures

**Files:**
- Create: `cflow-scxml/tests/w3c/test404.scxml`
- Create: `cflow-scxml/tests/w3c/test405.scxml`
- Create: `cflow-scxml/tests/w3c/test406.scxml`
- Create: `cflow-scxml/tests/w3c/test412.scxml`

- [x] Transform test 404 to preserve child-before-parent, reverse-document exit ordering followed by transition content.
- [x] Transform test 405 to preserve all exits before selected transition content and document ordering among selected transitions.
- [x] Transform test 406 to preserve transition content before parent-before-child, document-ordered entry actions.
- [x] Transform test 412 to preserve parent `onentry`, explicit initial-transition content, then child `onentry`.
- [x] Keep the null data model, exact pass/fail final states, and only finite wrong-order event transitions.
- [x] Run the focused test and confirm GREEN. If any assertion fails semantically, stop and diagnose the runtime path instead of changing the assertion.

### Task 3: Document provenance and the exact claim

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`

- [x] Add the four upstream fixture links and one-sentence assertions.
- [x] Record the timeout-send and wildcard-transition transformations.
- [x] Keep the explicit statement that this is selected regression coverage, not W3C certification or full conformance.

### Task 4: Verify the bounded change

**Files:**
- Verify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Verify: `cflow-scxml/tests/w3c/*.scxml`
- Verify: `cflow-scxml/tests/w3c/README.md`

- [x] Run the focused W3C conformance test.
- [x] Run the adjacent `cflow-scxml` test set.
- [x] Run the full configured CTest suite.
- [x] Synchronize CodeGraph and inspect the affected-file report.
- [x] Confirm the worktree contains only the plan, harness registrations, four fixtures, and README changes.

### Task 5: Prepare integration

- [x] Commit the verified change on `test/cflow-scxml-w3c-corpus-2`.
- [ ] Use the finishing-development-branch workflow to present integration choices before pushing or opening a pull request.
- [ ] After integration, update issue #122 to list the new passing assertions while leaving the parent conformance-corpus checkbox open.
