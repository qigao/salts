# CFlow Dual-Native Echo Benchmark Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a cross-platform TCP Echo benchmark in which both client and server use the selected CFlow native backend, with 1 KiB, 4 KiB, and 64 KiB payload evidence comparable to ogrenet's single-pair serial Echo workload.

**Architecture:** Keep the existing raw-peer TCP/UDP benchmark behavior as the default. Add an explicit `CFLOW_NETWORK_PEER=native` mode backed by two independent bounded endpoint owners; each endpoint owns one native backend, Actor, manual Executor, completion probe, and socket identity, while both Actors signal one fixture-owned wake latch. The benchmark thread is the only submit/ack owner; each direction preposts the receiver before the matching sender and pumps both endpoints, then the reverse direction runs after the first is complete. Native reactor/completion threads only publish authoritative completions.

**Tech Stack:** C11, CFlow I/O Actor/native backends, TurboUtils Platform synchronization, TinyTest benchmarks, CMake Presets, GitHub Actions PowerShell report aggregation.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`

## Global Constraints

- Preserve the existing `cflow-network-benchmark/v1` schema and default raw peer behavior.
- `CFLOW_NETWORK_PEER=native` supports TCP only and returns `TURBO_ENOTSUP` for UDP rather than falling back.
- Both native endpoints use the same explicitly selected epoll, kqueue, IOCP, or io_uring backend kind.
- Each endpoint permits at most one outstanding benchmark operation; successful submit owns the heap operation until Actor acknowledge invokes its release callback exactly once.
- The benchmark thread owns submit, manual Actor/Executor driving, completion validation, and acknowledge. Backend threads may publish completion and signal the shared wake latch.
- TCP short sends/receives are completed by bounded paired loops; zero-byte progress is an error.
- Socket identities are forgotten only after both endpoints are quiescent and the original sockets are closed.
- CI uses five independent processes per `(host, payload, wait_mode)` and treats throughput as informational evidence, never a hard gate.

---

### Task 1: Add the peer-mode and report contract

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: `CFLOW_NETWORK_PEER` environment variable.
- Produces: `network_peer_mode`, `network_parse_peer_mode()`, `peer_mode` and `exchanges_per_second` JSON fields.

- [x] **Step 1: Write a failing parser contract test**

Add a TinyTest case that expects `NULL`/`raw` to select `NETWORK_PEER_RAW`, `native` to select `NETWORK_PEER_NATIVE`, and other strings to return `TURBO_EINVAL`.

- [x] **Step 2: Build to verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_network_benchmark
```

Expected: link failure for the declared but not implemented `network_parse_peer_mode()`.

- [x] **Step 3: Implement parsing and compatible report fields**

Parse the environment before fixture creation. Keep schema `v1`; emit `peer_mode: "raw" | "dual-native"` and calculate `exchanges_per_second = total_exchanges / wall_seconds` with a zero-time guard.

