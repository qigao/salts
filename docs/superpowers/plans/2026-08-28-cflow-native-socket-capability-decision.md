# CFlow Native Socket Capability Decision Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve issue #132 with an evidence-backed decision for vectored TCP I/O, UDP ancillary data, and UDP batching without weakening CFlow's bounded one-request/one-completion contract.

**Architecture:** Keep the installed single-buffer socket operation ABI unchanged. Publish why each candidate is deferred or rejected, the ownership protocol any future proposal must satisfy, backend feasibility differences, and objective reopen gates tied to consumers and profiling.

**Tech Stack:** C11 public ABI, CFlow I/O Actor, IOCP, io_uring, epoll, kqueue, poll, Markdown architecture records, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-28-cflow-native-socket-capability-decision.md`

## Global Constraints

- Preserve one authoritative terminal Actor completion for every accepted request.
- Preserve the installed `cflow_io_native_operation` layout and current completion schema.
- Keep all admission and retained storage bounded with checked arithmetic.
- Do not add blocking workers, backend fallback, DNS, TLS, multicast policy, or socket configuration.
- Distinguish platform feasibility from evidence that a capability belongs in the portable API.

---

### Task 1: Establish repository and platform evidence

**Files:**
- Inspect: `cflow/include/cflow/io_native.h`
- Inspect: `cflow/include/cflow/io_actor.h`
- Inspect: `cflow/src/io_native_iocp.c`
- Inspect: `cflow/src/io_native_io_uring.c`
- Inspect: `cflow/src/io_native_readiness.c`
- Inspect: `cflow/tests/cflow_io_native_test.c`
- Inspect: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: current operation layout, request records, completion schema, buffer borrow lifetime, and backend primitives.
- Produces: a traceable constraint set for the design decision.

- [x] **Step 1: Refresh structural context and inspect the public contract**

  Confirm the current public operation has one payload buffer, one native address, and one accepted-socket result, while completion exposes only kind, byte count, and error.

- [x] **Step 2: Inspect all backend request records and data primitives**

  Verify that IOCP, io_uring, and readiness records retain a borrowed operation pointer until native completion and currently issue one-buffer operations.

- [x] **Step 3: Inspect callers and parity tests**

  Record positional aggregate initialization, address ownership, cancellation, partial byte counts, and cross-backend parity expectations.

- [x] **Step 4: Gather primary platform documentation**

  Compare Windows scatter/gather and message extensions, POSIX message/vector semantics, Linux multi-message calls, Darwin multi-message calls, and io_uring queue semantics.

### Task 2: Publish the capability decision

**Files:**
- Create: `docs/superpowers/specs/2026-08-28-cflow-native-socket-capability-decision.md`
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: evidence from Task 1 and issue #132 acceptance criteria.
- Produces: selected model, explicit candidate dispositions, ownership/lifetime protocol, backend matrix, compatibility analysis, and reopen triggers.

- [x] **Step 1: Record the no-ABI-expansion decision**

  Defer vectored TCP, reject raw ancillary buffers from the portable API, and reject UDP batching as a portable operation semantic.

- [x] **Step 2: Define future admission requirements**

  Specify bounded descriptor/message counts, checked total lengths, borrowed lifetimes, partial transfer cursors, normalized ancillary validity/truncation, per-message completion, and cancellation behavior.

- [x] **Step 3: Document backend feasibility without fallback**

  Cover IOCP, io_uring, epoll, kqueue, and poll separately, including provider/runtime capability discovery where required.

- [x] **Step 4: Add a user-facing README pointer**

  State the current single-buffer boundary and link to the decision record so users do not infer unsupported advanced message semantics.

### Task 3: Verify and hand off

**Files:**
- Verify: `docs/superpowers/specs/2026-08-28-cflow-native-socket-capability-decision.md`
- Verify: `docs/superpowers/plans/2026-08-28-cflow-native-socket-capability-decision.md`
- Verify: `cflow/README.md`

**Interfaces:**
- Consumes: completed documentation changes.
- Produces: reproducible validation evidence and a scoped diff suitable for issue #132 review.

- [x] **Step 1: Check links, referenced paths, and acceptance-criteria coverage**

  Verify every candidate has a disposition, ownership rules, all five backend entries, a no-fallback statement, and an objective reopen gate.

- [x] **Step 2: Run focused and adjacent CFlow regression tests**

  Build `cflow_io_native_test` and `cflow_header_cpp_test`, then run both through the Release CTest preset.

- [x] **Step 3: Review the final diff and repository state**

  Confirm that only the decision spec, execution plan, and README pointer changed and that no local index or build artifact is staged.
