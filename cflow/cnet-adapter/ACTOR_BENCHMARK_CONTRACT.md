# Phase 3j — Direct CNet vs Domain Actor benchmark contract

Tracking: #1022, #1030, #1033. Integration branch:
`feature/cnet-actor-strategy-composition`. Keep benchmark and SDK/release
gates separate. Do not merge this integration branch into master before
consumer and performance qualification.

## Comparable paths

| Mode | Receive boundary | Business settlement | Owners |
| --- | --- | --- | --- |
| `direct-cnet` | Real CNet `on_receive` | Synchronous equivalent payload validation and settlement counter | Source only |
| `actor-same-owner` | Same real CNet `on_receive` | Bounded route -> typed Mailbox -> Machine action -> ACK | Source = target |
| `actor-cross-owner` | Same real CNet `on_receive` | Bounded route -> typed Mailbox -> Machine action -> ACK -> source wake | Distinct OS threads |

All modes use the same TCP loopback peer, payload bytes, message count, parser
work, native backend and connection/receive-credit settings for a given run.
Keep CNet client progression and ACK semantics explicit. Compare
`receive-callback-to-business-settlement` in every mode; do **not** equate
I/O completion, Mailbox ACCEPTED, or network closure with business ACK.

For direct mode, settlement means execution of the same deterministic payload
validation + counter update as the Actor action, *not* a claimed Actor ACK.
Report transport receive latency separately from domain handoff latency.
Record setup/teardown separately and exclude warmup. Any per-mode difference
in callback work, allocation, batching or progress loop invalidates a direct
latency ratio and must be disclosed.

## Matrix and sampling

- Platform/backend: Linux epoll, Windows IOCP, Darwin kqueue; Linux ARM64
  where executable CI is available. Android/iOS: SDK/ABI build only.
- Owners: 1, 2 and 4 source owners; same-owner or fixed cross-owner target
  as appropriate. Never migrate a live connection.
- Connection grouping: one managed connection / two semantic routes, plus
  independent connections and strict-key placement/credit FULL.
- Payloads: 64 B, 1 KiB and 16 KiB, respecting configured maximum receive
  chunk. State any segmentation observed; one TCP send is not necessarily one
  CNet callback.
- 2,000 warmup logical messages then at least 20,000 sampled business
  completions per mode for statistical runs; fixed message counts and
  bounded backlog. No unbounded retries or implicit loss.
- Repeat each comparable run >= 5 times; publish per-run rows, not just
  a best-case aggregate. Reserve a separate stress profile for cancel,
  terminal, FULL, and ACK/poll races (not in the steady-state latency mean).

## Timing points and ownership

Use `cmeta_hrtime()` on the same monotonic timeline. Every item carries an
owned, generation-safe sample ID; never retain a borrowed CNet callback
pointer. Record:

1. `t_receive`: enter authoritative source `on_receive`.
2. `t_admit`: Actor Mailbox ACCEPTED, or direct-mode equivalent admission.
3. `t_business`: business validation/settlement completed by the target.
4. `t_release`: source owner observes settled context (manager test only).

Report ns p50/p95/p99/max for (3 - 1), and ns (2 - 1) /
(3 - 2) separately for Actor modes. The source may see a completion
after business settlement; do not assert ordering for post-ACK wake.
For `t_release`, report a separate connection-level metric, not a
per-message ACK latency.

All histograms have fixed capacity set before receiving messages. Overflow,
missing samples, negative/nonmonotonic timing, stale generations and
unmatched ACKs must fail the run, not silently disappear. Keep source and
target progress quanta bounded. Explicitly distinguish Actor Mailbox FULL
from route-slot ENOBUFS and CNet receive-credit backpressure.

## Required output (machine-readable rows)

```text
platform,backend,mode,source_owners,target_owners,connections,routes,payload_bytes,run_index,warmup,samples,received,settled,canceled,actor_full,route_full,peak_retained_bytes,owner_hops_total,source_cpu_ns,target_cpu_ns,elapsed_ns,p50_ns,p95_ns,p99_ns,max_ns,transport_errors
```

- `owner_hops_total`: count concrete source-to-target dispatches plus target
  wake-to-source handoffs; exclude source-local calls.
- `cpu_ns/msg`: CPU time **separately** measured on source and target,
  divided by settled logical messages; never substitute wall-clock time for
  CPU time. Mark unsupported platforms explicitly rather than fabricate
  zero values.
- `throughput`: settled logical messages / measured elapsed seconds.
- `peak_retained_bytes`: route-owned bytes observed, distinct from CNet
  receive buffer capacity and manager context hold count.
- `transport_errors`: nonzero fails the benchmark; also publish the failing
  error code.

## Acceptance gates

- [ ] Implement a dedicated opt-in benchmark executable. Do not inject heavy
      measurement into normal CNet core, release tests or NativeIO.
- [ ] Qualify functional equivalence, checksum and EXACT settled count across
      direct/same-owner/cross-owner paths before comparing latency.
- [ ] Cross-platform build and real executable sample test, with no perf
      threshold on shared CI runners.
- [ ] Repeated local runs with raw CSV/JSON rows, median of runs and
      clearly stated system/compiler/CPU/backend.
- [ ] Separate ACK/wake stress from latency runs and retain #1030/#1022
      until consumer qualification (CHttp, FlowMQ, TurboRaft).
