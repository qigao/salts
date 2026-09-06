# CNet VSOCK Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add Linux AF_VSOCK SOCK_STREAM client and listener support to CNet without changing existing TCP, TLS, UDP, or Pipe ABI and behavior.

**Architecture:** Treat VSOCK as a socket-backed ordered byte stream. NativeIO exposes transport-neutral stream operation names while retaining TCP aliases; CNet owns strict URI parsing, copied sockaddr_vm storage, explicit owner dispatch, and additive listener/adoption APIs. Unsupported platforms fail with SALTS_ENOTSUP and never fall back to TCP or Pipe.

**Tech Stack:** C11, CMake presets, NativeIO epoll/io_uring/IOCP contracts, TinyTest, Linux AF_VSOCK.

**Spec:** https://github.com/qigao/salts/issues/220

## Global Constraints

- Phase 1 supports plaintext VSOCK only; TLS options with vsock:// are rejected.
- Outbound syntax is exactly vsock://CID:PORT with checked decimal uint32 fields.
- CID_ANY and PORT_ANY are listener-only; outbound connects reject UINT32_MAX.
- Existing public structures retain their layouts; new public API is additive.
- URI/address/observer data is copied before successful admission returns.
- CNet remains caller-driven and bounded; no worker thread, fallback, or new queue is added.
- Linux uses socket(AF_VSOCK, SOCK_STREAM, 0); other platforms return SALTS_ENOTSUP.

---

### Task 1: Strict VSOCK URI model

**Files:**
- Modify: `cnet/src/cnet_uri.h`
- Modify: `cnet/src/cnet_uri.c`
- Test: `cnet/tests/cnet_uri_test.c`

**Interfaces:**
- Produces: `CNET_URI_VSOCK`, `cnet_uri.vsock_cid`, and `cnet_uri.vsock_port`.
- Preserves: the existing uint16 network `port` field and TCP/TLS/UDP/Pipe parsing.

- [x] **Step 1: Write failing URI behavior tests**

Add literal assertions for `vsock://2:5000`, uint32 upper boundaries, malformed delimiters, signs, whitespace, path/query/fragment input, and reserved outbound ANY values.

```c
check_equal(cnet_uri_parse("vsock://2:5000", &uri), SALTS_OK);
check_equal(uri.scheme, CNET_URI_VSOCK);
check_equal(uri.vsock_cid, UINT32_C(2));
check_equal(uri.vsock_port, UINT32_C(5000));
check_equal(cnet_uri_parse("vsock://4294967295:5000", &uri), SALTS_EINVAL);
check_equal(cnet_uri_parse("vsock://2:4294967295", &uri), SALTS_EINVAL);
check_equal(cnet_uri_parse("vsock://2:4294967296", &uri), SALTS_ERANGE);
```

- [x] **Step 2: Run RED**

Run the `cnet_uri_test` target and CTest case through `win-release-user`; expect compilation failure because `CNET_URI_VSOCK` and uint32 fields do not exist.

- [x] **Step 3: Implement bounded parsing**

Add an exact `vsock://` prefix path before UriParser. Parse both components digit-by-digit with checked arithmetic, require exactly one colon and non-empty fields, reject UINT32_MAX, and clear the output on every error.

- [x] **Step 4: Run GREEN**

Build and run `cnet_uri_test`; expect all URI tests to pass.

- [x] **Step 5: Commit**

Commit the URI test and implementation as `feat(cnet): parse strict vsock URIs`.

### Task 2: Transport-neutral NativeIO stream operations

**Files:**
- Modify: `native-io/include/salts/native_io.h`
- Modify: `native-io/src/native_io.c`
- Modify: `native-io/src/native_io_internal.h`
- Modify: readiness/io_uring/IOCP backend comparisons that name TCP operations
- Test: `native-io/tests/native_io_test.c`
- Test: `native-io/tests/native_io_header_cpp_test.cpp`

**Interfaces:**
- Produces: `NATIVE_IO_OPERATION_STREAM_RECV`, `STREAM_SEND`, and `STREAM_CONNECT` with numeric values 1, 2, and 7.
- Preserves: `NATIVE_IO_OPERATION_TCP_*` as aliases with identical values and ABI.

- [x] **Step 1: Write failing header and stream-socket tests**

Assert alias equality in the C++ header test. On POSIX, exercise STREAM_RECV/STREAM_SEND with a real connected SOCK_STREAM fixture so tests catch an incorrect resource mapping.

- [x] **Step 2: Run RED**

Build `native_io_test` and `native_io_header_cpp_test`; expect missing STREAM enumerators.

- [x] **Step 3: Add compatible aliases and neutral contracts**

Declare STREAM values first, alias the TCP names, update operation validation/resource mapping/backend comparisons to the neutral names, and document attach_socket as accepting stream operations for any supported SOCK_STREAM family.

- [x] **Step 4: Run GREEN**

Build and run both NativeIO tests; expect existing TCP behavior and new stream names to pass.

- [x] **Step 5: Commit**

Commit as `refactor(native-io): generalize TCP operations to streams`.

### Task 3: VSOCK client transport and owner dispatch

