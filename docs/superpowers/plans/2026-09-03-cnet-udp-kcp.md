# CNet UDP Listener And KCP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded caller-driven UDP server socket and a composable bounded KCP session engine to Salts CNet.

**Architecture:** A new `cnet_datagram` public object owns one bound UDP socket and NativeIO backend. A separate `cnet_kcp` object owns protocol/timer state and composes with UDP through borrowed callbacks, keeping UDP peer identity distinct from connection handles.

**Tech Stack:** C11, Salts NativeIO, upstream vcpkg KCP, TinyTest, CMake Presets.

**Spec:** `cnet/DESIGN_UDP_KCP.md`

## Global Constraints

- No hidden I/O thread; all progress stays on the caller-owned poll lane.
- Every retained payload and protocol queue has a configured hard bound.
- Successful UDP send admission produces exactly one terminal callback.
- UDP receive and KCP callback payloads are callback-scoped borrowed views.
- KCP adds no implicit encryption or authentication claim.

---

### Task 1: Public UDP contract

**Files:**
- Modify: `cnet/include/cnet/cnet.h`
- Create: `cnet/tests/cnet_datagram_test.c`
- Modify: `cnet/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cnet_datagram_*`, `cnet_datagram_config`, `cnet_datagram_peer`, and terminal callbacks.

- [x] Write public behavior tests for invalid bounds, bind/port, copied send, demanded receive, saturation, wake, stop and destroy.
- [x] Build `cnet_datagram_test` and confirm compilation fails because the public API is absent.
- [x] Add the documented declarations and confirm the header compiles as C and C++.

### Task 2: NativeIO UDP owner

**Files:**
- Create: `cnet/src/cnet_datagram.c`
- Modify: `cnet/CMakeLists.txt`
- Test: `cnet/tests/cnet_datagram_test.c`

**Interfaces:**
- Consumes: NativeIO attach/submit/observe/cancel/wake/close/destroy.
- Produces: the public UDP behavior from Task 1.

- [x] Implement synchronous bind plus fixed receive, send-slot and completion storage.
- [x] Implement checked peer conversion, copied send admission and receive demand.
- [x] Implement poll callbacks without locks or hot-path allocation.
- [x] Implement stop/cancel/drain/destroy and verify stale lifecycle calls fail fast.
- [x] Run the focused test until all UDP behavior passes.

### Task 3: Bounded KCP session

**Files:**
- Modify: `vcpkg.json`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/include/cnet/cnet.h`
- Create: `cnet/src/cnet_kcp.c`
- Create: `cnet/tests/cnet_kcp_test.c`
- Modify: `cnet/tests/CMakeLists.txt`

**Interfaces:**
- Produces: `cnet_kcp_init`, `cnet_kcp_send`, `cnet_kcp_input`, `cnet_kcp_update`, `cnet_kcp_check`, and `cnet_kcp_destroy`.

- [x] Write tests for config validation, two-session round-trip, fragmentation, loss/retransmit, output failure and send-segment HWM.
- [x] Build `cnet_kcp_test` and confirm compilation fails because the KCP API is absent.
- [x] Add the `kcp` manifest dependency and private `kcp::kcp` linkage.
- [x] Implement the bounded wrapper and drive all callbacks on the caller thread.
- [x] Run the focused KCP test until all behavior passes.

### Task 4: Documentation and regression

**Files:**
- Modify: `cnet/README.md`
- Modify: `cnet/tests/cnet_header_cpp_test.cpp`

**Interfaces:**
- Consumes: Tasks 1-3 public ABI.
- Produces: consumer documentation and installed-header coverage.

- [x] Document ownership, capacity, error and shutdown examples for UDP and KCP composition.
- [x] Build CNet and all adjacent CNet tests with `win-release-user`.
- [x] Run the focused CTest filter, then the complete CNet test label/name set.
- [x] Install the Release profile and compile the installed-package consumer.
