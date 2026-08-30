# NativeIO Pipe and IPC Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make root-level NativeIO the single native byte-I/O data plane for sockets and byte pipes, add a separate NativeIPC rendezvous control plane, and adapt CFlow Actor/Source without platform I/O code.

**Architecture:** NativeIO keeps one fixed-owner submit/cancel/observe contract and adds an internal endpoint resource discriminator plus pipe operations. NativeIPC owns platform-specific creation/rendezvous and transfers connected endpoints. A bounded CFlow bridge maps one Actor's request IDs to NativeIO request handles; the existing I/O Source supplies the reactive Graph layer.

**Tech Stack:** ISO C11, TurboUtils Platform errors/thread primitives, NativeIO IOCP/epoll/io_uring/kqueue backends, CFlow Actor/Executor/Source, TinyTest, CMake Presets, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-30-native-io-pipe-ipc-design.md`

**Execution choice:** Inline Execution, as selected by the user; do not dispatch
sub-agents while executing this plan.

## Global Constraints

- Track all implementation and validation work in GitHub issue #168.
- Preserve existing NativeIO socket symbols, enum values, handle layout, payload borrowing, fixed capacities, and explicit backend selection.
- Preserve existing CFlow public behavior until a separately approved compatibility removal; new owner-driven APIs are additive.
- One backend has exactly one owner thread and creates no worker; producer threads enter only through the existing Actor MPSC mailbox.
- Successful NativeIO submit produces exactly one terminal observed completion; cancellation alone is never terminal evidence.
- No initialized data-path allocation, payload copy, hidden queue, blocking fallback, implicit backend downgrade, or partially implemented public API.
- Linux development and runtime tests use `root@eu` or hosted CI; do not claim local Linux execution from the Windows checkout.
- Use `win-dev-user` for focused local correctness and `win-release-user` for release/benchmark verification. Discover presets with `cmake --list-presets`, `cmake --build --list-presets`, and `ctest --list-presets` before invocation.

---

## Task 1: Add the typed pipe endpoint contract and resource-safe core

**Files:**

- Modify: `native-io/include/turbo/native_io.h`
- Modify: `native-io/src/native_io.c`
- Modify: `native-io/src/native_io_internal.h`
- Modify: `native-io/tests/native_io_test.c`
- Modify: `native-io/tests/native_io_header_cpp_test.cpp`

**Interfaces:**

- Consumes: existing `turbo_io_endpoint`, `turbo_io_operation`, and backend ops.
- Produces: `TURBO_IO_PIPE_READ`, `TURBO_IO_PIPE_WRITE`,
  `TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE`,
  `turbo_io_backend_pipe_supported()`, `attach_pipe()`, and `release_pipe()`.

- [ ] **Step 1: Write failing public validation tests**

Add assertions that preserve numeric socket kinds and define pipe shapes:

```c
check_equal(TURBO_IO_TCP_RECV, 1);
check_equal(TURBO_IO_UDP_SEND_TO, 4);
check_equal(TURBO_IO_PIPE_READ, 5);
check_equal(TURBO_IO_PIPE_WRITE, 6);

turbo_io_operation pipe = {
    .kind = TURBO_IO_PIPE_READ,
    .endpoint = {1u, 1u},
    .buffer = &byte,
    .length = 1u};
