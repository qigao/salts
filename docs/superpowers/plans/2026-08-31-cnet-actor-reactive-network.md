# CNet Network Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task. Steps use
> checkbox (`- [ ]`) syntax for tracking. Execute inline; do not delegate tasks
> to subagents.

**Goal:** Implement the independent CNet client/session layer described by
issue #194, backed by NativeIO and unrelated to CFlow Actor or Reactive APIs.

**Architecture:** `TurboUtils::CNet` owns generation-checked sessions,
protocols, bounded command/payload storage, I/O owner shards, and callback
dispatch. NativeIO remains its raw operation dependency. CFlow Graph/Event,
Actor, and Reactive are separate modules; Actor and Reactive depend on NativeIO
directly and none of them depends on CNet.

**Tech Stack:** C11, NativeIO, TurboUtils Concurrency (`turbo_threadpool` and
`disruptor`), TinyTest, CMake Presets, BoringSSL, c-ares,
llhttp, Wslay, and upstream KCP.

**Spec:**
`docs/superpowers/specs/2026-08-31-cnet-actor-reactive-network-design.md`

## Global Constraints

- Keep issue #194's checklist synchronized after each merged milestone.
- Use `rg.exe` and `fd.exe` for repository searches and sync CodeGraph before
  each milestone's impact analysis.
- Read the current implementation, test, and one caller for every modified
  public boundary before editing.
- Write the failing focused test first, run it, implement the minimum complete
  behavior, and rerun the focused and adjacent suites.
- Keep `CNET_ENABLE_EXPERIMENTAL` off by default and exclude CNet from install
  exports until Task 9 completes.
- Do not expose a partially implemented scheme. Each scheme is compiled only
  when its complete state, error, shutdown, and protocol tests are present.
- Preserve one CNet/NativeIO owner per shard. No protocol library may create or
  drive a session socket.
- Keep all capacities bounded; validate checked arithmetic before allocation.
- Do not add fallback backends, unbounded queues, hidden retry allocation,
  insecure TLS modes, or compatibility aliases.

---

## Task 1: Add the Experimental CNet Build Boundary

**Files:**

- Modify: `CMakeOptions.cmake`
- Modify: `CMakeLists.txt`
- Create: `cnet/CMakeLists.txt`
- Create: `cnet/src/cnet_internal.h`
- Create: `cnet/tests/CMakeLists.txt`
- Create: `cnet/tests/cnet_build_boundary_test.c`
- Modify: `cmake/VerifyInstalledPackage.cmake`

- [ ] Add a configure test that proves `CNET_ENABLE_EXPERIMENTAL=OFF` creates no
  `TurboUtils::CNet` target and installs no CNet header.
- [x] Add `CNET_ENABLE_EXPERIMENTAL` with default `OFF`; conditionally add the
  `cnet/` subdirectory only when enabled.
- [x] Create private `turbo_cnet_experimental` and TinyTest targets without an
  install rule, export name, or public header.
- [ ] Configure, build, and run the boundary test with the actual Windows preset
  selected through the `cmake-presets` skill.
- [ ] Run `git diff --check` and commit with
  `build(cnet): add experimental module boundary`.

## Task 2: Prove and Implement the Session State Core

**Files:**

- Create: `cnet/src/cnet_session.h`
- Create: `cnet/src/cnet_session.c`
- Create: `cnet/tests/cnet_session_test.c`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CNet/Session.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/CNetSession.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`

- [ ] Write table-driven tests for valid transitions, invalid transitions,
  first-error preservation, one terminal notification, generation reuse, stale
  handles, duplicate close, and stop during every nonterminal state.
- [x] Write a Lean transition model proving terminal exclusivity, no transition
  out of terminal state, and generation increase before slot reuse. Keep the C
  transition table and Lean constructors one-to-one by name.
- [x] Implement a fixed-capacity session table with checked allocation,
  owner-only mutation, and a single transition function.
- [x] Make failed initialization leave the public/internal destination in zero
  state and make destroy reject nonterminal sessions.
- [x] Run the session TinyTest target and `lake build` from
  `formal/cmeta_cflow_calculus`.
- [ ] Commit with `feat(cnet): add verified session state core`.

## Task 3: Add Bounded Inline-Payload Commands

**Files:**

- Create: `cnet/src/cnet_command.h`
- Create: `cnet/src/cnet_command.c`
- Create: `cnet/tests/cnet_command_test.c`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`

- [x] Write failing tests for payload ownership, full-ring rejection,
  oversize rejection, checked resident-memory arithmetic, close/drain, and
  exact view release.
