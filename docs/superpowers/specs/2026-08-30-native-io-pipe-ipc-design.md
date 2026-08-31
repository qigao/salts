# NativeIO Pipe and IPC Boundary Design

Issue: [#168](https://github.com/qigao/turbo-utils/issues/168)

Related evidence: [#100](https://github.com/qigao/turbo-utils/issues/100),
[#105](https://github.com/qigao/turbo-utils/issues/105),
[#133](https://github.com/qigao/turbo-utils/issues/133), and
[#148](https://github.com/qigao/turbo-utils/issues/148).

## Decision

Root-level NativeIO becomes the only native byte-I/O data plane for connected
sockets and connected byte pipes. CFlow Actor and Source remain optional
adapters above NativeIO. Pipe creation and rendezvous move to a separate
NativeIPC control-plane surface in the same root module; they do not become
socket-shaped NativeIO operations.

The portable rule is:

> Unify ownership, bounded admission, cancellation, terminal completion, and
> shutdown. Specialize endpoint creation, system calls, readiness, and
> completion mechanics by resource and backend.

The implementation uses a Bridge between the stable NativeIO contract and the
four platform strategies. It does not introduce a universal kernel-event type,
a second Actor mailbox, a hidden worker, or an automatic backend fallback.

```text
direct caller | CFlow Actor | CFlow Source/Runtime
                         |
                         v
            NativeIO operation contract
             | connected stream socket
             | datagram socket
             | connected byte pipe
                         |
          +--------------+---------------+
          |              |               |
        IOCP          io_uring      epoll / kqueue
      completion      completion       readiness

NativeIPC control plane
  Windows named-pipe accept/connect
  POSIX FIFO open
             |
             +---- transfers a caller-owned byte-pipe endpoint
```

## Current repository facts

- `native-io/include/turbo/native_io.h` exposes only `attach_socket`,
  `release_socket`, TCP receive/send, and UDP receive-from/send-to.
- NativeIO already fixes endpoint, request, and completion-batch capacity at
  initialization and performs no data-path allocation after initialization.
- One NativeIO backend is a single-owner-thread object and creates no worker.
- The old CFlow native layer already proves byte-pipe read/write on IOCP,
  io_uring, epoll, kqueue, and poll, but it owns a second native request table
  and platform execution model.
- `cflow/io_pipe.h` separately proves Windows named-pipe rendezvous and POSIX
  FIFO open semantics.
- CFlow Actor already owns the MPSC admission ring, request lifecycle,
  completion delivery, acknowledgement, and release. NativeIO must not add a
  second Disruptor between Actor and the OS backend.

These facts make migration preferable to copying: NativeIO owns native
endpoint/request truth; Actor owns application request/delivery truth; the
adapter owns only the bounded correlation between them.

## Scope

### Included

- One-shot byte-pipe read and write through IOCP, io_uring, epoll, and kqueue.
- Windows overlapped byte-mode named pipes.
- POSIX anonymous pipes and FIFOs admitted by readiness backends only when
  already nonblocking.
- Existing Windows named-pipe accept/client-connect and POSIX FIFO-open
  behavior moved behind a root-level NativeIPC control plane.
- A NativeIO-to-CFlow Actor adapter and reuse of the existing windowed I/O
  Source for reactive Graph execution.
- Compatibility migration of existing CFlow pipe/rendezvous entry points.
- Direct NativeIO versus Actor/NativeIO versus Source/NativeIO correctness and
  performance evidence.

### Excluded

- Shared memory, semaphore, event, signal, futex, and condition-variable APIs.
  These synchronize or share state; they do not obey byte-I/O payload
  completion semantics.
- Message-mode named pipes, transactions, impersonation, ACL/security policy,
  handle passing, peer credentials, framing, serialization, or RPC.
- A fake portable accept/connect operation shared by named pipes, FIFOs, and
  sockets. Their ownership and rendezvous semantics differ.
- Unix-domain socket naming and lifecycle in this increment. Connected Unix
  stream sockets can be admitted only after a separate additive API gives
  `TURBO_IO_TCP_RECV/SEND` an honest connected-stream spelling.
- Regular files, vectored I/O, TCP accept/connect, or advanced socket messages.
  Existing CFlow implementations remain until root NativeIO has independently
  specified and tested parity for those capabilities.
- Per-request threads, blocking workers, implicit backend downgrade, and
  application-level write-all/read-exactly loops.

## Public NativeIO contract

Existing socket symbols and numeric enum values remain unchanged. Pipe support
is additive:

```c
typedef enum turbo_io_operation_kind {
  TURBO_IO_TCP_RECV = 1,
  TURBO_IO_TCP_SEND = 2,
  TURBO_IO_UDP_RECV_FROM = 3,
  TURBO_IO_UDP_SEND_TO = 4,
  TURBO_IO_PIPE_READ = 5,
  TURBO_IO_PIPE_WRITE = 6
} turbo_io_operation_kind;

typedef enum turbo_io_pipe_endpoint_flags {
  TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE = 1u << 0
} turbo_io_pipe_endpoint_flags;

bool native_io_pipe_supported(turbo_io_backend_kind kind);

int native_io_attach_pipe(turbo_io_backend *backend,
                                 uintptr_t native_handle,
                                 uint32_t flags,
                                 turbo_io_endpoint *out_endpoint);

int native_io_release_pipe(turbo_io_backend *backend,
                                  turbo_io_endpoint endpoint);
```

Pipe operations reuse `turbo_io_operation` and the existing
`native_io_submit()`/`observe()` functions. Unlike the legacy CFlow
descriptor, the NativeIO descriptor already names a generation-checked
endpoint rather than a raw socket and contains no accept-result ownership.
For PIPE_READ/PIPE_WRITE, every address field must be zero. This preserves one
submit/cancel/observe state machine and avoids a parallel pipe request API.

`turbo_io_endpoint` remains the existing two-word `{slot, generation}` public
handle. Resource kind belongs to the backend endpoint record, so adding pipe
support does not change the public handle layout. `release_socket` rejects a
pipe endpoint and `release_pipe` rejects a socket endpoint as stale/resource
mismatches without releasing either record.

`TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE` is an explicit caller assertion about
how the native resource was created:

- Windows requires a byte-mode named-pipe handle opened or created with
  `FILE_FLAG_OVERLAPPED`. Windows cannot recover the creation flag reliably,
  so a false assertion violates the API precondition; attach still validates
  pipe type and byte mode where the granted handle rights permit it.
- epoll/kqueue independently verify `O_NONBLOCK` using `fcntl`; the flag never
  authorizes NativeIO to mutate caller descriptor flags.
- io_uring accepts a valid pipe/FIFO descriptor but still requires the flag so
  one endpoint contract remains portable across explicitly selected backends.
- Windows anonymous handles returned by `CreatePipe` are not async-capable and
  return `TURBO_ENOTSUP` rather than falling back to a worker.

The backend borrows the native handle and never closes it. The caller stops
submission, observes every terminal request, closes the native handle, and
then calls the matching release function.

## Internal endpoint and request model

Each backend endpoint record adds an internal discriminator:

```c
typedef enum turbo_io_resource_kind {
  TURBO_IO_RESOURCE_SOCKET = 1,
  TURBO_IO_RESOURCE_BYTE_PIPE = 2
} turbo_io_resource_kind;
```

The discriminator is not exposed in `turbo_io_endpoint`; it is checked on
every attach, submit, and release. The same live native identity cannot be
attached twice, even under different resource kinds. Endpoint generation is
incremented on reuse, and a stale endpoint never aliases a new descriptor or
handle that happens to reuse the same numeric value.

The fixed request record copies the operation descriptor and retains the
endpoint handle. Payload storage is not copied:

- PIPE_WRITE payload is immutable from successful submit until its completion
  is returned by `observe`.
- PIPE_READ payload is exclusively mutable by NativeIO for the same interval.
- Returning a terminal completion ends the NativeIO borrow and invalidates the
  request handle.
- A CFlow adapter may impose a longer Actor-level borrow through delivery and
  acknowledgement; NativeIO performs no access after observation.

Per endpoint, read and write lanes remain independent FIFO queues. A pipe read
cannot overtake an earlier pipe read on the same endpoint; the same is true for
writes. Read and write may progress independently.

## Backend strategy matrix

| Backend | Model | Pipe primitive | Admission checks | Cancellation evidence |
|---|---|---|---|---|
| IOCP | completion | overlapped `ReadFile` / `WriteFile` | async-capable byte-mode named pipe | `CancelIoEx`; completion packet remains authoritative |
| io_uring | completion | `IORING_OP_READ` / `IORING_OP_WRITE` | valid retained pipe/FIFO fd | CQE including `-ECANCELED` |
| epoll | readiness | `read` / guarded `write` | fd is `O_NONBLOCK` | queued removal or terminal retry result |
| kqueue | readiness | `read` / guarded `write` | fd is `O_NONBLOCK` | queued removal or terminal retry result |

The readiness implementation derives kernel interest from non-empty endpoint
lanes. Interest is a cache of request state, not a second fact source. A ready
event drives only a bounded completion batch and rearms when the lane head
would block again.

POSIX pipe writes suppress process-visible `SIGPIPE` using the existing
readiness guard. `EPIPE` becomes a FAILED completion carrying `TURBO_EPIPE` or
the normalized negative native error selected by the existing NativeIO error
policy. No signal is consumed if it was already pending before the write.

## Completion and error normalization

| Condition | API result |
|---|---|
| malformed operation, unknown flags, nonzero pipe address fields | `TURBO_EINVAL` before native admission |
| unsupported backend/resource pair | `TURBO_ENOTSUP` |
| stale endpoint or request | `TURBO_ENOENT` |
| endpoint/request capacity exhausted | `TURBO_ENOBUFS` |
| admission closed | `TURBO_ESHUTDOWN` |
| observe deadline without terminal packet | `TURBO_ETIMEDOUT`, count zero |
| pipe read returns positive bytes | `TURBO_IO_COMPLETION_OK` |
| pipe read returns zero / broken peer read EOF | `TURBO_IO_COMPLETION_EOF` |
| pipe write transfers a partial positive prefix | `TURBO_IO_COMPLETION_OK` with actual bytes |
| broken pipe write | `TURBO_IO_COMPLETION_FAILED` with broken-pipe status |
| authoritative cancellation | `TURBO_IO_COMPLETION_CANCELLED` |
| other native terminal error | `TURBO_IO_COMPLETION_FAILED` with native status |

Submit failure rolls its request slot back to FREE and produces no completion.
After successful submit, exactly one terminal completion must be observed.
Cancel success only marks or submits cancellation; it does not end any borrow.

## NativeIPC control plane

NativeIPC is a separate installed header, `turbo/native_ipc.h`, within the
NativeIO target. It creates or accepts byte-pipe resources and transfers their
ownership; it does not submit payload operations.

The first public surface preserves already-tested CFlow semantics under root
names. The complete initial type vocabulary is:

```c
typedef uint64_t turbo_ipc_request_id;

typedef enum turbo_ipc_pipe_direction {
  TURBO_IPC_PIPE_READ = 1u,
  TURBO_IPC_PIPE_WRITE = 2u,
  TURBO_IPC_PIPE_DUPLEX = 3u
} turbo_ipc_pipe_direction;

typedef enum turbo_ipc_pipe_capability {
  TURBO_IPC_WINDOWS_SERVER_ACCEPT = 0,
  TURBO_IPC_WINDOWS_CLIENT_CONNECT,
  TURBO_IPC_POSIX_FIFO_OPEN
} turbo_ipc_pipe_capability;

typedef struct turbo_ipc_pipe_endpoint {
  uintptr_t handle;
  uint32_t native_io_flags;
} turbo_ipc_pipe_endpoint;

typedef struct turbo_ipc_pipe_server {
  void *impl;
} turbo_ipc_pipe_server;

typedef enum turbo_ipc_completion_kind {
  TURBO_IPC_COMPLETION_OK = 1,
  TURBO_IPC_COMPLETION_CANCELLED,
  TURBO_IPC_COMPLETION_FAILED
} turbo_ipc_completion_kind;

typedef struct turbo_ipc_completion {
  turbo_ipc_request_id request_id;
  turbo_ipc_completion_kind kind;
  int status;
  uint32_t native_status;
} turbo_ipc_completion;

typedef void (*turbo_ipc_pipe_accept_fn)(
    void *user,
    const turbo_ipc_completion *completion,
    turbo_ipc_pipe_endpoint endpoint);

typedef struct turbo_ipc_pipe_server_config {
  const char *name;
  turbo_ipc_pipe_direction direction;
  size_t request_capacity;
  size_t input_buffer_size;
  size_t output_buffer_size;
  turbo_ipc_pipe_accept_fn completion;
  void *completion_user;
} turbo_ipc_pipe_server_config;

typedef struct turbo_ipc_pipe_server_stats {
  size_t request_capacity;
  size_t active_requests;
  uint64_t submitted;
  uint64_t completed;
  uint64_t cancelled;
  uint64_t failed;
  uint64_t rejected_full;
  bool admission_open;
} turbo_ipc_pipe_server_stats;

bool turbo_ipc_pipe_capability_supported(
    turbo_ipc_pipe_capability capability);
void turbo_ipc_pipe_endpoint_init(turbo_ipc_pipe_endpoint *endpoint);
bool turbo_ipc_pipe_endpoint_valid(const turbo_ipc_pipe_endpoint *endpoint);
int turbo_ipc_pipe_endpoint_close(turbo_ipc_pipe_endpoint *endpoint);

int turbo_ipc_pipe_server_init(turbo_ipc_pipe_server *server,
                               const turbo_ipc_pipe_server_config *config);
int turbo_ipc_pipe_server_try_accept(turbo_ipc_pipe_server *server,
                                     turbo_ipc_request_id *out_request_id);
int turbo_ipc_pipe_server_cancel(turbo_ipc_pipe_server *server,
                                 turbo_ipc_request_id request_id);
int turbo_ipc_pipe_server_observe(turbo_ipc_pipe_server *server,
                                  size_t max_events,
                                  size_t *out_count);
int turbo_ipc_pipe_server_close(turbo_ipc_pipe_server *server);
bool turbo_ipc_pipe_server_is_quiescent(
    const turbo_ipc_pipe_server *server);
bool turbo_ipc_pipe_server_get_stats(
    const turbo_ipc_pipe_server *server,
    turbo_ipc_pipe_server_stats *out_stats);
int turbo_ipc_pipe_server_destroy(turbo_ipc_pipe_server *server);

int turbo_ipc_named_pipe_connect(const char *name,
                                 turbo_ipc_pipe_direction direction,
                                 turbo_ipc_pipe_endpoint *out_endpoint);
int turbo_ipc_fifo_open(const char *path,
                        turbo_ipc_pipe_direction direction,
                        turbo_ipc_pipe_endpoint *out_endpoint);
```

Windows server accept is fixed-capacity and owner-driven. A successful observe
transfers exactly one connected endpoint to the configured callback; failed or
cancelled slots remain server-owned and are closed by the server. Client
connect is one synchronous `CreateFile` attempt: missing and busy are returned,
and NativeIPC does not call `WaitNamedPipe` or retry.

POSIX FIFO open is one synchronous nonblocking control-plane operation. Writer
open with no reader maps `ENXIO` to `TURBO_EPIPE`. FIFO has no portable accept
object, so its server APIs return `TURBO_ENOTSUP` on POSIX. Path creation,
unlink policy, permissions, and sandbox/security policy remain caller-owned.

The endpoint wrapper is move-only by contract despite C's copy syntax. Close
first invalidates the wrapper, then closes the native identity. On success,
`native_io_flags` contains `TURBO_IO_PIPE_ENDPOINT_ASYNC_CAPABLE` and can be
passed directly to `native_io_attach_pipe()`.

## CFlow Actor and Source adapter

CFlow adds a thin NativeIO adapter; it does not call IOCP, epoll, kqueue,
io_uring, `recv`, `send`, `read`, or `write` directly.

```c
typedef struct cflow_io_native_adapter {
  void *impl;
} cflow_io_native_adapter;

typedef struct cflow_io_native_adapter_config {
  turbo_io_backend_config backend;
} cflow_io_native_adapter_config;

typedef struct cflow_io_native_adapter_stats {
  turbo_io_backend_stats native;
  size_t active_bridges;
  uint64_t actor_completions;
  uint64_t stale_actor_completions;
} cflow_io_native_adapter_stats;

int cflow_io_native_adapter_init(
    cflow_io_native_adapter *adapter,
    const cflow_io_native_adapter_config *config);

cflow_io_backend_ops cflow_io_native_adapter_actor_ops(void);

int cflow_io_native_adapter_attach_socket(
    cflow_io_native_adapter *adapter,
    uintptr_t native_socket,
    turbo_io_endpoint *out_endpoint);
int cflow_io_native_adapter_attach_pipe(
    cflow_io_native_adapter *adapter,
    uintptr_t native_handle,
    uint32_t flags,
    turbo_io_endpoint *out_endpoint);
int cflow_io_native_adapter_release_socket(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint);
int cflow_io_native_adapter_release_pipe(
    cflow_io_native_adapter *adapter,
    turbo_io_endpoint endpoint);

int cflow_io_native_adapter_observe(cflow_io_native_adapter *adapter,
                                    uint32_t timeout_ms,
                                    size_t *out_completed);
int cflow_io_native_adapter_close(cflow_io_native_adapter *adapter);
int cflow_io_native_adapter_destroy(cflow_io_native_adapter *adapter);
bool cflow_io_native_adapter_get_stats(
    const cflow_io_native_adapter *adapter,
    cflow_io_native_adapter_stats *out_stats);
```

`cflow_io_native_adapter_stats` embeds a copied
`turbo_io_backend_stats native`, then reports `active_bridges`,
`actor_completions`, and `stale_actor_completions`. The Actor ops consume a
caller-owned `turbo_io_operation *` as `operation_user`; the Actor operation
token retains it through terminal delivery and acknowledgement.

The adapter owns one NativeIO backend plus a fixed bridge table sized from
`backend.request_capacity`. It binds to one `cflow_io_actor` because the
existing Actor cancel callback carries a request ID but no Actor identity.
A second Actor submit fails fast with `TURBO_EINVAL`; sharing one adapter across
Actors would make duplicate per-Actor request IDs ambiguous.

Actor producers continue to publish commands through the Actor's existing
bounded MPSC Disruptor. The single Actor driver translates one admitted
`turbo_io_operation` into NativeIO submit. Adapter `observe` maps completion
`user_data` back to its bridge slot and calls `cflow_io_actor_complete()`.
There is no Actor-to-NativeIO queue beyond the Actor mailbox and no
NativeIO-to-Actor queue beyond NativeIO's bounded completion batch.

```text
Actor bridge slot:

FREE
  --Actor backend submit / NativeIO accepted-->
NATIVE_PENDING
  --cancel request--> NATIVE_PENDING(cancel requested)
  --NativeIO terminal observed / Actor completion accepted-->
FREE
```

If NativeIO submit fails, the bridge slot is released before returning the
error to Actor, and NativeIO produces no completion. Once NativeIO accepts the
request, the slot cannot be reused until a terminal is observed. Actor stale or
closed completion is counted, the NativeIO borrow still ends, and the bridge
slot is released exactly once.

The adapter's submit, cancel, observe, endpoint, and lifecycle operations run
on one fixed owner thread. Producer threads call only Actor `try_submit`.
Existing `cflow_source_from_io_actor_windowed()` remains the reactive adapter;
its owner drive callback invokes NativeIO observe and Actor/Executor progress.
Graph/Stream never owns the native endpoint or backend.

## Capacity and backpressure

All variable storage is allocated during initialization:

```text
endpoint records          = endpoint_capacity
NativeIO request records  = request_capacity
Actor bridge records      = request_capacity
native completion batch   = completion_batch_capacity
IPC accept slots          = ipc_request_capacity
```

Required checked constraints are:

```text
endpoint_capacity > 0
request_capacity > 0
0 < completion_batch_capacity <= request_capacity
each capacity <= UINT32_MAX
capacity * record_size does not overflow SIZE_MAX
```

Full admission returns `TURBO_ENOBUFS`; there is no spin, wait-for-space,
unbounded allocation, overwrite, drop, or blocking fallback. Configuration is
a memory and descriptor budget, not a throughput promise. Benchmarks must
report rejections and peak retained resources alongside throughput.

## State ownership and linearization

Native endpoint/request slots are the only native I/O fact source. Actor slots
are the application command/delivery fact source. Adapter bridge slots are a
bounded correlation index and never independently declare I/O completion.

```text
Native endpoint:
FREE --attach--> ACTIVE --all requests observed + native close + release--> FREE

Native request:
FREE --submit accepted--> PENDING --terminal observe--> FREE(next generation)
                              |
                              +--cancel requested--+

IPC accept slot:
FREE -> CONNECT_PENDING -> READY_SUCCESS -> TRANSFERRED/FREE
                       |-> READY_FAILED ----> CLOSED/FREE
                       `-> CANCEL_PENDING -> READY_CANCELLED -> CLOSED/FREE
```

Linearization points:

- attach: endpoint record changes from FREE to ACTIVE;
- submit: request record changes from FREE to PENDING;
- cancel: cancellation request is accepted, not terminal publication;
- terminal: observe removes exactly one terminal event and invalidates the
  request generation;
- Actor bridge release: terminal NativeIO event has been offered exactly once
  to Actor completion;
- IPC transfer: the success callback becomes endpoint owner before invocation.

## Shutdown protocol

Direct NativeIO shutdown is:

1. stop external producers;
2. call `native_io_close()` to stop admission;
3. request cancellation or allow accepted requests to finish;
4. call `observe()` until `active_requests == 0`;
5. close each caller-owned socket/pipe native identity;
6. call the matching endpoint release function;
7. call `native_io_destroy()`.

CFlow shutdown adds delivery acknowledgement:

1. stop Actor producers and close Actor admission;
2. drive Actor commands so accepted work reaches NativeIO;
3. close NativeIO admission and cancel/drain NativeIO requests;
4. observe terminals and drive Actor/Executor delivery;
5. acknowledge every delivered Actor completion and release operation tokens;
6. destroy Actor/Source/Run state;
7. close native identities, release NativeIO endpoints, and destroy adapter;
8. destroy Executor after no completion task can be posted.

NativeIPC server shutdown stops accept admission, requests cancellation,
observes authoritative accept completions, invokes terminal callbacks, and
destroys only when all server-owned slots are free. It never closes endpoints
already transferred to callers.

## Compatibility and migration

Migration is incremental and source-compatible:

1. Add NativeIO pipe endpoint/operation support and shared platform tests.
2. Add NativeIPC control-plane APIs with behavior parity to `cflow/io_pipe.h`.
3. Add the CFlow NativeIO adapter for scalar socket and pipe operations.
4. Change the owner-driven CFlow rendezvous facade to delegate to NativeIPC
   while preserving its public structures, errors, callbacks, and ownership
   rules.
5. Migrate in-tree pipe data callers to the owner-driven NativeIO adapter and
   mark the autonomous legacy pipe backend as superseded without changing its
   behavior.
6. Remove the autonomous legacy pipe API, syscalls, workers, request tables,
   and platform branches only after #147 authorizes the public removal and all
   known callers have migrated.
7. Leave legacy vector/file/accept/connect code in place until separate
   NativeIO parity work authorizes its migration; do not disguise incomplete
   migration by deleting still-used behavior.

The root dependency remains one-way:

```text
CFlow -> NativeIO -> Platform
```

NativeIO never links CFlow, Actor, Graph, Executor, or TurboSTL. Existing
NativeIO socket callers do not need to migrate. Rollback before CFlow migration
removes additive pipe/control-plane APIs; rollback during migration restores
the compatibility facade implementation without changing caller data.

The legacy `cflow_io_native_backend` currently promises autonomous completion
through backend workers, while the new adapter promises caller-owned progress.
Changing old callers to require `observe()` would be a public behavior break.
Making the worker own NativeIO would require a second cross-thread command
queue for submit/cancel; allowing Actor and worker threads to alternate calls
would violate NativeIO's fixed-owner contract. Both alternatives are rejected.
The legacy data backend therefore remains behaviorally unchanged and
deprecated while in-tree callers migrate. New adapter and Source paths create
no worker. Removing the legacy worker and platform Pipe code requires the
public-API removal gate in #147; until that gate, #168 remains open rather than
claiming one physical implementation prematurely.

## Alternatives rejected

### Treat pipe handles as sockets

Rejected. IOCP named pipes use `ReadFile`/`WriteFile`, while POSIX pipes use
`read`/`write`; socket `WSARecv`/`recv` is not a valid resource-neutral path.

### Give Actor and Reactor separate Pipe APIs

Rejected. Actor owns admission/delivery and readiness/completion backends own
mechanics. Separate public APIs would duplicate operation, cancellation, and
shutdown semantics without adding capability.

### Put rendezvous into `turbo_io_operation`

Rejected. Windows server accept mutates a pre-created instance, Windows client
connect creates a handle, and POSIX FIFO has no connection object. A payload
descriptor cannot truthfully express all three ownership models.

### Add a second Disruptor between Actor and NativeIO

Rejected. Actor already provides bounded MPSC command admission and NativeIO is
single-owner. Another ring adds capacity, acknowledgement, and shutdown state
without removing a cross-thread boundary.

### Implement blocking fallback with a thread or coroutine pool

Rejected. It changes resource capacity, cancellation, affinity, scheduling, and
latency semantics. Unsupported resources return `TURBO_ENOTSUP`; a future
worker-backed adapter must be explicitly named and independently bounded.

### Place shared memory and synchronization objects in NativeIO

Rejected. Their data unit and lifecycle are mappings or synchronization state,
not borrowed byte buffers completed by `observe`.

## Verification and benchmark gates

Correctness precedes performance:

- C and C++ public header compilation;
- invalid flags, invalid operation shapes, resource-kind mismatch, stale
  handles, capacity, close, and destroy preconditions;
- real pipe read/write, partial transfer, EOF, broken peer, read/write lane
  ordering, cancellation races, and identity reuse;
- Windows overlapped named-pipe accept/connect/read/write and rejection of
  unsupported handles;
- Linux epoll/io_uring and macOS kqueue runtime coverage;
- repeated success/failure/cancel/shutdown resource-count stability;
- Actor completion, Executor delivery, acknowledgement, Source WAIT/wake,
  cancellation, and Run close ordering;
- install/export consumption of `TurboUtils::NativeIO` and
  `TurboUtils::CFlow`.

Performance reports compare only equivalent semantic paths on the same commit:

1. direct NativeIO;
2. Actor over NativeIO;
3. windowed Source over Actor/NativeIO.

TCP/UDP and pipe/IPC tables remain separate. Pipe tables group payload sizes
and report latency p50/p95/p99, operations/s, MiB/s, process CPU, rejections,
native errors, stale completions, submit time, observe time, Actor transition
time, Executor delivery time, and acknowledgement time. No performance
threshold is accepted without same-machine evidence and an issue-level
hypothesis.

## Documentation sources

- Current root contract: `native-io/include/turbo/native_io.h`
- Current NativeIO implementation notes: `native-io/README.md`
- Legacy pipe data semantics:
  `docs/superpowers/specs/2026-08-25-cflow-native-pipe-design.md`
- Legacy rendezvous/process ownership:
  `docs/superpowers/specs/2026-08-28-cflow-pipe-rendezvous-subprocess-design.md`
- [Microsoft synchronous and overlapped pipe I/O](https://learn.microsoft.com/windows/win32/ipc/synchronous-and-overlapped-input-and-output)
- [Microsoft anonymous pipe operations](https://learn.microsoft.com/windows/win32/ipc/anonymous-pipe-operations)
- [Linux io_uring userspace API](https://www.kernel.org/doc/html/latest/userspace-api/io_uring.html)
- [POSIX `read`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/read.html),
  [`write`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/write.html),
  and [`mkfifo`](https://pubs.opengroup.org/onlinepubs/9799919799/functions/mkfifo.html)
