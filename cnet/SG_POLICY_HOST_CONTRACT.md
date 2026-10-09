# CNet strategies on NativeIO SG Owner lanes

**Scope:** experimental *host-composed* Server + Client policy admission over the
existing CNet and NativeIO Sharded Graph APIs. This document and the associated
CTest do **not** introduce a new runtime, scheduler, parser, policy registry,
public ABI, or permission to modify the Salts 2.3 release candidate.

Tracking: [#1050](https://github.com/qigao/salts/issues/1050),
[#1051](https://github.com/qigao/salts/issues/1051),
[#1057](https://github.com/qigao/salts/issues/1057). Independent released-SDK
admission remains [#1081](https://github.com/qigao/salts/issues/1081).

## Actual ownership boundaries

| Layer | Responsibility | Forbidden substitution |
| --- | --- | --- |
| Application / optional ACE Configurator | Read deployment config, validate kind/version, build one immutable policy plan and Owner topology before startup | No CNet file-parser dependency, hot-swap, per-message lookup or implicit fallback |
| CNet Server placement | Choose an advisory **final local SG Owner** at new connection admission; actual handoff or local CNet reservation commits it | Do not migrate established connections or confuse pressure snapshots with credits |
| CNet Client destination / pool / recovery | Choose a permitted endpoint on new dial, then separately reserve real compatible pool/protocol capacity, connect and qualify protocol READY | No transport retry of application DATA, new threads/backend or default protocol fallback |
| NativeIO SG | Fixed Shard/Owner workers, one backend per shard, bounded routed tasks, long-lived host lease and single authoritative observe | No HTTP/FlowMQ-aware policies or default handler executor |
| CNet core | Transport, TLS, completion routing, callback affinity, real connection terminal and context lifecycle | No Actor/Mailbox shim or second NativeIO observe |

An ACE Configurator **may** use a YML/JSON file at the application level, but
must pass validated built-in CNet policy kind + versioned bounded parameters to
owner-local CNet code. This preserves [#1001](https://github.com/qigao/salts/issues/1001):
no mandatory Component Configurator, Plugin registry, config parser or
runtime Service Configurator in the CNet library itself. Bad versions/unknown
kinds fail immediately; they do not turn into round robin or raw CNet.

## Real owner-local integration

The current executable SG fixture lives in
[cnet_sg_hosted_gateway_test.c](tests/cnet_sg_hosted_gateway_test.c). Its
per-Owner startup configuration is written by the host **before**
`native_io_sharded_submit_to`:

1. An SG Owner task acquires a durable `native_io_sharded_host_lease` and
   creates separate inbound/outbound CNet clients borrowing that shard's
   existing NativeIO backend (also one listener). No second backend is created.
2. On inbound *accepted completion*, the host invokes
   `cnet_owner_placement_choose` **once**. The fixture accepts only a
   successful same-Owner selection, then separately invokes the actual
   `cnet_listener_accept` transport commit. A foreign selection fails closed:
   cross-Owner cases must use a separately credited handoff, not an implicit
   Actor/SG hot-path hop.
3. On outbound *new dial*, the host first invokes
   `cnet_destination_choose` **once** using an immutable, already-authorized
   endpoint snapshot and proves the selected stable identity matches the
   configured loopback endpoint. The physical connection is subsequently
   committed by `cnet_managed_dial_advance` through a real Owner-local
   `CNetManager` reservation/connect, after a separate bounded
   `cnet_pool_reserve_connecting` budget is reserved. The policy does not
   allocate or connect by itself.
4. Every short progress turn advances CNet's external client state, calls
   `native_io_sharded_context_observe_host` **once**, demultiplexes the
   **entire** batch with `cnet_sg_host_route_batch`, then advances the
   Owner-local CNetManager with a finite record budget. SG-owned completions
   have already been settled by SG; never route them twice.
5. Borrowed receive callbacks go directly to the application and remain on
   their fixed Owner. The test verifies the **actual bidirectional peer
   payload exchange** before the application explicitly publishes ManagedDial
   `protocol_ready` using its generation-safe ticket. Stale/double READY
   fails; TCP CONNECTED alone is not protocol READY or permission to replay.
6. The actual Manager BOUND identity becomes Pool READY, then one real
   owner-local nonmultiplexed lease can be acquired. A different authority
   cannot borrow that connection; full/duplicate leases are rejected.
   Calling Pool terminal while Manager still reports BOUND fails.
7. Seal Pool acquisition, retain the live lease, seal Dial and close the
   inbound connection. Continue short SG turns until real CNet terminal and
   Manager `on_recycle` have completed. Pool terminal does not drop its live
   lease or allow premature destroy; release exactly once before reclaim.
   Destroy Dial, Pool, Manager and CNet clients, release SG host lease
   **after** all borrowed state is quiescent, then shut down the shared SG.
   Timeout never makes a pending terminal safe to free.
8. This test checks exactly **one Server placement and one Client destination
   selection** per actual established connection across **1/2/4 shards**, with
   zero further strategy evaluation during send/receive/close.

No additional runtime, permanent poll task, hidden retry, CFlow Actor, or
connection-owner migration is introduced. The existing raw, non-SG CNet API
remains an **explicit** supported entry path, not an automatic fallback.

## Qualification boundaries still open

Phase 1 established pure decision-time Server/Client selection on real SG
Owners ([CI #37898635381](https://github.com/qigao/salts/actions/runs/37898635381)
PASS). Phase 2 adds the **same-Owner** CNetManager + ManagedDial + ClientPool
physical binding, explicit post-payload READY ticket and terminal/lease
settlement; its exact-source [CI #37901777035](https://github.com/qigao/salts/actions/runs/37901777035)
is a separate qualification gate. Until CI passes this test must not be
reported as fully accepted.

These synthetic payloads constitute only the fixture's checked application
exchange, **not** real HTTP/FlowMQ authentication or a general protocol-ready
classifier. This phase does not create a default client pool or retry service.

Follow-up gates: remote handoff credit/rollback, wrong-owner/reentrant
progress, full/unrouted completion batch, multiple real authorities and TLS
profiles, multiplexed per-protocol capacity, subsequent connection reuse,
failure/stop of one service sharing an SG backend, actual HTTP/MQ consumers,
multi-RID installed SDK and equivalent workload benchmarks under
[#1051](https://github.com/qigao/salts/issues/1051) through
[#1057](https://github.com/qigao/salts/issues/1057).
Do not interpret this fixture as shipping a CNet Configurator API or a
production combined Server/Client manager. Stable 2.3 release and candidate
publication are **separate review decisions**.

## Cross-Owner handoff (Phase 3, experimental)

[Draft PR #1085](https://github.com/qigao/salts/pull/1085) also adds
[cnet_sg_handoff_owner_test.c](tests/cnet_sg_handoff_owner_test.c).
This is a **different topology** from same-shard Server/Client:
SG Owner 0 owns the listener plus outbound CNet, while Owner 1 owns the
adopted inbound CNet connection and its CNetManager.

The source Owner's **real externally observed accept completion** is followed
by a single pure `cnet_owner_placement_choose` selecting Owner 1, then
`cnet_listener_accept_detached`, the destination's bounded
`cnet_handoff_reserve` and `cnet_handoff_publish`. A full credit budget
rejects a second reserve; stale ticket publication fails with the detached
descriptor still owned by the source. Once published, its socket and credit
belong to the inbox, and the producer **must never** release or republish
them, even if the explicit nonblocking SG notification to Owner 1 is full.

Only Owner 1 takes the ticket, makes a real `cnet_manager_reserve`, proves
the separate manager credit cannot be oversubscribed, and calls
`cnet_manager_adopt` with the move-only stream. All CNet callbacks run on
the final Owner. An actual bidirectional 4-byte application exchange is
verified on both shards before closing either connection. Both terminal
callbacks then run, the Manager's real on_recycle retires the transport,
and only **after** that does the target release the TAKEN handoff ticket.
After owner/backend quiescence the caller seals/destroys the inbox and
shuts down SG. There is no cross-shard socket migration *after* adoption
and no per-packet placement, Actor or duplicate NativeIO observe.

Exact-head CI result **pending**. The negative paths exercised in this
fixture are bounded credit denial and rejected stale publish, plus
double-release protection after real terminal. Additional rejection
at actual `manager_adopt` (full transport, TLS init errors), publish-vs-
seal races, backpressure across multiple accepted connections, competing
listeners, failed notification during shutdown and fault-isolated
neighbor consumers remain separate #1052/#1056/#1057 gates. Do not mark
the remote handoff product interface fully qualified from a single loopback.
