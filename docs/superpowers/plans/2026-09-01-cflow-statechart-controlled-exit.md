# CFlow Statechart Controlled Exit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task by task.

**Goal:** Add an executor-owned controlled-exit primitive to CFlow and consume
it from TurboSCXML so W3C invoke cancellation test 250 executes every active
state's `onexit` handler.

**Architecture:** CFlow remains the only active-configuration and extended-state
fact source. A mutex-protected request closes admission, while the existing
SerialExecutor performs a bounded transaction over the published active
configuration. TurboSCXML only selects this lifecycle operation; it does not
mirror state or synthesize transitions.

**Spec:**
`docs/superpowers/specs/2026-09-01-cflow-statechart-controlled-exit-design.md`

## Task 1: Specify CFlow behavior with failing tests

**Files:**
- Modify: `cflow/tests/cflow_statechart_instance_test.c`
- Modify: `cflow/include/cflow/statechart_instance.h`

- [x] Add a quiescent nested-exit test proving exit order, empty published
  configuration, state mutation, cancelled terminal stats, and rejected later
  admission.
- [x] Add an in-flight microstep test proving the current commit wins before
  controlled exit and no raised internal continuation executes afterward.
- [x] Add exit-action failure coverage proving rollback and first-error
  propagation.
- [x] Build the focused target and record RED because the new function is not
  implemented.

## Task 2: Implement the controlled-exit transaction

**Files:**
- Modify: `cflow/src/statechart_instance.c`

- [x] Add a distinct idempotent request flag and immediate admission closure.
- [x] Reuse the existing staging buffers, exit ordering, executable context,
  and effect journal to publish an empty terminal configuration.
- [x] Integrate the exit operation into the existing driver/reservation
  lifecycle, preserving an already-committing microstep.
- [x] Preserve hard cancel, clean completion, error-first-winner, timer, waiter,
  and destroy semantics.
- [x] Run focused tests to GREEN, then adjacent Statechart and Actor tests.

## Task 3: Publish and verify the Salts change

- [x] Run `git diff --check` and inspect the public API documentation.
- [x] Build all CFlow targets and run all `cflow_*` tests.
- [x] Run the complete Windows Release test preset.
- [ ] Commit, push, create the Salts PR, and merge it after checks pass.
- [ ] Install the merged Salts Release package used by TurboSCXML.

## Task 4: Integrate TurboSCXML and W3C test 250

**Files:**
- Modify: `src/scxml_session.c`
- Modify: `tests/scxml_w3c_conformance_test.c`
- Modify: `tests/w3c/scxml_manifest.c`
- Modify: `tests/w3c/README.md`
- Modify: `docs/specs/scxml-invoke-cancellation-design.md`

- [ ] Change only `scxml_session_cancel()` to request controlled CFlow exit.
- [ ] Add the real parent/child test 250 fixture with nested child `onexit`
  observations and no `done.invoke` return.
- [ ] Change manifest test 250 from mandatory UNSUPPORTED to PASS and update
  the conformance counts and cancellation documentation.
- [ ] Run focused cancellation and W3C tests, then all TurboSCXML tests.
- [ ] Commit, push, create the TurboSCXML PR, merge it, and update issue #2.

## Completion checks

- [x] Existing CFlow hard cancel tests retain their exact behavior.
- [x] No callback executes under the instance mutex.
- [x] Every staged exit effect reaches exactly one commit/discard terminal path.
- [x] TurboSCXML creates no parallel configuration or data-model fact source.
- [ ] Local Windows results are reported separately from Linux/macOS CI.
