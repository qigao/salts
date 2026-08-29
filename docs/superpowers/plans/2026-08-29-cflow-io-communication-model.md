# CFlow I/O Communication Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the shared Actor/I/O communication contract from readiness, completion, and blocking driver models without breaking the existing CFlow I/O Actor API, and prove the corresponding refinement properties in Lean.

**Architecture:** Add a public value-only communication contract, model the existing Actor backend callbacks explicitly as a completion driver, and model each driver family independently. Lean projects each concrete driver state onto one common abstract contract and proves preservation and refinement.

**Tech Stack:** C11, TurboUtils Disruptor/thread primitives, TinyTest, CMake Presets, Lean 4/Lake.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-io-communication-model-design.md`

## Global Constraints

- Preserve every existing `cflow_io_actor` and `cflow_io_source` public signature and callback timing in this phase.
- Do not use blocking as an implicit fallback for readiness or completion initialization failure.
- Communication structures contain stable IDs and scalar observations only; native buffer ownership remains in driver operation slots.
- All queues and pending sets are bounded and distinguish full, closed, invalid, stale, and not-found outcomes.
- Lean proofs contain no `sorry`, `admit`, or new `axiom`.
- Implementation stays inline in the current agent session; no sub-agent dispatch.
- Performance reports compare every Actor, Source, adapter, coroutine adapter,
  and runtime result with the same-run Direct result. Internal stage timing is
  reported as absolute time only.

---

### Task 1: Public communication value contract

**Files:**
- Create: `cflow/include/cflow/io_communication.h`
- Create: `cflow/src/io_communication.c`
- Create: `cflow/tests/cflow_io_communication_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_io_identity_valid`, `cflow_io_readiness_command_valid`, `cflow_io_readiness_event_valid`, `cflow_io_completion_command_valid`, `cflow_io_completion_event_valid`, `cflow_io_blocking_command_valid`, and `cflow_io_blocking_event_valid`.
- Consumes: Turbo error/status conventions only; no native OS header.

- [x] **Step 1: Write failing TinyTest cases**

  Add cases that accept one valid structure for each model and reject zero identity fields, unsupported readiness masks, non-terminal completion kinds, success events with nonzero errors, and failed events with zero errors.

- [x] **Step 2: Run the focused test and confirm RED**

  Configure with the repository Windows Debug user preset, build `cflow_io_communication_test`, and confirm failure is caused by the missing header/API.

- [x] **Step 3: Implement the minimal value contract**

  Define separate readiness/completion/blocking enums and structs. Implement validation as total switch statements with no fallback conversion between models.

- [x] **Step 4: Run focused and adjacent tests GREEN**

  Run `cflow_io_communication_test`, then `cflow_io_actor_test`, `cflow_io_source_test`, and `cflow_io_native_test`.

### Task 2: Internal completion callback driver

**Files:**
- Create: `cflow/src/io_driver_internal.h`
- Create: `cflow/src/io_driver.c`
- Create: `cflow/tests/cflow_io_driver_test.c`
- Modify: `cflow/src/io_actor.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Produces: internal `cflow_io_driver`, `cflow_io_driver_init_completion_callbacks`, `cflow_io_driver_submit`, and `cflow_io_driver_cancel`.
- Consumes: unchanged public `cflow_io_backend_ops` and `backend_user`.

- [x] **Step 1: Add a failing bridge compatibility test**

  Add an internal fake backend probe that asserts exact submit/cancel arguments,
  status propagation, optional callback cancellation, model kind separation, and
  invalid-driver reporting. Existing Actor tests retain coverage of synchronous
  completion reentry and call ordering.

- [x] **Step 2: Build `cflow_io_driver_test` and confirm RED**

  The new test must fail because the internal driver header/adapter does not yet
  exist, not because of fixture setup.

- [x] **Step 3: Add and wire the bridge**

  Store one internal driver in `cflow_io_actor_impl`; initialize it as a completion callback driver from the existing backend config; route submit/cancel through the driver while preserving execution outside the Actor mutex.

- [x] **Step 4: Run Actor/Source/native regression tests GREEN**

  Verify all existing public behavior and shutdown tests remain unchanged.

### Task 3: Lean common contract and independent driver states

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/Communication.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/ReadinessDriver.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/CompletionDriver.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/IO/BlockingDriver.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/IOCommunication.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/IOCommunication.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

**Interfaces:**
- Produces: `IO.Communication.Contract`, three concrete `State.project` functions, and refinement theorems for admission, terminal publication, observation, and close.
- Consumes: `IO.BoundedMpsc` and its preservation proofs.

- [x] **Step 1: Add a failing focused Lean test**

  Import the four missing model modules and state example theorems showing each model projects to the same empty common contract.

- [x] **Step 2: Run the focused Lean test and confirm RED**

  Run `lake env lean Test/PhaseATests/IOCommunication.lean`; expect unknown module/import failures.

- [x] **Step 3: Implement common and concrete state models**

  Define distinct readiness, completion, and blocking phases. Each state carries bounded command/event mailboxes and a bounded unique pending identity set; `project` erases only backend-specific phases.

- [x] **Step 4: Prove refinement and safety**

  Prove valid-state preservation, accepted admission adds exactly one pending identity, terminal publication removes exactly one pending identity and appends one event, duplicate/stale terminal publication leaves state unchanged, and close rejects new admission while retaining accepted work.

- [x] **Step 5: Run Lean GREEN**

  Run the focused test, `lake build`, `lake test`, and scan `formal/cmeta_cflow_calculus` for proof placeholders.

### Task 4: Integrated verification and documentation review

**Files:**
- Modify: `docs/superpowers/specs/2026-08-29-cflow-io-communication-model-design.md` only if implementation evidence requires a correction.
- Verify: all files changed by Tasks 1-3.

**Interfaces:**
- Consumes: public C contract, completion callback driver, Lean refinement results.
- Produces: one reproducible compatibility and proof report.

- [x] **Step 1: Run the smallest CFlow test set**

  Build and run the four focused CFlow I/O tests through documented user presets.

- [x] **Step 2: Run adjacent CFlow regression**

  Run the CFlow CTest label/filter selected by the repository presets. Do not run network release benchmarks as a proof of semantic correctness.

- [x] **Step 3: Verify formal and source hygiene**

  Run serial `lake build`/`lake test`, proof-placeholder scan, `git diff --check`, and inspect the final diff for public ABI changes.

- [x] **Step 4: Record measured scope**

  Record Direct-relative latency, throughput, tail-latency, CPU, and control-path
  ratios only after the concrete driver path is benchmarked with identical paired
  workloads.
