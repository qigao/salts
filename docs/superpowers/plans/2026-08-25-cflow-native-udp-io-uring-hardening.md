# CFlow Native UDP and io_uring Hardening Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add dual-native UDP Echo evidence and a mandatory hosted io_uring benchmark path without changing public APIs.

**Architecture:** Reuse the existing two-endpoint Actor fixture. UDP preposts a receive before each send, captures the receive operation's published source-address length in the completion probe, and echoes the exact datagram back through the other native endpoint. CI expands grouping by protocol and adds a dedicated Ubuntu 24.04 io_uring row.

**Tech Stack:** C11, CFlow I/O Actor/native backends, TurboUtils Platform synchronization, TinyTest benchmark assertions, GitHub Actions PowerShell, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-25-cflow-native-udp-io-uring-hardening-design.md`

## Global Constraints

- Preserve `cflow-network-benchmark/v1` and default `CFLOW_NETWORK_PEER=raw` behavior.
- Do not change `cflow/io_native.h`, Actor, Executor, or backend public contracts.
- Native UDP payload is one atomic datagram in `1..65507`; short send/receive is an error.
- Keep at most one outstanding operation per endpoint and acknowledge every delivered completion exactly once.
- Explicit io_uring selection must fail fast; no epoll, thread, or blocking-I/O fallback.
- Performance values are evidence, not hard cross-host thresholds.

---

### Task 1: Dual-native UDP Echo

**Files:**
- Modify: `cflow/benchmarks/cflow_network_benchmark.c`
- Test: `cflow/benchmarks/cflow_network_benchmark.c`

**Interfaces:**
- Consumes: existing `network_submit_native_operation()`, `network_finish_native_operation()`, `cflow_io_native_operation`, and two-endpoint fixture lifecycle.
- Produces: internal UDP paired transfer and source-address capture; no installed symbol.

- [x] **Step 1: Write the failing UDP behavior test**

Add one TinyTest case beside the TCP dual-native test:

```c
it("round trips one UDP datagram through dual native endpoints") {
  cflow_io_native_backend_kind backend_kind = CFLOW_IO_NATIVE_EPOLL;
  unsigned char sent[1024];
  unsigned char received[sizeof(sent)] = {0};
  network_fixture fixture = {0};
  int status = network_select_backend(&backend_kind);

  memset(sent, 0x5a, sizeof(sent));
  check_equal(status, TURBO_OK);
  check_equal(network_fixture_init(&fixture, NETWORK_PROTOCOL_UDP,
                                   backend_kind, NETWORK_WAIT_BLOCKING,
                                   NETWORK_PEER_NATIVE, 1u, sizeof(sent)), TURBO_OK);
  check_equal(network_exchange(&fixture, NETWORK_PROTOCOL_UDP,
                               (unsigned char *)sent, received, sizeof(sent), 1u), TURBO_OK);
  check_equal(received, sent, sizeof(sent));
  check_equal(network_fixture_destroy(&fixture), TURBO_OK);
}
```

The production mutation caught is restoring the TCP-only native peer guard or losing
the UDP source address before the response send.

- [x] **Step 2: Run the focused executable and verify RED**

Build with `win-release-user`, then run:

```text
build\Msvc-Release\bin\cflow_network_benchmark.exe --filter "round trips one UDP datagram through dual native endpoints" --no-color
```

Expected: FAIL because `network_fixture_init()` returns `TURBO_ENOTSUP`.

- [x] **Step 3: Implement the minimal UDP paired path**

Extend `network_completion_probe` with the address length observed from
`network_operation.native.address_length`. Add an optional address-length output to
the internal completion finish helper. Implement this sequence in `network_exchange()`:

```c
server_recvfrom(source_address)
client_sendto(server_address)
finish(client_send)
finish(server_recv, &source_length)
client_recvfrom(response_address)
server_sendto(source_address, source_length)
finish(server_send)
finish(client_recv)
```

Validate `source_length > 0`, `source_length <= sizeof(source_address)`, both sent and
received byte counts equal `payload_size`, and final payload equality. Remove only the
benchmark's TCP-only native guard.

- [x] **Step 4: Run focused and adjacent tests GREEN**

Run the focused UDP case, the large TCP window case, the pending-cleanup case, and
`cflow_io_native_test`. Expected: all pass with zero failed assertions.

- [x] **Step 5: Commit the behavior slice**

```text
git add cflow/benchmarks/cflow_network_benchmark.c
git commit -m "bench(cflow): add dual-native UDP echo"
```

---

### Task 2: Protocol-aware CI matrix and mandatory io_uring

**Files:**
- Modify: `.github/workflows/cflow-release-benchmarks.yml`
- Modify: `docs/superpowers/specs/2026-08-25-cflow-native-io-backends-design.md`

**Interfaces:**
- Consumes: JSON `protocol`, `backend`, `peer_mode`, byte counts, outcomes, and performance fields.
- Produces: twelve five-run protocol/payload/wait groups per host plus a dedicated `cflow-release-ubuntu-24.04-io-uring` artifact.

- [x] **Step 1: Add a workflow validation that fails against the old matrix**

Change the dual-native loop to the declared matrix:

```powershell
$nativePayloads = @{
  tcp = @(1024, 4096, 65536)
  udp = @(1024, 4096, 65507)
}
foreach ($protocol in @("tcp", "udp")) {
  foreach ($payloadBytes in $nativePayloads[$protocol]) {
    foreach ($waitMode in @("blocking", "busy")) {
      # run and validate one report
    }
  }
}
```

Group by `protocol, payload_bytes, wait_mode` and require twelve groups. Before Task 1,
the UDP native process exits non-zero with `TURBO_ENOTSUP`, proving the workflow catches
the missing behavior.

- [x] **Step 2: Add the explicit io_uring host row**

Duplicate the Ubuntu 24.04 build configuration under host
`ubuntu-24.04-gcc-io-uring` and set `network_backend: io_uring`. Preserve the existing
epoll Ubuntu row. The existing expected-backend validation makes unsupported init,
fallback, or the wrong report backend fail the job.

- [x] **Step 3: Validate PowerShell syntax and report arithmetic locally**

Parse the explicit `shell: pwsh` run block with `ScriptBlock::Create`. Run one reduced
Windows dual-native UDP process for each wait mode and verify:

```text
attempted = samples * exchanges_per_sample
application_bytes = attempted * payload_bytes
wire_bytes = 2 * application_bytes
errors = rejections = stale_completions = 0
```

- [x] **Step 4: Update design documentation**

Change the existing native-backend design's performance section from TCP-only native
peer to TCP/UDP native peer, describe the protocol-specific legal maxima, and retain
the raw-peer compatibility statement.

- [x] **Step 5: Commit the CI slice**

```text
git add .github/workflows/cflow-release-benchmarks.yml docs/superpowers/specs
git commit -m "ci(cflow): cover native UDP and io_uring"
```

---

### Task 3: Cross-platform verification and evidence

**Files:**
- Modify: `docs/superpowers/plans/2026-08-25-cflow-native-udp-io-uring-hardening.md`

**Interfaces:**
- Consumes: Windows IOCP local tests, Linux epoll/io_uring isolated-host tests, and GitHub artifacts.
- Produces: reproducible correctness and performance evidence attached to PR #95.

- [ ] **Step 1: Run local Release verification**

Run the benchmark target, `cflow_io_native_test`, affected readiness tests, then the
full `win-release-user` CTest suite. Record exact pass/fail counts.

- [ ] **Step 2: Verify Linux epoll and io_uring without fallback**

In the existing isolated `root@eu` worktree, build the benchmark and run reduced
dual-native UDP with `CFLOW_NETWORK_BACKEND=epoll` and `io_uring`. Assert the JSON
backend exactly matches the request and all outcomes are zero.

- [ ] **Step 3: Run review and source checks**

Run `codegraph sync .`, `git diff --check`, inspect the complete diff, and obtain an
independent HIGH/MED review. Resolve every HIGH/MED before pushing.

- [ ] **Step 4: Push and monitor PR checks**

Push the feature branch and monitor the full PR matrix until terminal. Any io_uring
unsupported/policy failure is a real failed acceptance criterion, not a skip.

- [ ] **Step 5: Validate artifacts and publish evidence**

Download all artifacts, verify each normal host has sixty dual-native reports and the
io_uring host reports backend `io_uring`; verify zero errors/rejections/stale
completions, summarize TCP/UDP medians, and add the evidence to PR #95.
