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
   committed by `cnet_connect_endpoint` on this same SG Owner. The policy
   does not allocate or connect by itself.
4. Every short progress turn advances CNet's external client state, calls
   `native_io_sharded_context_observe_host` **once**, demultiplexes the
   **entire** batch with `cnet_sg_host_route_batch`, then allows owner-local
   accept/adopt and CNet advancement. SG-owned completions have already been
   settled by SG; never route them twice.
5. Borrowed receive callbacks go directly to the application and remain
   on their fixed Owner. There is **zero further strategy evaluation**
   during send/receive/close; the test checks exactly one placement and one
   destination decision per actual established connection for **1/2/4** shards.
6. Seal admission/acquire/dial first; continue necessary completion routing
   and real CNet/manager/pool retirement; stop and destroy borrowed clients;
   release host lease **after** quiescence; only then shut down shared SG.
   Timeout never makes a pending terminal safe to free.

No additional runtime, permanent poll task, hidden retry, CFlow Actor, or
connection-owner migration is introduced. The existing raw, non-SG CNet API
remains an **explicit** supported entry path, not an automatic fallback.

## Qualification boundaries still open

This test proves the decision **placement** relative to real SG callback and
transport progression; it does **not** yet exercise cross-Owner handoff,
CNetManager, ClientPool, or ManagedDial in the very same multi-shard fixture.
Those APIs have independent tests, but their combined control plane requires
separate real consumer acceptance and lifecycle fault injection under
[#1051](https://github.com/qigao/salts/issues/1051) through
[#1057](https://github.com/qigao/salts/issues/1057).

Follow-up gates: wrong-owner/reentrant progress, full/unrouted completion
batch, remote handoff credit fail/rollback, protocol-restricted Pool leases,
explicit reconnect ticket/READY, failure/stop of one service sharing an SG
backend, multi-RID installed SDK consumer, and equivalent workload benchmarks.
Do not interpret this fixture as shipping a CNet Configurator API or a
production combined Server/Client manager. Stable 2.3 release and candidate
publication are **separate review decisions**.
