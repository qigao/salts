# NativeIO execution and endpoint architecture

**Status:** canonical design contract for #471 / #473  
**Implementation trackers:** #474, #475, #476, #481  
**Related CNet work:** #472, #477–#480  
**NativeIPC/Pipe migration:** #168

This document freezes the ownership and layering contract for NativeIO.
The #474 Sharded/SMP runtime now provides a bounded fixed-shard execution
substrate plus owner-local shard-bound endpoint/request wrappers. Cross-shard
payload operation routing and reply/completion ownership remain tracked by
#474/#475.

## 1. Two independent dimensions

NativeIO execution style and application semantics are orthogonal.

```text
                     semantic consumers
              Raw / CNet / Reactive / Actor
                         |
                         v
                      NativeIO
          +--------------+---------------+
          |              |               |
       Direct         Coroutine       Sharded/SMP
      current          current       routing current
          |              |               |
          +--------------+---------------+
                         |
             epoll / io_uring / IOCP / kqueue
```

The three execution styles must use the same NativeIO endpoint/request slots
and the same authoritative terminal completion. They are not three I/O models.

Semantic consumers remain independent:

- **CNet** owns network/session semantics such as URI/DNS, TLS, connection
  lifecycle, timeouts and ordered callbacks.
- **Reactive** owns Publisher/Subscriber/Subscription demand, scheduling,
  cancellation and terminal semantics.
- **Actor** owns identity, mailbox, lifecycle and Machine/Statechart state.
- **Raw** callers may use NativeIO directly without any of those layers.

No consumer is mandatory for another unless its semantic contract is actually
needed.

## 2. NativeIO execution styles

### Direct

Direct is the mechanism and performance baseline.

```text
owner
  -> prepare / submit
  -> optional flush
  -> observe
  -> terminal completion
```

Properties:

- one owner thread per backend;
- zero message hop;
- no hidden worker;
- no payload copy required by NativeIO;
- payload/address storage follows the documented borrow until terminal
  observation;
- batching is explicit through prepare/flush/observe.

### Coroutine

Coroutine is structured control flow over the same owner and request slots.

```text
owner coroutine
  -> await / await_prepared
  -> suspend execution position
  -> owner observe
  -> NativeIO terminal completion
  -> resume
```

The coroutine frame is not a second I/O state machine. NativeIO request slots
remain authoritative for progress, cancellation and terminal result.
`await_prepared` exists so coroutine style can preserve backend batching; one
await must not imply one syscall.

### Sharded/SMP

Sharded/SMP is delivered incrementally by #474. The first executable slice
provides the bounded shared-nothing routing substrate; endpoint-bound I/O
routing remains a later slice of #474/#475.

```text
Shard 0                 Shard 1                 Shard N
owner + backend         owner + backend         owner + backend
local endpoints         local endpoints         local endpoints
      ^                       ^
      | bounded dispatch      |
      +-----------------------+
```

Frozen contract:

- one NativeIO backend owner per shard;
- endpoint affinity becomes fixed at/after attach or admission;
- a live endpoint does not migrate between shard owners;
- same-shard dispatch must be eligible for a direct zero-message-hop path;
- cross-shard dispatch uses a bounded explicit queue;
- no shared endpoint protected by a cross-core mutex;
- existing Coroutine Executor / Concurrency / Disruptor primitives are reused
  where suitable instead of creating another generic executor;
- routed message/reply state never becomes a second I/O terminal source.

Current routing checkpoint:

- `native_io_sharded` creates one fixed Coroutine Executor shard per NativeIO
  owner and initializes/destroys that shard's NativeIO backend on the owner;
- explicit `submit_to(shard,...)` routing copies one bounded task descriptor;
- same-shard nested routing executes directly with zero message hop;
- off-shard routing uses preallocated command slots plus the existing bounded
  Coroutine Executor queue;
- accepted task arguments remain borrowed through one exact finalizer edge, so
  higher layers can attach retained/move-owned payload tokens without making
  NativeIO depend on `Salts::Core`;
- rejected admission invokes no run/cancel/finalize callback and transfers no
  ownership;
- initialized steady-state routing allocates no command storage.