check_true(turbo_io_operation_valid(&pipe));
pipe.address = &byte;
check_false(turbo_io_operation_valid(&pipe));
```

Extend the C++ header test to construct the new enum and confirm the existing
endpoint remains `{uint32_t slot, uint32_t generation}` compatible.

- [ ] **Step 2: Run the focused tests and confirm RED**

```powershell
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user --target native_io_test native_io_header_cpp_test
ctest --preset win-dev-user -R "^(native_io_test|native_io_header_cpp_test)$" --output-on-failure
```

Expected failure: missing pipe enum/functions or failed compile, not an
unrelated configure error.

- [ ] **Step 3: Add the public declarations and common validation**

Add the exact declarations from the specification. In
`turbo_io_operation_valid()`, require non-null, positive-length buffers and
zero address fields for both pipe kinds. Validate unknown attach flags before
calling a platform backend. Clear `out_endpoint` on every attach failure.

- [ ] **Step 4: Generalize the private backend strategy without changing the public handle**

Add a private resource kind and strategy callbacks:

```c
typedef enum turbo_io_resource_kind {
  TURBO_IO_RESOURCE_SOCKET = 1,
  TURBO_IO_RESOURCE_BYTE_PIPE = 2
} turbo_io_resource_kind;

int (*attach_pipe)(turbo_io_impl *, uintptr_t, uint32_t,
                   turbo_io_endpoint *);
int (*release_pipe)(turbo_io_impl *, turbo_io_endpoint);
```

Every backend endpoint record stores the resource kind. Operation admission
checks kind before retaining a request slot; a mismatch returns
`TURBO_EINVAL` with no native effect.

- [ ] **Step 5: Run focused tests and confirm GREEN**

Run the Task 1 command again. Confirm both tests pass and existing TCP/UDP
validation cases remain unchanged.

- [ ] **Step 6: Commit the contract slice**

```powershell
git add native-io/include/turbo/native_io.h native-io/src/native_io.c native-io/src/native_io_internal.h native-io/tests/native_io_test.c native-io/tests/native_io_header_cpp_test.cpp
git commit -m "feat(native-io): define typed byte-pipe endpoints"
```

## Task 2: Implement the readiness pipe strategy

**Files:**

- Modify: `native-io/src/native_io_readiness.h`
- Modify: `native-io/src/native_io_readiness.c`
- Modify: `native-io/src/native_io_epoll.c`
- Modify: `native-io/src/native_io_kqueue.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**

- Consumes: Task 1 resource kind and pipe operation kinds.
- Produces: epoll/kqueue nonblocking pipe/FIFO attach, FIFO lanes, terminal
  mapping, and resource-kind-safe release.

- [ ] **Step 1: Add shared failing POSIX pipe tests**

For every compiled readiness backend, create a nonblocking `pipe()` pair and
cover write/read, read EOF after peer close, broken write without process-visible
`SIGPIPE`, same-lane FIFO order, cancel of queued lane entries, full capacity,
release while active, stale endpoint, and repeated descriptor reuse.

Add an explicit blocking-descriptor case:

```c
check_equal(turbo_io_backend_attach_pipe(
                &backend, (uintptr_t)blocking_fd,
                TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE, &endpoint),
            TURBO_EINVAL);
check_false(turbo_io_endpoint_valid(endpoint));
```

- [ ] **Step 2: Run the Linux tests remotely and confirm RED**

From a branch pushed for remote testing, use a fresh remote checkout:

```bash
ssh root@eu
native_io_worktree=$(mktemp -d /tmp/turbo-utils-native-io-pipe.XXXXXX)
git clone --branch design/native-io-pipe-ipc https://github.com/qigao/turbo-utils.git "$native_io_worktree"
cd "$native_io_worktree"
cmake --fresh --preset linux-dev-user
cmake --build --preset linux-dev-user --target native_io_test
ctest --preset linux-dev-user -R '^native_io_test$' --output-on-failure
```

Expected failure: pipe attach/operation unsupported on readiness.

- [ ] **Step 3: Split socket and byte-pipe syscall execution**

Keep the lane machinery shared, but dispatch at the resource boundary:

```c
switch (endpoint->resource_kind) {
  case TURBO_IO_RESOURCE_SOCKET:
    return readiness_try_socket(endpoint, request, bytes, address_length);
  case TURBO_IO_RESOURCE_BYTE_PIPE:
    return readiness_try_pipe(endpoint, request, bytes);
  default:
    return TURBO_EINVAL;
}
```