- [x] **Step 4: Build and run the parser test GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_network_benchmark
build/Msvc-Release/bin/cflow_network_benchmark.exe --filter "peer mode"
```

---

### Task 2: Implement the dual-native endpoint lifecycle

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Modify: `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`

**Interfaces:**
- Consumes: existing `cflow_io_native_backend_*`, `cflow_io_actor_*`, and manual Executor APIs.
- Produces: `network_fixture_init(..., network_peer_mode, ...)` and a dual-native branch in `network_exchange()`.

- [x] **Step 1: Write a failing real round-trip test**

Add a platform-neutral TinyTest case that initializes TCP with `NETWORK_PEER_NATIVE`, exchanges one literal 1 KiB payload, checks exact equality and zero Actor/native errors, then destroys the fixture. The production change that makes it pass is creation and driving of the second native endpoint; a raw server thread must not satisfy it.

- [x] **Step 2: Run the focused test to verify RED**

```powershell
build/Msvc-Release/bin/cflow_network_benchmark.exe --filter "dual native"
```

Expected: fixture initialization returns `TURBO_ENOTSUP` until the second endpoint exists.

- [x] **Step 3: Refactor the fixture into explicit endpoint owners**

Define `network_endpoint` with backend, Actor, Executor, completion probe, socket identity, and init flags. Initialize the client endpoint for all modes and the server endpoint only for dual-native mode. Both blocking Actors borrow the fixture wake latch; no endpoint destroys it.

- [x] **Step 4: Implement exact serial dual-native Echo**

For TCP, prepost server receive before client send, then prepost client receive before server send. Complete both directions with short-I/O loops and pump both Actors/Executors so either endpoint can make progress even when the payload exceeds the socket send window. Preserve the raw peer and UDP branches unchanged.

- [x] **Step 5: Implement quiescent reverse-order shutdown**

Stop native work, close both original sockets, close/drain/destroy each Actor, forget each closed identity, shutdown/destroy each backend, then shutdown/destroy Executors and finally the shared wake latch. Preserve the first useful cleanup error in the benchmark result path.

- [x] **Step 6: Run focused Windows GREEN verification**

```powershell
cmake --build --preset win-release-user --target cflow_network_benchmark cflow_io_native_test
build/Msvc-Release/bin/cflow_network_benchmark.exe --filter "dual native"
ctest --preset win-release-user -R "^cflow_io_native_test$" --output-on-failure
```

---

### Task 3: Add the comparable CI matrix and artifact report

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**
- Consumes: `CFLOW_NETWORK_PEER=native`, `CFLOW_NETWORK_PAYLOAD`, existing JSON v1 output.
- Produces: five-run raw JSON and Markdown rows for TCP dual-native 1/4/64 KiB in blocking and busy modes.

- [x] **Step 1: Add validation that fails against the old report**

Require `peer_mode == "dual-native"`, the requested payload, zero errors/rejections/stale completions, and positive `exchanges_per_second` for dual-native runs. Before Task 1/2 this validation fails because those fields/mode do not exist.

- [x] **Step 2: Add the bounded matrix**

Run payloads `1024`, `4096`, and `65536`, wait modes `blocking` and `busy`, five processes each, TCP only. Store files under `network/dual-native/` and append median-source rows to the existing network summary without performance thresholds.

- [x] **Step 3: Run local 1/4/64 KiB smoke evidence**

Run one Windows process per payload in blocking mode with reduced samples/exchanges, verify the unique JSON record and exact payload field, then run one full 64 KiB sample.

---

### Task 4: Cross-platform verification and PR evidence

**Files:**
- Update this plan's checkboxes and measurement section.

**Interfaces:**
- Consumes: Windows IOCP local build, Linux epoll remote isolated worktree, GitHub macOS kqueue runner.
- Produces: reproducible 1/4/64 KiB dual-native Echo evidence and PR #95 update.

- [x] **Step 1: Run Windows affected and full Release tests**
- [x] **Step 2: Sync only changed files to the existing `root@eu` diagnostic worktree and run Linux focused tests**
- [x] **Step 3: Run Linux dual-native 1/4/64 KiB five-sample evidence**
- [x] **Step 4: Run `git diff --check` and independent review**
- [ ] **Step 5: Commit, push the existing feature branch, and monitor all PR checks**
- [ ] **Step 6: Download GitHub artifacts and compare native Echo medians with ogrenet #83 using the same application-byte convention**

## Measurement Evidence

- Windows IOCP Release and Linux epoll Release diagnostic worktree, five independent
  processes per cell, throughput median:

| Backend | Payload | Blocking MiB/s | Busy MiB/s | Blocking Echo/s | Busy Echo/s |
| :--- | ---: | ---: | ---: | ---: | ---: |
| IOCP | 1 KiB | 38.57 | 40.65 | 39,496.9 | 41,620.8 |
| IOCP | 4 KiB | 158.87 | 192.79 | 40,671.5 | 49,353.1 |
| IOCP | 64 KiB | 1,449.13 | 1,361.57 | 23,186.1 | 21,785.1 |
| epoll | 1 KiB | 3.59 | 7.40 | 3,676.0 | 7,579.7 |
| epoll | 4 KiB | 11.98 | 15.90 | 3,068.0 | 4,069.5 |
| epoll | 64 KiB | 73.15 | 169.04 | 1,170.4 | 2,704.7 |

All 60 Windows/Linux reports had zero errors, rejections, and stale completions. The remote
diagnostic worktree contains pre-existing readiness experiments, so these values validate
the data path but are not used as clean cross-project performance evidence; PR artifacts are
the authoritative comparison source.
