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

## UDP and WebSocket policy review (#1095)

### Baseline and review findings

This is the design-review deliverable for
[#1095](https://github.com/qigao/salts/issues/1095), reviewed on 2026-10-10 against
[`10c17c9c2b0d558addf2490c93cc13aa430906b0`](https://github.com/qigao/salts/tree/10c17c9c2b0d558addf2490c93cc13aa430906b0/cnet).
The `cnet/` tree at that revision is identical to `v2.3.0-rc.1`.
The external datagram and tagged WS primitives from
[#999](https://github.com/qigao/salts/issues/999) and
[#1000](https://github.com/qigao/salts/issues/1000) are present; their open issue
status does not mean those primitives are absent or all acceptance gates passed.
This review changes documentation only. It adds no public API, runtime support,
release qualification, or new test result. Earlier phase results above remain
separate evidence.

The following are **integration risks and acceptance gaps**, not demonstrated
defects in the existing primitives:

| Severity | Trigger and impact | Source evidence | Minimum design requirement |
| --- | --- | --- | --- |
| HIGH | Treating a connected UDP socket or a TCP/TLS CONNECTED event as protocol READY admits unauthenticated or incomplete sessions. A stalled WS handshake can hold admission indefinitely if only Dial backoff is driven. | [ManagedDial contract](include/cnet/managed_dial.h), [active-record early return and callback ordering](src/cnet_managed_dial.c) | Protocol adapter owns handshake/authentication phase and absolute deadline, and publishes READY with the exact attempt ticket only after success. |
| HIGH | Mapping every WS CLOSED event to a transient transport failure retries normal closure or security failure. Late authentication results can qualify a replacement attempt if their original identity is discarded. | [ManagedDial classifier](src/cnet_managed_dial.c), [recovery contract](include/cnet/recovery_policy.h) | Keep protocol failure cause and attempt identity until terminal; classify before recovery runs, explicitly authorize reconnect, and reject old results. |
| HIGH | Returning WS writer success after queue admission, or releasing output after cancel submission, reports premature completion and permits reuse of in-flight storage. | [WS writer/tagged contract](include/cnet/websocket.h), [datagram external routing and stop contract](include/cnet/cnet.h) | Connect writer completion to the authoritative transport terminal, retain buffers through drain, and distinguish logical send, native write and peer response. |
| HIGH | Releasing a dedicated WS lease after each message, or advertising a closing session as READY, permits incompatible reuse. A lease counter alone does not prove H2 stream capacity. | [pool key, protocol callbacks and drain contract](include/cnet/client_pool.h), [admission implementation](src/cnet_client_pool.c) | Hold the dedicated lease for the session; block new acquisition before close. Multiplexing requires a real protocol reservation and a separate stream lifecycle. |
| MED | Combining independently tested UDP/WS and policy primitives is reported as production support without real mixed-transport, protocol and platform evidence. | [acceptance inventory below](#acceptance-inventory-and-open-gates) | Qualify the composition with actual I/O and exact source/SDK identities; do not infer it from selector or engine unit tests. |

### Strategy applicability matrix

**Existing** means the named primitive is present in this baseline; **Adapter**
means protocol/host composition is required and its acceptance is still open;
**Unsupported** identifies a current API boundary. None of these labels means
end-to-end product qualification.

| Concern | UDP | WS over HTTP/1.1 (including WSS) | WS over HTTP/2 |
| --- | --- | --- | --- |
| Destination | **Existing + Adapter:** pure endpoint selection can choose a permitted peer at new transaction/session admission. Pin endpoint ID and generation; protocol defines eligibility. | **Existing + Adapter:** choose the authorized TCP/TLS destination once per new connection; HTTP authority, TLS identity and subprotocol remain adapter inputs. | **Adapter:** select a compatible physical connection, then reserve an actual Extended CONNECT stream; endpoint selection is not stream admission. |
| Placement | **Adapter:** choose a socket Owner at construction; separately pin protocol transaction/session ownership. The connection-placement helper is not a datagram dispatcher. | **Existing + Adapter:** final Owner before TCP/TLS/HTTP/WS state construction; no migration of a live session. | **Adapter:** streams inherit their physical connection Owner; no independent placement of a live stream onto another backend. |
| Real admission | **Existing + Adapter:** datagram/request buffers plus bounded protocol transaction/session and any cross-Owner queue credits. Pressure or reuseport selection reserves none of these. | **Existing + Adapter:** Manager/CNet connection credits, adapter handshake/session budget and bounded WS input/output. | **Adapter:** physical credits plus authoritative stream count/window and bounded per-stream storage. |
| CNetManager | **Unsupported:** `manager_connect` admits `tcp://` / `tls://`, not datagrams. Use the existing external datagram lifecycle. | **Existing:** manages the underlying TCP/TLS connection, not HTTP Upgrade, WS authentication or a `ws://` URI. | **Existing + Adapter:** may manage physical TCP/TLS; does not manage H2 streams. |
| Dial / recovery | **Unsupported** as a UDP ManagedDial transport. **Adapter:** protocol owns request timeout, retransmission, deduplication, pacing and session readiness. | **Existing + Adapter:** ManagedDial handles physical attempts/backoff/recycle. Adapter owns HTTP/WS/auth deadline, retry eligibility, close semantics and reauthentication. | **Adapter:** stream recovery and physical recovery are distinct; closing one WS stream must not automatically close/redial unrelated streams. |
| Pool | **Unsupported** as a UDP socket/transaction pool through the Manager-bound ClientPool. Do not fabricate a managed connection to bind it. | **Existing + Adapter:** dedicated capacity-one session by default; exact compatibility key and session-long lease. No per-message release or implicit reuse after Close. | **Adapter:** capacity greater than one requires real reserve/release callbacks for protocol slots; generic lease counters are insufficient. |
| Retention / drain | **Existing + Adapter:** external datagram owns socket/request storage, borrows backend; protocol owns retained callback data and transaction terminal. Route the whole observed batch before reclaim. | **Existing + Adapter:** tagged WS copies one logical message and retains pending frame output; adapter maps real transport terminals, stops admission and drains before freeing session state. | **Adapter:** retain WS and H2 write state through stream/native settlement; physical connection lifetime also covers sibling streams. |

Sources: [destination](include/cnet/destination_policy.h),
[placement](include/cnet/owner_placement.h), [Manager](include/cnet/manager.h),
[ManagedDial](include/cnet/managed_dial.h), [pool](include/cnet/client_pool.h),
[datagram](include/cnet/cnet.h), and [WS](include/cnet/websocket.h).
WSS retains TLS verification; WS-to-plain-TCP or WSS-to-WS fallback is not a
recovery option. The H2 column records design obligations, not a reviewed or
available CHTTP adapter. IPC and KCP remain outside this review; in particular,
raw datagram external progress does not establish packet-endpoint external
progress or change [packet send terminals](ADR_PACKET_SEND_TERMINALS.md).

### State, capacity and progress ownership

For UDP, distinguish three identities: **remote endpoint**, **socket Owner**,
and **protocol transaction/session owner**. The socket and borrowed backend
remain on their initializing Owner. A protocol adapter chooses a bounded
transaction/session key and pins its endpoint and Owner when admitting it.
SNMP request identity, STUN transaction identity and TURN/ICE session identity
need their respective protocol rules; a peer address or per-packet round robin
is not a general replacement. These are profiles to design with consumers,
not protocol support supplied by CNet.

The host owns the key-to-session records and their generation; it must retain
the chosen assignment through completion even if endpoint membership changes.
`STRICT_KEY` Owner selection is `key_hash % owner_count`, whereas destination
selection uses stable endpoint IDs. Changing Owner topology can remap keys;
it requires an explicit admission epoch and drain policy, not a live rehash.
Cross-Owner delivery, if needed, uses bounded owning messages and explicit
rejection. Borrowed receive views cannot be queued past their callbacks.
NAT rebinding or a peer-address change requires protocol validation before
changing the accepted peer identity; it does not move the socket Owner.
Profiles must specify source-address/port preservation and any consent or
pacing requirements. Reselecting a destination cannot silently replace them.

Admission budgets must name their units and authority: native request slots,
packet count, retained bytes, transaction records, session records and queued
cross-Owner messages. Reserve actual credits after advisory selection and
release each only at its corresponding terminal. Preserve API errors such as
`ENOBUFS`, `EBUSY`, `ESHUTDOWN` and `ENOTSUP`; the protocol must define any
allowed loss, response or retry on rejection. Deadline expiry stops new work
but never authorizes freeing a pending native request. UDP local send success
does not prove remote receipt, transaction success, ordering or deduplication.

Each Owner's existing host loop is the sole backend observer. It advances
bounded CNet/protocol work, routes the entire observed batch by request
identity/generation, and continues draining all matched terminals even when
one result reports an error. UDP and TCP neighbors must receive fair progress.
Stopping one datagram must not close the host backend. Keep quiescence separate
from first error, as `cnet_datagram_stop_external(..., out_stopped)` already does.

`SO_REUSEPORT` is an optional platform socket capability, not proof of
deterministic protocol-key routing or failover. The current shared-port test
is Linux-guarded; [implementation](src/cnet_datagram.c) rejects unsupported
reuseport rather than changing topology silently. Windows IOCP, Linux
epoll/io_uring and macOS kqueue each need their supported topology recorded;
distinct ports passing is not evidence that same-port sharing passed.

For WS/H1, the adapter owns this admission sequence:

```text
authorized destination + final Owner
  -> actual physical admission -> TCP/TLS CONNECTED
  -> HTTP Upgrade + WS negotiation -> application authentication
  -> protocol READY (original attempt ticket) -> dedicated session lease
  -> stop acquisition -> WS/transport drain -> terminal/recycle + lease release
```

The protocol adapter owns the phase, absolute opening/authentication deadline,
close deadline and failure cause; ManagedDial owns the physical attempt and
recovery state. `cnet_managed_dial_advance` returns `EBUSY` while a Manager
record remains active, so its backoff wait is not a handshake timer. The host
must check protocol deadlines in its existing progress loop, latch the cause
and close the current connection on failure. A stale deadline/authentication
callback cannot close or qualify a newer attempt. Bound the entire opening or
recovery episode, including protocol work, rather than resetting the budget
for each phase.

ManagedDial classifies a physical terminal **before** forwarding its state
callback. The classifier therefore needs already-recorded protocol context;
it cannot obtain a WS failure cause by setting it later in that same callback.
Missing classifiers fail permanently. Authentication/security failures must
not become transient failures; normal application closure needs explicit
policy rather than a blanket reconnect. `seal` is busy inside Dial callbacks:
defer requested stop to the next Owner control turn and process it before
another dial attempt. Keep old attempt storage until Manager recycle.
Reauthentication, resubscription and DATA replay are independent application
decisions; successful reconnect grants none of them. Eligible abnormal closure
uses bounded backoff/jitter, consistent with
[RFC 6455 section 7.2.3](https://www.rfc-editor.org/rfc/rfc6455.html#section-7.2.3).

The writer adapter returns `WRITE_PENDING` for accepted asynchronous output
and calls `write_complete` only at the authoritative full-frame terminal.
`SALTS_OK` from a copied-send enqueue is insufficient. Tagged admission copies
one message, but its pending output and adapter context must survive cancel,
close and transport drain. Continue bounded `advance` after errors/close to
dispatch the reserved logical terminal; control frames do not acknowledge
application messages. For H1, pause that connection's receive demand when
necessary and retain bounded unconsumed input. For H2, the adapter must bound
each stream and manage its windows while continuing connection control and
sibling progress; a slow WS stream must not simply suspend the whole physical
reader. See [RFC 9113 section 5.2.2](https://www.rfc-editor.org/rfc/rfc9113.html#section-5.2.2).

Pool keys preserve exact authority, endpoint/peer generation, transport,
trust/SNI/client identity, ALPN, protocol/subprotocol and session identity
through stable host-assigned IDs. The adapter defines these IDs without
placing secrets in keys. For dedicated WS, hold the lease through session
settlement and call `begin_drain` when closing starts, before lease release
could admit another borrower. A closed WS session is not reusable. Pool drain
neither closes the transport nor releases a lease; a READY
entry cannot be declared terminal while its Manager connection is BOUND.
For H2, releasing a completed stream's protocol token is distinct from marking
the pooled physical connection terminal. These lifetimes need a real adapter
test, not a counter-based multiplexing mock.

### Acceptance inventory and open gates

This inventory is source inspection, **not a new execution report**. The named
formal tests and benchmarks exist at the baseline above; their coverage must
not be extrapolated to the missing composition.

| Gate | Existing evidence | Required composition acceptance |
| --- | --- | --- |
| UDP external ownership | [datagram external tests](tests/cnet_datagram_external_test.c): two UDP instances on one backend; colliding local tags, borrowed views, observed-but-unrouted stop, malformed/stale terminals, neighbor isolation and endpoint-release failure | Mixed TCP/UDP on the same backend; protocol-key stability through loss/duplicates/reordering, full transaction/byte budgets, validated peer changes, cancellation plus late completion, and independent stop |
| UDP Owner topology | [transport Owner benchmark](benchmarks/cnet_transport_owner_benchmark.c): real UDP and separate IPC modes, fixed four duplex pairs across 1/2/4 actual threads/backends, three lifecycle trials | Add policy/transaction composition with equivalent work; assert single observe, affinity, fair progress and bounded full behavior. Existing UDP mode is neither mixed TCP/UDP nor a strategy workload. |
| WS send lifecycle | [tagged WS tests](tests/cnet_websocket_tagged_test.c): fragmentation, busy writer, control frames, short completion, close/cancel and frozen output | Real CNet TCP/TLS writer and HTTP adapter; slow peer, fragmented data with control/close, cancellation race and exactly one logical terminal per accepted send. Current fixture copies to local wire storage and explicitly injects `write_complete`. |
| WS readiness / recovery | [ManagedDial tests](tests/cnet_managed_dial_test.c): actual Manager/transport attempts and READY tickets; [recovery tests](tests/cnet_recovery_policy_test.c): backoff, security, deadlines and stale generations | Real Upgrade/authentication failure and timeout; CONNECTED never admits application DATA; stale auth/deadline after reconnect, normal close, bounded jitter/backoff, old-attempt drain and no implicit replay |
| Pool compatibility / capacity | [pool tests](tests/cnet_client_pool_test.c): actual Manager binding, authority mismatch, full/stale leases and terminal rules; multiplex callbacks use synthetic tokens | Different TLS trust/SNI/client identities and WS subprotocol/session IDs; closing session never rented; actual H2 stream reservation and window exhaustion; stream release versus physical terminal; leases survive required drain |
| Combined SG execution | [hosted gateway](tests/cnet_sg_hosted_gateway_test.c): TCP plus policies/Manager/Dial/Pool on 1/2/4 shards; [handoff](tests/cnet_sg_handoff_owner_test.c): two-Owner TCP adoption | UDP and WS compositions on 1/2/4 actual Owners; full admission, wrong-owner/reentrant calls, whole-batch routing, bounded service isolation and shutdown with pending terminals. Existing TCP evidence does not qualify these transports. |
| Platform / installed SDK | Existing CNet test registration in [tests/CMakeLists.txt](tests/CMakeLists.txt), including the Linux io_uring datagram variant | Local Windows contracts first, then applicable native CI backends and existing installed C11/C++ contract suites. Record exact SDK tag/SHA, topology, test names and unsupported platform cases; cross-compilation alone is not runtime evidence. |

Performance acceptance belongs to #1057: extend the existing opt-in CTest
benchmark registration in [benchmarks/CMakeLists.txt](benchmarks/CMakeLists.txt).
Compare direct CNet with the composed path at equal total endpoints/messages,
payload, byte/request limits, security mode and Owner counts. Retain raw repeated
trials and report throughput, p95/p99 latency, CPU, rejects/loss, configured
capacity, resident memory, setup and drain separately. The existing transport
benchmark is a useful baseline, not evidence of policy cost or WS performance.
No performance improvement is claimed by this review.

### Decision, delivery ownership and rollback

The selected design direction is **existing CNet primitives plus explicit
host/protocol adapters**. It preserves transport state ownership and avoids
making the TCP/TLS Manager pretend to manage datagrams. A new generic UDP
manager or WS convenience API would add lifecycle/ABI commitments without
evidence that the current boundaries are insufficient; reconsider only after
concrete adapter requirements expose a repeated gap. Per-packet placement,
transparent protocol fallback, a hidden polling thread, a second backend
observer and an Actor shim are not accepted alternatives.

The proposed first qualification slices are (1) fixed-Owner external UDP with
one consumer-defined transaction profile plus a TCP neighbor, and (2) dedicated
WS/H1 and WSS/H1 with actual Upgrade/authentication, authoritative sends and
capacity-one session ownership. H2 multiplexing, UDP peer migration profiles
and any new public API remain separate decisions. These are recommendations;
the joint consumer MVP and its concrete protocol profile remain open.

- #1095 owns this applicability decision and the composition acceptance ledger.
  #999 retains UDP external/platform obligations; #1000 retains WS terminal and
  adapter obligations. #1052 through #1057 retain their individual policy,
  handoff, conformance and benchmark work; this review closes none of them.
- CNet owns transport/progress primitives and the WS engine. The HTTP adapter
  (CHTTP or another consumer) owns Upgrade, Extended CONNECT, route/subprotocol
  negotiation and authentication integration. CNet must not depend on CHTTP.
- [SaltsNet #50](https://github.com/qigao/salts-net/issues/50) and its parent
  [#46](https://github.com/qigao/salts-net/issues/46) must agree on their protocol
  profile and capacity units before qualification. No downstream source audit
  or downstream test is claimed here.
  Consumer integration remains a separate qualification gate.

Implement each accepted slice behind explicit opt-in configuration without
changing the existing raw transport path or public ABI. The host owns immutable
configuration and generation changes; no live session migration is implied.
Rollback stops new admission, drains accepted native/protocol work and leases,
then starts the previously selected explicit path. It must not silently switch
transport, replay DATA or free timed-out in-flight state. Record implementation
PR/SHA and actual test/CI evidence in the owning gate before claiming acceptance;
creating this matrix, merging documentation and publishing an SDK are separate
events.
