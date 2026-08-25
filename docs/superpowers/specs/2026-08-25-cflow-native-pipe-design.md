# CFlow Native Pipe Read/Write Design

Issue: [#105](https://github.com/qigao/turbo-utils/issues/105)  
Parent tracker: [#100](https://github.com/qigao/turbo-utils/issues/100)

## Decision

Add a pipe-specific public operation type and Actor strategy. Do not reuse the
socket-shaped `cflow_io_native_operation`, and do not add named-pipe connection
lifecycle, FIFO pathname management, regular files, devices, or a blocking
worker fallback in this increment.

The first contract is a one-shot byte-stream read or write:

- one accepted operation produces exactly one authoritative terminal Actor
  completion;
- reads return `OK` with positive bytes or `EOF` when the peer has closed;
- writes return `OK` with the actual, possibly partial, byte count;
- cancellation is a request and native completion remains authoritative;
- the caller owns the endpoint and buffer through terminal callback return;
- the backend never closes the caller's endpoint.

## Evidence and constraints

### Repository facts

- `cflow_io_native_operation` names its native identity `socket` and contains
  TCP/UDP address and accepted-socket fields. Reusing it for a pipe would expose
  irrelevant fields and make its ownership comment false.
- The readiness adapter executes socket data operations with `recv`/`send` and
  retains two ordered lanes per identity. Pipe operations need `read`/`write`
  but can reuse the same bounded read/write lane state machine.
- The IOCP adapter executes socket data operations with `WSARecv`/`WSASend`.
  Pipe operations need overlapped `ReadFile`/`WriteFile` while reusing the same
  completion port, request records, cancellation, and retained identity table.
- The io_uring adapter uses `IORING_OP_RECV`/`IORING_OP_SEND`. Pipe operations
  need `IORING_OP_READ`/`IORING_OP_WRITE` with the non-seekable current-position
  offset.
- `cflow_io_native_backend_forget_socket` is used by tests and benchmarks.
  It remains source-compatible; pipe adds a parallel forget entry point and
  both route to resource-neutral internal identity cleanup.

### Platform facts

- Microsoft documents that asynchronous pipe operations require handles opened
  with `FILE_FLAG_OVERLAPPED` and a valid `OVERLAPPED` structure:
  <https://learn.microsoft.com/en-us/windows/win32/ipc/synchronous-and-overlapped-input-and-output>.
- Microsoft documents that handles returned by `CreatePipe` do not support
  overlapped read/write:
  <https://learn.microsoft.com/en-us/windows/win32/ipc/anonymous-pipe-operations>.
- Microsoft documents that a synchronous handle can still receive a non-null
  `OVERLAPPED` pointer and the call then waits synchronously:
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile>.
- The supported `GetFileInformationByHandleEx` API explicitly says its handle
  should not be a pipe handle, so CFlow cannot safely infer the original
  `FILE_FLAG_OVERLAPPED` choice from an arbitrary pipe handle:
  <https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getfileinformationbyhandleex>.

Therefore each pipe operation must carry the caller's explicit
`CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE` declaration. IOCP rejects an operation
without that declaration as `TURBO_ENOTSUP`. On POSIX the declaration is also
required, and the readiness backend independently verifies `O_NONBLOCK` with
`fcntl`; a false declaration cannot cause its worker to block.

## Public API

The following additions are source-compatible with the existing socket API:

```c
typedef enum cflow_io_native_pipe_operation_kind {
    CFLOW_IO_NATIVE_PIPE_READ = 0,
    CFLOW_IO_NATIVE_PIPE_WRITE
} cflow_io_native_pipe_operation_kind;

typedef enum cflow_io_native_pipe_operation_flags {
    CFLOW_IO_NATIVE_PIPE_ASYNC_CAPABLE = 1u << 0
} cflow_io_native_pipe_operation_flags;

typedef struct cflow_io_native_pipe_operation {
    cflow_io_native_pipe_operation_kind kind;
    uintptr_t handle;
    void *buffer;
    size_t length;
    uint32_t flags;
} cflow_io_native_pipe_operation;

bool cflow_io_native_backend_pipe_supported(
    cflow_io_native_backend_kind kind);

cflow_io_backend_ops cflow_io_native_backend_pipe_actor_ops(void);

int cflow_io_native_backend_forget_pipe(
    cflow_io_native_backend *backend, uintptr_t closed_handle);
```

Validation rejects unknown kinds or flags, `UINTPTR_MAX`, null buffers,
zero-length transfers, and lengths greater than `UINT32_MAX`. Descriptor zero
remains valid on POSIX. IOCP additionally rejects handle zero, non-pipe handles,
message-mode pipes, and operations without the async-capable declaration.

The capability query reports compile-time backend support for this contract.
It does not attest that a particular Windows handle was created overlapped or
that a particular POSIX descriptor is nonblocking; submit performs the
resource-specific checks described above.

## Internal model

Extend `cflow_io_native_impl_ops` with a separate `submit_pipe` callback and a
separate public-facing `forget_pipe` callback. Socket submit remains unchanged.
Each backend record stores a resource discriminator plus exactly one borrowed
operation pointer. Internal identity tables become resource-neutral because a
live POSIX descriptor or Windows handle already has process-wide uniqueness.

The same backend instance is still configured for one Actor strategy at a
time. Socket and pipe Actor strategies may each use a backend instance, but the
backend is not changed into a multi-Actor request-ID namespace in this issue.

## Backend behavior

| Backend | Read/write primitive | Admission rule | EOF rule |
|---|---|---|---|
| epoll | `read` / guarded `write` | fd is `O_NONBLOCK` | read returns 0 |
| kqueue | `read` / guarded `write` | fd is `O_NONBLOCK` | read returns 0 |
| poll | `read` / guarded `write` | fd is `O_NONBLOCK` | read returns 0 |
| io_uring | `IORING_OP_READ` / `IORING_OP_WRITE` | valid fd | read CQE result is 0 |
| IOCP | overlapped `ReadFile` / `WriteFile` | declared async-capable byte pipe | zero-byte read or `ERROR_BROKEN_PIPE` |

On Darwin, each backend-owned write descriptor duplicate is configured once
with `F_SETNOSIGPIPE` before reactor registration. Apple's XNU header defines
that descriptor command as suppressing `SIGPIPE` when an operation returns
`EPIPE`: <https://github.com/apple-oss-distributions/xnu/blob/main/bsd/sys/fcntl.h>.
Other POSIX guarded writes temporarily block `SIGPIPE` on the executing thread,
record whether it was already pending, call `write`, consume only a newly
generated pending `SIGPIPE` after `EPIPE`, and restore the previous mask. Both
paths preserve the caller's process-wide signal disposition.

Readiness backends keep FIFO ordering within the existing read and write lanes.
They may execute one read and one write lane for the same full-duplex FIFO
identity where the OS endpoint supports both directions. A conventional POSIX
anonymous pipe has distinct read and write descriptors and therefore distinct
identities.

## Ownership and shutdown

The operation, buffer, and endpoint are borrowed only after Actor submission
succeeds. They remain valid until the terminal completion callback returns.
Rejected submission retains caller ownership. Closing an endpoint with an
active request violates the contract; the caller cancels or drains first,
closes the endpoint, then calls `forget_pipe` for a retained readiness or IOCP
identity.

Backend shutdown closes admission first and returns `TURBO_EBUSY` while native
requests remain. No pipe-specific drain path bypasses the Actor. IOCP and
io_uring workers join only after all accepted requests are terminal; readiness
uses the existing Platform reactor worker and retained lane cleanup.

## Error semantics

- malformed operation: `TURBO_EINVAL` before native admission;
- unsupported backend/resource or missing Windows async declaration:
  `TURBO_ENOTSUP`;
- blocking POSIX readiness descriptor: `TURBO_EINVAL`;
- full bounded request/resource table: existing `TURBO_EBUSY` behavior;
- closed pipe read: `CFLOW_IO_COMPLETION_EOF` with zero bytes and `TURBO_OK`;
- broken pipe write: `CFLOW_IO_COMPLETION_FAILED` with the native negative error;
- cancelled native operation: `CFLOW_IO_COMPLETION_CANCELLED` only after
  authoritative backend completion.

No error path closes the caller's pipe endpoint or silently moves work to a
blocking worker thread.

## Verification matrix

- C and C++ header compilation preserves existing socket aggregate initializers.
- Windows IOCP tests use byte-mode named-pipe endpoints created with
  `FILE_FLAG_OVERLAPPED` and cover read, write, EOF, cancellation, capacity,
  forget, and rejection of a missing async-capable declaration.
- Linux readiness tests cover epoll and poll; macOS covers kqueue and poll.
  They verify `O_NONBLOCK`, partial transfer, EOF, cancellation, lane ordering,
  identity reuse, and broken-reader writes without a process-visible `SIGPIPE`.
- Linux io_uring runs the same pipe contract when initialization is available.
- Existing TCP/UDP/accept/connect tests remain unchanged and pass through all
  existing backends.

## Deferred work

Windows named-pipe server connection lifecycle, POSIX FIFO pathname/open
rendezvous, subprocess standard-stream ownership, message-mode framing,
transactions, impersonation, file I/O, serial/TUN/TAP, and USB transfers each
require separate contracts and are not exposed by this change.
