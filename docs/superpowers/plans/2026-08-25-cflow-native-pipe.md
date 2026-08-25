# CFlow Native Pipe Read/Write Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add typed, bounded native pipe read/write operations for IOCP, epoll, kqueue, poll, and io_uring while preserving the existing socket API and Actor ownership contract.

**Architecture:** A new pipe-specific public operation and Actor strategy route through additive backend callbacks. Each backend reuses its existing request records, cancellation, completion, capacity, and shutdown machinery while selecting pipe-native read/write primitives and resource validation. Internal descriptor/handle identity storage becomes resource-neutral; existing socket entry points remain compatible wrappers.

**Tech Stack:** C11, CFlow I/O Actor, TurboUtils Platform readiness reactor, Windows IOCP, POSIX `read`/`write`, Linux io_uring, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-pipe-design.md`

## Global Constraints

- Preserve all existing socket operation enumerators, structure layout, aggregate initializers, and entry points.
- Fixed `request_capacity` remains the hard bound for socket and pipe requests and retained identities.
- Every accepted request produces exactly one authoritative terminal completion.
- No backend fallback, blocking-worker fallback, implicit endpoint close, or partial public implementation is allowed.
- Pipe operation, endpoint, and buffer remain borrowed through terminal callback return.
- Windows requires an explicit `CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE` declaration; POSIX readiness also verifies `O_NONBLOCK`.
- Named-pipe connect lifecycle, FIFO open lifecycle, files, devices, USB, and subprocess ownership remain out of scope.

---

### Task 1: Public pipe contract and core dispatch

**Files:**
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/src/io_native_internal.h`
- Modify: `cflow/src/io_native.c`
- Modify: `cflow/tests/cflow_io_native_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `cflow_io_backend_ops`, native backend configuration, and socket Actor strategy.
- Produces: `cflow_io_native_pipe_operation`, `cflow_io_native_backend_pipe_supported`, `cflow_io_native_backend_pipe_actor_ops`, and `cflow_io_native_backend_forget_pipe`.

- [x] **Step 1: Write failing public contract tests**

Add a TinyTest case that constructs valid and invalid pipe operations and calls
the internal validator. The production change that makes this test fail is a
missing kind/flag/length/handle check.

```c
it("validates the bounded native pipe operation contract") {
    unsigned char byte = 0u;
    check_true(cflow_io_native_pipe_operation_valid(
        &(cflow_io_native_pipe_operation){
            CFLOW_IO_NATIVE_PIPE_READ, 1u, &byte, 1u,
            CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE}));
    check_false(cflow_io_native_pipe_operation_valid(
        &(cflow_io_native_pipe_operation){
            CFLOW_IO_NATIVE_PIPE_READ, UINTPTR_MAX, &byte, 1u,
            CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE}));
    check_false(cflow_io_native_pipe_operation_valid(
        &(cflow_io_native_pipe_operation){
            CFLOW_IO_NATIVE_PIPE_WRITE, 1u, NULL, 1u,
            CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE}));
    check_false(cflow_io_native_pipe_operation_valid(
        &(cflow_io_native_pipe_operation){
            CFLOW_IO_NATIVE_PIPE_WRITE, 1u, &byte, 0u,
            CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE}));
}
```

Reference all new public names in the C11 native test and the existing C++
public-header test while leaving the existing positional socket initializers
unchanged.

- [x] **Step 2: Run the focused target and verify RED**

Run:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_io_native_test cflow_header_cpp_test'
```

Expected: compilation fails because the new pipe types and functions do not
exist; existing socket declarations still compile independently.

- [x] **Step 3: Add the minimal public declarations and core validation**

Add the exact API from the spec. Extend the internal ops table without changing
the existing socket callback signature:

```c
typedef struct cflow_io_native_impl_ops {
    int (*submit)(cflow_io_native_impl *, cflow_io_actor *,
                  cflow_io_request_id, cflow_io_native_operation *);
    int (*submit_pipe)(cflow_io_native_impl *, cflow_io_actor *,
                       cflow_io_request_id,
                       cflow_io_native_pipe_operation *);
    int (*cancel)(cflow_io_native_impl *, cflow_io_request_id);
    bool (*get_stats)(const cflow_io_native_impl *,
                      cflow_io_native_backend_stats *);
    int (*forget_socket)(cflow_io_native_impl *, uintptr_t);
    int (*forget_pipe)(cflow_io_native_impl *, uintptr_t);
    int (*shutdown)(cflow_io_native_impl *);
    int (*destroy)(cflow_io_native_impl *);
} cflow_io_native_impl_ops;
```

