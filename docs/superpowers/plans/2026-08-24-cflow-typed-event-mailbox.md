# CFlow Typed Event and Bounded Mailbox Implementation Plan

> **For agentic workers:** Execute this plan task-by-task. Keep each production
> change behind a test that was first observed failing, and verify every task
> before committing it.

**Goal:** Implement GitHub issue #62: a CMeta-typed finite event schema, bounded
MPSC/single-consumer mailbox, precise shutdown/backpressure semantics, waitable
wake coalescing, and a Lean model of its functional invariants.

**Architecture:** Add an independent `cflow/event.h` module rather than changing
the existing homogeneous channel. The C implementation uses preallocated ring
metadata and aligned payload storage protected by one mutex. The Lean module is
a pure transition system under `CFlow.Mailbox`, separate from kernel trace
labels.

**Tech Stack:** C11, CMeta type descriptors, CFlow waitables, Salts mutexes,
TinyTest, CMake/CTest Release presets, Lean 4/Lake.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-typed-event-mailbox-design.md`

---

## Task 1: Define the public contract through failing tests

**Files:**

- Create: `cflow/tests/cflow_event_mailbox_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Create: `cflow/include/cflow/event.h`
- Modify: `cflow/include/cflow/cflow.h`
- Create: `cflow/src/event.c`
- Modify: `cflow/CMakeLists.txt`

1. Add tests for initialization with two trivial CMeta payload types, rejection
   of duplicate/zero IDs, zero capacity, and a non-trivial payload descriptor.
2. Register only the test target and run a Release build. Confirm it fails
   because `<cflow/event.h>` does not exist.
3. Add the public declarations and the minimum initialization/destruction
   implementation: checked memory sizing, copied schema rows, payload-trait
   validation, mutex initialization, and complete cleanup on every failure.
4. Reconfigure, build, and run only `cflow_event_mailbox_test`.
5. Commit: `feat(cflow): define typed event mailbox schema`.

## Task 2: Implement bounded FIFO admission and observation

**Files:**

- Modify: `cflow/tests/cflow_event_mailbox_test.c`
- Modify: `cflow/src/event.c`

1. Add failing tests proving heterogeneous FIFO receive, `EMPTY`, `FULL`,
   `TYPE_MISMATCH`, buffer preservation on `BUFFER_TOO_SMALL`, and exact stats.
2. Run the focused test and confirm the new assertions fail for missing behavior.
3. Implement `try_send` and `try_receive` with an O(1) ring commit/dequeue path
   after O(schema-size) immutable type lookup. Copy only trivial payload bytes.
4. Keep output metadata transactional: clear it on entry and publish it only
   after a successful copy/dequeue.
5. Run the focused test and commit:
   `feat(cflow): add bounded typed event admission`.

## Task 3: Implement close, cancellation, and waitable wake coalescing

**Files:**

- Modify: `cflow/tests/cflow_event_mailbox_test.c`
- Modify: `cflow/src/event.c`

1. Add failing tests for graceful close-and-drain, cancellation discard counts,
   post-terminal send/receive statuses, immediate terminal arm, and one wake for
   multiple sends before rearm.
2. Add a bounded MPSC test with several producers and one consumer. Assert each
   accepted sequence value is observed exactly once; retry only explicit `FULL`
   results and treat any other status as failure.
3. Implement terminal state transitions and the `cflow_waitable` adapter. Take
   the waker under the mutex and invoke it only after unlocking.
4. Run the focused test repeatedly and under the existing sanitizer-compatible
   configuration when available.
5. Commit: `feat(cflow): complete mailbox lifecycle and wake protocol`.

## Task 4: Add the Lean transition model and proofs

**Files:**

- Create: `formal/cmeta_cflow_calculus/CFlow/Mailbox.lean`
- Create: `formal/cmeta_cflow_calculus/Proofs/Mailbox.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/Mailbox.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

1. Add failing Lean examples importing the not-yet-existing mailbox proof module.
2. Define typed events, terminal modes, statuses, and pure send/receive/close/
   cancel transitions.
3. Prove capacity preservation, append-once admission, FIFO observation,
   close-preserves-queue/rejects-send, and cancel-empties-queue.
4. Run `lake test` from `formal/cmeta_cflow_calculus`.
5. Commit: `formal(cflow): prove bounded mailbox transitions`.

## Task 5: Integrate headers and complete Release verification

**Files:**

- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `docs/superpowers/specs/2026-08-24-cflow-typed-event-mailbox-design.md`
  only if implementation evidence requires a contract correction

1. Add C++ aggregate-header coverage for the new public types and functions.
2. Configure and build with:

   ```powershell
   cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user'
   ```

3. Run focused CTest cases, then the full Release suite with
   `ctest --preset win-release-user --output-on-failure`.
4. Run the complete Lean suite with `lake test`.
5. Inspect `git diff --check`, `git status --short`, and the branch diff against
   `origin/master`.
6. Commit any final integration-only changes, push the branch, and create a PR
   referencing #62 with the exact verification output.
