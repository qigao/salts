# CFlow Native I/O Final Validation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. This repository task is executed inline because the user explicitly disabled sub-agents.

**Goal:** Close issue #134 with runnable socket, typed-pipe, and regular-file examples plus reproducible installed-package, capability, ownership, and resource validation evidence.

**Architecture:** Keep the existing public API unchanged. Socket and typed-pipe examples assemble the public bounded `cflow_io_native_backend`, manual `cflow_executor`, and `cflow_io_actor` directly so acknowledgement, endpoint close, identity forget, Actor drain, backend shutdown, and Executor shutdown remain visible. The regular-file example uses the owning `cflow_io_file` facade and proves the same terminal state through its public statistics and quiescent destroy contract.

**Tech Stack:** ISO C11, CFlow native I/O/Actor/Executor APIs, platform socket/pipe setup APIs, CMake user presets, CTest, installed `TurboUtils::CFlow` package target.

**Spec:** https://github.com/qigao/turbo-utils/issues/134

## Global Constraints

- Preserve existing public API, error values, backend selection, and fail-fast unsupported behavior.
- Every capacity is a named nonzero hard bound; example loops have a five-second monotonic deadline.
- Accepted operation storage and buffers remain alive through completion callback return and acknowledgement.
- Socket/pipe handles are closed only after terminal delivery and acknowledgement, then forgotten before backend shutdown.
- Unsupported backend or operation combinations print the selected capability and return `77`; CTest treats `77` as skipped.
- Examples are built in-tree and the same source files are compiled against the installed package target.
- No performance claim is published without benchmark evidence.

---

### Task 1: Register executable behavior tests before implementation

**Files:**
- Modify: `cflow/CMakeLists.txt`
- Create: `cflow/examples/CMakeLists.txt`

**Interfaces:**
- Consumes: `BUILD_EXAMPLES`, `cmake_add_executable()`, `TurboUtils::CFlow`.
- Produces: `cflow_native_socket_example`, `cflow_native_pipe_example`, and `cflow_native_file_example` executable/CTest targets.

- [ ] **Step 1: Add the failing build contract**

```cmake
if(BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()
```

Register each missing source as an executable and CTest test. Set `SKIP_RETURN_CODE 77`; on Windows link `ws2_32` only to the socket example.

- [ ] **Step 2: Verify RED**

Run the documented `win-release-user` configure command through `VsDevCmd.bat`.

Expected: configure fails because `cflow_native_socket_example.c`, `cflow_native_pipe_example.c`, and `cflow_native_file_example.c` do not exist. This proves the build contract consumes the real example artifacts.

- [ ] **Step 3: Keep the failing harness for the three implementation cycles**

Do not weaken the CTest registration or replace it with source-text checks.

### Task 2: Implement the bounded native socket example

**Files:**
- Create: `cflow/examples/cflow_native_socket_example.c`

**Interfaces:**
- Consumes: `cflow_io_native_backend_init()`, `cflow_executor_manual_init_with_capacity()`, `cflow_io_actor_init()`, `cflow_io_actor_try_submit()`, `cflow_io_actor_acknowledge()`, `cflow_io_native_backend_forget_socket()`, and their close/shutdown functions.
- Produces: a loopback TCP send/receive that exits `0` only after two authoritative completions, two acknowledgements, two release callbacks, zero active requests, and complete teardown.

- [ ] **Step 1: Name the break caught by the test**

The CTest must fail if the example omits bounded admission, loses a completion, releases an operation more or less than once, closes a socket before acknowledgement, fails to forget retained socket identity, or cannot reach quiescent teardown.

- [ ] **Step 2: Implement the minimal self-contained example**

Use `CFLOW_IO_NATIVE_IOCP` on Windows and `CFLOW_IO_NATIVE_POLL` on POSIX. Create one loopback TCP pair, make both endpoints nonblocking, submit one receive and one send, drive Actor and Executor with a five-second deadline, acknowledge both request IDs, close and forget both sockets, then close/destroy Actor, shutdown/destroy backend, and shutdown/destroy Executor.

The success record is derived from literal invariants:

```c
return completion_count == 2u && release_count == 2u &&
       actor_stats.active_requests == 0u &&
       actor_stats.accepted == actor_stats.acknowledged
           ? EXIT_SUCCESS : EXIT_FAILURE;
```

- [ ] **Step 3: Verify GREEN for socket**

Build `cflow_native_socket_example` with `win-release-user`, then run:

```text
ctest --preset win-release-user -R ^cflow_native_socket_example$ --output-on-failure
```

Expected: one real loopback transfer passes.

### Task 3: Implement the typed byte-pipe example

**Files:**
- Create: `cflow/examples/cflow_native_pipe_example.c`

**Interfaces:**
- Consumes: the same Actor/Executor lifecycle with `cflow_io_native_backend_pipe_actor_ops()` and `cflow_io_native_pipe_operation`.
- Produces: one typed write and one typed read over an overlapped Windows named-pipe pair or nonblocking POSIX pipe pair, followed by acknowledgement, close, forget, and teardown.

- [ ] **Step 1: Name the break caught by the test**

The CTest must fail if `CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE` is omitted, bytes differ from the literal `"typed-pipe"`, release is not exactly once, either endpoint remains live, or identity forget/shutdown cannot complete.