The current owner-local data plane adds runtime/shard-bound endpoint and request
wrappers without changing `native_io_endpoint` or `native_io_request`.
Attach, release, submit, prepare, cancel and observe must execute from the live
owner callback context. A wrapper naming another shard of the same runtime is
rejected with `SALTS_EPERM`; another runtime or stale raw generation is rejected
rather than forwarded or migrated.

Raw payload/address pointers still use the Direct borrow contract only after
execution has reached the endpoint owner shard.

The request-lifetime ownership checkpoint adds an explicit generic ownership
token to owner-local submit/prepare. Successful raw admission transfers that
token into preallocated per-shard storage keyed by the authoritative NativeIO
request slot/generation. Failed admission transfers nothing. Cancellation does
not release the token: only observation of the matching terminal completion
settles it. An optional terminal callback runs first while payload storage is
still valid, then one exact finalizer edge releases the token. The ownership
record is removed before callbacks so a callback may immediately submit new
work even when the raw request slot is reused. For a multi-completion observe,
ownership for the entire dequeued batch is detached into bounded preallocated
settlement scratch before the first callback runs; this prevents an immediate
reentrant submit from overwriting the ownership record of a later completion in
that same batch. Recursive observe from a terminal callback is rejected with
`SALTS_EBUSY`.

This ownership carrier is intentionally independent of `mem_buffer_t` and
`Salts::Core`. CNet may retain a `mem_buffer_t` and use the generic finalizer
to release that retain; another consumer may move-own storage instead.

The current cross-shard admission slice adds a one-call owned-operation route.
It copies only the bounded operation descriptor, explicit ownership token and
optional admission callback into preallocated per-shard route storage, then
dispatches only to `endpoint.owner_shard`. A rejected route transfers nothing.
Once the route is accepted, owner-local raw admission either transfers the token
again into the authoritative request slot or reports an admission/control error
and finalizes the route-owned token. The admission callback is not an I/O
completion and never becomes a second terminal truth source.

This slice still relies on owner-side observe to progress and settle raw
requests. Automatic owner progress, shutdown cancellation/drain and bounded
reply routing remain later #474/#475 work. No arbitrary raw borrowed pointer is
made cross-shard-safe by implication.

## 3. Endpoint/data-plane categories

NativeIO endpoint categories stay mechanism-oriented:

```text
STREAM
DATAGRAM
PIPE
```

Transport/address policy does not create new NativeIO endpoint kinds.

- TCP/IPv4/IPv6 are STREAM/DATAGRAM transports selected above NativeIO.
- Linux `AF_VSOCK SOCK_STREAM` is STREAM, not a VSOCK-specific NativeIO kind.
- Windows byte-mode overlapped Named Pipe is PIPE.
- POSIX pipe/FIFO is PIPE.

This prevents transport naming, security policy and rendezvous semantics from
leaking into the data-plane operation vocabulary.

## 4. Capability matrix

“Current” describes the existing Direct/Coroutine mechanism. “Target” is the
contract for #474/#475; it is not an implementation-status claim.

| Endpoint / transport | Control plane | NativeIO category | Direct | Coroutine | Sharded/SMP target | CNet |
| --- | --- | --- | --- | --- | --- | --- |
| TCP stream | socket / optional CNet | STREAM | current | current | target | optional session layer |
| UDP datagram | socket / optional CNet | DATAGRAM | current | current | target | optional session layer |
| Linux AF_VSOCK stream | socket / optional CNet | STREAM | current where backend supports it | current | target | optional session layer |
| Windows Named Pipe | NativeIPC | PIPE | current | current | target | optional session layer |
| POSIX pipe / FIFO | caller / NativeIPC | PIPE | current where backend supports it | current | target | optional session layer |
| file | caller / higher adapter | backend-specific I/O | backend-specific | possible through adapters | future/explicit | not a CNet transport |

Unsupported backend/resource combinations fail explicitly. No row authorizes a
backend fallback, transport fallback or hidden blocking worker.

## 5. NativeIPC boundary

NativeIPC is rendezvous/control-plane only.

It may:

- run the fixed-capacity Windows overlapped Named Pipe accept service;
- perform one-shot Windows Named Pipe client connect;
- open an existing POSIX FIFO nonblocking;
- report platform capability;
- transfer/close move-owned pipe endpoints.

