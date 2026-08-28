# CFlow Advanced Native Socket Capability Decision

Issue: [#132](https://github.com/qigao/turbo-utils/issues/132)
Parent tracker: [#100](https://github.com/qigao/turbo-utils/issues/100)

## Decision

Do not expand the portable native socket API in this issue:

- **Vectored TCP send/receive is deferred.** It is implementable on every
  supported host, but no repository consumer or profile currently justifies a
  new public operation ABI, segment bound, and partial-transfer cursor contract.
- **Raw UDP ancillary buffers are rejected from the portable API.** A future
  API may expose a normalized, typed subset of packet information, ECN, and
  timestamps, but it must not expose `cmsghdr`, `WSACMSGHDR`, or backend flags.
- **UDP batching is rejected as a portable operation semantic.** A future
  backend may coalesce already-independent bounded requests only when profiling
  proves it useful and every request retains its own authoritative completion.

The installed `cflow_io_native_operation`, `cflow_io_completion`, backend
configuration, capability queries, and six socket operation kinds remain
unchanged. No fallback backend or blocking worker is added.

## Repository evidence

The current public and runtime model has one fact source per request:

- `cflow/include/cflow/io_native.h` defines one payload pointer and length, one
  host-native address region, and one accepted-socket result. Its documented
  borrow lasts from successful Actor submission through terminal callback
  return.
- `cflow/include/cflow/io_actor.h` publishes only completion kind, total bytes,
  and one error. It has no per-message result or payload/control truncation
  channel.
- IOCP records retain one borrowed operation pointer and one `WSABUF`; io_uring
  records retain one operation pointer and synthesize one `iovec`/`msghdr` for
  UDP; readiness records retain one operation pointer and call
  `recv`/`send`/`recvfrom`/`sendto`.
- `cflow/tests/cflow_io_native_test.c` applies the same TCP/UDP completion,
  cancellation, capacity, and endpoint-forget contract across available
  backends.
- `cflow/benchmarks/cflow_network_benchmark.c` uses positional aggregate
  initialization for the installed socket operation and explicitly owns a copy
  of each UDP address. Growing the public struct would change the installed C
  ABI even where a source rebuild zero-initialized new trailing fields.
- `completion_batch_capacity` already bounds how many independent completions a
  worker drains in one turn. It is not a UDP multi-message operation and does
  not merge request identity.

Therefore advanced message features cannot be represented as a small backend
switch. They require new public data and result contracts plus bounded retained
metadata in every request slot.

## Platform feasibility

Feasible does not mean admitted. The table records the primitive a separate
future proposal could use; an unavailable runtime primitive must fail explicitly
rather than route to another backend.

| Backend | Vectored TCP | UDP ancillary | UDP multi-message opportunity |
|---|---|---|---|
| IOCP | `WSARecv`/`WSASend` accept a bounded `WSABUF` array | provider-specific `WSARecvMsg`/`WSASendMsg` extension functions obtained at runtime | keep multiple overlapped requests independent; a Registered I/O redesign would be a separate backend |
| io_uring | vector or message SQEs | `IORING_OP_RECVMSG`/`IORING_OP_SENDMSG` | submit multiple independent SQEs; advanced multishot/buffer-ring receive requires a separate registered-buffer ownership protocol |
| epoll | readiness followed by `readv`/`writev` or `recvmsg`/`sendmsg` | POSIX `recvmsg`/`sendmsg` control data | Linux `recvmmsg`/`sendmmsg`, with partial-prefix and lost-later-error semantics |
| kqueue | readiness followed by `readv`/`writev` or `recvmsg`/`sendmsg` | Darwin `recvmsg`/`sendmsg` control data | Darwin `recvmsg_x`/`sendmsg_x`, whose ABI and results differ from Linux `mmsghdr` |
| poll | host readiness followed by the same host vector/message calls | host `recvmsg`/`sendmsg` when its normalized capability exists | no portable primitive: Linux and Darwin extensions must be selected explicitly |

Primary platform references:

- Microsoft documents scatter/gather arrays and overlapped buffer lifetime for
  [`WSASend`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasend).
- Microsoft documents optional receive control data and runtime extension lookup
  for
  [`WSARecvMsg`](https://learn.microsoft.com/en-us/windows/win32/api/mswsock/nc-mswsock-lpfn_wsarecvmsg),
  optional send control data in
  [`WSASendMsg`](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-wsasendmsg),
  and both provider extension identifiers in
  [Winsock IOCTLs](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls).
- POSIX specifies the vector and control-data shape for
  [`recvmsg`](https://man7.org/linux/man-pages/man3/recvmsg.3p.html), while
  Linux documents `MSG_TRUNC` and `MSG_CTRUNC` in its
  [`recvmsg` implementation](https://man7.org/linux/man-pages/man2/recvmsg.2.html).
- Linux documents `sendmmsg` as a Linux extension, including bounded vector
  count, partial-prefix results, and the possibility that a later error is not
  returned by the call:
  [`sendmmsg(2)`](https://man7.org/linux/man-pages/man2/sendmmsg.2.html).
- Apple's XNU syscall table defines the distinct Darwin
  [`recvmsg_x` and `sendmsg_x` calls](https://github.com/apple-oss-distributions/xnu/blob/main/bsd/kern/syscalls.master).

## Required ownership protocol for any reopened proposal

No item below is a current public API. These are admission requirements that
prevent a future implementation from weakening the existing lifetime model.

| Object | Required ownership and lifetime |
|---|---|
| operation or message wrapper | caller-owned; borrowed only after successful submission and through terminal callback return |
| vector descriptor array | caller-owned immutable shape for the same borrow; a backend copy of descriptors must not shorten payload lifetimes |
| send payload segments | caller-owned immutable bytes through terminal callback return |
| receive payload segments | caller-owned, backend-exclusive mutable bytes through terminal callback return |
| destination address | caller-owned immutable host address through send completion |
| source address result | caller-owned, backend-exclusive mutable storage through receive completion |
| ancillary input | caller-owned immutable normalized values through send completion |
| ancillary output/result | caller-owned, backend-exclusive normalized storage through receive completion; validity bits distinguish absent values |
| batch message array | caller-owned stable shape through completion of every accepted member; each member retains a distinct request identity and terminal result |

Rejected submission leaves every object under immediate caller control.
Cancellation is only a request and never ends a borrow early. Closing a socket,
resizing a segment container, reusing a batch entry, or releasing any payload,
address, or result before terminal callback return violates the contract.

## Bounds and checked arithmetic

A future vector proposal must add an explicit nonzero segment bound rather than
trust a platform constant. Admission must check each segment pointer/length and
the accumulated length before addition; the portable transfer total must remain
within the current `UINT32_MAX` ceiling unless a separate ABI deliberately
changes that ceiling. A zero-length segment policy must be explicit. Exceeding
the configured or host bound fails before native submission; the backend must
not split an operation silently.

If a backend copies descriptors, retained storage is calculated as:

```text
request_capacity * max_segments * sizeof(portable_segment)
    + backend request and address/control metadata
```

Every multiplication and addition is checked against `SIZE_MAX` before
allocation. A future batch proposal likewise needs a positive `max_messages`
hard bound and must count payload-retention exposure, address/control storage,
and result metadata, not only array slots.

## Candidate-specific semantics and reopen gates

### Vectored TCP send and receive: deferred

A one-shot completion reports a byte count over the logical concatenation of
segments. Partial send/receive is terminal for that request; CFlow does not
advance a hidden cursor or resubmit. The caller derives the next segment and
offset from the returned byte count. This preserves current one-shot TCP
semantics and avoids a second mutable cursor fact source.

Reopen only when both conditions hold:

1. a concrete CFlow consumer otherwise copies non-contiguous TCP data or issues
   materially more syscalls; and
2. a representative profile attributes at least 20% of total workload time to
   that copy/submission path, followed by a direct single-buffer versus vector
   benchmark on Windows, Linux, and macOS.

The proposal should use a separate versioned/vector operation type rather than
grow `cflow_io_native_operation`. It must include C and C++ header coverage,
overflow/count validation, partial-boundary tests, cancellation tests, and
parity tests for all enabled backends before claiming a performance benefit.

### UDP ancillary data: raw form rejected, normalized form deferred

Raw control buffers are not portable data. Header layout, alignment macros,
socket-option prerequisites, timestamp clocks, packet-info structures, ECN
extraction, and truncation flags differ by host and provider. Exposing raw bytes
would leak backend types and would make one portable result mean different
things.

A reopened proposal must name a typed normalized subset and validity bits. At a
minimum it must distinguish payload truncation from ancillary truncation and
must never report missing metadata as zero-valued metadata. If a backend cannot
report a requested field or exact truncation meaning, capability discovery or
submission fails explicitly. No backend guesses, drops requested metadata, or
switches to plain `recvfrom`.

Reopen when one concrete consumer requires packet destination/interface,
received ECN, or a specified timestamp clock on at least two supported host
families. The proposal must document required socket options, clock semantics,
address and result lifetimes, Windows provider discovery, and exact tests for
absent data, short control capacity, payload truncation, cancellation, and each
supported address family.

### UDP batching: portable semantic rejected

Linux `sendmmsg`/`recvmmsg`, Darwin `sendmsg_x`/`recvmsg_x`, multiple IOCP
overlapped operations, and multiple io_uring SQEs do not share one partial-error
or completion model. A public batch completion would also conflict with the
Actor's one accepted request, one release, and one terminal result invariant.

Reopen a backend-internal optimization only when a production-like UDP profile
shows native submission or receive syscalls consume at least 20% of total
workload time and the existing bounded multi-request window is saturated. Such
coalescing must preserve individual request IDs, FIFO rules where promised,
cancellation, errors, releases, and completion callbacks. It must have a fixed
batch bound and a non-batched A/B benchmark. If an application instead needs a
batch-visible API, that is a separate versioned contract with per-message
results; it is not an extension of `cflow_io_completion`.

## Compatibility, migration, and rollback

This decision changes no public behavior, data format, dependency, or build
configuration. Existing TCP/UDP callers, aggregate initializers, cancellation,
statistics, and shutdown remain authoritative. DNS, TLS, bind/listen setup,
multicast membership, and socket-option policy remain above or beside the native
operation layer.

Rollback removes this record and its README link; no code or data migration is
required. A future accepted proposal supersedes only the relevant candidate
section and must carry its own implementation and cross-platform verification
plan.
