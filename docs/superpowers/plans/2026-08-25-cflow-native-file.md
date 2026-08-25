# CFlow Native Regular-File I/O Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded offset-based regular-file read/write and native flush where supported, using IOCP and io_uring without readiness or thread-pool fallback.

**Architecture:** A file-specific operation and Actor strategy route through additive backend callbacks. IOCP reuses its completion-port records for overlapped reads/writes; io_uring reuses its generation-token records for read/write/fsync. Readiness backends explicitly reject file work. Actor IDs, fixed request capacity, authoritative native completion, and caller-owned handle/buffer lifetimes remain the single execution protocol.

**Tech Stack:** C11, CFlow I/O Actor, Windows IOCP, Linux io_uring, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-file-design.md`

## Global constraints

- Preserve all existing socket and pipe layouts, enumerator values, aggregate initializers, and entry points.
- `request_capacity` remains the hard shared bound; no overflow allocation is permitted.
- Every accepted request produces exactly one authoritative terminal completion.
- No readiness, synchronous-flush, or `turbo_fs_*_async` fallback is allowed.
- Every transfer is offset-based and leaves a current file position unchanged.
- Caller-owned operation, handle, and buffer remain valid through callback return.
- Devices, paths, direct-I/O alignment, vectored I/O, and Windows flush remain out of scope.

---

### Task 1: Public contract and core Actor dispatch

**Files:**
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/src/io_native_internal.h`
- Modify: `cflow/src/io_native.c`
- Modify: `cflow/tests/cflow_io_native_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Add: `docs/superpowers/specs/2026-08-25-cflow-native-file-design.md`
- Add: `docs/superpowers/plans/2026-08-25-cflow-native-file.md`

**Produces:** file operation types, operation-specific capability discovery,
file Actor ops, core validation, and additive `submit_file`/`forget_file`
backend callbacks.

- [x] **Step 1: Write public contract tests and observe RED**

Add C and C++ references to every new public name. Add TinyTest cases that
accept valid `READ_AT`, `WRITE_AT`, and `FLUSH` shapes and reject unknown kinds,
unknown flags, `UINTPTR_MAX`, wrong buffer/length shapes, offsets above
`INT64_MAX`, and ranges where `length > INT64_MAX - offset`.

Run:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --build --preset win-release-user --target cflow_io_native_test cflow_header_cpp_test'
```

Expected RED: compilation fails only because the file types and entry points do
not exist.

- [x] **Step 2: Implement the smallest public/core contract**

Add the exact API from the spec. Extend the internal ops table additively:

```c
int (*submit_file)(cflow_io_native_impl *, cflow_io_actor *,
                   cflow_io_request_id,
                   cflow_io_native_file_operation *);
int (*forget_file)(cflow_io_native_impl *, uintptr_t);
```

Use an explicit operation-kind switch and checked range predicates; do not add
signed casts before proving the range. Implement file Actor submission as the
socket/pipe strategy pattern with `submit_file` as its sole native entry point.
The capability function returns false for invalid enum values and for a
compiled backend that lacks the requested operation.

- [x] **Step 3: Verify GREEN and commit**

Run the two focused tests. Then commit the public contract, core dispatch,
tests, spec, and plan as `feat(cflow): define native file I/O contract`.

---

### Task 2: Explicit readiness rejection

**Files:**
- Modify: `cflow/src/io_native_readiness.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Produces:** stable unsupported behavior for epoll, kqueue, and poll without
opening a hidden blocking path.

- [x] **Step 1: Add readiness capability/submission regression tests**

For every compiled readiness backend, assert all three file capability queries
are false. Submit a valid file operation through the file Actor strategy and
assert unsupported completion while checking that the real temporary file's
contents and current position are unchanged. Task 1's RED/GREEN core contract
already supplies this result, so this step adds backend regression evidence and
does not manufacture a second production change.

- [x] **Step 2: Keep explicit unsupported callbacks**

Keep `submit_file` absent or route it to the existing unsupported core result;
do not call `read`, `write`, `pread`, `pwrite`, `fsync`, or a worker pool from a
readiness adapter. Add `forget_file` only as the no-retained-identity behavior
required by the public contract.

- [ ] **Step 3: Verify GREEN and commit**

Run the focused native test on Windows for core behavior and through hosted
Linux/macOS jobs for epoll/kqueue/poll. Commit as
`test(cflow): define readiness file rejection`.

---

### Task 3: IOCP offset read/write

**Files:**
- Modify: `cflow/src/io_native_iocp.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Produces:** bounded overlapped regular-file reads/writes, EOF and partial
completion mapping, cancellation races, and retained file identity cleanup.

- [x] **Step 1: Write real-file IOCP tests and observe RED**

Create a TinyTest temporary path and open it with `CreateFileA` using
`GENERIC_READ | GENERIC_WRITE`, `FILE_ATTRIBUTE_TEMPORARY`, and
`FILE_FLAG_OVERLAPPED`. Add separate behaviors for:

