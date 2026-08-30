# CFlow SCXML W3C Execution Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add selected mandatory W3C SCXML assertions 376, 378, 387, 407, 421, and 504 to the local regression corpus without changing the public SCXML profile.

**Architecture:** Keep the existing null-datamodel, file-driven harness for history, exit, queue-drain, and transition-domain fixtures. Add one test-only Event I/O session path whose adapter deterministically returns `CFLOW_SCXML_ADAPTER_ERROR_EXECUTION`; this lets tests 376 and 378 observe that an aborted executable block does not suppress a later independent `onentry` or `onexit` block. All assertions are observed through finite exact events and terminal `pass` states.

**Tech Stack:** C11, CFlow Statechart runtime, CFlow SCXML frontend/session, TinyTest, CMake Presets, official W3C SCXML Implementation Report fixtures.

## Global Constraints

- Preserve public SCXML syntax, runtime APIs, error semantics, and capacities outside the test harness.
- Keep upstream provenance and document each local transformation in `cflow-scxml/tests/w3c/README.md`.
- Do not add wildcard event descriptors or weaken a failed semantic assertion.
- Verify the focused corpus first, adjacent `cflow-scxml` tests second, and the configured full CTest suite last.
- Do not commit `.codegraph/`, build outputs, or unrelated changes.

### Task 1: Establish RED corpus coverage

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Create: `cflow-scxml/tests/w3c/test376.scxml`
- Create: `cflow-scxml/tests/w3c/test378.scxml`
- Create: `cflow-scxml/tests/w3c/test387.scxml`
- Create: `cflow-scxml/tests/w3c/test407.scxml`
- Create: `cflow-scxml/tests/w3c/test421.scxml`
- Create: `cflow-scxml/tests/w3c/test504.scxml`

- [x] Register six named TinyTest cases and add semantic-preserving local fixtures.
- [x] Run the focused corpus through the existing raw runtime harness.
- [x] Confirm tests 376 and 378 fail because external `send` requires an owning Event I/O session; diagnose any other failure before changing an assertion.

### Task 2: Add the test-only adapter-error harness

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

- [x] Add a minimal adapter probe that rejects exactly one prepared send with `CFLOW_SCXML_ADAPTER_ERROR_EXECUTION` and is always quiescent.
- [x] Run tests 376 and 378 through an owning `cflow_scxml_session`, requiring one rejected send, terminal completion, and no runtime error.
- [x] Keep all existing fixtures and the other four new fixtures on the original raw runtime path.
- [x] Run the focused corpus and confirm all selected assertions are GREEN.

### Task 3: Document provenance and bounded claims

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`

- [x] Add official source links and concise preserved assertions for all six fixtures.
- [x] Record the counter, wildcard, timeout, and external-send transformations, including why test 421 needs only internal events for its named assertion.
- [x] Retain the explicit selected-regression, non-certification wording.

### Task 4: Verify and prepare integration

**Files:**
- Verify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Verify: `cflow-scxml/tests/w3c/*.scxml`
- Verify: `cflow-scxml/tests/w3c/README.md`

- [x] Run the focused W3C conformance target.
- [x] Run adjacent `cflow-scxml` CTest tests.
- [x] Run the full configured CTest suite.
- [x] Synchronize CodeGraph, inspect affected files, and review the final diff.
- [x] Commit, push, open a pull request, and update issue #122 after verification succeeds.