`readiness_try_pipe()` uses `read`/`write`, the existing guarded SIGPIPE
protocol, EINTR retry, EAGAIN/EWOULDBLOCK rearm, read-zero EOF, and partial byte
completion. It never changes descriptor flags.

- [ ] **Step 4: Implement pipe attach/release**

Verify `O_NONBLOCK`, reject duplicate native identities across both resource
kinds, allocate from the existing endpoint free stack, and derive epoll/kqueue
interest only from the two request lanes. Release validates resource kind and
returns `TURBO_EBUSY` while either lane retains an unobserved request.

- [ ] **Step 5: Run Linux and macOS readiness validation**

Run the remote Linux focused command for epoll. Push the exact revision and let
the macOS CI job execute the same shared test through kqueue. Record both job
URLs in #168 before checking the platform items.

- [ ] **Step 6: Commit the readiness slice**

```powershell
git add native-io/src/native_io_readiness.h native-io/src/native_io_readiness.c native-io/src/native_io_epoll.c native-io/src/native_io_kqueue.c native-io/tests/native_io_test.c
git commit -m "feat(native-io): drive byte pipes through readiness backends"
```

## Task 3: Implement IOCP named-pipe data operations

**Files:**

- Modify: `native-io/src/native_io_iocp.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**

- Consumes: Task 1 pipe attach contract and common completion vocabulary.
- Produces: IOCP association, overlapped read/write, cancellation, completion,
  and endpoint release for connected byte-mode named pipes.

- [ ] **Step 1: Add failing Windows named-pipe tests**

Create connected byte-mode server/client handles with
`FILE_FLAG_OVERLAPPED`. Test successful read/write, pending cancellation,
partial transfer, EOF/broken peer, duplicate attach, capacity, close/drain, and
handle-number reuse. Assert an anonymous `CreatePipe` handle returns
`TURBO_ENOTSUP` without entering an operation.

- [ ] **Step 2: Run the focused Windows test and confirm RED**

```powershell
cmake --build --preset win-dev-user --target native_io_test
ctest --preset win-dev-user -R '^native_io_test$' --output-on-failure
```

- [ ] **Step 3: Add resource-neutral IOCP endpoint records**

Store socket versus byte-pipe kind next to the retained native identity. Keep
one IOCP handle and one request free stack. Associate overlapped named-pipe
handles with the existing port and reject unsupported/non-byte-mode resources
before allocating a request record.

- [ ] **Step 4: Submit and complete pipe operations**

Use `ReadFile`/`WriteFile` with the request record's stable `OVERLAPPED`.
Synchronous success and `ERROR_IO_PENDING` are both admitted exactly once.
`CancelIoEx` only requests cancellation. Normalize `ERROR_OPERATION_ABORTED`,
`ERROR_BROKEN_PIPE`, zero-byte read, partial bytes, and other Win32 errors per
the specification.

- [ ] **Step 5: Run the focused test and repeated resource loop**

Run `native_io_test` and its TinyTest filter for named pipes repeatedly. Compare
`GetProcessHandleCount` before and after the repeated success/error/cancel loop.

- [ ] **Step 6: Commit the IOCP slice**

```powershell
git add native-io/src/native_io_iocp.c native-io/tests/native_io_test.c
git commit -m "feat(native-io): support overlapped named-pipe IO"
```

## Task 4: Implement io_uring pipe operations

**Files:**

- Modify: `native-io/src/native_io_io_uring.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**

- Consumes: common pipe contract and existing io_uring generation-tagged CQE
  protocol.
- Produces: `IORING_OP_READ`/`IORING_OP_WRITE` pipe lanes with authoritative
  CQ completion and cancellation.

- [ ] **Step 1: Add failing io_uring-specific parity cases**

Reuse the shared pipe suite while explicitly selecting io_uring. Add queued
same-lane cancellation so one active SQE and one retained descriptor settle in
FIFO order without generation aliasing.

- [ ] **Step 2: Run the remote focused test and confirm RED**

