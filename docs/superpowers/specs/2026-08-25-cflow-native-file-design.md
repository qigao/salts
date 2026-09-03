# CFlow Native Regular-File I/O Design

Issue: [#107](https://github.com/qigao/salts/issues/107)
Parent tracker: [#100](https://github.com/qigao/salts/issues/100)

## Decision

Add a file-specific Actor strategy for bounded, offset-based regular-file
operations. The first public contract contains `READ_AT`, `WRITE_AT`, and
`FLUSH`. It does not expose pathname open/create policy, directories, devices,
USB, serial ports, memory mapping, file watching, or a blocking-worker
fallback.

An accepted operation has these invariants:

- one operation produces exactly one authoritative terminal Actor completion;
- read and write always use the supplied offset and never consume or mutate a
  shared current file position;
- reads return `OK` with positive bytes or `EOF` with zero bytes;
- writes return `OK` with the actual, possibly partial, byte count;
- flush returns `OK` with zero bytes only after the native flush completion;
- the caller owns the operation, file handle, and buffer through terminal
  callback return;
- the backend never closes the caller's handle;
- fixed `request_capacity` bounds all accepted socket, pipe, and file work.

## Evidence and constraints

### Repository facts

- The existing native socket and pipe types describe different resources and
  route through separate Actor strategies. A file operation needs an explicit
  offset and optional flush, so reusing either shape would expose false fields
  and ownership semantics.
- `salts_fs_*_async` is a thread-pool-backed whole-file facility. It is not a
  native completion backend and cannot be an implicit implementation of this
  API.
- IOCP and io_uring already own bounded request records, cancellation,
  completion dispatch, and shutdown. File work should extend those state
  machines instead of creating a second scheduler.
- epoll, kqueue, and poll implement readiness. Regular disk files do not have a
  portable readiness contract, so those backends must report file operations
  unsupported rather than run blocking syscalls on the reactor thread.

### Platform facts

- Microsoft requires `FILE_FLAG_OVERLAPPED` when a handle is used for
  asynchronous file I/O and requires byte offsets in `OVERLAPPED.Offset` and
  `OffsetHigh`:
  <https://learn.microsoft.com/en-us/windows/win32/fileio/synchronous-and-asynchronous-i-o>.
- Microsoft documents overlapped `ReadFile` and `WriteFile`, including the rule
  that asynchronous handles do not maintain a file pointer:
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-readfile>
  and
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-writefile>.
- `FlushFileBuffers` has no `OVERLAPPED` argument and is synchronous. IOCP
  therefore reports `FLUSH` unsupported instead of blocking its owner thread:
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-flushfilebuffers>.
- `GetFileType` distinguishes disk handles, but public user-mode file metadata
  does not reliably expose whether a handle was originally opened with
  `FILE_FLAG_OVERLAPPED`. The caller must declare that property:
  <https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-getfiletype>
  and
  <https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ne-minwinbase-file_info_by_handle_class>.
- Linux io_uring `READ`/`WRITE` SQEs carry an explicit offset and `FSYNC`
  completes through a CQE:
  <https://github.com/axboe/liburing/blob/master/man/io_uring_prep_read.3>
  and
  <https://github.com/axboe/liburing/blob/master/man/io_uring_prep_fsync.3>.

## Public API

The additions are parallel to the pipe API and source-compatible with existing
socket and pipe declarations:

```c
typedef enum cflow_io_native_file_operation_kind {
    CFLOW_IO_NATIVE_FILE_READ_AT = 0,
    CFLOW_IO_NATIVE_FILE_WRITE_AT,
    CFLOW_IO_NATIVE_FILE_FLUSH
} cflow_io_native_file_operation_kind;

typedef enum cflow_io_native_file_operation_flags {
    CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE = 1u << 0
} cflow_io_native_file_operation_flags;

typedef struct cflow_io_native_file_operation {
    cflow_io_native_file_operation_kind kind;
    uintptr_t handle;
    void *buffer;
    size_t length;
    uint64_t offset;
    uint32_t flags;
} cflow_io_native_file_operation;

bool cflow_io_native_backend_file_operation_supported(
    cflow_io_native_backend_kind kind,
    cflow_io_native_file_operation_kind operation_kind);

cflow_io_backend_ops cflow_io_native_backend_file_actor_ops(void);

int cflow_io_native_backend_forget_file(
    cflow_io_native_backend *backend, uintptr_t closed_handle);
```

`READ_AT` and `WRITE_AT` require a handle other than `UINTPTR_MAX`, a non-null
buffer, `1..UINT32_MAX` bytes, known flags only, `offset <= INT64_MAX`, and
`length <= INT64_MAX - offset`. The portable contract deliberately rejects
larger offsets even on filesystems that support unsigned 64-bit positions.

`FLUSH` requires the same valid handle, a null buffer, zero length, zero
offset, and known flags only. Descriptor zero remains valid on POSIX. IOCP
additionally rejects handle zero, non-disk handles, and read/write operations
without `CFLOW_IO_NATIVE_FILE_ASYNC_CAPABLE`.

The capability query is operation-specific because IOCP supports read/write
but not nonblocking flush. It reports compiled backend capability, not whether
a particular path, filesystem, mount, descriptor, or handle supports the
operation.

## Internal model

`cflow_io_native_impl_ops` gains `submit_file` and `forget_file` callbacks.
Each native request record stores a resource discriminator and exactly one
borrowed socket, pipe, or file operation pointer. Existing public socket and
pipe functions remain unchanged.

The file Actor strategy uses the existing Actor request ID as the cancellation
and completion identity. The backend remains the fact source for native
request state. No file-specific queue duplicates Actor or backend ownership.

The state sequence is:

```text
caller-owned -> Actor accepted -> backend record reserved -> native submitted
             -> native terminal result -> Actor callback -> acknowledged
             -> record reusable -> caller may close -> forget retained identity
```

Rejected admission leaves the operation, buffer, and handle wholly
caller-owned. A successfully reserved record must reach exactly one completion
or be synchronously rolled back before submission returns failure.

## Backend behavior

| Backend | `READ_AT` / `WRITE_AT` | `FLUSH` | Resource check |
|---|---|---|---|
| IOCP | overlapped `ReadFile` / `WriteFile` | unsupported | nonzero `FILE_TYPE_DISK` handle plus async declaration |
| io_uring | `IORING_OP_READ` / `IORING_OP_WRITE` | `IORING_OP_FSYNC` | valid descriptor and `fstat(..., S_ISREG)` |
| epoll | unsupported | unsupported | no readiness fallback |
| kqueue | unsupported | unsupported | no readiness fallback |
| poll | unsupported | unsupported | no readiness fallback |

IOCP clears each record's `OVERLAPPED`, writes the low and high 32-bit offset
fields, and then starts `ReadFile` or `WriteFile`. Synchronous success still
completes through the associated completion port under the backend's existing
association mode. `CancelIoEx` targets the record's retained handle and
`OVERLAPPED`. A zero-byte read or `ERROR_HANDLE_EOF` maps to Actor EOF.

io_uring writes the checked offset directly to `sqe->off`. `FLUSH` uses
`IORING_OP_FSYNC` with `fsync_flags = 0`, meaning the full fsync operation
rather than data-only sync. CQE result zero completes flush with zero bytes;
zero-byte reads map to EOF. `fstat` rejects pipes, sockets, directories, and
device nodes so device I/O remains a separate future contract.

Operations on one file are not implicitly serialized. Different offsets may
run concurrently and complete out of submission order. Callers requiring
ordering must await/acknowledge one operation before submitting the dependent
operation, or express the dependency in their graph.

## Ownership, capacity, and shutdown

- The operation object, buffer, and handle are borrowed from successful Actor
  admission until terminal callback return.
- Read buffers are exclusively writable by the backend during that interval;
  write buffers are immutable during that interval.
- Cancellation is a request. The native terminal result wins races and exactly
  one terminal callback is delivered.
- Closing a handle with accepted work is a contract violation. The caller
  cancels or drains, acknowledges, closes, and then calls `forget_file` for a
  retained IOCP identity.
- `request_capacity` is the hard shared bound. Full admission reports the
  existing busy/backpressure result; it never allocates an overflow record.
- Shutdown first closes admission and reports `SALTS_EBUSY` while native
  requests remain. It does not synchronously flush caller files or close them.

## Error semantics

- malformed operation or offset range: `SALTS_EINVAL` before native admission;
- backend/operation unsupported, IOCP flush, or missing IOCP async declaration:
  `SALTS_ENOTSUP`;
- wrong resource type: `SALTS_EINVAL`;
- full bounded request/resource storage: existing `SALTS_EBUSY` behavior;
- read past end: `CFLOW_IO_COMPLETION_EOF`, zero bytes, `SALTS_OK`;
- partial read/write: `CFLOW_IO_COMPLETION_OK` with actual bytes;
- native failure: `CFLOW_IO_COMPLETION_FAILED` with the mapped negative error;
- cancelled native operation: `CFLOW_IO_COMPLETION_CANCELLED` only after the
  backend's authoritative completion.

No error path closes the file, mutates a current file position, or submits work
to `salts_fs_*_async`.

## Verification matrix

- C and C++ public-header compilation covers all new names without changing
  existing socket or pipe aggregate initializers.
- Pure contract tests cover kinds, flags, buffer/length shape, invalid handles,
  and checked offset arithmetic.
- Windows uses a real temporary regular file opened with
  `FILE_FLAG_OVERLAPPED`; tests cover offset read/write, EOF, partial read,
  async-declaration rejection, file-type rejection, IOCP flush rejection,
  bounded capacity, cancellation races, forget, and slot reuse.
- Linux io_uring uses a real temporary regular file; tests cover offset
  read/write without changing `lseek` position, EOF, partial read, flush,
  resource-type rejection, bounded capacity, cancellation races, and reuse.
- epoll, kqueue, and poll tests assert operation-specific capability is false
  and Actor submission returns unsupported without touching the file.
- Existing TCP, UDP, accept/connect, and pipe suites remain unchanged and pass.

## Compatibility and migration

This is additive public API. Existing layouts, enumerator values, entry points,
backend selection, and socket/pipe behavior do not change. Applications may
adopt file operations per backend using the operation-specific capability
query. There is no automatic migration from `salts_fs_*_async`, because its
whole-file thread-pool semantics differ from offset-based native completion.

## Deferred work

Path open/create/rename/delete, directory enumeration, vectored I/O, direct-I/O
alignment, file locking, sparse-file control, file watching, memory mapping,
Windows asynchronous flush alternatives, serial ports, TUN/TAP, USB, and other
device protocols remain separate issues. A device is not synonymous with USB:
it may be a character/block device or platform handle, and each class needs a
typed lifecycle, transfer, cancellation, and privilege contract.
