# CFlow Native TCP Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add bounded asynchronous TCP accept and connect operations with one ownership and error contract across IOCP, io_uring, epoll, and kqueue.

**Architecture:** Extend the caller-owned native operation descriptor with two additive kinds and one accepted-socket result. Each backend retains provisional accept ownership inside its existing bounded request record, publishes the result immediately before the Actor's authoritative completion, and closes it on every non-transfer terminal path. Existing Actor, Executor, capacity, cancellation, identity, and shutdown boundaries remain unchanged.

**Tech Stack:** C11, Winsock IOCP/AcceptEx/ConnectEx, Linux io_uring, Salts readiness reactor (epoll/kqueue), CFlow I/O Actor, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-tcp-lifecycle-design.md`

## Global Constraints

- Preserve numeric values and behavior of all existing native I/O operations.
- Keep caller sockets caller-owned; only a provisional accepted socket is backend-owned.
- Transfer an accepted socket only when Actor terminal completion is accepted.
- Never mutate blocking flags on caller-owned POSIX descriptors.
- Use existing bounded records and workers; add no queue, pool, worker, or fallback.
- Keep every accepted request paired with exactly one terminal Actor completion.

---

### Task 1: Define and validate the additive public contract

**Files:**
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/src/io_native.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_io_native_test.c`

- [x] **Step 1: Add failing validation tests**

Add TinyTest cases for valid accept/connect shapes and reject missing result storage, nonempty lifecycle buffers, malformed optional peer output, missing destination, and invalid destination lengths.

- [x] **Step 2: Build the focused target and record RED**

```powershell
cmake --build --preset win-release-user --target cflow_io_native_test
```

Expected: compilation fails because the two operation enumerators and invalid-socket constant do not exist.

- [x] **Step 3: Add enum, result field, documentation, and validation**

Append both enum values and `result_socket`; define the portable invalid sentinel and implement kind-specific fail-fast validation without changing old operation shapes.

- [x] **Step 4: Build and run validation GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_io_native_test
build/Msvc-Release/bin/cflow_io_native_test.exe --filter "operation contract"
```

### Task 2: Implement and prove IOCP lifecycle operations

**Files:**
- Modify: `cflow/src/io_native_iocp.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

- [x] **Step 1: Add failing loopback and cancellation tests**

Submit accept and connect before pumping the Actor, wait for both completions,
verify peer endpoints and exact data transfer, then cover a cancelled accept that
leaves the result invalid and permits request-slot reuse.

- [x] **Step 2: Run focused IOCP tests and record RED**

Expected: lifecycle submissions complete as `FAILED`/`SALTS_EINVAL` until IOCP handles the new kinds.

- [x] **Step 3: Implement bounded AcceptEx and ConnectEx records**

Resolve provider extensions, create/close provisional accepted sockets, bind
ConnectEx sockets when required, apply update-context options, normalize accept
results, and handle submit rollback, cancellation, stale Actor completion, and
shutdown without changing caller socket ownership.

- [x] **Step 4: Run focused IOCP tests GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_io_native_test
ctest --preset win-release-user -R "^cflow_io_native_test$" --output-on-failure
```

### Task 3: Implement readiness and io_uring parity

**Files:**
- Modify: `cflow/src/io_native_readiness.c`
- Modify: `cflow/src/io_native_io_uring.c`
- Modify: `cflow/tests/cflow_io_native_test.c`

- [x] **Step 1: Add platform-neutral lifecycle assertions**

Route the same lifecycle helper through epoll/kqueue and through io_uring when
runtime initialization succeeds. Assert nonblocking accepted descriptors,
zero-byte success, cancellation cleanup, stats, quiescence, and forget behavior.

- [x] **Step 2: Implement readiness accept/connect**

Map accept to the read lane and connect to the write lane, verify caller
nonblocking flags, preserve connect-started state across rearm, use `SO_ERROR`
for completion, and close provisional results on every non-transfer path.

- [x] **Step 3: Implement io_uring accept/connect**

Prepare `IORING_OP_ACCEPT`/`IORING_OP_CONNECT`, keep record-owned address state,
and apply the same provisional result and Actor-transfer protocol.

- [ ] **Step 4: Compile Linux paths and run platform tests**

Use the repository Linux Release preset or GitHub CI. No unavailable backend is
silently substituted.

### Task 4: Documentation, review, and delivery

**Files:**
- Modify: `cflow/include/cflow/io_native.h`
- Modify: `cflow/README.md`
- Update: this plan

- [x] **Step 1: Document lifecycle usage and ownership**
- [x] **Step 2: Run format/diff checks and self-review ownership/error paths**
- [x] **Step 3: Run focused and full Windows Release verification**

```powershell
cmake --build --preset win-release-user
ctest --preset win-release-user -R "cflow_io_native_test|cflow_header_cpp_test" --output-on-failure
ctest --preset win-release-user --output-on-failure
git diff --check
```

- [ ] **Step 4: Commit, push, and open a PR closing #101**
- [ ] **Step 5: Monitor GitHub checks and fix in-scope failures**