Run `native_io_test` under `linux-dev-user` on `root@eu`. Treat kernel policy
rejection as an explicit environment result; do not run epoll as fallback.

- [ ] **Step 3: Map operation kinds to io_uring opcodes**

For byte-pipe endpoints, prepare `IORING_OP_READ`/`IORING_OP_WRITE` with the
non-seekable current-position offset. Keep the existing `(generation, index)`
`user_data`, one in-flight SQE per lane, queued descriptor retention, CQ drain,
and cancel-CQE filtering.

- [ ] **Step 4: Normalize CQ results and advance FIFO lanes**

Map positive bytes, read zero, `-ECANCELED`, `-EPIPE`, and other negative CQE
results. After terminal observation, advance only the same endpoint/direction
lane and submit its next retained descriptor.

- [ ] **Step 5: Run remote io_uring, epoll, and header regression tests**

```bash
cmake --build --preset linux-dev-user --target native_io_test native_io_header_cpp_test
ctest --preset linux-dev-user -R '^(native_io_test|native_io_header_cpp_test)$' --output-on-failure
```

- [ ] **Step 6: Commit the io_uring slice**

```powershell
git add native-io/src/native_io_io_uring.c native-io/tests/native_io_test.c
git commit -m "feat(native-io): submit pipe operations through io_uring"
```

## Task 5: Move rendezvous into the NativeIPC control plane

**Files:**

- Create: `native-io/include/turbo/native_ipc.h`
- Create: `native-io/src/native_ipc.c`
- Create: `native-io/src/native_ipc_windows.c`
- Create: `native-io/src/native_ipc_posix.c`
- Create: `native-io/tests/native_ipc_test.c`
- Create: `native-io/tests/native_ipc_header_cpp_test.cpp`
- Modify: `native-io/CMakeLists.txt`
- Modify: `native-io/tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 1 pipe endpoint flags; behavior from `cflow/io_pipe.h`.
- Produces: typed endpoint ownership, Windows bounded owner-driven server,
  single-attempt client connect, and POSIX FIFO open.

- [ ] **Step 1: Copy behavioral tests, not implementation**

Port the existing CFlow rendezvous cases into NativeIO names: Windows
connect-before-accept race, pending cancellation, full capacity, missing/busy
client, close/drain, handle-count stability; POSIX writer-without-reader,
reader-then-writer success, direction validation, and descriptor-count
stability.

- [ ] **Step 2: Add public header and compile tests, then confirm RED**

Register `native_ipc_test` and `native_ipc_header_cpp_test` with
`cmake_add_test()`. Build both and confirm the absent API fails compilation.

- [ ] **Step 3: Implement common ownership and validation**

`native_ipc.c` validates names, directions, capacities, checked allocation,
endpoint zero-state, close invalidation, and capability queries. It dispatches
only control-plane operations to platform files and never calls NativeIO
submit/observe for payloads.

- [ ] **Step 4: Implement Windows owner-driven rendezvous**

Preallocate all accept slots. Each accepted slot owns one byte-mode overlapped
named-pipe instance and event until terminal observation. Normalize
`ERROR_IO_PENDING`, `ERROR_PIPE_CONNECTED`, cancellation, failure, transfer,
close, and quiescent destroy exactly as the specification state machine.

- [ ] **Step 5: Implement POSIX FIFO open**

Perform one `open` with `O_NONBLOCK | O_CLOEXEC`, reject duplex, map writer
`ENXIO` to `TURBO_EPIPE`, and publish
`TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE` only on success. Do not create, unlink,
wait, retry, or change permissions.

- [ ] **Step 6: Run NativeIPC and NativeIO tests**

```powershell
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user --target native_ipc_test native_ipc_header_cpp_test native_io_test
ctest --preset win-dev-user -R '^(native_ipc_test|native_ipc_header_cpp_test|native_io_test)$' --output-on-failure
```

Run the matching Linux preset remotely and obtain macOS CI evidence.

- [ ] **Step 7: Commit the control-plane slice**

```powershell
git add native-io/include/turbo/native_ipc.h native-io/src/native_ipc.c native-io/src/native_ipc_windows.c native-io/src/native_ipc_posix.c native-io/tests/native_ipc_test.c native-io/tests/native_ipc_header_cpp_test.cpp native-io/CMakeLists.txt native-io/tests/CMakeLists.txt
git commit -m "feat(native-io): add bounded IPC rendezvous control plane"
```

## Task 6: Add the owner-driven CFlow NativeIO adapter

**Files:**

- Create: `cflow/include/cflow/io_native_adapter.h`
- Create: `cflow/src/io_native_adapter.c`
- Create: `cflow/tests/cflow_io_native_adapter_test.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**

