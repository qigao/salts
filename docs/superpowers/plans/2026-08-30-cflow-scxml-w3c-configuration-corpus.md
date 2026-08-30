# CFlow SCXML W3C Configuration Corpus Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Expand the selected W3C-derived SCXML regression corpus with mandatory assertions 403a, 409, 411, 503, 505, 506, and 533 without changing the public SCXML profile.

**Architecture:** Keep the existing file-driven conformance harness as the only execution path. Each local null-datamodel fixture observes one upstream state-selection, configuration, or transition-domain invariant through finite internal events and ordinary `pass`/`fail` final states. The fixtures reuse the existing exact-event profile and `In(state)` support; they do not add wildcard descriptors, external I/O, datamodel variables, or new runtime APIs. A semantic failure is treated as evidence of an implementation defect and must be diagnosed rather than hidden by weakening the fixture.

**Tech Stack:** C11, CFlow Statechart runtime, CFlow SCXML frontend, TinyTest, CMake Presets, official W3C SCXML Implementation Report fixtures.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`

## Global Constraints

- Preserve the existing public SCXML syntax, runtime API, event capacities, and selected-conformance wording.
- Keep upstream provenance and describe every semantic-preserving transformation in `cflow-scxml/tests/w3c/README.md`.
- Use exact event names as finite failure witnesses; do not introduce wildcard matching as part of this test-only increment.
- Run the focused conformance executable first, then adjacent `cflow-scxml` tests, then the configured full CTest suite.
- Do not commit `.codegraph/`, build products, or unrelated user changes.

### Task 1: Add failing corpus registrations

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`

- [x] Add named TinyTest cases for fixtures 403a, 409, 411, 503, 505, 506, and 533.
- [x] Build and run only `cflow_scxml_w3c_conformance_test`.
- [x] Confirm RED solely because the seven fixture files cannot be read; compilation or runtime failures are not the intended RED signal.

### Task 2: Add state-selection and configuration-timing fixtures

**Files:**
- Create: `cflow-scxml/tests/w3c/test403a.scxml`
- Create: `cflow-scxml/tests/w3c/test409.scxml`
- Create: `cflow-scxml/tests/w3c/test411.scxml`

- [x] In test 403a, use one queued event to prove descendant-source priority and first document-order tie selection, then a second event whose false child guard falls through to the enabled ancestor transition.
- [x] In test 409, exit a descendant before its ancestor and evaluate `In(descendant)` from the ancestor `onexit`; queue `wrong` only if the exited state remains active, then queue `ready` as the success witness.
- [x] In test 411, evaluate `In(child)` from the parent `onentry` and from the child `onentry`; fail if the child is active too early and pass only when it is active before its own entry handler.
- [x] Run the focused test and confirm these three fixtures are GREEN.

### Task 3: Add transition-domain fixtures

**Files:**
- Create: `cflow-scxml/tests/w3c/test503.scxml`
- Create: `cflow-scxml/tests/w3c/test505.scxml`
- Create: `cflow-scxml/tests/w3c/test506.scxml`
- Create: `cflow-scxml/tests/w3c/test533.scxml`

- [x] In test 503, use an `onexit` witness to prove that a targetless transition executes content without exiting or re-entering its source.
- [x] In test 505, prove that an internal compound-to-proper-descendant transition exits and re-enters the child while retaining the compound source.
- [x] In test 506, prove that an internal transition targeting its own source is treated with external transition-domain semantics.
- [x] In test 533, prove that an internal transition from a parallel source is treated with external transition-domain semantics and preserves reverse-document exit order.
- [x] Observe every exit sequence with finite exact-event chains so any missing, extra, or misordered exit reaches `fail` or prevents completion.
- [x] Run the focused test and confirm all seven fixtures are GREEN. If one fails semantically, stop and use systematic debugging before changing production code or the fixture assertion.

### Task 4: Document provenance and bounded claims

**Files:**
- Modify: `cflow-scxml/tests/w3c/README.md`

- [x] Add official upstream links and one-sentence preserved assertions for all seven fixtures.
- [x] Record replacement of datamodel counters, wildcard failure transitions, and timeout sends with finite internal-event observation chains and harness completion checks.
- [x] Keep the explicit statement that this corpus is selected regression coverage, not W3C certification or complete conformance.

### Task 5: Verify and prepare integration

**Files:**
- Verify: `cflow-scxml/tests/cflow_scxml_w3c_conformance_test.c`
- Verify: `cflow-scxml/tests/w3c/*.scxml`
- Verify: `cflow-scxml/tests/w3c/README.md`

- [x] Run the focused W3C conformance test.
- [x] Run the adjacent `cflow-scxml` CTest set.
- [x] Run the full configured CTest suite.
- [x] Synchronize CodeGraph and inspect the affected-file report.
- [x] Review the diff and confirm the worktree contains only the plan, harness registrations, seven fixtures, and README changes.
- [ ] Commit, push, open a pull request, and update issue #122 only after verification succeeds.
