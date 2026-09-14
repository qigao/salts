# CFlow Process NativeIO Migration Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Route `cflow-process` platform-native pipe I/O through the owner-driven CFlow NativeIO adapter while preserving the explicit legacy `POLL` compatibility backend until #147 authorizes removal.

**Architecture:** `cflow_process_impl` keeps the Actor and slots as the single request/completion state owner. `IOCP`, `EPOLL`, `IO_URING`, and `KQUEUE` map explicitly to root NativeIO and are advanced by `cflow_process_run_ready`; `POLL` remains on the existing autonomous backend without implicit remapping or fallback. The three parent stdio handles are attached once, released only after Actor quiescence, and closed through the existing process endpoint owner.

**Tech Stack:** C11, CFlow Actor/Executor, CFlow NativeIO adapter, NativeIO/NativeIPC pipe endpoints, Salts process/platform APIs, TinyTest, CMake Presets.

---

### Task 1: Freeze the worker-ownership and compatibility contracts

**Files:**
- Modify: `cflow-process/tests/cflow_process_test.c`

**Step 1: Add a platform-native test configuration**

Select `IOCP` on Windows, `EPOLL` on Linux, `KQUEUE` on macOS, and retain `POLL` only on platforms without a root NativeIO mapping.

**Step 2: Add a failing owner-driven thread-count test**

On Windows and Linux, count the current process threads before and after starting a child blocked on stdin. Assert that a platform-native `cflow_process` adds only the Salts child monitor thread. The current autonomous backend must fail this assertion by adding a second I/O worker.

**Step 3: Preserve explicit POLL behavior**

Add a POSIX compatibility test that explicitly selects `CFLOW_IO_NATIVE_POLL` and completes a round trip. This protects the #147 compatibility gate while the native path changes.

**Step 4: Run the focused test and confirm RED**

Run `cflow_process_test` through the configured Windows Release tree. Expected: the new thread-ownership assertion fails before production changes; existing behavior tests and explicit POLL compatibility pass.

### Task 2: Route platform-native process I/O through the owner-driven adapter

**Files:**
- Modify: `cflow-process/src/process.c`

**Step 1: Introduce an internal backend mode and exact mapping**

Map each public native backend enum explicitly to its root NativeIO counterpart. Select the legacy mode only for explicit `POLL`; reject unsupported platform/backend combinations with the existing `SALTS_ENOTSUP` behavior.

**Step 2: Attach the three parent pipe endpoints**

Keep OS handle creation and child inheritance in the process control plane. Attach each parent endpoint to NativeIO once, store the returned generation-safe endpoint beside the raw owning handle, and unwind partial attachment failures in reverse ownership order.

**Step 3: Submit Actor operations through the selected backend**

Use a per-slot union for legacy pipe operations and root `native_io_operation` values. Keep the slot as the sole callback/result owner and expose no second queue.

**Step 4: Advance NativeIO from `cflow_process_run_ready`**

Call the adapter's nonblocking `observe` phase from the owner thread. Treat timeout as no progress, propagate other errors, and leave the existing `max_steps` accounting defined in terms of Actor/Executor transitions.

**Step 5: Preserve ordered shutdown**

Stop Actor admission, drive accepted operations to terminal delivery, close the selected backend, close the owned OS handles, release NativeIO endpoint metadata, and finally destroy backend state. No endpoint or borrowed operation may outlive its slot.

**Step 6: Run focused tests and confirm GREEN**

Build and run `cflow_process_test`; verify the owner-driven thread assertion, round trips, EOF, cancel, and resource-count cases all pass.

### Task 3: Strengthen lifecycle evidence and document the compatibility boundary

**Files:**
- Modify: `cflow-process/tests/cflow_process_test.c`
- Modify: `cflow-process/README.md`
- Modify: `cflow-process/include/cflow/process.h`

**Step 1: Exercise repeated cancellation/shutdown cycles**

Extend the resource-count test so repeated platform-native cycles include an admitted request, cancellation, drain, close, and destroy before comparing process handles/descriptors with the baseline.

**Step 2: Document backend selection and ownership**

State that platform-native selections are owner-driven by `cflow_process_run_ready`, `POLL` is a deprecated explicit compatibility path pending #147, and there is no implicit fallback between them.

**Step 3: Run adjacent verification**

Configure a fresh Release tree and build/test `cflow_process_test`, `cflow_process_header_cpp_test`, `cflow_io_native_adapter_test`, and `native_io_test`. Run the install/package consumer checks used by this repository if available.

**Step 4: Review and commit**

Inspect the diff for ABI/layout changes, run formatting if required by repository conventions, request code review, perform verification-before-completion, then commit the isolated branch.
