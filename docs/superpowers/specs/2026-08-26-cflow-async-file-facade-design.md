# CFlow Bounded Async File Facade Design

Issue: [#109](https://github.com/qigao/salts/issues/109)
Parent tracker: [#100](https://github.com/qigao/salts/issues/100)
Native file substrate: [#107](https://github.com/qigao/salts/issues/107)

## Decision

Add `cflow_io_file`, an owning facade over the existing native regular-file
Actor strategy. Pathname open remains a synchronous control-plane operation.
Offset reads, offset writes, and supported flushes remain bounded native
asynchronous data-plane operations.

The facade owns one native file handle, one explicitly selected native backend,
one manual Executor, one I/O Actor, and a fixed operation-slot array. It does
not add a thread-pool fallback and does not restore the removed
`salts_fs_*_async` API.

## Context and alternatives

### Current structure

- `salts_fs` provides synchronous whole-file, descriptor, metadata, directory,
  path, lock, and mapping-adjacent control operations.
- `cflow_io_native_file_operation` provides native `READ_AT`, `WRITE_AT`, and
  `FLUSH` after the caller has already opened a suitable native handle and
  assembled an Executor, Actor, backend, operation storage, callback,
  acknowledgement, and shutdown protocol.
- IOCP requires a disk handle opened with `FILE_FLAG_OVERLAPPED`; the existing
  `salts_fs_open()` returns a CRT descriptor and cannot establish that contract.

### Candidates

1. Keep caller assembly. This adds no API, but every consumer must duplicate a
   high-risk lifecycle and Windows handle-opening policy.
2. Add native open/close helpers only. This fixes handle creation but leaves
   Actor ownership, bounded storage, acknowledgement, and shutdown duplicated.
3. Add an owning facade. This centralizes the complete protocol while retaining
   `cflow_io_native` as the lower-level extension point.

Choose candidate 3. The migration cost is additive: existing native callers do
not change, while ordinary file-I/O consumers can use the facade.

## Public contract

The new public header is `cflow/io_file.h`, included by `cflow/cflow.h`.

```c
typedef struct cflow_io_file { void *impl; } cflow_io_file;

typedef enum cflow_io_file_open_flags {
    CFLOW_IO_FILE_READ = 1u << 0,
    CFLOW_IO_FILE_WRITE = 1u << 1,
    CFLOW_IO_FILE_CREATE = 1u << 2,
    CFLOW_IO_FILE_TRUNCATE = 1u << 3
} cflow_io_file_open_flags;

typedef enum cflow_io_file_submit_status {
    CFLOW_IO_FILE_SUBMIT_ACCEPTED = 0,
    CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT,
    CFLOW_IO_FILE_SUBMIT_UNSUPPORTED,
    CFLOW_IO_FILE_SUBMIT_ACCESS_DENIED,
    CFLOW_IO_FILE_SUBMIT_FULL,
    CFLOW_IO_FILE_SUBMIT_CLOSED,
    CFLOW_IO_FILE_SUBMIT_LEASE_IN_USE,
    CFLOW_IO_FILE_SUBMIT_ID_EXHAUSTED
} cflow_io_file_submit_status;

typedef struct cflow_io_file_submit_result {
    cflow_io_file_submit_status status;
    cflow_io_request_id request_id;
} cflow_io_file_submit_result;

typedef void (*cflow_io_file_completion_fn)(
    void *user, cflow_io_request_id request_id, cflow_io_lease_id lease_id,
    cflow_io_native_file_operation_kind operation_kind,
    const cflow_io_completion *completion);

typedef struct cflow_io_file_config {
    cflow_io_native_backend_kind backend_kind;
    size_t request_capacity;
    size_t command_capacity;
    size_t completion_batch_capacity;
    uint32_t open_flags;
    uint32_t create_mode;
    cflow_io_file_completion_fn completion;
    void *completion_user;
} cflow_io_file_config;

typedef struct cflow_io_file_stats {
    cflow_io_actor_stats actor;
    cflow_io_native_backend_stats backend;
    size_t operation_slots_in_use;
    bool close_requested;
} cflow_io_file_stats;

int cflow_io_file_open(cflow_io_file *file, const char *path,
                       const cflow_io_file_config *config);
bool cflow_io_file_operation_supported(
    const cflow_io_file *file,
    cflow_io_native_file_operation_kind operation_kind);
cflow_io_file_submit_result cflow_io_file_try_read_at(
    cflow_io_file *file, cflow_io_lease_id lease_id,
    void *buffer, size_t length, uint64_t offset);
cflow_io_file_submit_result cflow_io_file_try_write_at(
    cflow_io_file *file, cflow_io_lease_id lease_id,
    const void *buffer, size_t length, uint64_t offset);
cflow_io_file_submit_result cflow_io_file_try_flush(
    cflow_io_file *file, cflow_io_lease_id lease_id);
cflow_io_cancel_status cflow_io_file_try_cancel(
    cflow_io_file *file, cflow_io_request_id request_id);
int cflow_io_file_run_ready(cflow_io_file *file, size_t max_steps,
                            size_t *progressed);
int cflow_io_file_close(cflow_io_file *file);
bool cflow_io_file_is_quiescent(const cflow_io_file *file);
bool cflow_io_file_get_stats(const cflow_io_file *file,
                             cflow_io_file_stats *out);
int cflow_io_file_destroy(cflow_io_file *file);
```

`open_flags` must contain READ or WRITE. CREATE and TRUNCATE require WRITE.
Unknown bits, an empty path, zero capacities, invalid create modes, a nonzero
destination, or a null completion callback fail with `SALTS_EINVAL` before any
pathname side effect. A backend that cannot perform every access operation
requested by the open flags fails with `SALTS_ENOTSUP` before opening the path.

Windows maps the flags to `CreateFileA` with `FILE_FLAG_OVERLAPPED`. POSIX maps
them to `open()` with `O_CLOEXEC`. The facade never exposes or transfers the
native handle.

## Data and ownership protocol

| Item | Contract |
|---|---|
| Data unit | One fixed operation slot containing a native operation, request identity, and delivery state |
| Fact source | Actor owns request phase; backend owns native completion; facade slot owns operation storage |
| Payload | Caller-owned buffer borrowed from accepted submit through completion callback return |
| Capacity | Exactly `request_capacity` slots; no overflow allocation |
| Producer topology | MPSC submit/cancel |
| Consumer topology | Exactly one driver calls `run_ready`, completion callbacks, close, and destroy |
| Ordering | No cross-request completion order; explicit offsets do not share a file position |
| Backpressure | Slot or Actor saturation returns `CFLOW_IO_FILE_SUBMIT_FULL` without ownership transfer |

On accepted submission, the Actor owns the slot release obligation. On
rejection, the facade immediately returns the slot to its fixed pool and the
caller retains the buffer. The completion callback runs on the single driver
thread through the facade-owned manual Executor. After the callback returns,
the driver acknowledges the Actor request automatically; acknowledgement
releases the slot exactly once.

No borrowed buffer may be freed, mutated contrary to its read/write role, or
reused before its callback returns. A read buffer is backend-exclusive mutable
storage; a write buffer is immutable during the borrow.

## State and shutdown

```text
zero
  -> open resources and native handle
  -> OPEN
  -> close admission / request cancellation
  -> CLOSING
  -> drive terminal callbacks and automatic acknowledgements
  -> QUIESCENT
  -> stop native backend worker
  -> destroy Actor
  -> close native handle
  -> forget retained backend identity
  -> destroy backend and Executor
  -> zero
```

`cflow_io_file_close()` is nonblocking and starts logical close; a repeated
call returns `SALTS_EALREADY` without changing state. It does not close the
native handle while work is live. `destroy()` returns
`SALTS_EBUSY` until close has reached quiescence. Once quiescent, destroy
consumes all owned resources and restores the public handle to zero. Native
handle close errors are returned after the remaining owned resources are
released; the object is still destroyed because retrying a POSIX `close()` is
not portable.

All submit/cancel producer threads must be stopped and joined before destroy.
Close may race submission through the Actor's MPSC admission contract, but
destroy is an exclusive control-plane operation and never races another facade
entry.

## Error semantics

- Open/configuration errors use `SALTS_E*`, negative POSIX errno, or negative
  Win32 error values.
- A concurrent or reentrant driver call returns `SALTS_EBUSY` without running
  callbacks; `run_ready` reports only actions completed by the successful
  single driver.
- Invalid operation shape is `CFLOW_IO_FILE_SUBMIT_INVALID_ARGUMENT`.
- Backend/operation mismatch is `CFLOW_IO_FILE_SUBMIT_UNSUPPORTED`.
- Read/write access mismatch is `CFLOW_IO_FILE_SUBMIT_ACCESS_DENIED`.
- Capacity, closed admission, lease collision, and request-ID exhaustion remain
  distinct submission statuses.
- Accepted native failure is reported only through the terminal completion.
- There is no fallback after backend init, open, submit, or native completion
  failure.

## Compatibility and impact

This is additive for CFlow. Existing `cflow_io_native`, socket, pipe, and file
operation layouts remain unchanged. `salts_fs` remains synchronous and does not
depend on CFlow. The facade adds one public header and one CFlow source module;
consumers that use it already link `Salts::CFlow`.

The preceding removal of `salts_fs_*_async` is intentionally source- and
ABI-breaking for that unused API. Repository searches show no production
caller; out-of-repository consumers must migrate to this facade or remain on
the synchronous `salts_fs` contract.

## Verification

- C and C++ aggregate-header compilation covers every new public name.
- Unsupported readiness backends fail before creating or truncating a path.
- Real-file tests cover offset write/read, EOF, partial transfer, access
  mismatch, checked range rejection, capacity saturation/reuse, cancellation,
  close/drain, automatic acknowledgement, and repeated open/destroy.
- Windows covers IOCP overlapped handles and immediate flush rejection.
- Linux covers io_uring read/write/flush when runtime initialization succeeds.
- Existing `cflow_io_native_test`, `cflow_io_actor_test`, and `test_salts_fs`
  remain green.

## Deferred scope

Asynchronous pathname open, directory operations, metadata mutation, append
allocation, vectored/direct I/O, memory mapping, file watching, device/USB
semantics, implicit backend selection, and blocking-worker fallback remain out
of scope.