Implement pipe validation with an explicit switch, exact known-flags mask,
`length > 0`, `length <= UINT32_MAX`, non-null buffer, and identity other than
`UINTPTR_MAX`. Add a pipe Actor submit callback mirroring socket submit but
calling `submit_pipe`. Capability discovery returns true only for compiled
backends whose implementation supplies pipe operations.

- [x] **Step 4: Run the focused tests and verify GREEN**

Run the three targets and then:

```powershell
ctest --preset win-release-user -R "^cflow_(io_native|header_cpp)_test$" --output-on-failure
```

Expected: the contract and header tests pass. Backend pipe submission is not
invoked yet.

- [x] **Step 5: Commit**

```powershell
git add cflow/include/cflow/io_native.h cflow/src/io_native_internal.h cflow/src/io_native.c cflow/tests/cflow_io_native_test.c cflow/tests/cflow_header_cpp_test.cpp docs/superpowers/specs/2026-08-25-cflow-native-pipe-design.md docs/superpowers/plans/2026-08-25-cflow-native-pipe.md
git commit -m "feat(cflow): define native pipe I/O contract"
```

### Task 2: IOCP named-pipe data path

**Files:**
- Modify: `cflow/src/io_native_iocp.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: Task 1 pipe operation, pipe Actor strategy, and internal `submit_pipe`/`forget_pipe` callbacks.
- Produces: bounded overlapped byte-pipe read/write, cancellation, EOF mapping, and resource identity cleanup on IOCP.

- [x] **Step 1: Write failing real named-pipe tests**

Add a Windows-only helper that creates a unique byte-mode named pipe server
with `PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED`, opens its client with
`CreateFileW(..., FILE_FLAG_OVERLAPPED, ...)`, and accepts the already-connected
`ERROR_PIPE_CONNECTED` state. The helper returns two caller-owned `HANDLE`s.

Add separate `it` coverage for:

```c
it("runs byte pipe read and write through IOCP") {
    native_check_pipe_read_write(CFLOW_IO_NATIVE_IOCP);
}

it("cancels a pending byte pipe read through IOCP") {
    native_check_pipe_cancel(CFLOW_IO_NATIVE_IOCP);
}

it("reports named pipe peer close as EOF through IOCP") {
    native_check_pipe_eof(CFLOW_IO_NATIVE_IOCP);
}

it("rejects a pipe handle without the async capability declaration") {
    native_check_pipe_missing_async_flag(CFLOW_IO_NATIVE_IOCP,
                                         TURBO_ENOTSUP);
}
```

Each helper submits through `cflow_io_native_backend_pipe_actor_ops`, waits for
the real Actor completion, acknowledges the request, closes both handles, calls
`forget_pipe` for retained identities, and destroys the fixture.

- [x] **Step 2: Build and verify RED**

Run the native-I/O target and executable filtered to `pipe`.

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_io_native_test && build\Msvc-Release\bin\cflow_io_native_test.exe --filter pipe'
```

Expected: the pipe Actor accepts the core shape but IOCP returns
`TURBO_ENOTSUP` because `submit_pipe` is not implemented.

- [x] **Step 3: Generalize IOCP records and implement pipe primitives**

Give each IOCP record a resource discriminator, a `HANDLE native_handle`, and a
union of socket/pipe operation pointers. Generalize the retained socket table
to an internal resource table keyed by `HANDLE`; preserve socket casting only
at Winsock call sites.

Implement pipe begin logic separately so Win32 errors are never read with
`WSAGetLastError`:

```c
static int iocp_begin_pipe_operation(cflow_iocp_record *record) {
    cflow_io_native_pipe_operation *operation = record->pipe_operation;
    BOOL started = operation->kind == CFLOW_IO_NATIVE_PIPE_READ
        ? ReadFile(record->native_handle, operation->buffer,
                   (DWORD)operation->length, NULL, &record->overlapped)
        : WriteFile(record->native_handle, operation->buffer,
                    (DWORD)operation->length, NULL, &record->overlapped);
    if (started)
        return TURBO_OK;
    return GetLastError() == ERROR_IO_PENDING
        ? TURBO_OK : iocp_error(GetLastError());
}
```

