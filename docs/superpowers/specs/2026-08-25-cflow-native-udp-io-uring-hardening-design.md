# CFlow Native UDP and io_uring Hardening Design

## Background

The CFlow I/O Actor already exposes bounded TCP and UDP operations over epoll,
kqueue, IOCP, and io_uring. The release benchmark currently drives both endpoints
through native backends only for TCP, and the hosted matrix selects epoll on Linux.
Consequently, UDP has backend contract tests but no dual-native benchmark evidence,
while io_uring can be skipped when runtime initialization is unavailable.

This change closes those evidence gaps without changing the public CFlow API.

## Scope

- Permit `CFLOW_NETWORK_PEER=native` for UDP.
- Echo one complete UDP datagram through two independently owned native endpoints.
- Add dual-native throughput evidence for 1 KiB and 4 KiB plus a larger portable
  payload (`65536` TCP, `8192` UDP), in blocking and busy wait modes. The 8 KiB UDP
  cell stays below macOS's runtime datagram limit while preserving cross-host comparability.
- Add an explicit Ubuntu 24.04 io_uring matrix row. Selecting io_uring is mandatory
  in that row: unsupported or policy-rejected initialization fails the job.
- Preserve the existing JSON schema and raw-peer matrix.

Connect, accept, TLS, DNS, backend auto-selection, fallback, and public ABI changes
are outside this hardening slice.

## UDP Data-path Protocol

| Concern | Contract |
| --- | --- |
| Data unit | One datagram with `1..65507` payload bytes where the host permits it. CI uses a portable 8 KiB maximum cell. Partial datagrams are errors. |
| Fact source | The caller-owned `sent` bytes are authoritative; the server buffer and final client buffer are derived copies and must compare exactly. |
| Ownership | The fixture owns sockets and payload buffers. Each accepted Actor operation owns one heap operation wrapper until completion acknowledgement releases it exactly once. |
| Address lifetime | Each heap operation wrapper embeds its own `sockaddr_storage`; submit deep-copies send addresses and redirects receive storage there. The completion callback copies received bytes and length into the probe before acknowledgement, so timeout cleanup never leaves native I/O borrowing a caller stack address. |
| Topology | The benchmark thread is the sole submit/pump/ack owner. Each endpoint has one native backend, one Actor, and one manual Executor. Native workers may publish completion and signal the shared latch. |
| Ordering | Prepost server receive, submit client send, finish both; then prepost client receive, submit server send to the captured source address, and finish both. |
| Capacity | Each endpoint retains the existing fixed request/command capacity. The benchmark has at most one outstanding operation per endpoint. |
| Backpressure | Non-accepted submit returns `TURBO_EBUSY`; there is no retry queue, fallback, overwrite, or unbounded allocation. |
| Failure | Wrong byte count, absent/oversized or unexpected source endpoint, non-OK completion, timeout, acknowledgement failure, or payload mismatch returns an explicit error. |
| Shutdown | Existing close, Actor drain/ack, socket identity forget, backend shutdown, and Executor shutdown order remains authoritative. |
| Observation | Existing errors, rejections, stale completions, P50/P95/P99, CPU, RSS, Echo/s, and application MiB/s fields remain unchanged. |

## CI Evidence Contract

Each job must emit five independent reports for every
`(protocol, payload, wait_mode)` cell. Grouping includes protocol, so the expected
dual-native group count is twelve. Every record must have the selected backend,
`peer_mode=dual-native`, exact application and wire-byte counts, positive Echo/s and
throughput, and zero errors/rejections/stale completions.

The io_uring row uses `CFLOW_NETWORK_BACKEND=io_uring` and
`NETWORK_EXPECTED_BACKEND=io_uring`. It does not accept an epoll report and does not
skip an initialization error. Hosted throughput remains informational rather than a
pass/fail comparison threshold.

## Compatibility and Rollback

The default peer remains raw and the report schema remains
`cflow-network-benchmark/v1`. Existing TCP filenames gain no semantic change; new UDP
files and the io_uring artifact are additive. Rollback removes the new matrix row and
UDP native benchmark branch without migrating data or changing installed headers.
