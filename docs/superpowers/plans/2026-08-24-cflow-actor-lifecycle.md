# CFlow Bounded Actor Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded, reference-safe Actor lifecycle over one existing CFlow Machine instance, Mailbox, identity Graph, and Run, with exact admission/stop/failure semantics and Lean refinement evidence.

**Architecture:** A heap control block owns the Machine instance, lifecycle state, identity Graph, Run, error copy, and counters. Retained producer refs share only this control block; an Actor gate linearizes send against stop/destruction, while the existing SerialExecutor and Scheduler perform all execution. No new runtime, thread, pump, supervision, hierarchy, remoting, persistence, or restart layer is introduced.

**Tech Stack:** C11, TurboUtils platform synchronization, CMeta typed descriptors/interfaces, CFlow Mailbox/Machine/Run/Scheduler, TinyTest, CMake Presets, Lean 4/Lake.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-actor-lifecycle-design.md`

## Global Constraints

- Preserve the exact public API, state transitions, send-result mapping, ownership, callback, stop, and stale-ref contracts in the design spec.
- `cflow_actor_ref_try_send` must be bounded, non-blocking, and allocation-free; it must never retry, overwrite, resize, silently drop, or mutate Machine state directly.
- The Actor gate is the admission fact source. Lock order is Actor gate before Machine/Mailbox, and no user callback may run under the Actor gate.
- Machine state/statistics remain the transition fact source; Actor state only owns Actor admission/lifecycle status.
- Stop changes admission state before Machine close, rejects all later sends, cancels queued Events, and permits at most one in-flight transition commit.
- Destroy marks refs stale before synchronously closing runtime resources. The control block survives until every retained ref is released.
- Require a scheduler with `CMETA_SCHED_CAP_CONCURRENT`; do not pump a manual scheduler inside Actor APIs.
- Keep supervision, restart, parent/child hierarchy, remoting, persistence, mailbox resize, and fallback paths unavailable.
- Use TDD for every behavioral task: capture focused RED output before implementation, then focused GREEN output. Do not weaken a failing assertion to obtain GREEN.
- Use only `rg.exe`/`fd.exe` for repository search. Preserve unrelated changes and never commit `.codegraph/`, build trees, or subagent reports.
- Lean commands that share `.lake/build` must run sequentially. New proofs contain no `sorry`, `admit`, or axioms.

---

## Task 1: Add the Actor public boundary and core lifecycle

**Files:**

- Create: `cflow/include/cflow/actor.h`
- Create: `cflow/src/actor.c`
- Create: `cflow/tests/cflow_actor_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

- [ ] Read the Actor design spec plus `event.h`, `machine_runtime.h`, `runtime.h`, and the corresponding source/test patterns before editing. Confirm scheduler capability access and identity-Graph construction from existing APIs.
- [ ] Add a focused TinyTest target with compile-time/public-boundary coverage and failing runtime cases for: invalid configuration, rejection of a manual scheduler, `START`/`RUNNING`/`STOPPING`/`STOPPED` observations, exact not-started/type/full/stopping/stopped sends, one successful transition, repeated start/stop, and normal wait completion. Build/run it and record the expected RED failure caused by missing Actor API/implementation.
- [ ] Define the exact enums, structs, function signatures, ownership/lifetime documentation, callback restrictions, and C linkage from the design spec in `actor.h`. Include it from the aggregate header and add a zero-handle C++ compile/use check.
- [ ] Implement the Actor control block with one root reference, lifecycle mutex/condition, exact scheduler validation, Machine initialization, identity Graph ownership, Run/source startup, `SIZE_MAX` request, sink callback bridge, exact status mapping, stats/error queries, wait, idempotent stop, synchronous destroy, and final ref-counted shell reclamation.
- [ ] Keep callback invocation outside the Actor gate. Ensure start failures become owned `FAILED` state, and cleanup handles every partially initialized resource exactly once.
- [ ] Register the source/test in CMake, then build and run `cflow_actor_test` and `cflow_header_cpp_test` in Release. Record GREEN output and run the adjacent `cflow_event_mailbox_test`, `cflow_machine_runtime_test`, and `cflow_runtime_test` once before commit.
- [ ] Self-review the public API against every table/ownership statement in the design spec, inspect the full diff, then commit with a focused message.

## Task 2: Close concurrency, reentrancy, failure, stale-ref, and replay gaps

**Files:**

- Modify: `cflow/tests/cflow_actor_test.c`
- Modify: `cflow/src/actor.c`
- Modify: `cflow/include/cflow/actor.h` only if documentation needs clarification without changing the approved signatures or semantics

