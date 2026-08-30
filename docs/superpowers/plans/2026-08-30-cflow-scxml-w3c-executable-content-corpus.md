# CFlow SCXML W3C Executable-Content Corpus Plan

> **For Codex:** Execute this plan inline with focused red/green verification at each task boundary.

**Goal:** Expand the selected W3C-derived SCXML regression corpus with mandatory executable-content assertions 147, 148, 149, 158, and 159 without changing the public SCXML profile.

**Architecture:** Keep the existing null-datamodel compiler/runtime path and the existing W3C harness as the facts under test. Transform generator-only `conf:` data checks into finite internal-event traces. Reuse the owning-session adapter-error harness for assertion 159 so the first `onentry` block fails deterministically while a second independent block supplies a completion witness.

**Tech Stack:** C11, CFlow Statechart/SCXML, TurboParser XML frontend, TinyTest, CMake Presets.

---

### Task 1: Register the selected assertions

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

- [x] Add named TinyTest cases for fixtures 147, 148, 149, and 158 through `check_w3c_fixture`.
- [x] Add assertion 159 through `check_w3c_adapter_error_fixture`.
- [x] Build and run the focused target and confirm RED because the fixture files do not yet exist.

### Task 2: Add minimal W3C-derived fixtures

**Files:**
- Create: `cflow-scxml/tests/w3c/test147.scxml`
- Create: `cflow-scxml/tests/w3c/test148.scxml`
- Create: `cflow-scxml/tests/w3c/test149.scxml`
- Create: `cflow-scxml/tests/w3c/test158.scxml`
- Create: `cflow-scxml/tests/w3c/test159.scxml`

- [x] Preserve assertion 147 by observing that only the first true partition raises its event.
- [x] Preserve assertion 148 by observing the `else` event after all conditions are false.
- [x] Preserve assertion 149 by observing the post-`if` witness with no branch event queued.
- [x] Preserve assertion 158 with a two-state observer for document-order raises.
- [x] Preserve assertion 159 by rejecting `send`, failing on the subsequent same-block event, and passing only on the later independent-block witness.
- [x] Run the focused target and require all selected fixtures to terminate in `pass` without runtime errors.

### Task 3: Record provenance and transformation boundaries

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`

- [x] Link each local fixture to its official W3C `.txml` source.
- [x] Explain the finite exact-event replacements for generator counters and wildcard failure transitions.
- [x] State that assertion 159 uses the existing test-only adapter failure and a separate block only as an observable witness.
- [x] Keep the named-assertion-only, non-certification scope explicit.

### Task 4: Verify the change in layers

- [x] Build and run `cflow_scxml_w3c_conformance_test`.
- [x] Run the adjacent `cflow-scxml` test set.
- [x] Run the full configured CTest suite.
- [x] Inspect `git diff --check`, branch status, and the final diff before integration.