- writes at nonadjacent offsets and reads those bytes back;
- a short read at end-of-file and EOF beyond end-of-file;
- missing `CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE` rejection;
- non-disk handle rejection;
- operation-specific `FLUSH` unsupported behavior;
- capacity exhaustion and slot reuse;
- a cancellation race that accepts either the already authoritative data
  completion or the authoritative cancelled completion, but never two;
- close-after-terminal followed by `forget_file`.

Expected RED: core accepts the shape and IOCP reports unsupported because
`submit_file` is absent.

- [x] **Step 2: Generalize IOCP records without changing socket/pipe behavior**

Add a file resource discriminator and file-operation pointer. Preserve a
single `HANDLE` fact source for association, cancellation, retained identity,
and forget. Validate operation support, async declaration, nonzero handle, and
`GetFileType(handle) == FILE_TYPE_DISK` before reserving a record.

- [x] **Step 3: Start overlapped operations with explicit offsets**

After zeroing `OVERLAPPED`, assign:

```c
record->overlapped.Offset = (DWORD)(operation->offset & UINT32_MAX);
record->overlapped.OffsetHigh = (DWORD)(operation->offset >> 32u);
```

Use `ReadFile`/`WriteFile` with `DWORD` length, treat `ERROR_IO_PENDING` as an
accepted native request, and consume completion only through IOCP. Do not call
`FlushFileBuffers`. Map read zero/`ERROR_HANDLE_EOF` to EOF, preserve partial
byte counts, and target cancellation with the record's `OVERLAPPED`.

- [x] **Step 4: Verify GREEN and socket/pipe regression, then commit**

Run the filtered file cases and the complete `cflow_io_native_test`. Commit as
`feat(cflow): add IOCP regular-file I/O`.

---

### Task 4: io_uring offset read/write/flush

**Files:**
- Modify: `cflow/src/io_native_io_uring.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Produces:** bounded native Linux file operations with explicit offsets,
full-fsync completion, resource validation, cancellation, and slot reuse.

- [ ] **Step 1: Route real-file tests through io_uring and observe RED**

Open a TinyTest temporary file with `O_RDWR | O_CLOEXEC | O_TRUNC`. Reuse the
offset/EOF/partial/capacity/cancellation checks where portable. Add Linux-only
checks that preset `lseek`, complete read/write, and prove the current position
did not change. Add a flush completion case and reject a pipe descriptor via
the file API. Skip only when the existing runtime probe cannot initialize
io_uring.

- [ ] **Step 2: Add file records, validation, and SQE preparation**

Validate the descriptor fits `int`, call checked `fstat`, and require
`S_ISREG(st_mode)` before reservation. Prepare read/write SQEs with the supplied
offset and prepare flush as:

```c
sqe->opcode = IORING_OP_FSYNC;
sqe->fd = fd;
sqe->fsync_flags = 0u;
```

Reuse the generation token, async cancel, CQE validation, fixed record pool,
and shutdown machinery. Map only zero-byte reads to EOF; flush zero is normal
`OK` with zero bytes. Clear the file pointer before record reuse.

- [ ] **Step 3: Verify GREEN and all native regressions, then commit**

Run Linux Release with io_uring available, followed by the epoll and explicit
poll native tests. Commit as `feat(cflow): add io_uring regular-file I/O`.

---

### Task 5: Documentation, review, hosted verification, and PR

**Files:**
- Modify: `cflow/README.md`
- Modify only if current routing is insufficient: `.github/workflows/ci.yml`

**Produces:** public capability/ownership documentation and reproducible
cross-platform evidence for issue #107.

- [ ] **Step 1: Update user-facing documentation**

Document the per-operation matrix, offset and partial-transfer semantics,
Windows async declaration, io_uring flush, readiness rejection, borrowed
lifetime, cancel/drain/close/forget order, and deferred device/path work.

- [ ] **Step 2: Run local Windows Release verification**

Run:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure'
```

- [ ] **Step 3: Inspect structure and diff**

Run `git diff --check`, `codegraph sync .`, and `codegraph affected` for all
changed native files. Confirm `.codegraph/` is untracked and no TODO/FIXME,
placeholder implementation, fallback, accidental ABI change, or unrelated
worktree content enters the diff.

- [ ] **Step 4: Request review and address evidence-backed findings**

Review ownership, exactly-once completion, offset arithmetic, IOCP synchronous
success semantics, cancellation races, handle/fd reuse, shutdown, and all
socket/pipe regressions. Every HIGH or MED finding receives a reproducing test
before its fix.

- [ ] **Step 5: Push and open a PR**

Push `feat/cflow-native-file` and create a PR against `master` with
`Closes #107`. Keep the PR unmerged until Windows IOCP, Linux
epoll/poll/io_uring, macOS kqueue/poll, C/C++ public-header, CMeta, and Android
required checks are green.

- [ ] **Step 6: Merge and update the parent tracker**

After required checks and review pass, merge using the repository's established
PR method. Fast-forward local `master`, rerun the focused post-merge tests, and
comment the verified backend matrix on #100.