Before reserving a record, reject handle zero, missing async flag, non-pipe
`GetFileType`, and `PIPE_TYPE_MESSAGE` reported by `GetNamedPipeInfo`.
Associate the handle with the existing completion port, use `CancelIoEx` with
the record's `native_handle`, and map `ERROR_BROKEN_PIPE`/`ERROR_HANDLE_EOF` on
a pipe read to Actor EOF. Pipe writes keep the native failure.

- [x] **Step 4: Verify GREEN and socket regression**

Run the filtered pipe executable, then the complete `cflow_io_native_test`.
Expected: all pipe tests and the existing TCP/UDP/accept/connect cases pass.

- [x] **Step 5: Commit**

```powershell
git add cflow/src/io_native_iocp.c cflow/tests/cflow_io_native_test.c
git commit -m "feat(cflow): run named pipe I/O through IOCP"
```

### Task 3: POSIX readiness pipe data path

**Files:**
- Modify: `cflow/src/io_native_readiness.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: Task 1 pipe contract and existing two-lane readiness state machine.
- Produces: nonblocking pipe/FIFO read/write for epoll, kqueue, and poll with contained `SIGPIPE`.

- [x] **Step 1: Write failing POSIX pipe behavior tests**

Create a POSIX helper using `pipe2(O_NONBLOCK | O_CLOEXEC)` on Linux and
`pipe` plus checked `fcntl` on other POSIX hosts. Add shared checks for read,
write, EOF, pending-read cancellation, capacity reuse, same-lane FIFO ordering,
blocking-descriptor rejection, forget/reuse, and a closed-reader write that
returns FAILED while the test process remains alive.

Run the shared checks for epoll/kqueue/poll under their existing platform
guards. A mutation replacing guarded write with raw process-visible `write`
must terminate or fail the broken-reader test.

- [x] **Step 2: Build on Linux/macOS CI or a native host and verify RED**

Run:

```sh
cmake --fresh --preset linux-release-user
cmake --build --preset linux-release-user --target cflow_io_native_test
ctest --preset linux-release-user -R '^cflow_io_native_test$' --output-on-failure
```

Expected: pipe cases fail with unsupported submit before readiness pipe logic
exists. On macOS use the repository's macOS CI configure/build commands and
the same CTest target.

- [x] **Step 3: Implement resource-neutral readiness lanes**

Add a resource discriminator and pipe pointer to each request record. Replace
socket-only identity helpers with internal resource identity helpers while
leaving public socket names unchanged. A pipe read selects the read lane and a
pipe write selects the write lane.

Validate the descriptor with `F_GETFL`; return `TURBO_EINVAL` unless
`O_NONBLOCK` is set. Use `read` for pipe reads. For writes, add a local helper
that blocks `SIGPIPE` on the current thread, records prior pending state,
performs `write`, consumes only the newly generated signal after `EPIPE`, and
restores the original mask on every exit. Return `-errno` to the existing
completion mapping.

Extend EOF detection to:

```c
if (((record->resource_kind == CFLOW_NATIVE_RESOURCE_SOCKET &&
      record->operation->kind == CFLOW_IO_NATIVE_TCP_RECV) ||
     (record->resource_kind == CFLOW_NATIVE_RESOURCE_PIPE &&
      record->pipe_operation->kind == CFLOW_IO_NATIVE_PIPE_READ)) &&
    bytes == 0u)
    return (cflow_io_completion){CFLOW_IO_COMPLETION_EOF, 0u, TURBO_OK};
```

- [x] **Step 4: Verify GREEN on each readiness backend**

Run the native test for epoll and poll on Linux and kqueue and poll on macOS.
Then run the existing socket contract on the same backends. Expected: no
hang, no process signal termination, and all cases pass.

- [x] **Step 5: Commit**

```sh
git add cflow/src/io_native_readiness.c cflow/tests/cflow_io_native_test.c
git commit -m "feat(cflow): add POSIX readiness pipe I/O"
```

### Task 4: Linux io_uring pipe data path

**Files:**
- Modify: `cflow/src/io_native_io_uring.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