- [ ] Add failing TinyTest cases before fixes for bounded saturation under a blocked transition; multiple concurrent senders with unique event IDs/payload sequence accounting; self-send from a Machine action; `request_stop` from an Actor sink callback; stop with queued Events and exactly one in-flight commit; unhandled Event; guard, action, and sink failures; first-error stability; stale refs after owner destruction; retain/release behavior; and deterministic replay across two Actors.
- [ ] Add a finite stress loop that races distinct producer refs against stop, classifies every result, validates `accepted == completed + cancelled_events` after termination, and has explicit time/iteration bounds. Record the focused RED failure before implementation changes.
- [ ] Fix only behavior exposed by the new tests. Preserve the Actor-gate/Machine lock order, never invoke callbacks under locks, never allocate on send, and never add a fallback or second scheduling loop.
- [ ] Verify all exact status counters, first-error ownership, terminal-state stability, self-send ordering, ref shell lifetime, and cleanup on both `STOPPED` and `FAILED` paths.
- [ ] Build/run `cflow_actor_test` in Release and record GREEN output. Run it repeatedly with the test framework's supported repetition/filter mechanism or an explicit bounded PowerShell loop, then run adjacent CFlow runtime tests once before commit.
- [ ] Self-review concurrency assumptions and every shared field, then commit the tests/fixes with a focused message.

## Task 3: Formalize Actor lifecycle and Machine handoff refinement

**Files:**

- Create: `formal/CMetaCFlowCalculus/CMetaCFlowCalculus/CFlow/Actor.lean`
- Create: `formal/CMetaCFlowCalculus/CMetaCFlowCalculus/Proofs/Actor.lean`
- Create: `formal/CMetaCFlowCalculus/Test/PhaseATests/Actor.lean`
- Modify: `formal/CMetaCFlowCalculus/CMetaCFlowCalculus.lean`
- Modify: `formal/CMetaCFlowCalculus/Test/PhaseATests.lean`

- [ ] Read the existing Mailbox, MachineRuntime, and TimerEvent models/proofs/tests, then add a failing Actor test import/example and run `lake test` to record RED output from the missing model.
- [ ] Model the five Actor states and exact send outcomes. Delegate running admission to the existing bounded Mailbox model; model stop/failure cancellation and terminal-state stability without introducing hierarchy or supervision state.
- [ ] Define Actor validity and prove initialization/start/send/stop/settle/fail preserve it; prove sends append exactly once only when accepted; prove stopping/stopped/failed reject future sends; and prove terminal states cannot restart.
- [ ] Define a handoff relation from accepted Actor send through Mailbox receive to the existing Machine runtime step for the identical Event, and prove the trace/refinement theorem. Reuse existing facts rather than duplicating Mailbox or Machine semantics.
- [ ] Add `native_decide` examples for exact statuses, saturation, stop cancellation, failure rejection, stale-ref abstraction if modeled, and deterministic replay. Add root imports and verify no `sorry`, `admit`, or new `axiom` with `rg.exe`.
- [ ] Run `lake build` and only after it completes run `lake test`; record GREEN output, inspect the diff, and commit the formal model/proofs/tests.

## Task 4: Document and verify the integrated feature

**Files:**

- Modify: `cflow/README.md`
- Modify: `README.md` only if its CFlow capability list has a matching entry
- Modify: generated/export metadata only when the repository's checked generator requires it

- [ ] Add concise user documentation for Actor construction, borrowed/owned lifetimes, retained refs, exact backpressure results, callback restrictions, stop semantics, and explicitly unavailable capabilities. Include one complete compilable usage example with cleanup in dependency-safe order.
- [ ] Run the repository's formatting/generated-file check and apply only required mechanical output. Confirm `.codegraph/`, build trees, and subagent artifacts are absent from `git status`.
- [ ] Configure/build the relevant Release preset and run focused Actor plus adjacent Event/Machine/Runtime/Timer tests. Then run the full CFlow CTest set.
- [ ] Configure/build and run the supported ASan preset for the Actor target/tests; if the preset/platform cannot run, record the exact command/failure and residual risk instead of claiming coverage.
- [ ] Run the bounded Actor concurrency stress repeatedly in Release. Sequentially run Lean `lake build` and `lake test`, then scan for forbidden proof placeholders.
- [ ] Review the complete branch diff for public API compatibility, ownership, state/error facts, boundedness, and issue #68 exit criteria. Commit documentation or generated changes with a focused message.
