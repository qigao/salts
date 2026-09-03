# CFlow Bounded Async Filesystem Control Design

Issue: [#112](https://github.com/qigao/salts/issues/112)
Parent: [#111](https://github.com/qigao/salts/issues/111)
Data-plane facade: [#110](https://github.com/qigao/salts/pull/110)

## Decision

Add `Salts::CFlowFS`, a separate adapter target that combines the
synchronous `salts_fs` pathname API with CFlow's bounded Worker Executor. The
target exposes `cflow_fs_service` for `stat`, `lstat`, directory enumeration,
`mkdir`, `rmdir`, `rename`, and `unlink`.

This backend is explicitly worker-backed. It does not claim io_uring or IOCP
kernel-native semantics and does not restore the removed `salts_fs_*_async`
surface. `Salts::CFlow` remains independent of `Salts::Core`, avoiding
the existing `Core -> CFlow` dependency becoming a cycle.

## Public contract

The public header is `cflow/fs.h`; consumers link `Salts::CFlowFS`.

```c
typedef struct cflow_fs_service { void *impl; } cflow_fs_service;

typedef enum cflow_fs_operation_kind {
    CFLOW_FS_STAT = 0,
    CFLOW_FS_LSTAT,
    CFLOW_FS_READ_DIRECTORY,
    CFLOW_FS_MKDIR,
    CFLOW_FS_RMDIR,
    CFLOW_FS_RENAME,
    CFLOW_FS_UNLINK
} cflow_fs_operation_kind;

typedef enum cflow_fs_submit_status {
    CFLOW_FS_SUBMIT_ACCEPTED = 0,
    CFLOW_FS_SUBMIT_INVALID_ARGUMENT,
    CFLOW_FS_SUBMIT_FULL,
    CFLOW_FS_SUBMIT_CLOSED,
    CFLOW_FS_SUBMIT_ID_EXHAUSTED
} cflow_fs_submit_status;

typedef struct cflow_fs_dir_buffer {
    salts_fs_dirent_t *entries;
    size_t entry_capacity;
    char *names;
    size_t names_capacity;
    size_t entry_count;
    size_t names_used;
} cflow_fs_dir_buffer;

typedef void (*cflow_fs_completion_fn)(
    void *user, uint64_t request_id,
    cflow_fs_operation_kind operation, int result);

typedef struct cflow_fs_config {
    size_t worker_count;
    size_t request_capacity;
    size_t path_capacity;
    cflow_fs_completion_fn completion;
    void *completion_user;
} cflow_fs_config;
```

Initialization validates positive capacities with checked multiplication and
allocates exactly `request_capacity` slots plus two `path_capacity` buffers per
slot. Accepted submissions copy one or two NUL-terminated paths before return.
Oversized paths are invalid and cause no side effect.

Stat submissions borrow a caller-owned `salts_fs_stat_t` until callback return.
Directory submissions borrow a caller-owned `cflow_fs_dir_buffer`, entry array,
and name arena until callback return. Every returned entry name points into the
caller's name arena. If either directory capacity is insufficient, the terminal
result is `SALTS_ENOBUFS`, both used counts are zero, and no partial listing is
committed.

## Ownership and concurrency

| Property | Contract |
|---|---|
| Data unit | One fixed slot containing copied paths, operation arguments, result, and phase |
| State owner | Service owns slots; Worker Executor owns accepted task execution |
| Producers | MPSC submission and cancellation |
| Consumer | Exactly one driver invokes `run_ready`, callbacks, close, and destroy |
| Result borrow | From accepted submission through callback return |
| Backpressure | No free slot or full Executor returns `CFLOW_FS_SUBMIT_FULL` |
| Ordering | No cross-request completion ordering guarantee |

Successful Executor admission produces exactly one worker `run` or `cancel`
terminal path. A worker stores its result in the slot; the single driver claims
completed slots and invokes callbacks. Callback return releases the slot.

Cancellation before syscall start completes with `SALTS_ECANCELED`. Once the
blocking syscall has started, cancellation cannot revoke the OS side effect;
the actual syscall result remains authoritative. Close stops admission and
uses cancel-pending shutdown: queued work is cancelled, running work completes.

## Error and lifecycle semantics

- Rejected submission never borrows result storage and performs no side effect.
- Accepted operational failures are reported only through the callback as the
  negative `salts_fs` result.
- Request IDs are nonzero, monotonically allocated, and exhaustion is distinct.
- Concurrent or reentrant driving returns `SALTS_EBUSY`.
- `close` is nonblocking and repeated close returns `SALTS_EALREADY`.
- `destroy` returns `SALTS_EBUSY` until the Executor is closed and every
  terminal callback has returned; successful destroy restores the public
  handle to zero.

## Verification

Tests cover all seven operations, missing paths, path bounds, fixed-capacity
saturation and reuse, transactional directory overflow, cancellation during
close, callback affinity, repeated lifecycle, C/C++ public-header compilation,
and adjacent Executor plus `salts_fs` regressions.
