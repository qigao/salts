# CNet strategy x CFlow Actor composition

> Phase 1 **architecture/ownership contract** for the long-lived branch
> [`feature/cnet-actor-strategy-composition`](https://github.com/qigao/salts/tree/feature/cnet-actor-strategy-composition).
> This document specifies integration and acceptance rules; **it does not claim
> that owner-bound Actor execution, Actor-aware policies, or a new public API
> already exists**. The initial comparison baseline is Salts 3.0.0
> (`44f1d4fbff03034884a1ae760888205ce35202f4`).
>
> Tracking: [CNet management strategies #1001](https://github.com/qigao/salts/issues/1001),
> [ACE composition #1012](https://github.com/qigao/salts/issues/1012),
> [owner-bound SerialExecutor #1015](https://github.com/qigao/salts/issues/1015).
> Existing foundations: [NativeIO/CNet/Actor boundaries #471](https://github.com/qigao/salts/issues/471),
> [thin IO adapters #481](https://github.com/qigao/salts/issues/481),
> [multi-owner host composition #696](https://github.com/qigao/salts/issues/696).

## 1. Baseline: shipped surfaces vs proposed composition

| Area | Current, source-verified contract | Still to design/qualify |
| --- | --- | --- |
| NativeIO/CNet | NativeIO is the backend request/completion authority. Each `cnet_client` has a single progress owner; host-owned `cnet_client_init_external` can join an existing owner loop. | No new I/O loop, no automatic Actor ownership or cross-owner callback forwarding. |
| CNetManager | Optional `Salts::CNetManager`: fixed records/credits, `reserve -> connect/adopt -> terminal -> recycle`, explicit context hold, owner-only `advance/destroy`. `record_capacity` and `connection_capacity` do **not** reserve the CNet client's transport slots. | Policy-aware placement, admission, retention and bounded management decisions remain staged work under #1001. |
| CNet handoff | Optional, independent bounded MPSC `cnet_handoff` for **detached TCP stream admission** to one final owner; publication moves the stream and wake is host-managed. | Not a general Actor-message queue, network-stream transfer after attach, or automatic connection migration. |
| CFlow Actor | `cflow_actor` owns Machine/Statechart, typed bounded Mailbox, identity/lifecycle and retained producer refs. `cflow_actor_ref_try_send` reports ACCEPTED/FULL/STALE/etc. | Does not by itself select a CNet owner or reserve capacity for a callback that has already received bytes. |
| CFlow execution | `cflow_machine_instance_init` requires SERIAL and **not MANUAL** Executor; `cflow_actor_init` requires a CONCURRENT Scheduler. The built-in serial Executor runs one worker from a private thread pool. | Owner-bound serial dispatch on an existing host lane must satisfy both contracts honestly; tracked by #1015. |
| CFlowCNet | Existing optional `Salts::CFlowCNet` single-connection receive-credit IO Actor/Publisher adapter. CNet callback borrows bytes and bridge uses bounded operation storage; cancel leaves a bounded receive-credit tombstone, not a CNet close. | A **domain** `cflow_actor` / strategy integration is a different boundary. Do not call the IO Actor a domain Actor. |

Canonical code: [manager.h](../../cnet/include/cnet/manager.h),
[handoff.h](../../cnet/include/cnet/handoff.h),
[actor.h](../include/cflow/actor.h),
[event.h](../include/cflow/event.h),
[io_cnet_adapter.h](../include/cflow/io_cnet_adapter.h),
[executor.c](../src/executor.c),
[machine_instance.c](../src/machine_instance.c).

## 2. Composition, not another runtime

```text
                       host / application (owns topology, policy configuration)
                                      |
                +---------------------+----------------------+
                |                                            |
      CNet Strategy decisions                        optional Supervisor Actor
      admission/placement/retention                  domain policy & telemetry
                |                                            |
          reserve actual credit                 asynchronous bounded control /
                |                              immutable owner-local snapshot
                v                                            |
   accept + optional detached TCP handoff                    |
                |                                            |
                v                                            v
     +---------------- OWNER LANE 0 ---------------------------+
     | host-owned NativeIO backend and CNet progress           |
     | CNetManager: records, connections, terminal, context    |
     | protocol/session host: framing, credit, payload lease   |
     | optional domain Actor(s): typed mailbox + state machine |
     +---------------------------------------------------------+
                  | bounded *semantic* cross-owner handoff, if needed
     +---------------- OWNER LANE 1 ---------------------------+
     | independently owned backend, CNet client and Actor(s)   |
     +---------------------------------------------------------+
```

The dependency direction must remain:

- `Salts::NativeIO`: platform operation/request completion truth.
- `Salts::CNet`: transport/session/TLS state and per-connection I/O truth, **no dependency on CFlow/Actor**.
- `Salts::CNetManager`: optional CNet connection-management helper, no dependency on CFlow/Actor.
- `Salts::CFlow`: typed events, Mailbox, Machine/Statechart Actor, Executor/Scheduler; **no dependency on CNet**.
- `Salts::CFlowCNet` or a host-owned thin adapter: opt-in composition, never a second connection/IO-terminal authority.
- A host application decides whether its business state is a per-connection, per-session, per-Raft-group, or per-service Actor. **One CNet connection does not imply one Actor**.

The Actor may reside on the same host owner or on a different owner. An
Actor's logical single-writer guarantee is not automatically a CNet thread
affinity guarantee. Until #1015 is qualified, built-in serial-worker execution
still carries a scheduling/thread boundary.

## 3. Policy and decision boundaries

| Policy in #1001 | Where evaluation belongs | Actor integration | Forbidden coupling |
| --- | --- | --- | --- |
| Admission | Listener/owner management admission | Consume a bounded snapshot of Actor availability/quotas **as a hint**; reserve actual CNetManager, handoff and business credits before accepting. | A best-effort Actor queue-length snapshot is not an atomic reservation. |
| Placement | Connection admission **before attach/adopt** | Stable key (tenant, group) may choose domain Actor owner. Choose CNet connection owner separately when peer links are shared. | No live endpoint migration, no per-message rebalancing. |
| Retention | Manager on connection owner | Actor can asynchronously report outstanding work / eligibility for closing; manager requests close and waits for real CNet terminal. | Transport CLOSED does not destroy a persistent MQTT/Raft/HTTP session or finish Actor work. |
| Management batching | Owner progress budget | Bound management work and Actor task quantum **independently**, so network and other Actors make progress. | Do not tune one queue's window by copying another layer's fairness constant. |
| Notification | Host wake/runnable coordination | Coalesce already-ready work; use Actor mailbox/waker for actual domain messages. | No per-packet round trip to a central Actor or extra forced delay to accumulate work. |
| Draining | Host lifecycle coordinator | Domain Actor can coordinate graceful application stop and policy decisions; manager owns connection retirement. | Actor shutdown is not CNet shutdown, and neither is proof of NativeIO/backend quiescence. |

A strategy chooses candidates/actions; **the owning manager/host performs the
validated side effect**. Strategy functions may use immutable CMeta descriptors,
narrow typed interfaces or initialization-selected policy kinds, but shall not
allocate a new executor/queue, drive another owner's CNet client, block waiting
for an Actor answer, or introduce a hot-path virtual-policy call for every
network packet.

**No policy configuration is added to the existing public `cnet_manager_config`
by this Phase 1.** Its currently shipped fields remain the authority. Only
introduce ABI-reviewed versioned policy configuration after an executable
consumer requires a specific choice.

## 4. Authority, identity and ownership

| Thing | Sole authority | Cross-owner / callback lifetime rule |
| --- | --- | --- |
| Native request, cancellation, I/O terminal | NativeIO request slot + generation | A routed descriptor does not create a second completion. Accepted move/retain tokens settle once at the authoritative terminal. |
| Network connection, TLS/session state | CNet generation-checked connection and owner | Observer data and receive view are borrowed for callback duration. Close/stop must stay on declared owner. |
| Managed attachment / credit | CNetManager record + incarnation | A manager identity does not pin a CNet connection. `hold_context` extends record retention only, and needs explicit release on the owner. |
| Detached handoff / credit | `cnet_handoff` ticket + stream ownership | Successful publish transfers stream/ticket to inbox. Failed publish transfers **nothing**. Wake failure after publication does not revert acceptance. |
| Domain Actor / producer | `cflow_actor` and retained `cflow_actor_ref` | A producer ref pins the Actor control block, not a CNet connection, managed context, payload, or plugin/module. Owner destruction makes producer refs stale. |
| Typed domain message | `cflow_mailbox` | Accepted event is a **trivial byte copy**. FIFO is by successful mailbox commit, not by arbitrary cross-producer wall-clock order. |
| Payload / business session | Host-owned bounded operation/session storage | A copied pointer or token does not implicitly retain the pointee. Keep explicit lease/ref + exactly-once cleanup even when mailbox is cancelled. |
| Business transition / commit | CFlow Machine/Statechart or actual domain runtime | Message acceptance != processing != WAL durable != business commit; do not use transport completion as business completion. |
| Actor scheduling | CFlow Executor and Scheduler | Serial transitions are guaranteed by the actual Executor; same-thread owner execution is **not** guaranteed by built-in serial-worker creation. |

A CNet observer must not synchronously invoke `cflow_actor_wait`, wait for
cross-owner decisions, or destroy the Actor whose callback stack is active.

### Typed envelope and failure ownership

`cflow_mailbox_try_send` and `cflow_actor_ref_try_send` accept CMeta-typed
**trivially copyable/destructible** values. They cannot adopt an arbitrary
move-only `mem_buffer_t` or turn a raw borrowed network slice into a retained
payload. The suggested *future host-level* envelope consists of immutable
stable IDs/generations and an optional bounded **host-managed payload lease
key**. It is not a new CNet/Actor wire format or a public type proposed here.

If an event carries a lease key, the host must hold its payload in separate
bounded storage. The host owns it until successful admission; after acceptance
it remains tracked through processing/explicit cancellation. Since Mailbox
`cancel` discards trivial event bytes, **external payload reclamation cannot
depend solely on receiving an event**. Settlement must close every admitted
lease exactly once, even for discarded messages, stale actors, transport
terminal, or stop races.

Rejection / acceptance accounting:

| Boundary outcome | Meaning | Required owner action |
| --- | --- | --- |
| CNetManager reserve ENOBUFS / ESHUTDOWN | No manager admission | Do not open/adopt into unavailable helper credit; rollback the host's tentative budget. |
| Handoff publish accepted | Detached stream/ticket ownership transferred | Host must schedule owner progress or cancellation; no retry after post-publication wake failure. |
| Handoff publish failed | Stream/ticket still producer-owned | Producer chooses bounded retry/release/close; never assume partial transfer. |
| Actor `SEND_ACCEPTED` | Only a copied domain event is queued | Keep domain-operation payload lease until processing/cancel settlement. |
| Actor `SEND_FULL` | No event accepted | Caller keeps payload; apply bounded staging, stop new receive demand, or reject *before* consuming protocol data if allowed. Never silently drop. |
| Actor `SEND_STALE/STOPPING/STOPPED` | Domain owner unavailable for this event | Do not create another delivery by retrying blindly. Resolve through explicit shutdown/terminal policy; release caller-owned resources. |
| CNet send admission accepted | CNet owns the admitted send according to its transfer contract | Do not assume a logical-send terminal or Raft/HTTP commit until its corresponding authoritative event occurs. |

**Critical gap:** the public Actor Mailbox has no generic atomic
"reserve a future event slot" interface. An IO credit already issued to CNet
does not reserve Actor Mailbox space. A lossless adapter must either reserve a
**separate bounded staging slot before issuing CNet receive demand**, or
provide a carefully reviewed coordinated reservation protocol later. Both
the stage and the Actor mailbox must have explicit capacity. Do **not**
pretend a callback-time SEND_FULL is safe to fix with an unbounded retry queue.

### Phase 3a: bounded one-credit delivery into a domain Actor (PR #1024)

The optional `Salts::CFlowCNet` target now includes
[`<cflow/cnet_domain_actor.h>`](../include/cflow/cnet_domain_actor.h).
It retains a producer ref to an already existing Machine Actor and allocates a
fixed number of **payload leases**, but no second Actor queue. Phase 3a is an
explicit owner-driven staging primitive, not yet a CNet client/observer runtime
adapter or cross-owner router.

Its host receives a normal CNet callback, never a new I/O terminal. The
host's protocol must obey one outstanding receive demand and no direct
`cnet_receive()` calls outside this single-credit path:

```c
/* The host has already initialized its domain Actor and bridge, installed
 * a CNet observer that forwards on_receive to the bridge, and bound the
 * generation-checked connection; this snippet shows only one demand. */
cflow_cnet_domain_credit credit = {0};
int status = cflow_cnet_domain_reserve_credit(&bridge, &credit);
if (status == SALTS_OK) {
    status = cnet_receive(&client, connection, 1u);
    if (status != SALTS_OK)
        (void)cflow_cnet_domain_cancel_credit(&bridge, credit);
}
/* CNet on_receive (borrowed view):
 *     cflow_cnet_domain_receive(&bridge, connection, view)
 * return ENOBUFS => staged bytes preserved; stop new receive demand.
 * After owner drives Actor progress, call retry_actor once per budget.
 * The Actor's business action obtains bytes through borrow(delivery) and
 * acknowledges only after its chosen application-processing boundary.
 * CNet CLOSED/FAILED calls transport_terminal; it never implies Actor ACK.
 */
```

Admission is **tentative** until `cnet_receive()` succeeds, so the host must
use exactly one of rejection rollback or authoritative receive/terminal
settlement for that credit. `cflow_cnet_domain_receive()` copies an entire
borrowed CNet receive *chunk*, not an application protocol frame. Oversize
is a hard `SALTS_EMSGSIZE` failure with new receive demand sealed; the host
chooses the application-level close policy and cannot pretend the bytes were
delivered. Actor Mailbox FULL keeps exactly one staged event/owned buffer
and may be retried only by explicit owner progress. The typed
`cflow_cnet_domain_delivery` contains incarnation, generation, slot, session
identity, size and kind but **not** a borrowed payload pointer.

Application ACK is a separate capability: successful Actor Mailbox admission
does not reclaim a stage slot or establish processing/commit durability.
The host must settle all accepted domain events; if its Actor mailbox closes
and discards trivial events, it may invoke
`cflow_cnet_domain_abort_after_quiescence()` only after the Actor and CNet
callbacks have **actually quiesced**. The bridge never polls, closes, owns,
migrates or retries CNet resources on its own, and does not introduce a second
transport terminal authority.

Phase 3a validation now exercises a real Machine Actor on the shared Owner
Executor / Concurrent Scheduler against both **synthetic borrowed CNet views**
(for deterministic rejection, terminal and retry tests) and a **real TCP
loopback**, using host-owned `cnet_connect()`, `cnet_receive()`,
`cnet_client_poll()`, and callback forwarding. The real loopback test drives
multiple receive credits, Actor Mailbox FULL, bounded retry, ACK-based slot
reuse, and a second outstanding receive credit retiring on peer EOF without
releasing the already delivered business lease.

This remains one host-selected CNet owner and one co-located Actor owner;
the bridge does **not** install its own CNet observer, frame the application
protocol, call `cnet_receive()` automatically or claim business durability.
**CNetManager retention, 2/4-owner handoff, explicit policy placement
and representative consumer qualification remain later #1022 slices.**
Phase 3b below adds the external-progress qualification, not an automatic
runtime API or a cross-owner transport.

### Phase 3b: host-owned NativeIO progress with Domain Actor (PR #1027)

The same TCP loopback / Machine-backed Actor test now qualifies a second mode
where the host creates a single `native_io_backend` and passes it to
`cnet_client_init_external()`. In this mode **CNet cannot poll the backend**:
`cnet_client_poll()` and `cnet_client_stop()` must return
`SALTS_ENOTSUP`. All NativeIO completion observation stays in the host's
existing fixed owner loop, and the host routes **each observed completion**
to exactly one authoritative CNet client before resuming progress.

```c
/* Schematic host-side progress; the host owns NativeIO, CNet and scheduling
 * lifetimes, and supplies the finite completion batch and fairness budgets. */
size_t events = 0u, count = 0u;
native_io_completion batch[8];
int rc = cnet_client_advance_external(&client, &events);
if (rc != SALTS_OK) return rc;
rc = native_io_backend_observe(&backend, batch, 8u, wait_ms, &count);
if (rc != SALTS_OK && rc != SALTS_ETIMEDOUT) return rc;
if (rc == SALTS_OK) {
    for (size_t i = 0u; i < count; ++i) {
        bool consumed = false;
        size_t routed = 0u;
        rc = cnet_client_route_external_completion(
            &client, &batch[i], &consumed, &routed);
        if (rc != SALTS_OK || !consumed) return SALTS_EPROTO;
    }
}
rc = cnet_client_advance_external(&client, &events);
if (rc != SALTS_OK) return rc;
/* The host separately executes a FINITE quantum of existing owner tasks:
 *   cflow_executor_run_one(&owner_executor)
 * and checks pending work before blocking in NativeIO again. */
```

The test's unmatched-completion guard applies to its one-CNet-client backend.
A general host serving multiple backend consumers must first offer an observed
completion to their authoritative owners (as in the existing CNet/NativeIO
routing protocols), not blindly fail on a completion owned by a neighbor.

The same tests assert Actor FULL retains bounded copied bytes, owner progress
allows explicit retry and application ACK-based reuse, and a peer EOF with a
separate pending receive credit retires that **transport** credit but preserves
the previously accepted business payload. On shutdown, the host closes the
CNet connection, routes any pending native terminals, calls
`cnet_client_stop_external()` until quiescent, destroys CNet, and **only then**
closes/destroys the borrowed NativeIO backend. Actor/Subscription settlement
remains separate.

This remains an explicit host-composed **single owner**, not a new CFlow
scheduler/backend, implicit listener handoff, 2/4-owner routing or proof of
performance superiority.

## 5. Execution placement and fairness

Two legitimate integration topologies must be measured, not conflated:

1. **Available today — separate Actor worker:** CNet callback on network
   owner posts a typed event into the Actor's existing mailbox; Machine work
   executes on the configured serial worker. This is semantically correct if
   the callback's data is copied/retained and backpressure is managed. It is
   **not** a zero-thread-hop or zero-mailbox-cost claim.
2. **Proposed, #1015 — owner-bound Actor:** host's CNet progress lane also
   runs CFlow serialized domain tasks through a truthful Executor/Scheduler
   interface; no recursive Actor pump in CNet callbacks. This must preserve
   Machine's non-MANUAL SERIAL and Actor's CONCURRENT Scheduler validation,
   or explicitly review the corresponding capability model before changing
   that validation. Merely relabelling a Manual Executor is invalid.

Multiple Actors may share one lane. Use bounded, fair work quanta and wake
coalescing, keeping a runnable lane runnable when work remains. Retain CNet's
direct owner-local hot path: policy selection and Actor enqueue are performed
only for genuinely semantic/domain events, not every raw send/receive.

For Raft, `group_id -> group owner` and `peer connection -> CNet owner` are
**different mappings**. A shared physical peer connection may carry many
groups. Group message order, WAL durability, apply order and peer framing
remain TurboRaft/FlowMQ responsibilities, not CNetManager policy.

## 6. Joint shutdown: a dependency protocol, not a single close flag

1. Seal **new external** business/connection admissions; seal relevant
   manager and handoff entry points. Keep capacity reserved for already
   accepted terminal/control deliveries.
2. Quiesce external producer calls, including the last
   publication-then-wake tail. Do not destroy a handoff inbox/wake target
   merely because its queue snapshot says drained.
3. Drive application/Actor graceful work with bounded budgets while
   NativeIO/CNet callbacks and storage completions still have valid owners.
   The host defines whether pending business requests finish or cancel.
4. Drain or cancel queued detached connections, settle associated tickets
   exactly once, and initiate the intended CNet closes on their actual
   connection owners.
5. Continue authoritative NativeIO observe/route and CNet progression.
   Deliver or account for each accepted terminal/control event (possibly
   via reserved staging) **before** sealing its last domain recipient.
6. Close/drain Actor admission and outstanding semantic work; reclaim
   externally owned payload leases on processed and cancelled paths, and
   release retained producer refs once no callback can use them.
7. Release any held managed context on its manager owner; advance and
   snapshot CNetManager until drained. Destroy it only when callbacks and
   host context obligations are over, without stopping unrelated clients.
8. Stop/destroy CNet clients/backends according to their own quiescence
   contracts. Destroy the Actor/Executor/Scheduler after their callbacks
   and borrows have finished; order by actual borrowed dependencies, not
   a universal object-type ordering.

A timeout reports incomplete obligations; **it is never authorization for
forced free**, a guessed transport terminal, or a second cancellation truth.

## 7. Phase and acceptance matrix

### Phase 1 — this documentation checkpoint

- [x] Identify existing public entry points, ownership authorities,
      admission/terminal differences, and policy boundaries.
- [x] Separate existing CNetManager/handoff and CFlowCNet IO adapter
      capabilities from proposed domain Actor policy integration.
- [x] Record the missing owner-bound execution contract in #1015.
- [ ] Run integration/runtime tests; none are claimed by this design-only checkpoint.

### Phase 2 — owner-bound execution (#1015)

- [ ] Executor/Scheduler capability and cancellation/reentrancy design;
      no false capability advertisement, no additional I/O owner.
- [ ] 1/2/4 host owners, many Actors/owner, multi-producer ordering,
      fairness, FULL/STALE, stop/reuse, wake-before-sleep and race tests.
- [ ] Compare worker-bound vs owner-bound Actor and direct CNet costs.

### Phase 3 — policy and message admission

- [ ] Capacity-aware admission/placement with a **real atomic reservation**
      on the selected owner; stale placement snapshots may reject.
- [ ] Business message staging or credit protocol from CNet receive to
      Actor mailbox, preserving borrowed callback buffers and terminal truth.
- [ ] Strict-key placement does not silently fail over or migrate connections.
- [ ] Retention eligibility + manager context holds stay independent of
      Actor and transport terminal.

### Phase 4 — consumers and release gates

- [ ] Validate CHttp deferred replies/H2 streams, FlowMQ shared peer links,
      TurboRaft multi-group, optionally MQTT persistent sessions.
- [ ] Test 1/2/4 owner, high-connection/low-activity, TLS churn, small
      message, large payload, slow peer, overload and shutdown.
- [ ] Report messages/s, p50/p99/p99.9, CPU/msg, queue/wake/cross-owner
      hops, peak retained bytes, and exact cleanup; compare against plain
      owner-local CNet, not an invented universally superior backend.
- [ ] Qualify current Windows/Linux/macOS/Android SDK/ABI and relevant
      runtime gates before any merger/release. Do not merge the
      long-lived development branch merely because documentation passed.

## 8. Explicit non-goals

No dependency from CNet/CNetManager onto CFlow; no new Actor runtime,
mailbox, generic executor or global Actor registry; no automatic Actor per
connection; no hidden fallback/retry or unbounded allocation; no per-packet
strategy dispatch in the transport hot path; no central callback aggregation
to preserve legacy `poll` threading semantics; no live connection migration;
no duplicate native I/O terminal or business commit authority; no new
platform-specific I/O abstraction; and no Actor/Plugin lifetime inferred
from an unmanaged pointer or metadata descriptor.
