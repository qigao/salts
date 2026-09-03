# CFlow Pipe Rendezvous and Subprocess Ownership Design

**Issue:** [#133](https://github.com/qigao/salts/issues/133)
**Status:** approved, implemented, and verified on Windows and Linux; macOS CI pending
**Scope:** connection/rendezvous and process-standard-stream ownership only. The
existing typed byte-pipe read/write data path remains unchanged.

## Decision summary

Split the feature into three explicit layers:

1. `cflow_io_native_pipe_operation` remains the borrowed, already-connected
   byte data plane. Its layout, read/write operation kinds, completion rules,
   and endpoint ownership do not change.
2. A new CFlow pipe-rendezvous facade owns pathname/instance control state and
   transfers one asynchronous-capable endpoint on success. Windows server
   accept is a real overlapped operation. Windows client connect and POSIX FIFO
   open are synchronous, single-attempt control-plane calls; neither is
   presented as native asynchronous completion.
3. A separate `Salts::CFlowProcess` adapter combines `Salts::Core`
   process ownership with `Salts::CFlow` byte-pipe execution. Core gains
   one additive spawn entry point for borrowed child-side standard handles;
   CFlow itself does not depend on Core and does not duplicate process creation.

No hidden blocking worker, implicit backend downgrade, unbounded retry, or
message-framing protocol is introduced.

## Evidence and current constraints

### Repository facts

- `cflow/include/cflow/io_native.h` defines pipe operations only for `READ` and
  `WRITE` on an already-created endpoint. The backend borrows the endpoint and
  buffer through terminal callback return and never closes either.
- `cflow/src/io_native_iocp.c` lowers those operations directly to overlapped
  `ReadFile` and `WriteFile`. It has no named-pipe connection state.
- `cflow/tests/cflow_io_native_test.c` creates and connects named-pipe pairs
  before submission. It also proves that synchronous `CreatePipe` handles are
  rejected rather than admitted to IOCP.
- `utils/src/salts_process.c` is the single process-lifecycle fact source. Its
  monitor thread drains captured stdout/stderr and closes those parent
  endpoints. Exporting the same endpoints to CFlow would create two consumers
  and two close paths.
- `utils/CMakeLists.txt` links Core privately to CFlow, while CFlow cannot link
  back to Core without a cycle. A combined adapter therefore belongs in a
  separate target added after Core, following the existing CFlowFS layering.

### Platform facts

- Windows `ConnectNamedPipe` supports overlapped server connection. A client
  may connect between `CreateNamedPipe` and `ConnectNamedPipe`; the latter then
  returns `ERROR_PIPE_CONNECTED`, which denotes successful connection rather
  than failure. See Microsoft
  [`ConnectNamedPipe`](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-connectnamedpipe).
- Windows clients open named pipes through `CreateFile`. If every instance is
  busy, it returns `ERROR_PIPE_BUSY`; the documented waiting mechanism is the
  synchronous `WaitNamedPipe`. There is no client-side counterpart that yields
  an overlapped connect completion. See Microsoft
  [`Named Pipe Client`](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-client).
- An asynchronous client endpoint must be opened with
  `FILE_FLAG_OVERLAPPED`. See Microsoft
  [`Named Pipe Open Modes`](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-open-modes).
- `GetNamedPipeInfo` requires `GENERIC_READ` on a server handle. A least-
  privilege `PIPE_ACCESS_OUTBOUND` server therefore cannot expose its mode to
  IOCP validation even though overlapped writes are valid. CFlow retains the
  directional handle and treats byte mode as the same explicit caller
  precondition already used for the non-queryable `FILE_FLAG_OVERLAPPED`. See
  Microsoft [`GetNamedPipeInfo`](https://learn.microsoft.com/en-us/windows/win32/api/namedpipeapi/nf-namedpipeapi-getnamedpipeinfo).
- POSIX FIFO `open()` is rendezvous only when blocking. With `O_NONBLOCK`, a
  read-only open returns immediately, while a write-only open fails if no
  reader is present. See POSIX
  [`open()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/open.html).
- Windows child handle inheritance can be restricted to an explicit
  `PROC_THREAD_ATTRIBUTE_HANDLE_LIST`; every listed handle must be inheritable
  and `CreateProcess` must receive `bInheritHandles=TRUE`. See Microsoft
  [`UpdateProcThreadAttribute`](https://learn.microsoft.com/en-us/windows/desktop/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute).
- POSIX child descriptor redirection is an ordered `dup2`/close operation; all
  remaining close-on-exec descriptors are then closed. See POSIX
  [`posix_spawn()`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/posix_spawn.html).

## Capability matrix

| Capability | Windows IOCP | POSIX readiness | Linux io_uring | Contract |
|---|---|---|---|---|
| Already-connected byte read/write | supported | supported for `O_NONBLOCK` | supported | Existing native pipe API, unchanged |
| Named-pipe server accept | supported with overlapped `ConnectNamedPipe` | not applicable | not applicable | Bounded asynchronous rendezvous service |
| Named-pipe client connect | synchronous single `CreateFile` attempt | not applicable | not applicable | Control plane; busy/missing is returned, never waited internally |
| FIFO read-side pathname open | not applicable | synchronous `O_RDONLY | O_NONBLOCK` | same | Control plane; success does not claim a writer exists |
| FIFO write-side pathname open | not applicable | synchronous `O_WRONLY | O_NONBLOCK` | same | Control plane; no reader maps from `ENXIO` to `SALTS_EPIPE` |
| FIFO accept/connect completion | not applicable | unsupported | unsupported | `SALTS_ENOTSUP`; POSIX exposes no equivalent connection object |
| Subprocess async stdin/stdout/stderr | named-pipe parent ends opened overlapped | nonblocking parent pipe ends | nonblocking parent pipe ends | Separate CFlowProcess adapter |
| Message-mode records / transactions | unsupported | unsupported | unsupported | Outside #133 |

Capability queries describe these distinctions explicitly. Unsupported
combinations return `SALTS_ENOTSUP`; no backend is selected as a fallback.

## Ownership model

### Data unit and fact sources

The transferred data unit is a `cflow_io_pipe_endpoint`: one native handle or
descriptor plus the flags required by the existing pipe data plane. The handle
is the sole resource identity; the wrapper is not a second fact source.

- A pending Windows server instance is owned by the rendezvous service.
- Successful accept completion transfers its endpoint exactly once to the
  completion callback.
- A successful synchronous client/FIFO open transfers its endpoint exactly
  once to the output argument.
- A failed or cancelled accept remains service-owned and is closed by the
  service after authoritative native completion.
- Once transferred, the caller owns close. If the endpoint was submitted to a
  native backend, the caller first drains and acknowledges every request, then
  closes, then calls `cflow_io_native_backend_forget_pipe()`.

Endpoint close is idempotent only through the wrapper: it atomically replaces
the stored identity with the invalid sentinel before invoking the platform
close. Copying a live endpoint wrapper is forbidden.

### Borrowed objects

- Path strings are copied during service initialization or consumed entirely
  by a synchronous open call.
- Submit metadata is copied into a fixed-capacity slot.
- Read/write buffers retain the existing native pipe borrowing contract.
- A process spawn binding borrows child-side handles only until spawn returns;
  it never consumes or closes them.

### Subprocess ownership

`salts_process_t` remains the only owner of the child process, process group or
Job Object, monitor thread, and terminal result. `cflow_process` owns only:

- the `salts_process_t` handle;
- the three parent-side async pipe endpoints that were never captured by the
  Core monitor thread;
- its bounded native backend, Executor, Actor, and operation slots.

Child-side endpoints are closed in the parent immediately after successful or
failed spawn. They are not stored in either owner. Parent-side endpoints are
closed only by `cflow_process` after admitted operations settle.

## Public API boundaries

Names below describe the intended surface; exact spelling may be adjusted to
the repository style during TDD.

### Core: borrowed standard-handle spawn

Add a versioned, additive entry point rather than extending
`salts_process_options_t`, whose by-value public layout is already compiled into
consumers:

```c
typedef struct salts_process_stdio_bindings_t {
    uintptr_t stdin_handle;
    uintptr_t stdout_handle;
    uintptr_t stderr_handle;
} salts_process_stdio_bindings_t;

int salts_process_spawn_with_stdio(
    const salts_process_options_t *options,
    const salts_process_stdio_bindings_t *bindings,
    salts_process_t **out_process);
```

Each binding is either a native handle/descriptor or the documented inherit
sentinel. The call rejects a supplied stdin binding combined with
`SALTS_PROCESS_PIPE_STDIN`, and supplied stdout/stderr bindings combined with
the corresponding capture flag. Existing `salts_process_spawn()` and its ABI
remain unchanged.

On Windows the implementation duplicates supplied handles as inheritable child
handles and places only those duplicates in the existing explicit handle list.
On POSIX the fork child performs `dup2` before closing temporary descriptors.
The caller's supplied handles remain unchanged on every path.

### CFlow: pipe rendezvous

Add `<cflow/io_pipe.h>` to Core CFlow:

- endpoint init/valid/close helpers;
- explicit capability queries;
- a bounded Windows named-pipe server with `init`, `try_accept`, `run_ready`,
  `try_cancel`, `close`, `is_quiescent`, and `destroy`;
- synchronous single-attempt Windows client connect;
- synchronous single-attempt POSIX FIFO read/write open.

The Windows server config includes hard `request_capacity`, pipe buffer sizes,
byte direction, and a fixed local-only policy. It is an exclusive control-plane
service rather than an Actor mailbox: `request_capacity` bounds all
service-owned instances and ready completions, while `run_ready(max_steps)`
bounds callback work. A successful callback transfers its endpoint out of that
bound; the caller then owns it, and any later native-backend identity is bounded
by that backend's separate request capacity.
Capacity zero, arithmetic overflow, invalid names, message mode, remote clients,
and unsupported platforms fail during initialization.

The server callback receives the accepted endpoint by value on success. That
callback becomes the owner before invocation and must eventually close it.
Failure and cancellation callbacks receive an invalid endpoint.

### CFlowProcess: combined adapter target

Add a static `Salts::CFlowProcess` target after Core and CFlow, exposing
`<cflow/process.h>`. It provides:

- start/poll/terminate and terminal process result observation;
- `try_write_stdin`, `try_read_stdout`, and `try_read_stderr` using the existing
  exactly-once CFlow Actor completion vocabulary;
- explicit stdin half-close;
- `run_ready`, `close`, `is_quiescent`, and `destroy` lifecycle operations;
- hard operation, command, and completion-batch capacities.

It does not expose raw parent endpoints, duplicate shell execution, capture
buffers, an unbounded output queue, or a second process state machine.

## State machines

### Named-pipe server

Service state:

```text
ZERO --init--> OPEN --close--> CLOSING --all slots settled--> QUIESCENT
  ^                                                          |
  +------------------------ destroy <-------------------------+
```

Per accept slot:

```text
FREE --admit/create instance--> CONNECT_PENDING
CONNECT_PENDING --native success/ERROR_PIPE_CONNECTED--> READY_SUCCESS
READY_SUCCESS --run_ready transfers endpoint/calls callback--> FREE
CONNECT_PENDING --cancel request--> CANCEL_PENDING
CANCEL_PENDING --native completion--> READY_CANCELLED
READY_CANCELLED --run_ready closes/calls callback--> FREE
CONNECT_PENDING --native failure--> READY_FAILED
READY_FAILED --run_ready closes/calls callback--> FREE
```

Cancellation never releases or reuses a slot. `CancelIoEx` only requests
cancellation; `GetOverlappedResult` supplies the authoritative completion. The
`ERROR_PIPE_CONNECTED` create/connect race is normalized to success and queued
through the same authoritative completion lane.

Server `close()` stops admission and requests cancellation for pending accepts.
`destroy()` returns `SALTS_EBUSY` until all terminal callbacks have run and all
slots are free. It never disconnects or closes transferred endpoints.

### Synchronous open

Client/FIFO open has no admitted asynchronous request:

```text
INVALID_OUTPUT --single platform open--> OWNED_ENDPOINT | EXPLICIT_ERROR
```

Windows `ERROR_PIPE_BUSY` maps to `SALTS_EBUSY`; missing pipe maps to
`SALTS_ENOENT`. POSIX FIFO writer `ENXIO` maps to `SALTS_EPIPE`. No sleep,
`WaitNamedPipe`, retry, or worker dispatch occurs internally.

### CFlowProcess

Process ownership and I/O requests remain separate facts:

```text
ZERO --start--> RUNNING --process exits/terminate/timeout--> PROCESS_TERMINAL
RUNNING or PROCESS_TERMINAL --close--> CLOSING
CLOSING --all I/O terminal + stdin closed + monitor joined--> QUIESCENT
QUIESCENT --destroy--> ZERO
```

Process exit does not discard stdout/stderr: those endpoints remain readable
until their own EOF completions. EOF does not imply the process has been reaped.
Closing stdin waits for or cancels an admitted write before closing the endpoint
so the child sees EOF. Adapter close requests cancellation, closes stdin after
write settlement, terminates a still-running child, drains authoritative pipe
completions, then joins/destroys the Core process owner.

## Error semantics

- Invalid configuration and contradictory ownership flags: `SALTS_EINVAL`.
- Unsupported platform/backend/semantic combination: `SALTS_ENOTSUP`.
- Full bounded admission: typed `CFLOW_IO_PIPE_SUBMIT_FULL`; no allocation
  fallback.
- Closed admission: typed `CFLOW_IO_PIPE_SUBMIT_CLOSED`.
- Windows missing/busy pipe: `SALTS_ENOENT` / `SALTS_EBUSY`.
- POSIX FIFO writer without reader: `SALTS_EPIPE`.
- Read after peer close: `CFLOW_IO_COMPLETION_EOF`.
- Broken write: `CFLOW_IO_COMPLETION_FAILED` with `SALTS_EPIPE`.
- Process termination and I/O cancellation are independent observations; one
  does not overwrite the other.
- The first control/lifecycle error is retained and later cleanup errors do not
  turn a failed close into success.

## Capacity and backpressure

All dynamically sized service storage is allocated at initialization. Each
accepted Windows admission additionally acquires one event and one named-pipe
handle, both bounded by the preallocated slot count:

- named-pipe server: exactly `request_capacity` accept slots and at most that
  many pending instances;
- CFlowProcess: exactly `request_capacity` data-I/O slots shared by stdin,
  stdout, and stderr;
- CFlowProcess command and completion queues have caller-specified hard
  capacities checked for multiplication/addition overflow;
- path storage has a documented maximum and is copied once;
- OS pipe buffer hints are bounded configuration, not application queues.

Full capacity is observable in submit results and statistics. There is no
silent drop, overwrite, unbounded growth, spin-until-space, or implicit
thread-pool path.

## Threading and callback rules

- CFlowProcess submission is MPSC through its Actor. The Windows named-pipe
  server is an exclusive control-plane object; its calls are serialized by its
  owner rather than hidden behind another mailbox.
- Exactly one driver thread calls each service's `run_ready()` and all user
  callbacks run on that thread.
- Lifecycle (`close`, final drain, `destroy`) belongs to that driver after
  producer threads have stopped and joined.
- No lock is held while invoking a user callback, closing an endpoint, joining
  a monitor thread, or calling a potentially blocking process operation.
- Synchronous pathname open is a control-plane call and must not be made from a
  latency-critical callback under a CFlow lock.

## Alternatives rejected

### Add accept/connect kinds to `cflow_io_native_pipe_operation`

Rejected because Windows server accept mutates a pre-created instance, Windows
client connect creates a handle synchronously, and POSIX FIFO has no connection
object. One operation kind would hide incompatible ownership and completion
semantics in a byte-buffer structure.

### Export Core's captured stdout/stderr handles

Rejected because the existing monitor thread is their active consumer and
close owner. Detaching them after spawn races with monitor reads; sharing them
creates nondeterministic byte distribution and double close.

### Reimplement process creation in CFlow

Rejected because it duplicates environment, argv, process-tree termination,
monitoring, timeout, error, and inheritance logic. It would create a second
process-lifecycle fact source.

### Put the process adapter directly in CFlow

Rejected because Core already links to CFlow. Making CFlow link to Core creates
a target cycle and reverses the existing layer direction.

### Hide client/FIFO open in a worker

Rejected for this issue because it changes capacity, scheduling, cancellation,
and shutdown semantics and conflicts with the explicit no-hidden-worker
contract. A future control-plane service may wrap these synchronous calls only
with its own named bounded-worker API.

## Compatibility and migration

- Existing CFlow pipe and Core process APIs keep their source and binary
  layouts and behavior.
- The Core spawn addition is a new symbol; existing callers do not migrate.
- `<cflow/io_pipe.h>` and `<cflow/process.h>` are additive headers.
- `Salts::CFlowProcess` is an additive exported target; CFlow alone does
  not acquire a Core dependency.
- Existing callers that manually create connected handles may continue using
  `cflow_io_native_backend_pipe_actor_ops()` unchanged.

Rollback removes the additive target, headers, and spawn symbol without
changing serialized data, existing ABI layouts, or current read/write behavior.

## Verification plan

### Windows named pipes

- client connects before `ConnectNamedPipe` (`ERROR_PIPE_CONNECTED` race);
- normal pending accept and endpoint transfer;
- cancellation before connection and cancel/connection race;
- capacity exhaustion and slot reuse after authoritative completion;
- missing and busy client connect without internal waiting;
- peer disconnect, server close with pending accepts, quiescent destroy;
- handle-count stability across repeated success/failure/cancel cycles.

### POSIX FIFO

- `mkfifo`, nonblocking reader open, writer-without-reader `SALTS_EPIPE`;
- reader then writer success and byte transfer;
- zero-byte read with no writers documented as request EOF, without implicit
  descriptor close;
- close/forget ordering on poll, kqueue, epoll, and io_uring where available;
- descriptor-count stability on success and error paths.

Linux tests run remotely on `root@eu`; macOS behavior is verified by CI.

### Subprocess stdio

- stdin/stdout/stderr success and partial transfers;
- stdout/stderr EOF after child close/exit and remaining-byte drain;
- child exit while reads are pending;
- cancellation races on all three streams;
- explicit stdin close delivers EOF;
- only the intended child handles/descriptors are inherited;
- spawn failure closes every temporary child and parent endpoint;
- terminate/timeout/close/destroy ordering and repeated resource-count checks;
- existing `test_salts_process` behavior remains passing, with additive
  borrowed and crossed-descriptor binding coverage.

### API and packaging

- C and C++ header compilation for both new headers;
- CMake install/export consumption of `Salts::CFlowProcess`;
- focused Debug/Release tests, adjacent native I/O/process regressions, then
  platform CI.
