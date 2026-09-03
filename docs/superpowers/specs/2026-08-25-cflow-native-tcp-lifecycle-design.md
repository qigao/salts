# CFlow Native TCP Lifecycle Design

## Context

`cflow_io_native_operation` currently covers connected TCP send/receive and UDP
send/receive only. Applications must leave the CFlow runtime to accept a TCP
peer or establish a connection, so the native backend abstraction is not yet a
complete TCP lifecycle boundary. Issue #101 tracks the first bounded increment:
native asynchronous TCP accept and connect on IOCP, io_uring, epoll, and kqueue.

This change is additive. Pipe, file, device, USB, generic `poll`, DNS, TLS, and
connection-policy abstractions remain in parent issue #100.

## Public Contract

Append `CFLOW_IO_NATIVE_TCP_ACCEPT` and `CFLOW_IO_NATIVE_TCP_CONNECT` to
`cflow_io_native_operation_kind`; existing enumerator values do not change.
Append `result_socket` to `cflow_io_native_operation`; existing positional
initializers remain source-compatible because the new field is ignored for the
four existing operation kinds.

`CFLOW_IO_NATIVE_TCP_ACCEPT` uses:

- `socket`: a live, listening TCP socket owned by the caller;
- `buffer == NULL` and `length == 0`;
- `result_socket == CFLOW_IO_NATIVE_INVALID_SOCKET` at submission;
- optional peer-address output described by either all-zero address fields or
  `address != NULL`, nonzero bounded `address_capacity`, and zero
  `address_length`.

On successful completion the backend publishes the accepted socket in
`result_socket`, publishes the peer address first when requested, and reports
zero bytes. The result socket is nonblocking (and close-on-exec on POSIX). Its
ownership transfers to the caller when `cflow_io_actor_complete()` accepts the
terminal success. Before that point the backend is its sole owner. Native
failure, cancellation, peer-address overflow, or rejected/stale Actor delivery
closes the socket and leaves `result_socket` invalid.

`CFLOW_IO_NATIVE_TCP_CONNECT` uses:

- `socket`: a live, unconnected TCP socket owned by the caller;
- `buffer == NULL`, `length == 0`, and ignored `result_socket`;
- `address[0..address_length)`: a host-OS `sockaddr` destination, with nonzero
  length no greater than `address_capacity` or `UINT32_MAX`.

Success reports zero bytes. The backend never closes the connect socket,
including failure or cancellation. The caller keeps the socket and decides
whether to inspect, forget, retry, or close it after terminal completion.

The operation, destination/peer-address storage, and accepted-socket result
field are borrowed until the completion callback returns. Caller sockets must
remain live through terminal completion. Readiness lifecycle operations require
caller-configured nonblocking sockets; the adapter verifies this precondition
without changing caller flags.

## Backend Strategies

### IOCP

Each bounded request record owns any provisional accepted socket and the
AcceptEx address workspace. Extension pointers are resolved from the operation's
socket provider with `WSAIoctl`; there is no process-global provider assumption.
Accept uses `AcceptEx`, applies `SO_UPDATE_ACCEPT_CONTEXT`, records the peer with
`getpeername`, and enables nonblocking mode before transfer. Connect uses
`ConnectEx`; an unbound socket is bound to the wildcard address matching its
family, then successful completion applies `SO_UPDATE_CONNECT_CONTEXT`.

The listening/connecting socket remains the IOCP-associated identity. The
accepted socket is not entered in the backend identity table until a later
operation actually uses it.

### io_uring

Accept submits `IORING_OP_ACCEPT` into record-owned `sockaddr_storage` and
`socklen_t`, requesting nonblocking/close-on-exec flags. Connect submits
`IORING_OP_CONNECT` with the caller-borrowed destination. A nonnegative accept
CQE is a provisional file descriptor owned by the record until address
publication and Actor completion are accepted.

### epoll / kqueue readiness adapter

Accept is a read-lane operation. It attempts a nonblocking accept and rearms on
`EAGAIN`; the accepted descriptor is normalized to nonblocking and close-on-exec
before transfer. Connect is a write-lane operation. Its first attempt calls
`connect`; `EINPROGRESS`, `EALREADY`, or `EWOULDBLOCK` records a started phase and
arms readiness. A later attempt reads `SO_ERROR` instead of calling `connect`
again. No adapter thread is added; the existing Platform reactor remains the
only readiness worker.

## State, Capacity, and Shutdown

The backend request record is the sole fact source for provisional accept
ownership. Its states remain bounded by `request_capacity`:

`FREE -> PENDING/QUEUED -> native terminal -> Actor completion -> FREE`.

Accept adds at most one provisional socket and one fixed peer-address workspace
per active request; there is no unbounded queue or allocation. Connect adds only
a phase bit to readiness records. Cancellation remains best effort and native
completion remains authoritative. Shutdown closes admission and returns
`SALTS_EBUSY` until all accepted requests have terminally completed; it does not
silently close caller-owned listening, connected, or connecting sockets.

## Error Semantics

Malformed operation shapes fail validation with `SALTS_EINVAL` before backend
submission. Capacity exhaustion remains `SALTS_EBUSY`. OS failures are reported
as existing negative native error values. An optional accept peer address that
does not fit fails with `SALTS_ERANGE` and closes the provisional socket. There
is no backend fallback and no synchronous blocking compatibility path.

## Compatibility and Migration

The enum and structure changes are additive at source level, but enlarging the
public C structure is an ABI change for binaries compiled against an older
header. Salts consumers must rebuild. Existing send/receive behavior,
socket ownership, configuration, statistics, and backend selection are
unchanged. No new dependency is introduced; Windows adds the platform
`mswsock.h` extension declarations already supplied by the SDK.

## Verification

TinyTest covers shape validation, real loopback accept/connect, accepted-socket
data transfer, peer-address publication, failure, cancellation, result reset,
capacity reuse, and shutdown/forget behavior. The same behavioral helper runs
against the platform backend selected by each CI host; Linux additionally runs
epoll and io_uring when available. Windows Release receives focused and full
CTest verification locally, while GitHub CI supplies Linux and macOS compile and
runtime evidence.