- Consumes: NativeIO backend, `cflow_io_backend_ops`, Actor completion, and
  existing windowed Source drive callback.
- Produces: one-Actor bounded request bridge and owner-thread observation.

- [x] **Step 1: Add failing bridge lifecycle tests**

Use a real local socket/pipe fixture and a manual Executor. Cover Actor submit,
NativeIO submit failure, terminal delivery, cancellation, acknowledgement,
capacity, stale Actor completion, second-Actor rejection, close/drain, and
operation release callback count. Assert every accepted operation releases
exactly once.

- [x] **Step 2: Add a failing windowed Source integration test**

Construct `cflow_source_from_io_actor_windowed()` with an adapter drive callback.
Request bounded demand, verify WAIT/wake/value/EOF, cancel the Run while native
I/O is pending, and check shutdown ordering.

- [x] **Step 3: Implement fixed bridge storage**

At init, allocate exactly `backend.request_capacity` bridge records and one
completion batch. A bridge stores Actor pointer, Actor request ID, NativeIO
request handle, original operation pointer, slot generation, and phase. Encode
`index + 1` in copied NativeIO `user_data`; validate phase before completing.

- [x] **Step 4: Implement Actor backend strategy**

Submit binds the first Actor and rejects another Actor. It reserves a bridge,
copies `turbo_io_operation`, replaces only the copied `user_data`, and calls
NativeIO submit. Failure releases the bridge before returning. Cancel locates
the bound Actor request and calls NativeIO cancel; terminal status remains
unknown until observe.

- [x] **Step 5: Implement observe and lifecycle forwarding**

Observe a fixed batch, translate each completion to
`cflow_io_actor_complete()`, update UDP address length when applicable, release
the bridge once, and count Actor-stale completions. Forward endpoint attach,
release, close, stats, and destroy while enforcing the same owner-thread
contract in documentation and tests.

- [x] **Step 6: Link CFlow publicly to NativeIO and run focused tests**

Because the public adapter header names NativeIO types, link
`TurboUtils::CFlow` PUBLIC to `TurboUtils::NativeIO`. Build and run:

```powershell
cmake --fresh --preset win-dev-user
cmake --build --preset win-dev-user --target cflow_io_native_adapter_test cflow_io_actor_test cflow_io_source_test native_io_test
ctest --preset win-dev-user -R '^(cflow_io_native_adapter_test|cflow_io_actor_test|cflow_io_source_test|native_io_test)$' --output-on-failure
```

- [ ] **Step 7: Commit the adapter slice**

```powershell
git add cflow/include/cflow/io_native_adapter.h cflow/src/io_native_adapter.c cflow/tests/cflow_io_native_adapter_test.c cflow/include/cflow/cflow.h cflow/CMakeLists.txt cflow/tests/CMakeLists.txt
git commit -m "feat(cflow): adapt Actor IO to NativeIO"
```

## Task 7: Migrate owner-driven callers and gate legacy removal

**Files:**