- [x] Build the command plane with TurboUtils `disruptor`; one shard has one
  consumer and any number of producers. Inline each bounded payload in its
  claimed entry; validate before claim so every successful claim publishes.
- [x] Add debug counters for live/peak commands, queued bytes, rejected
  commands, and rejected bytes.
- [x] Add a multi-producer stress test that verifies per-producer order and
  exact-once consumption while close races publication.
- [ ] Run focused tests under AddressSanitizer and ThreadSanitizer presets where
  supported.
- [ ] Commit with `feat(cnet): add bounded command and payload protocol`.

## Task 4: Add NativeIO Asynchronous TCP Connect

**Files:**

- Modify: `native-io/include/turbo/native_io.h`
- Modify: `native-io/src/native_io.c`
- Modify: `native-io/src/native_io_internal.h`
- Modify: `native-io/src/native_io_iocp.c`
- Modify: `native-io/src/native_io_readiness.c`
- Modify: `native-io/src/native_io_io_uring.c`
- Modify: `native-io/tests/native_io_test.c`
- Modify: `native-io/tests/native_io_header_cpp_test.cpp`
- Modify: `native-io/README.md`

- [x] Add header-contract tests for `NATIVE_IO_OPERATION_TCP_CONNECT` using a
  borrowed, already-created stream socket and native peer address.
- [x] Append the operation value without renumbering the six installed
  operation constants.
- [ ] Add backend tests for success, refused connection, cancellation,
  completion exactly once, stale request, and close/drain on IOCP, epoll,
  io_uring, and kqueue.
- [x] Implement `ConnectEx` on IOCP, nonblocking `connect` plus `SO_ERROR` on
  readiness backends, and `IORING_OP_CONNECT` on io_uring. NativeIO continues to
  borrow and never close the socket.
- [x] Update validation and endpoint lane rules so connect is accepted only for
  an unconnected stream endpoint and cannot race read/write admission.
- [ ] Run all NativeIO tests and direct TCP benchmark smoke tests on Windows;
  push the branch so Linux and macOS CI exercise their real backends.
- [ ] Commit with `feat(native-io): add asynchronous tcp connect operation`.

## Task 5: Implement Base CNet TCP, UDP, and Pipe Sessions

Current internal milestone: one owner shard now exercises TCP and connected UDP
over every supported NativeIO backend, plus byte-pipe read/write over backends
whose explicit capability reports Pipe support. Windows uses one duplex named
pipe handle; POSIX uses separate nonblocking read/write FIFO descriptors. The
owner accepts already-resolved socket addresses or bounded host/port input, and
now accepts a copied bounded Pipe name: the private platform adapter maps it to
one overlapped Windows named pipe or the POSIX `<base>.rx`/`<base>.tx` FIFO
pair, owns successful opens, and reports failures at the connect stage. Owner
deadlines and leased callback dispatch are implemented; the public client and
client-level shutdown deadline remain required before this task is complete.

The bounded DNS primitive is now implemented behind the experimental target:
the process control plane reference-counts c-ares initialization, each resolver
owns a fixed query/result mailbox, and the c-ares event thread copies one
address result before issuing a nonblocking owner wake. Query cancellation is
logical and generation checked because c-ares cancellation is channel-wide.
Each owner now drains its resolver mailbox and is the only writer of the
`RESOLVING -> TRANSPORT_CONNECTING` transition. The future client still has to
parse public connect options, select a stable shard, and publish the initial
bounded connect command.

The internal shard set now performs that stable round-robin reservation and
routing for already-normalized connect payloads. It runs exactly one long-lived
owner task per shard on `turbo_threadpool`, routes send/receive/close commands
without migration, propagates the first owner failure, and supports bounded
quiescent stop. Real TCP tests exercise two independent owner tasks in both
directions. This is an implementation detail rather than a new user-visible
runtime concept.

The callback-worker boundary was removed after benchmark evidence showed that
its queue, payload copy, wake, and scheduling dominated small-message latency.
Each shard owner now invokes its generation-checked observer inline after the
NativeIO coroutine handles a completion. The receive view borrows owner storage
only through that callback. Tests cover FIFO order, terminal recycle, callback
reentrancy, stop retry, and exact error propagation.

The owner now uses a generic fixed-capacity Concurrency deadline heap. Connect
deadlines span resolution and NativeIO connect; accepted read and write
requests have independent deadlines. Zero disables each deadline. Expiration
records the session's first failure before cancellation, so a late successful
NativeIO completion only retires its request and cannot reopen the session or
deliver data. The owner still observes every terminal cancellation completion
before recycling storage. Shutdown timeout remains a client-level drain
protocol and is not exposed as a partially implemented owner field.