**Files:**
- Create: `cnet/src/cnet_transport_vsock.c`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/src/cnet_transport.h`
- Modify: `cnet/src/cnet_client.c`
- Modify: `cnet/src/cnet_owner.c`
- Modify: `cnet/src/cnet_owner.h`
- Modify: `cnet/include/cnet/cnet.h`
- Test: `cnet/tests/cnet_transport_test.c`
- Test: `cnet/tests/cnet_owner_test.c`
- Test: `cnet/tests/cnet_api_test.c`
- Test: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Produces: CNet VSOCK constants, `cnet_transport_vsock_supported`, `cnet_transport_vsock_address`, `cnet_transport_vsock_prepare_connect`, `cnet_transport_adopt_vsock`, and `cnet_client_adopt_vsock`.
- Consumes: Task 1 URI fields and Task 2 NativeIO stream operations.

- [x] **Step 1: Write failing public and platform tests**

Add header compilation/use checks for the constants and adoption declaration. Add transport tests that expect Windows to return SALTS_ENOTSUP while clearing address/operation output. Add API tests that expect `cnet_connect` with a valid VSOCK URI to fail immediately with SALTS_ENOTSUP on Windows and leave the connection zero without callbacks.

- [x] **Step 2: Run RED**

Build the four CNet tests; expect missing public/internal interfaces.

- [x] **Step 3: Implement the platform adapter**

On Linux include `<linux/vm_sockets.h>`, construct a fully zeroed `sockaddr_vm`, assert it fits `CNET_OWNER_ADDRESS_CAPACITY`, create a nonblocking AF_VSOCK/SOCK_STREAM/protocol-0 socket, attach it to NativeIO, and describe a STREAM_CONNECT operation. On other platforms clear outputs and return SALTS_ENOTSUP.

- [x] **Step 4: Implement admission and exhaustive owner routing**

Reject TLS fields for VSOCK, copy the VSOCK sockaddr into the owner payload, bypass resolver submission, add explicit operation-kind helpers for read/write, and route connect/adopt through VSOCK-specific transport functions. Unknown schemes return SALTS_EINVAL rather than falling through to Pipe.

- [x] **Step 5: Run GREEN and adjacent regression tests**

Build and run `cnet_transport_test`, `cnet_owner_test`, `cnet_api_test`, and `cnet_header_cpp_test`, then rerun URI and NativeIO tests.

- [ ] **Step 6: Commit**

Commit as `feat(cnet): add vsock client transport`.

### Task 4: Additive VSOCK listener lifecycle

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Modify: `cnet/src/cnet_listener.c`
- Modify: `cnet/src/cnet_client_internal.h`
- Modify: `cnet/src/cnet_client.c`
- Test: `cnet/tests/cnet_api_test.c`
- Test: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Produces: `cnet_vsock_peer`, versioned `cnet_vsock_listener_config`, `cnet_listener_init_vsock`, `cnet_listener_vsock_local`, `cnet_listener_accept_vsock`, and `cnet_listener_accept_vsock_peer`.
- Preserves: existing TCP listener init/port/accept/TLS semantics and layout.

- [ ] **Step 1: Write failing API and validation tests**

On Windows assert VSOCK listener init returns SALTS_ENOTSUP without publishing an impl. Assert invalid size, zero backlog, excessive backlog, unsupported backend, wrong listener kind, and null outputs return the documented portable errors. Add C++ aggregate initialization coverage.

- [ ] **Step 2: Run RED**

Build `cnet_api_test` and `cnet_header_cpp_test`; expect missing listener types/functions.

- [ ] **Step 3: Implement listener kind and Linux lifecycle**

Add an internal TCP/VSOCK kind, use socket(AF_VSOCK, SOCK_STREAM, 0), nonblocking bind/listen, and getsockname/accept conversion for full uint32 CID/port. VSOCK accept transfers ownership through the VSOCK adoption path exactly once. Existing wait/close/destroy stay common; TCP peer and TLS entry points reject a VSOCK listener with SALTS_ENOTSUP.

- [ ] **Step 4: Run GREEN and listener regressions**

Build and run API/header tests, then all CNet tests. On Linux additionally run the same suite for epoll and io_uring-capable configurations; isolate runtime-dependent AF_VSOCK loopback coverage from unconditional validation tests.

- [ ] **Step 5: Commit**

Commit as `feat(cnet): add vsock listener support`.

### Task 5: Documentation and final verification

**Files:**
- Modify: `cnet/README.md`
- Modify: `native-io/README.md`

**Interfaces:**
- Documents: URI grammar, 32-bit addressing, supported platforms/backends, ownership, unsupported socket policy, plaintext security, and live-migration disconnect behavior.

- [ ] **Step 1: Update contracts and examples**

Document `vsock://2:5000`, listener ANY constants, no DNS, no implicit TLS/reconnect, and fail-fast unsupported-platform behavior. Describe NativeIO STREAM names and TCP aliases.

- [ ] **Step 2: Run formatting and focused verification**

Run the repository formatter for changed C/C++ files if configured, then fresh-build the affected targets and run all NativeIO and CNet CTest cases with `--output-on-failure`.

- [ ] **Step 3: Inspect diff and requirement coverage**

Check `git diff --check`, review every changed public declaration and ownership/error path, and map each issue acceptance criterion to a passing test or documented platform limitation.

- [ ] **Step 4: Commit**

Commit as `docs(cnet): document vsock transport`.