**Interfaces:**
- Consumes: Task 1 pipe contract and existing generation-token io_uring records.
- Produces: native io_uring pipe read/write, cancellation, EOF, and quiescent forget behavior.

- [x] **Step 1: Route the shared pipe tests through io_uring and verify RED**

When io_uring initialization succeeds, call the same read/write, EOF,
cancellation, capacity, and slot-reuse checks used by readiness. The test skips
only when the existing runtime probe cannot initialize io_uring.

Expected RED: accepted Actor submission fails because the io_uring
`submit_pipe` callback is absent.

- [x] **Step 2: Add pipe records and SQE preparation**

Store a resource discriminator and pipe operation pointer in each record. Add
pipe preparation using current-position semantics for non-seekable endpoints:

```c
sqe->fd = (int)operation->handle;
sqe->opcode = operation->kind == CFLOW_IO_NATIVE_PIPE_READ
                  ? IORING_OP_READ : IORING_OP_WRITE;
sqe->addr = (uint64_t)(uintptr_t)operation->buffer;
sqe->len = (uint32_t)operation->length;
sqe->off = UINT64_MAX;
```

Use the existing generation token and async-cancel SQE. Extend completion EOF
detection to a zero-result pipe read, clear the correct union member before
slot reuse, and implement `forget_pipe` with the existing global-quiescence
contract.

- [x] **Step 3: Verify GREEN**

Run the Linux Release native-I/O test with io_uring available, then run it once
with the epoll test definition and once for explicit poll. Expected: all socket
and pipe cases pass without changing the runtime-probe behavior.

- [x] **Step 4: Commit**

```sh
git add cflow/src/io_native_io_uring.c cflow/tests/cflow_io_native_test.c
git commit -m "feat(cflow): add io_uring pipe I/O"
```

### Task 5: Documentation and cross-platform verification

**Files:**
- Modify: `cflow/README.md`
- Modify if required by existing job routing: `.github/workflows/ci.yml`
- Modify if required by existing benchmark matrix wording: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: completed backend behavior from Tasks 1-4.
- Produces: user-facing capability matrix, ownership instructions, limitations, and CI evidence.

- [x] **Step 1: Update documentation**

Replace the sentence that says pipe support is future work with the exact
byte-stream contract. Split the capability matrix into socket and pipe columns,
state the POSIX `O_NONBLOCK` requirement, Windows async declaration, lack of
anonymous `CreatePipe` support, partial-transfer behavior, EOF mapping,
close-then-forget order, and the explicitly deferred lifecycle operations.

- [x] **Step 2: Run local Windows Release verification**

Run:

```powershell
cmd /c 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user && ctest --preset win-release-user --output-on-failure'
```

Expected: all configured tests pass with zero failures.

- [x] **Step 3: Inspect the diff and affected tests**

Run:

```powershell
git diff --check
codegraph sync .
codegraph affected -p . cflow/include/cflow/io_native.h cflow/src/io_native.c cflow/src/io_native_iocp.c cflow/src/io_native_readiness.c cflow/src/io_native_io_uring.c
git status --short
```

Confirm no `.codegraph` files are tracked and all changed production paths have
direct tests.

- [x] **Step 4: Commit documentation**

```powershell
git add cflow/README.md .github/workflows/ci.yml .github/workflows/cflow-release-benchmarks.yml
git commit -m "docs(cflow): document native pipe capabilities"
```

Stage workflow files only if their contents actually changed.

- [x] **Step 5: Push and open the PR**

```powershell
git push -u origin feat/cflow-native-pipe
gh pr create --base master --head feat/cflow-native-pipe --title "feat(cflow): add typed native pipe I/O" --body "Closes #105`n`nAdds bounded typed pipe read/write across declared native backends while preserving socket API compatibility and explicit unsupported behavior."
```

- [ ] **Step 6: Require hosted platform evidence**

Wait for Windows IOCP, Linux epoll/poll/io_uring, macOS kqueue/poll, public
header, and notation jobs. Any failure gets a reproducing failing test before a
fix. Do not merge until every required check is green and review finds no HIGH
or MED issue.
