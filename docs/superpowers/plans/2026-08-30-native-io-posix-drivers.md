# NativeIO POSIX Drivers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add direct Linux epoll/io_uring and BSD/macOS kqueue socket drivers to the root NativeIO module while preserving the existing bounded submit/observe contract.

**Architecture:** Keep `turbo_io_backend` as the public bridge and add explicit backend kinds. epoll and kqueue share a single-owner readiness request engine but retain separate syscall adapters; io_uring owns its mapped SQ/CQ and is driven only by submit/observe, with no worker, callback, Actor, Reactor, or CFlow dependency. All drivers use fixed endpoint/request/completion storage allocated at init.

**Tech Stack:** C11, POSIX sockets, Linux epoll, Linux raw io_uring ABI, BSD kqueue, TinyTest, CMake presets.

**Spec:** `native-io/README.md`

## Global Constraints

- Work inline in the repository root; do not dispatch sub-agents.
- Preserve IOCP behavior and the existing public operation/completion layouts except for appended enum values and documentation.
- Do not add liburing or another dependency; reuse `<linux/io_uring.h>` and the repository's already-tested ring mapping rules.
- One backend has exactly one owner thread; no internal thread, lock, callback, mailbox, or fallback backend.
- Socket, payload, and address memory remain borrowed until the matching terminal completion is observed.
- Endpoint, request, ready-event, and terminal-completion capacities are fixed at init; full admission returns `TURBO_ENOBUFS`.
- No commit or push is performed unless the user requests it.

## Data-path protocol

| Item | Contract |
|---|---|
| Data unit | One copied `turbo_io_operation`, identified by a generation-checked `turbo_io_request`. |
| Fact source | Backend endpoint/request records; kernel readiness is only a wakeup hint and kernel CQEs are only terminal evidence. |
| Ownership | Backend borrows native socket and caller buffers. Successful submit starts the borrow; observe of the terminal completion ends it. |
| Topology | Single producer/consumer owner thread. Control and data methods must not run concurrently. |
| Ordering | Readiness drivers serialize each endpoint's read lane and write lane FIFO. Completion drivers expose kernel completion order. |
| Capacity | `endpoint_capacity`, `request_capacity`, and `completion_batch_capacity`; all multiplication is overflow checked. |
| Backpressure | No free request or terminal slot returns `TURBO_ENOBUFS`; no dynamic growth or silent drop. |
| Cancellation | Readiness removes a queued request and publishes CANCELLED. io_uring submits `IORING_OP_ASYNC_CANCEL`; only the original `-ECANCELED` CQE proves cancellation. |
| Close | Close admission, cancel/drain requests, close sockets, release endpoints, destroy backend. Busy state retains ownership. |
| Observation | Existing counters plus backend/model queries; benchmarks compare only against the matching raw native API. |

Readiness request state:

```text
FREE --submit--> PENDING --syscall terminal--> TERMINAL --observe--> FREE(next generation)
                    |                              ^
                    +--cancel---------------------+
```

io_uring request state:

```text
FREE --SQE accepted--> PENDING --original CQE--> TERMINAL --observe--> FREE(next generation)
                           |
                           +--cancel SQE request (not terminal evidence)
```

---

### Task 1: Public backend admission

**Files:**
- Modify: `native-io/include/turbo/native_io.h`
- Modify: `native-io/src/native_io.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**
- Produces enum values `TURBO_IO_BACKEND_EPOLL`, `TURBO_IO_BACKEND_IO_URING`, `TURBO_IO_BACKEND_KQUEUE`.
- Produces `TURBO_IO_MODEL_READINESS`; IOCP/io_uring map to completion, epoll/kqueue map to readiness.

- [x] **Step 1: Write the failing support-matrix test**

```c
check_equal(turbo_io_backend_model(TURBO_IO_BACKEND_EPOLL),
            TURBO_IO_MODEL_READINESS);
check_equal(turbo_io_backend_model(TURBO_IO_BACKEND_IO_URING),
            TURBO_IO_MODEL_COMPLETION);
check_equal(turbo_io_backend_model(TURBO_IO_BACKEND_KQUEUE),
            TURBO_IO_MODEL_READINESS);