TCP connect, send, and receive now use NativeIO's owner-affine coroutine await
path. Arbitrary producer threads still publish bounded commands; only the shard
owner starts a frame, and only that owner's completion observation resumes it.
CNet request records retain command leases, deadlines, roles, and cancellation
handles until terminal observation. NativeIO lazily grows and then reuses a
frame pool capped by the same request capacity; owner tests observe both a
retained completed connect frame and an active suspended receive frame.

The internal client-owned dispatcher is now an inline routing boundary invoked
by each shard owner, not a worker or event-ring consumer. It generation-checks
observer routing and invokes the callback directly on that shard owner.
The session table remains the state fact source. A terminal callback must return
and successfully recycle the session before that slot can be registered again.
Drain closes dispatcher admission, requests close for every registered
connection, waits for terminal callback recycle, and only then permits
destruction.

**Files:**

- Create: `cnet/src/cnet_client.c`
- Create: `cnet/src/cnet_dispatcher.c`
- Create: `cnet/src/cnet_owner.c`
- Create: `cnet/src/cnet_uri.c`
- Create: `cnet/src/cnet_resolver.c`
- Create: `cnet/src/cnet_transport.h`
- Create: `cnet/src/cnet_transport_tcp.c`
- Create: `cnet/src/cnet_transport_udp.c`
- Create: `cnet/src/cnet_transport_pipe.c`
- Create: `cnet/tests/cnet_fake_transport_test.c`
- Create: `cnet/tests/cnet_dispatcher_test.c`
- Create: `cnet/tests/cnet_tcp_test.c`
- Create: `cnet/tests/cnet_udp_test.c`
- Create: `cnet/tests/cnet_pipe_test.c`
- Modify: `vcpkg.json`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`

- [ ] Write fake-transport tests for connect/send/receive/state, callback
  serialization, zero demand, send-full, connect timeout, shutdown timeout,
  stale completion, and stop retry after timeout.
- [ ] Write real loopback tests for TCP byte fragmentation/half-close/reset,
  connected UDP datagram boundaries, and platform pipe behavior.
- [x] Implement strict URI parsing with bounded copied components and typed
  scheme dispatch. Reject unknown fields and unavailable schemes.
- [x] Add the pinned c-ares dependency through the repository vcpkg manifest.
- [x] Implement hostname resolution with c-ares asynchronous queries. The
  resolver may own only DNS sockets; copy bounded results to the owning shard
  mailbox before calling NativeIO wake, and generation-check late cancellation
  results.
- [x] Invoke callbacks inline on the owning shard after coroutine completion;
  preserve per-connection FIFO, borrow payloads only through the callback, and
  release each accepted terminal obligation exactly once.
- [ ] Implement one long-lived owner task and NativeIO backend per shard,
  stable connection-to-shard assignment, fixed completion batches, timer
  deadlines, and first-error propagation.
- [x] Add a fixed-capacity single-owner deadline heap and enforce connect,
  read, and write expiration before resolver/NativeIO success observation.
- [ ] Implement TCP over NativeIO connect/read/write, connected UDP over
  datagram operations, and Pipe over NativeIO pipe operations. Keep OS socket
  creation/close in private per-platform transport adapters.
- [ ] Verify callbacks remain nonblocking on the I/O owner and no allocation
  occurs in the owner submit/observe hot path after initialization.
- [ ] Commit with `feat(cnet): add tcp udp and pipe sessions`.

## Task 6: Add the Public Base API and Lifecycle

**Files:**

- Create: `cnet/include/cnet/cnet.h`
- Create: `cnet/include/cnet/cnet_module.h`
- Create: `cnet/tests/cnet_api_test.c`
- Create: `cnet/tests/cnet_header_cpp_test.cpp`
- Create: `cnet/examples/cnet_client.c`
- Create: `cnet/README.md`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`

- [x] Write C and C++ header tests for every public type, enum, callback, and
  function in the design spec.
- [x] Write public API tests showing immediate failure leaves outputs zero,
  connect options are copied, send success owns one bounded copy, receive
  demand maps one-to-one to values, and stop/destroy follow quiescence.
- [x] Expose `cnet_client`, `cnet_connection`, configuration, observer, error,
  connect/send/receive/close, and client lifecycle exactly as specified.
- [ ] Add a complete TCP example whose output and cleanup are asserted by a
  test; do not include unavailable protocol examples.