- Modify: `cflow/src/io_pipe.c`
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/tests/cflow_io_pipe_test.c`
- Modify: `cflow/tests/cflow_io_native_test.c`
- Modify: `cflow/examples/cflow_native_pipe_example.c`
- Modify: `cflow/CMakeLists.txt`

**Interfaces:**

- Consumes: NativeIPC and the owner-driven adapter.
- Produces: an owner-driven rendezvous wrapper, migrated in-tree data callers,
  and an explicit #147 removal gate for the autonomous legacy backend.

- [ ] **Step 1: Freeze legacy behavior with focused tests**

Before changing implementation, run and retain tests for callbacks, typed
submit errors, automatic legacy completion, cancellation, stats, shutdown,
rendezvous transfer, FIFO open, and the runnable example. Add assertions that
compatibility conversions never copy a live endpoint wrapper as an owner.

- [ ] **Step 2: Delegate rendezvous to NativeIPC**

Replace `CreateNamedPipe`, `ConnectNamedPipe`, POSIX `open`, and close logic in
`cflow/src/io_pipe.c` with explicit NativeIPC calls and field-by-field endpoint
ownership transfer. Preserve all existing CFlow enum values, callback order,
errors, and public layouts.

- [ ] **Step 3: Migrate the runnable Pipe example and in-tree owner-driven callers**

Replace legacy data-backend construction with `cflow_io_native_adapter`,
`turbo_io_operation`, explicit adapter `observe`, and existing Actor/Executor
drive. Keep the same byte transfer and shutdown checks. Do not add a
compatibility queue or allow multiple threads to alternate NativeIO ownership.

- [ ] **Step 4: Mark the autonomous legacy Pipe backend as superseded**

Document `cflow_io_native_backend_pipe_actor_ops()` and its pipe descriptor as
legacy autonomous APIs whose replacement is the owner-driven adapter. Preserve
their implementation and tests until #147 explicitly authorizes removal;
deprecation alone must not change completion or worker behavior.

- [ ] **Step 5: Run legacy, new, and example regression tests**

```powershell
cmake --build --preset win-dev-user --target cflow_io_pipe_test cflow_io_native_test cflow_native_pipe_example cflow_io_native_adapter_test native_ipc_test native_io_test
ctest --preset win-dev-user -R '^(cflow_io_pipe_test|cflow_io_native_test|cflow_io_native_adapter_test|native_ipc_test|native_io_test)$' --output-on-failure
```

Run Linux remotely and macOS through CI at the same revision.

- [ ] **Step 6: Commit the additive migration**

```powershell
git add cflow/src/io_pipe.c cflow/include/cflow/io_native.h cflow/tests/cflow_io_pipe_test.c cflow/tests/cflow_io_native_test.c cflow/examples/cflow_native_pipe_example.c cflow/CMakeLists.txt
git commit -m "refactor(cflow): migrate pipe callers to NativeIO"
```

- [ ] **Step 7: Execute the breaking removal only after #147 approval**

After #147 records explicit authorization and repository search finds no
non-test callers, delete `cflow_io_native_backend_pipe_actor_ops()`,
`cflow_io_native_pipe_operation`, and only the Pipe-specific branches from
`io_native_readiness.c`, `io_native_iocp.c`, and `io_native_io_uring.c`.
Run the complete Task 7 regression set before committing the removal as a
separate reviewable change. If approval is absent, leave this step unchecked
and keep #168 open.

## Task 8: Add layered benchmarks, packaging, and final evidence

**Files:**

- Modify: `native-io/README.md`
- Modify: `native-io/benchmarks/native_io_benchmark.c`
- Modify: `native-io/benchmarks/native_io_benchmark_linux.c`
- Modify: `native-io/benchmarks/native_io_benchmark_kqueue.c`
- Create: `cflow/benchmarks/cflow_native_io_adapter_benchmark.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`
- Modify: `.github/workflows/native-io-release-benchmarks.yml`
- Modify: `docs/superpowers/specs/2026-08-30-native-io-pipe-ipc-design.md`

**Interfaces:**

- Consumes: completed direct and adapted implementations.
- Produces: separated Pipe tables, layer ratios, installation evidence, and
  issue checklist evidence.

- [x] **Step 1: Add correctness gates before timing**

Each benchmark validates full payload, byte count, EOF/error semantics,
completion identity, zero rejections, zero stale completions, and quiescent
cleanup before publishing a row. A failed semantic gate exits nonzero.

- [ ] **Step 2: Add separated benchmark tables**

NativeIO benchmark sources report direct Pipe performance by backend and
payload. The CFlow benchmark reports Actor/NativeIO and Source/NativeIO and
uses direct NativeIO from the same executable as denominator. Do not place a
CFlow dependency inside the NativeIO library target.

Required rows cover 1/4/8/16/32/64 KiB where the platform pipe supports the
payload through partial one-shot operations. Required columns are p50/p95/p99,
ops/s, MiB/s, CPU, errors, rejections, stale completions, submit, observe,
Actor transition, Executor delivery, and acknowledgement time.

- [x] **Step 3: Gate benchmark CI by affected paths**

Run direct NativeIO rows when `native-io/**` or the benchmark workflow changes.
Run adapted rows when `native-io/**`, `cflow/**`, or the workflow changes. Do
not trigger NativeIO release benchmarks for unrelated modules.

- [ ] **Step 4: Verify Windows Release and install/export**

```powershell
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target native_io_test native_ipc_test cflow_io_native_adapter_test native_io_benchmark
ctest --preset win-release-user -R '^(native_io_test|native_ipc_test|cflow_io_native_adapter_test|cflow_io_actor_test|cflow_io_source_test|cflow_io_pipe_test|cflow_io_native_test)$' --output-on-failure
cmake --build --preset install-win-release-user
```

- [ ] **Step 5: Verify Linux and hosted macOS at the identical SHA**

Run the Linux focused suite and benchmark on `root@eu`; push the same commit and
require hosted macOS kqueue correctness. Record exact SHA, commands, test
counts, benchmark artifacts, and job links in #168.

- [ ] **Step 6: Update docs and issue checkboxes from evidence**

Document the final public APIs, ownership invalidation points, capability
matrix, shutdown example, compatibility worker status, and measured tables.
Check only issue items proven by merged code or same-SHA CI; leave other items
open with their concrete remaining gate.

- [ ] **Step 7: Run self-review and broad adjacent regression**

Search the diff for placeholders and old Pipe syscalls in CFlow:

```powershell
rg.exe -n "T[O]DO|F[I]XME|H[A]CK|CreateNamedPipe|ConnectNamedPipe|ReadFile|WriteFile|IORING_OP_(READ|WRITE)|\bread\(|\bwrite\(" native-io cflow
git diff --check origin/master...HEAD
ctest --preset win-release-user -R '^(native_io_|cflow_)' --output-on-failure
```

Classify every remaining platform syscall occurrence as NativeIO/NativeIPC,
still-required non-Pipe legacy behavior, test fixture, or defect.

- [ ] **Step 8: Commit the validation slice**

```powershell
git add native-io/README.md native-io/benchmarks cflow/benchmarks .github/workflows/native-io-release-benchmarks.yml docs/superpowers/specs/2026-08-30-native-io-pipe-ipc-design.md
git commit -m "test(native-io): validate layered Pipe and IPC paths"
```

## Completion checklist

- [ ] Re-read the specification and map every included requirement to a task and passing test.
- [ ] Confirm no excluded IPC semantic leaked into the public byte-pipe API.
- [ ] Confirm all public headers compile as C and C++ and installed consumers link the correct target.
- [ ] Confirm `git diff --check origin/master...HEAD` is clean.
- [ ] Confirm Windows, Linux, and macOS required jobs use the same commit.
- [ ] Confirm #168 contains the final SHA, evidence links, updated checkboxes, and any separately tracked follow-up.