- [ ] **Step 2: Implement platform setup and public data path**

Windows creates a byte-mode `FILE_FLAG_OVERLAPPED` named-pipe pair. POSIX creates a nonblocking close-on-exec pipe pair. Submit the write and read as separate Actor requests with fixed capacity `2`, drive both completions, compare the literal payload, acknowledge both requests, close both endpoints, forget both identities, and tear down in dependency order.

- [ ] **Step 3: Verify GREEN for typed pipe**

Build and run `^cflow_native_pipe_example$`; expect one passing CTest.

### Task 4: Implement the owning regular-file facade example

**Files:**
- Create: `cflow/examples/cflow_native_file_example.c`

**Interfaces:**
- Consumes: `cflow_io_file_open()`, typed read/write submission, `cflow_io_file_run_ready()`, public stats, close/quiescent/destroy.
- Produces: an explicit-offset write/read round trip and a deleted temporary file after destroy.

- [ ] **Step 1: Name the break caught by the test**

The CTest must fail if the facade selects an implicit backend, fails to preserve explicit offsets, reports a wrong byte count, leaves an operation slot active, fails to auto-acknowledge, or cannot destroy after close/drain.

- [ ] **Step 2: Implement the facade lifecycle**

Use IOCP on Windows and io_uring on Linux. macOS prints that regular-file native completion is unsupported and returns `77`. Create a unique temporary path without pre-creating the file, open read/write/create/truncate, write the literal payload at offset `7`, read it back from offset `7`, and require:

```c
stats.operation_slots_in_use == 0u &&
stats.actor.accepted == 2u &&
stats.actor.acknowledged == 2u
```

Close, drive to quiescence, destroy, and remove the path in one cleanup path.

- [ ] **Step 3: Verify GREEN for regular file and all examples**

Build all three targets and run `ctest --preset win-release-user -R "^cflow_native_.*_example$" --output-on-failure`.

### Task 5: Compile the real examples through the installed package

**Files:**
- Modify: `tests/install_consumer/CMakeLists.txt`

**Interfaces:**
- Consumes: installed `TurboUtils::CFlow` and the three repository example source files.
- Produces: three installed-package consumer executables; no source-only surrogate.

- [ ] **Step 1: Add the installed-surface build test**

Add executables whose source paths are `${CMAKE_CURRENT_LIST_DIR}/../../cflow/examples/<name>.c`, link each to `TurboUtils::CFlow`, and link the socket example to `ws2_32` on Windows.

- [ ] **Step 2: Verify the test can detect package breakage**

Before rebuilding the install smoke target, run it once with the new consumer declarations and expect failure if any required public header, exported dependency, or symbol is absent.

- [ ] **Step 3: Verify GREEN**

Run `cmake --build --preset win-release-user --target verify_installed_package`; expect install, consumer configure, and all example-source links to succeed.

### Task 6: Publish the final capability and resource evidence

**Files:**
- Create: `cflow/examples/README.md`
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: the public capability queries, example target names, platform CTests, existing native-I/O and subprocess resource tests.
- Produces: one user-facing entry point and one auditable matrix tied to executable evidence.

- [ ] **Step 1: Document exact execution commands and observable output**

Document configure/build/CTest commands for Windows, Linux, and macOS and explain return `77` as explicit unsupported evidence rather than fallback.

- [ ] **Step 2: Publish the matrix**

Record per backend: socket, typed pipe, regular file, rendezvous, required endpoint flags, example exercised, authoritative regression test, and hosted workflow job. Mark unsupported cells explicitly.

- [ ] **Step 3: Publish resource-terminal evidence**

Map success, failure, cancellation, capacity, shutdown, endpoint identity, subprocess handle/descriptor, and retained-memory claims to executable tests or public stats. State that no throughput claim is made by this work.

- [ ] **Step 4: Link from the main CFlow README**

Add a concise link next to the existing Native I/O section; do not duplicate the matrix.

### Task 7: Cross-platform verification and delivery

**Files:**
- Verify all modified files.

**Interfaces:**
- Consumes: repository presets, remote Linux host `root@eu`, hosted CI after push.
- Produces: reproducible local/remote evidence and a reviewable commit.

- [ ] **Step 1: Run focused Windows Release verification**

Build the three example targets, run their CTests, run adjacent `cflow_io_native_test`, `cflow_io_pipe_test`, `cflow_io_file_test`, and `cflow_process_test`, then run `verify_installed_package`.

- [ ] **Step 2: Run the full Windows Release suite**

Run `ctest --preset win-release-user --output-on-failure`; expect zero failures.

- [ ] **Step 3: Run remote Linux verification**

On `root@eu`, use repository Linux user presets to build the example targets, run example and adjacent CTests, and report whether io_uring initializes or the regular-file example exits with explicit unsupported status `77`.

- [ ] **Step 4: Check source hygiene and commit**

Run `git diff --check`, inspect `git diff --stat` and status, then commit only #134 files with message `docs(cflow): add native I/O examples and validation matrix`.

- [ ] **Step 5: Push for hosted macOS/Linux/Windows evidence**

Push `docs/cflow-native-io-final-validation`; do not update #134 acceptance checkboxes until the corresponding hosted checks actually pass.