It must not:

- own the steady-state payload read/write engine;
- create a second request/completion model for pipe data;
- silently switch to a worker, another backend or another transport.

A successful NativeIPC rendezvous produces a move-owned
`salts_ipc_pipe_endpoint`. Its native identity can then be attached to
NativeIO, after which NativeIO owns the data-plane operation/completion
contract while the endpoint wrapper retains native-handle close ownership as
documented.

## 6. Ownership and affinity

### Direct and same-shard

Existing NativeIO borrow rules remain valid: the descriptor is copied and
payload/address storage remains valid until the matching terminal completion is
observed.

### Cross-shard

Cross-shard dispatch must not implicitly carry an arbitrary borrowed pointer.
#475 may implement only explicit cross-thread-safe ownership forms, for example:

- retained immutable `mem_buffer_t`;
- move-owned storage/token;
- bounded inline small payload;
- another explicitly documented immutable cross-thread-safe owner.

A raw borrowed pointer is not a default cross-shard payload contract.

### Endpoint affinity

- sockets, VSOCK streams and pipe endpoints select an owner shard before or at
  NativeIO attach/admission;
- release/close is performed by the owning shard;
- wrong-shard/stale-generation operations fail deterministically;
- a live endpoint is never concurrently progressed by two backend owners.

The NativeIO request slot remains the sole authoritative I/O progress and
terminal-completion state.

## 7. VSOCK boundary

NativeIO sees VSOCK as a STREAM socket. It may execute STREAM_CONNECT,
STREAM_RECV and STREAM_SEND using a native `sockaddr_vm` supplied by an upper
layer, but it does not own:

- CID/port parsing;
- listener policy;
- URI syntax;
- session lifecycle;
- authentication/encryption policy.

CNet may provide `vsock://CID:PORT`, listener/accept/adopt, generation-checked
session state, timeout and close semantics. Unsupported hosts/backends fail
without TCP/Pipe fallback.

## 8. Pipe/FIFO boundary

NativeIPC or the caller performs rendezvous/open. NativeIO performs payload
I/O.

For Sharded/SMP execution, affinity is selected before or at attach. A Named
Pipe/FIFO endpoint must not move between live NativeIO owners.

Raw NativeIO PIPE remains available even when CNet, Reactive and Actor adapters
exist.

## 9. Semantic adapters

Adapters are thin translations, not new semantic authorities.

```text
NativeIO completion -> optional Reactive Publisher wake/value
NativeIO/CNet event  -> optional Actor domain message
CNet receive         -> optional Reactive Publisher
```

Reactive owns demand. Actor owns domain mailbox/lifecycle. CNet owns session
semantics. NativeIO owns I/O progress and terminal completion.

A NativeIO sharded routing message is an execution descriptor, not an Actor
message and not Reactive demand.

## 10. Shutdown and cancellation

All styles preserve the same authoritative terminal rule:

1. stop admission;
2. request cancellation or drain accepted work;
3. keep the owning backend progressing until every admitted NativeIO request
   reaches an observed terminal completion;
4. release/close native endpoint identity on its owner;
5. destroy the backend/executor only after quiescence.

Cancellation is a request, not a synthesized completion.

## 11. Performance contract

- Direct remains the mechanism baseline.
- Coroutine overhead is measured separately from backend/transport cost.
- Sharded same-owner and cross-owner paths are measured separately.
- Pipe/VSOCK/TCP/UDP NativeIO mechanism benchmarks do not require CNet.
- Small expected deltas use same-run/exact-head evidence.
- p95/tail regressions must not be hidden by median-only summaries.
- initialized steady-state paths remain bounded: no fallback, unbounded queue
  or hidden per-operation worker.

## 12. Non-goals

This architecture does not authorize:

- turning CNet into a Reactive runtime;
- turning NativeIO sharded routing into an Actor model;
- creating another generic coroutine/thread-pool executor;
- adding a VSOCK-specific NativeIO endpoint kind;
- making CNet mandatory for TCP, UDP, VSOCK, Pipe or FIFO;
- adding hidden backend/transport compatibility fallbacks;
- introducing a second source of I/O progress or terminal truth.