- [x] Keep the target experimental and uninstalled until Task 9.
- [ ] Commit with `feat(cnet): expose complete base client api`.

## Task 7: Separate CFlow Event, Actor, and Reactive Targets

**Files:**

- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cmake/TurboUtilsConfig.cmake.in`
- Modify: `cmake/VerifyInstalledPackage.cmake`

- [ ] Characterize the current Graph/Stream, Event, Actor, and Reactive public
  headers and link dependencies before moving sources.
- [ ] Export I/O-neutral `TurboUtils::CFlow`, `TurboUtils::CFlowEvent`, and the
  NativeIO-dependent `TurboUtils::CFlowActor` and
  `TurboUtils::CFlowReactive` targets without compatibility aliases.
- [ ] Keep Event independent from NativeIO. Actor and Reactive link NativeIO
  directly and contain no CNet include, type, or session adapter.
- [ ] Add installed-package C and C++ link tests for all four targets and rerun
  the existing Actor, Reactive, Event, Graph, and Stream suites.
- [ ] Commit with `refactor(cflow): split event actor and reactive targets`.

## Task 8: Add TLS, WebSocket, and KCP Feature Targets

**Files:**

- Modify: `vcpkg.json`
- Create: `vcpkg-ports/wslay/vcpkg.json`
- Create: `vcpkg-ports/wslay/portfile.cmake`
- Create: `vcpkg-ports/kcp/vcpkg.json`
- Create: `vcpkg-ports/kcp/portfile.cmake`
- Create: `vcpkg-ports/kcp/per-session-allocator.patch`
- Create: `vendor/reed/CMakeLists.txt`
- Create: `vendor/reed/gf256.h`
- Create: `vendor/reed/gf256.c`
- Create: `vendor/reed/tests/test_gf256.c`
- Modify: `vendor/CMakeLists.txt`
- Modify: `CMakeOptions.cmake`
- Create: `cnet/src/cnet_tls.c`
- Create: `cnet/src/boringssl_linkage.cpp`
- Create: `cnet/src/cnet_websocket.c`
- Create: `cnet/src/cnet_kcp.c`
- Create: `cnet/src/cnet_kcp_secure.c`
- Create: `cnet/src/cnet_kcp_fec.c`
- Create: `cnet/tests/cnet_tls_test.c`
- Create: `cnet/tests/cnet_websocket_test.c`
- Create: `cnet/tests/cnet_kcp_test.c`
- Create: `cnet/tests/fixtures/kcp_wire_v1_vectors.h`
- Create: `cnet/tests/fixtures/cnet_test_ca.pem`
- Create: `cnet/tests/fixtures/cnet_test_server.pem`
- Create: `cnet/tests/fixtures/cnet_test_server_key.pem`
- Modify: `cnet/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`

- [ ] Add pinned vcpkg dependencies for BoringSSL and llhttp. Add Wslay
  and KCP overlay ports with upstream source, checksums, licenses, and an exact
  record of local patches.
- [ ] Migrate `vendor/reed/gf256.c`, `gf256.h`, and their tests from the neutral
  vendor component at `C:/projects/cpp/turbonet/turbonet/vendor/reed`, retaining
  the `xtaci/libkcp` commit, copyright, and MIT-license provenance. Add a
  caller-provided reconstruction-workspace API and verify the original and new
  APIs against the same vectors.
- [ ] Add `CNET_ENABLE_TLS`, `CNET_ENABLE_WEBSOCKET`, and `CNET_ENABLE_KCP`.
  Make each option fail configuration when its dependency is unavailable; WSS
  requires both TLS and WebSocket.
- [ ] Write TLS tests for mandatory chain/hostname verification, partial
  records, WANT_READ/WANT_WRITE, connect timeout, and orderly/abrupt shutdown.
- [ ] Implement TLS with BoringSSL memory BIOs so all encrypted bytes still flow
  through CNet buffers and NativeIO. Add the private C++ linkage translation
  unit required by BoringSSL while keeping installed CNet headers C11-only.
- [ ] Write WebSocket corpus tests for Upgrade validation, fragmented
  text/binary messages, masking, control frames, invalid lengths/UTF-8, bounded
  reassembly, and close handshake.
- [ ] Implement HTTP Upgrade parsing with llhttp and frame parsing/serialization
  with Wslay's low-level frame API; neither library may access sockets or own a
  message queue.
- [ ] Write deterministic KCP tests with a fake clock and packet link covering
  authenticated handshake/retry, wrong PSK, replay, tampered data/FEC shards,
  loss, duplication, reordering, recovery within parity limits, failure beyond
  parity limits, derived conversation mismatch, timer scheduling, window
  exhaustion, and shutdown drain. Add checked-in golden vectors for the
  existing `TKSH`, `TKSR`, and `TKF1` wire version and verify both encode and
  decode without linking the replacement runtime.
- [ ] Patch KCP minimally for per-control-block allocator context. Implement its
  segment/ack/protocol storage with a session-owned fixed pool, then connect
  upstream output/input callbacks to UDP and the CNet owner timer heap. Keep
  message mode and derive a non-zero conversation ID from the authenticated
  session epoch.
- [ ] Implement the mandatory KCP PSK handshake, keyed derivation, AEAD/replay
  protection, and FEC-frame MAC with the existing private Monocypher target.
  Encode Reed-Solomon shards with the migrated `gf256` target in the order
  `KCP -> AEAD -> FEC -> UDP`; do not add raw, unauthenticated, or FEC-disabled
  fallback modes.
- [ ] Run each feature's focused suite independently and with all features on.
- [ ] Commit with `feat(cnet): add tls websocket and kcp transports`.

## Task 9: Install CNet and Verify Consumers

**Files:**

- Modify: `cnet/CMakeLists.txt`
- Modify: `CMakeLists.txt`
- Modify: `cmake/VerifyInstalledPackage.cmake`
- Modify: `cmake/TurboUtilsConfig.cmake.in`
- Modify: `README.md`
- Modify: `cnet/README.md`
- Create: `cmake/verify/cnet_consumer/CMakeLists.txt`
- Create: `cmake/verify/cnet_consumer/main.c`

- [ ] Add installed-package tests that link `TurboUtils::CNet` from C and C++,
  with every enabled protocol feature represented in package metadata.
- [ ] Remove the `experimental` target spelling, export `TurboUtils::CNet`, and
  install only complete public headers.
- [ ] Remove `CNET_ENABLE_EXPERIMENTAL` and build/install the stable base CNet
  module unconditionally, matching NativeIO and CFlow; preserve explicit
  per-protocol feature options.
- [ ] Build and test Debug, Release, installed-package, ASan, and platform CI on
  Windows, Linux, and macOS.
- [ ] Update issue #194 with the exact supported scheme/platform matrix.
- [ ] Commit with `feat(cnet): install the stable network targets`.

## Task 10: Add Comparable Performance and Capacity Reports

**Files:**

- Create: `cnet/benchmarks/CMakeLists.txt`
- Create: `cnet/benchmarks/cnet_benchmark.c`
- Create: `.github/scripts/cnet-benchmark-stats.ps1`
- Modify: `.github/workflows/cflow-release-benchmark.yml`
- Modify: `cnet/README.md`

- [ ] Write parser tests before the workflow change. Reject missing/nonfinite
  metrics, mismatched transport/payload/window/thread topology, and percentile
  mismatches.
- [ ] Add direct NativeIO, direct CNet, Actor/NativeIO, and Reactive/NativeIO rows using
  identical peers and parameters for TCP, UDP, Pipe, TLS, WS/WSS, and KCP where
  supported.
- [ ] Split tables by transport and payload. Report p50/p95/p99, operations/s,
  MiB/s, CPU, payload copies, pool peaks, batch sizes, submit/observe, protocol,
  and callback-dispatch time.
- [ ] Run local Windows Release benchmark smoke tests. Use the configured remote
  Linux development host for profiles requiring Linux backends, then let GitHub
  CI provide the final Windows/Linux/macOS dataset.
- [ ] Document measured overhead and identified stages without turning noisy
  runner data into a correctness gate.
- [ ] Commit with `bench(cnet): compare direct actor and reactive transports`.

## Final Review Checklist

- [ ] Run `codegraph affected` for all changed public headers and CMake exports.
- [ ] Run `rg.exe -n "TODO|TBD|FIXME|HACK|Not implemented|assert\\(false\\)" cnet`
  and remove every unowned placeholder.
- [ ] Confirm NativeIO and CFlow contain no CNet include or link dependency.
- [ ] Confirm no public header exposes BoringSSL, llhttp, Wslay, KCP, Monocypher,
  GF256, OS socket, or
  internal pool types.
- [ ] Confirm every accepted connection, command, receive-buffer lease, and NativeIO
  request reaches one documented terminal outcome.
- [ ] Run `git diff --check`, focused tests, adjacent NativeIO/CFlow tests,
  installed-package verification, and the platform CI matrix before claiming
  completion.