```

- [x] **Step 2: Build `native_io_test` and verify RED**

Run the platform preset target. Expected: compile failure because the three enum values and readiness model do not exist.

- [x] **Step 3: Add the enum/model mapping**

```c
typedef enum turbo_io_backend_kind {
  TURBO_IO_BACKEND_IOCP = 1,
  TURBO_IO_BACKEND_EPOLL,
  TURBO_IO_BACKEND_IO_URING,
  TURBO_IO_BACKEND_KQUEUE
} turbo_io_backend_kind;
```

- [x] **Step 4: Rebuild and verify the model test is GREEN**

---

### Task 2: Single-owner readiness engine

**Files:**
- Create: `native-io/src/native_io_readiness.h`
- Create: `native-io/src/native_io_readiness.c`
- Modify: `native-io/tests/native_io_test.c`

**Interfaces:**
- Consumes existing `turbo_io_impl_ops`.
- Produces `turbo_io_readiness_backend_init(backend, config, driver_ops)`.
- Produces a driver boundary with `create`, `update(fd, token, old_interests, new_interests)`, `wait`, and `destroy`.

- [x] **Step 1: Add portable real-socket contract tests**

Move TCP, UDP, cancellation, capacity, stale-handle, timeout, close, and destroy tests out of the Windows-only block. Readiness operations use per-call nonblocking flags and do not mutate the caller's socket mode.

- [x] **Step 2: Configure/build on Linux and verify RED**

Expected: EPOLL/KQUEUE/IO_URING backend initialization returns `TURBO_ENOTSUP`.

- [x] **Step 3: Implement fixed records and lane queues**

Use endpoint records with read/write head/tail indices, request records with intrusive `previous/next`, O(1) free stacks, and a request-index terminal ring. Derive native interests exclusively from nonempty lane heads.

- [x] **Step 4: Implement immediate syscall, ready drive, cancel, observe, and cleanup**

Use `recv`, `send`, `recvfrom`, and `sendto`; retry `EINTR`, queue only `EAGAIN/EWOULDBLOCK`, return immediate submission errors before publishing a request, and use `MSG_NOSIGNAL` or the platform's per-socket suppression contract.

- [x] **Step 5: Run contract tests and keep them RED until an OS adapter exists**

---

### Task 3: Linux epoll adapter

**Files:**
- Create: `native-io/src/native_io_epoll.c`
- Create: `native-io/src/native_io_linux.c`
- Modify: `native-io/CMakeLists.txt`
- Test: `native-io/tests/native_io_test.c`

**Interfaces:**
- Produces `turbo_io_epoll_backend_init()`.
- `epoll_event.data.u64` stores the endpoint generation token.

- [x] **Step 1: Add an epoll TCP/UDP parameter row and verify RED remotely**

Run `native_io_test` on `root@eu`; expected failure is `TURBO_ENOTSUP` for EPOLL.

- [x] **Step 2: Implement direct level-triggered epoll**

Create with `epoll_create1(EPOLL_CLOEXEC)`. Use ADD/MOD/DEL according to derived read/write interests. Translate IN/PRI, OUT, ERR, HUP, and RDHUP without creating an eventfd or thread.

- [x] **Step 3: Run epoll tests remotely and verify GREEN**

Run TCP, UDP, cancellation, capacity, timeout, and lifecycle cases through EPOLL.

---

### Task 4: Linux io_uring completion driver

**Files:**
- Create: `native-io/src/native_io_io_uring.c`
- Modify: `native-io/src/native_io_linux.c`
- Modify: `native-io/CMakeLists.txt`
- Test: `native-io/tests/native_io_test.c`

**Interfaces:**
- Produces `turbo_io_io_uring_backend_init()`.
- Packs request slot/generation into `sqe.user_data`; zero is reserved for cancel CQEs.

- [x] **Step 1: Add io_uring TCP/UDP/cancel rows and verify RED remotely**

Expected: IO_URING init is unsupported before the driver is connected.

- [x] **Step 2: Implement checked ring setup and mapping**

Use `io_uring_setup`, checked SQ/CQ extents, `IORING_FEAT_SINGLE_MMAP`, C11 acquire/release on shared heads/tails, and fixed request/free storage. No worker thread is created.

- [x] **Step 3: Implement submit and cancellation SQEs**

Use RECV/SEND for TCP, RECVMSG/SENDMSG with record-owned `iovec/msghdr` for UDP, and ASYNC_CANCEL for cancellation. Publish SQ tail then call `io_uring_enter`; roll back only when the kernel consumed no SQE.

- [x] **Step 4: Implement timeout-aware observe**

Drain CQ without a syscall first; if empty, poll the ring fd for the requested timeout, then drain up to the public batch limit while consuming internal cancel CQEs separately.

- [x] **Step 5: Run io_uring tests remotely and verify GREEN**

The Debian 6.1 remote kernel must pass TCP, UDP, cancellation, stale-handle, and shutdown tests.

---

### Task 5: BSD/macOS kqueue adapter

**Files:**
- Create: `native-io/src/native_io_kqueue.c`
- Create: `native-io/src/native_io_bsd.c`
- Modify: `native-io/CMakeLists.txt`
- Test: `native-io/tests/native_io_test.c`

**Interfaces:**
- Produces `turbo_io_kqueue_backend_init()`.
- Stores endpoint generation tokens in `kevent.udata`.

- [x] **Step 1: Add the kqueue support row and portable contract invocation**

On non-kqueue hosts the explicit selector must return `TURBO_ENOTSUP`; on Apple/BSD it runs the same real-socket contract.

- [x] **Step 2: Implement direct kqueue filter updates**

Use EVFILT_READ/EVFILT_WRITE with level semantics. Apply and, on a multi-filter failure, roll back changes so the common engine's interest state remains authoritative. Translate EV_EOF and EV_ERROR.

- [ ] **Step 3: Compile-check on the available host and record macOS runtime coverage as pending if no host exists**

Do not claim macOS runtime success without actual CTest/CI evidence.

Implementation is present; no Apple/BSD host is available in this session, so compile/runtime evidence remains pending.

---

### Task 6: Native baselines and documentation

**Files:**
- Modify: `native-io/benchmarks/native_io_benchmark.c`
- Modify: `native-io/benchmarks/CMakeLists.txt`
- Modify: `native-io/README.md`

**Interfaces:**
- Windows compares IOCP with raw IOCP.
- Linux compares EPOLL with raw epoll and IO_URING with raw io_uring.
- macOS/BSD compares KQUEUE with raw kqueue.

- [ ] **Step 1: Add platform benchmark selection and correctness assertions**

Keep TCP and UDP in separate payload tables. TCP covers 1/4/8/16/32/64 KiB; UDP covers 1/4/8/16/32 KiB. Each row reports p50/p95, throughput, submit, and completion/observe stages.

Windows raw IOCP and Linux raw epoll/io_uring rows are complete. A raw kqueue benchmark remains pending with the Apple/BSD host.

- [x] **Step 2: Run Windows and Linux Release benchmarks**

Calculate delta as `(NativeIO / raw - 1) * 100%` for latency and `(NativeIO / raw - 1) * 100%` for throughput. Do not gate on one noisy p95 sample.

- [x] **Step 3: Document backend matrix and lifecycle differences**

State readiness FIFO lane ordering, io_uring completion ordering, per-call nonblocking behavior, timeout semantics, cancellation evidence, and lack of fallback.

---

### Task 7: Verification and package consumption

**Files:**
- Verify: all changed NativeIO and install-consumer files.

- [x] **Step 1: Format and static checks**

Run `clang-format`, `git diff --check`, placeholder scan, and CodeGraph sync.

- [x] **Step 2: Windows regression**

Build/run Debug and Release `native_io_test`, C++ header test, IOCP benchmark, adjacent `cflow_io_native_test`, and `verify_installed_package` through the Windows user presets inside `VsDevCmd`.

- [x] **Step 3: Linux verification on `root@eu`**

Configure `linux-dev-user` and `linux-release-user`; run NativeIO tests for EPOLL and IO_URING, Release benchmarks, and install consumer. Record kernel and tool versions with results.

- [ ] **Step 4: Review residual risk**

Report macOS runtime coverage separately, do not infer it from Linux/Windows, and list any benchmark regression beyond ±10% for follow-up.
