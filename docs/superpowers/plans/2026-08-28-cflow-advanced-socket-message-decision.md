# CFlow Advanced Socket Message Decision Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close #132 with an evidence-backed portable operation decision and exact reopen gates, without changing the current public ABI or runtime.

**Architecture:** Preserve the bounded Actor/native-backend completion protocol. Accept vectored TCP only as a later independent additive API; reject raw ancillary data and UDP batching from the current portable operation model until their ownership and state-machine gates are satisfied.

**Tech Stack:** CFlow C11 public contracts, Windows Winsock/IOCP, POSIX sockets, Linux io_uring, Markdown.

**Spec:** `docs/superpowers/specs/2026-08-28-cflow-advanced-socket-message-design.md`

## Global Constraints

- Do not modify public C declarations, enum values, structure layouts, backend selection, runtime behavior, error codes, or dependencies in this decision PR.
- Keep Actor request admission and exactly-once authoritative terminal completion as the single state-machine fact source.
- Do not introduce raw ancillary buffers, platform-specific socket types, implicit fallback, unbounded batching, or blocking workers.
- Every external platform claim must link to Microsoft, POSIX, Linux/kernel/liburing, or Apple primary documentation.
- Future public API work requires a separate implementation PR with TDD and the cross-platform matrix in the spec.

---

### Task 1: Publish the advanced socket decision

**Files:**
- Create: `docs/superpowers/specs/2026-08-28-cflow-advanced-socket-message-design.md`

**Interfaces:**
- Consumes: current `cflow_io_native_operation`, Actor completion protocol, readiness/IOCP/io_uring backend contracts.
- Produces: accepted vectored-TCP contract, rejected ancillary/batch boundaries, and reproducible reopen gates.

- [x] **Step 1: Record repository and platform evidence**

  Separate current repository facts, primary platform facts, and design inference. Link each external interface family to its primary documentation.

- [x] **Step 2: Select the portable model**

  Accept one-request vectored TCP for a later independent API. Reject raw ancillary data and UDP batch operations from the portable core. Define packet-info, ECN, timestamp and batching reopen gates independently.

- [x] **Step 3: Specify ownership and state transitions**

  Define descriptor copying, borrowed payload lifetime, fixed vector count, checked total length, partial completion, EOF, cancel, socket identity and shutdown behavior.

### Task 2: Synchronize consumer documentation

**Files:**
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: Task 1 decisions.
- Produces: a concise user-facing boundary next to the existing native socket capability matrix.

- [x] **Step 1: State current non-capabilities explicitly**

  Record that current socket operations remain scalar and do not expose ancillary metadata or batch semantics.

- [x] **Step 2: Link the full decision**

  Link the design spec for accepted vector ownership and ancillary/batching reopen criteria.

### Task 3: Verify and deliver the decision

**Files:**
- Verify: `docs/superpowers/specs/2026-08-28-cflow-advanced-socket-message-design.md`
- Verify: `docs/superpowers/plans/2026-08-28-cflow-advanced-socket-message-decision.md`
- Verify: `cflow/README.md`

**Interfaces:**
- Consumes: Tasks 1-2.
- Produces: a reviewable docs-only PR and #132 completion evidence.

- [x] **Step 1: Validate the baseline**

  Configure with `win-release-user`, build `cflow_io_native_test`, and run `ctest --preset win-release-user -R "^cflow_io_native_test$" --output-on-failure` from a VS developer environment.

- [x] **Step 2: Validate the documentation diff**

  Run `git diff --check`; inspect every changed file; confirm no public header, production source, test, workflow, dependency, or runtime file changed.

- [x] **Step 3: Update #132**

  After PR creation, check the design/capability/no-fallback criteria that this decision satisfies. Leave implementation-specific test and benchmark evidence to the vectored-TCP follow-up.

  Decision delivery: #138. Vectored-TCP implementation follow-up: #139.
