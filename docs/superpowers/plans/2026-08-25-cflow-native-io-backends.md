# CFlow Native IO Backends Implementation Plan

> **Execution:** Inline in the current isolated worktree. Platform files are
> conditionally compiled, while the shared public protocol evolves in one place.

**Goal:** Add bounded TCP/UDP Actor adapters for epoll, kqueue, IOCP and io_uring.

**Architecture:** A common opaque backend/factory validates operations and exposes
`cflow_io_backend_ops`; epoll/kqueue reuse the Platform readiness state machine through
a controller adapter, while IOCP/io_uring own fixed completion records, then all report
exactly one terminal result to the existing Actor.

**Tech Stack:** C11, POSIX sockets/epoll/kqueue, Winsock IOCP, Linux io_uring ABI,
Salts thread/error/clock primitives, TinyTest, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`

### Task 1: Freeze public behavior with RED tests

**Files:** create `cflow/include/cflow/io_native.h`,
`cflow/tests/cflow_io_native_test.c`; modify CFlow header/test CMake.

Add C/C++ ABI checks plus invalid config, unsupported backend, true TCP/UDP loopback,
terminal completion, cancellation, ownership and shutdown tests. Build the test and
confirm RED because implementation symbols do not exist.

### Task 2: Implement shared protocol and operation validation

**Files:** create `cflow/src/io_native.c`, `cflow/src/io_native_internal.h`,
`cflow/src/io_native_posix.c`; modify `cflow/CMakeLists.txt`.

Implement factory dispatch, Actor ops, exact state/stat contracts, bounded record helper,
common POSIX socket execution and terminal mapping. Keep callbacks/syscalls outside locks.

### Task 3: Implement kqueue and the shared Reactor adapter

**Files:** create `platform/src/readiness_kqueue.c`, its native contract test, and
`cflow/src/io_native_readiness.c`.

Implement kqueue under the existing Platform one-shot contract. Add a CFlow controller
that owns duplicate descriptors/registrations, retries on would-block, closes only after
readiness callback return, and resolves cancel/terminal races. Run the same real backend
contract on Linux/macOS.

### Task 4: Implement Proactor strategies

**Files:** create `cflow/src/io_native_iocp.c`, `cflow/src/io_native_uring.c`.

Implement stable preallocated OVERLAPPED records and GQCS batching; implement bounded
SQ/CQ mappings, serialized SQ publication, CQ consumption and async cancel/NOP control.
Run Windows/Linux real backend tests.

### Task 5: Verify native integration

Run focused `cflow_io_native_test`, existing `cflow_io_actor_test`, readiness tests,
header compatibility and platform-appropriate Release/dev presets. Use CI matrix for
platforms not locally available.
