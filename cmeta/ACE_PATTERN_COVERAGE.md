# CMeta x ACE / POSA2 — 17-pattern coverage matrix

> **Status:** design/coverage inventory, **not** 17/17 implementation or release qualification.
> **Baseline:** `feature/cmeta-ace-patterns`; ACE functional-freeze code SHA [`02b741a`](https://github.com/qigao/salts/commit/02b741a7f2ebceaeed68c681898d09072c5323d1), qualified by [CI #37775060929](https://github.com/qigao/salts/actions/runs/37775060929). This document is documentation-only: it does not reopen or supersede the frozen source contract in [ACE_FREEZE.md](ACE_FREEZE.md).
> **Change policy:** [#1012](https://github.com/qigao/salts/issues/1012), draft [PR #1013](https://github.com/qigao/salts/pull/1013) **DO NOT MERGE / DO NOT PUBLISH**; [#1018](https://github.com/qigao/salts/issues/1018) independently owns 2.3.0 package, ABI and downstream release qualification.

## Goal and counting rule

Target: **all 17 canonical patterns in POSA2 expressed and usable through CMeta semantics composed with existing Salts runtime owners**. This is **not** a request for 17 new classes, 17 new public `cmeta_*` aliases, or 17 runtimes.

Primary pattern catalog: [Schmidt/Stal/Rohnert/Buschmann POSA2 table of contents](https://www.cs.wm.edu/~dcschmidt/POSA/POSA2/). Additional GoF/ACE idioms (Strategy, Factory, Adapter, Observer, Pipes and Filters, etc.) are composition tools **outside** this canonical 17-item count.

Distinguish three independent questions per pattern:

1. **Mechanism:** is actual behavior already present in an authoritative Salts runtime?
2. **CMeta composition:** can canonical Type/FunctionDesc/FunctionAbi/Interface/ObjectRef/DataDesc/lifecycle + ordinary typed C11 glue represent the roles and constraints?
3. **Conformance:** is the intended POSA2 pattern behavior, including failure and lifetime, proven by an appropriate executable test (and installed consumer when a public interface is required)?

Status vocabulary:

- **QUALIFIED:** focused executable CMeta+runtime composition is included in frozen ACE regression evidence. Not a 2.3 SDK release claim.
- **REUSE:** runtime pattern is already implemented and directly exercised; no duplicate CMeta implementation is justified. Link its existing evidence.
- **PRIMITIVES:** canonical primitives exist, but full pattern-specific CMeta composition/acceptance is not yet proven.
- **GAP:** material pattern semantics or executable qualification need focused design and/or implementation.

## All 17 POSA2 patterns

| # | POSA2 family / pattern | Existing authoritative mechanism | Canonical CMeta representation | Status | Exact remaining work (no duplicated runtime) |
|---:|---|---|---|---|---|
| 1 | Service — **Wrapper Facade** | Platform / NativeIO / CNet | typed inline façade, Interface, FunctionAbi, explicit status/borrow | **PRIMITIVES** | Qualify a complete narrow native-resource façade: types, errors, close/borrow, C11/C++17 and out-of-tree consumer. No generic wrapper runtime. |
| 2 | Service — **Component Configurator** | Salts::Component + ComponentPlugin + Plugin | immutable `cmeta_component_desc`, manifests, typed provider, ObjectRef, leased Interface projections | **QUALIFIED** | Preserve [frozen Component/Plugin contracts](ACE_FREEZE.md); downstream/ABI remains [#1018](https://github.com/qigao/salts/issues/1018), not a new pattern engine. |
| 3 | Service — **Interceptor** | domain callback/dispatch owners | exact FunctionDesc/FunctionAbi + typed Interface roles + borrowed context | **GAP** | Typed before/after/error/short-circuit chain; result/error ownership, effect/ordering, reentrancy and Plugin Scope admission. No generic dynamic ABI reconstruction or hidden invocation registry. |
| 4 | Service — **Extension Interface** | existing provider/ObjectRef/Plugin lifetime | `cmeta_object_ref`, `cmeta_object_interface_provider`, `cmeta_interface_projection` | **QUALIFIED** | Preserve non-owning projections and explicit owning-self rejection; no second provider identity. |
| 5 | Event — **Reactor** | NativeIO epoll/kqueue readiness; CNet event callbacks | typed callback / Interface over existing owner-bound dispatch | **REUSE** | Existing NativeIO/CNet model, ownership and callback tests are evidence; **no second Reactor or CMeta scheduler**. |
| 6 | Event — **Proactor** | NativeIO IOCP/io_uring completion; CNet completion-driven semantics | typed callback / Interface over existing completion contract | **REUSE** | Existing NativeIO/CNet request/completion and terminal tests are evidence; **no second Proactor**. |
| 7 | Event — **Asynchronous Completion Token (ACT)** | NativeIO request `{slot,generation}` + `user_data` and terminal completion | typed request-to-context association + lifecycle / borrowed or owned context contract | **GAP** | Demonstrate match of exact token to completion, cancel/terminal races, stale reuse, in-flight borrow, teardown and exactly-once context settlement; do not create a second request registry. |
| 8 | Event — **Acceptor-Connector** | CNet listener / client, accepted stream and owner handoff | exact typed Handler Strategy / Interface; explicit borrowed owner lifetime | **QUALIFIED** | Real TCP loopback ACE fixture and invalid-admission/close contracts already qualified. |
| 9 | Sync — **Scoped Locking** | Platform mutex / RW lock, CMeta structured lifecycle scope | `cmeta_scope` / canonical DataDesc construct-and-restore + typed guard adapter | **PRIMITIVES** | Prove actual **lock acquire and unlock** around a critical section, early error and reverse cleanup; generic CMeta resource scope alone does not prove Scoped Locking. No lock owned by CMeta. |
| 10 | Sync — **Strategized Locking** | existing mutex / RW lock and Concurrency | type-safe Lockable Interface + selected policy/Guard | **GAP** | Demonstrate at least two locking policies with identical critical-section contract, acquire/release symmetry, immutable policy binding, invalid mode rejection; no virtual call on unrelated network hot paths. |
| 11 | Sync — **Thread-Safe Interface** | Platform/Concurrency locking and owner-affine execution | typed public Interface vs internal nonlocking methods; explicit threading/admission contract | **GAP** | Prove lock-once public boundary, internal calls without recursive self-deadlock, concurrent callers, owner-only violations, shutdown/reentrancy and no false synchronization promise. Generic `FunctionDesc.effects` is not a replacement for this execution contract. |
| 12 | Sync — **Double-Checked Locking Optimization** | Platform `cmeta_once`; Concurrency C11 atomics | typed init/once façade + explicitly safe publication/order | **PRIMITIVES** | Compare safe once vs C11 acquire/release DCL where warranted; test publication and teardown. **Never implement historic unsynchronized, data-racing DCL**; a safe once solution may qualify the intent without adding a public DCL macro. |
| 13 | Concurrency — **Active Object** | CFlow Actor, typed bounded Mailbox, Executor/Scheduler | reflected producer / action Strategy Interface | **QUALIFIED** | Preserve bounded admission, FIFO, stale refs and no hidden worker/queue through existing ACE tests. |
| 14 | Concurrency — **Monitor Object** | Platform mutex/condition + domain state | typed Interface + scoped guard + condition predicate/protocol | **GAP** | Actual synchronized method entry, condition-loop waiting, signal, nonrecursive internal calls, cancellation/close and no lost wake or early destruction; a mutex alone is not a Monitor Object. |
| 15 | Concurrency — **Half-Sync/Half-Async** | CNet TCP → bounded CFlow Channel → Subscription → Actor | typed Channel/Subscriber/Action interfaces and explicit transfer | **QUALIFIED** | Existing real-loopback ACE fixture proves explicit two-level FULL and shutdown; no extra queue. |
| 16 | Concurrency — **Leader/Followers** | existing thread/notification/executor primitives; CNet SG has fixed owners | explicit typed leader election/handoff and follower execution roles, if selected | **GAP** | First prove genuine baton transfer, exclusivity, fairness, stop/cancel and completion progress on an **opt-in execution topology**. Do not rename fixed SG ownership as Leader/Followers, and do not change CNet owners. |
| 17 | Concurrency — **Thread-Specific Storage** | Platform `SALTS_THREAD_LOCAL` / thread affinity; `cmeta_local_type` lifecycle adapter | typed per-thread value + explicit init/get/destroy and ownership | **PRIMITIVES** | Prove per-thread isolation, creation/exit, cleanup, nested calls and no borrow crossing thread exit/migration. `cmeta_local_type` is thread-*affine* storage, not automatic TLS allocation by itself. |

**Inventory only:** QUALIFIED 5 / REUSE 2 / PRIMITIVES 4 / GAP 6. These are **evidence categories**, not a completion percentage; neither REUSE nor PRIMITIVES becomes QUALIFIED simply by adding a documentation label.

## Grounded implementation / conformance evidence

- **Canonical pattern primitives:** [PATTERNS.md](PATTERNS.md), [Interface source](include/cmeta/interface.h), [Function metadata](include/cmeta/function.h), [Object Interface](include/cmeta/object_interface.h), [pattern C11/C++17 conformance](tests/cmeta_pattern_cases.h).
- **Component:** [declaration](include/cmeta/component.h), [runtime](../component/include/salts/component.h), [ComponentPlugin scopes](../component-plugin/include/salts/component_plugin.h); freeze tests and generation/epoch evidence in [ACE_FREEZE.md](ACE_FREEZE.md).
- **Reactor / Proactor:** [NativeIO public model](../native-io/include/salts/native_io.h) + [backend implementation](../native-io/src/native_io.c) + [native tests](../native-io/tests/native_io_test.c); [CNet](../cnet/README.md). Backend-kind distinction is explicit: epoll/kqueue = readiness; IOCP/io_uring = completion. Do not create another loop.
- **Acceptor-Connector:** [real TCP ACE conformance](../cnet/tests/cnet_ace_acceptor_connector_test.c).
- **Active Object / Half-Sync:** [Actor conformance](../cflow/tests/cflow_actor_test.c), [TCP → Channel → Actor fixture](../cflow/cnet-adapter/tests/cflow_ace_half_sync_async_test.c), [ACE_FREEZE.md](ACE_FREEZE.md).
- **Scoped lifetime / TLS / synchronization primitives:** [CMeta scope](include/cmeta/scope.h), [thread-local CMeta façade](include/cmeta/local.h), [Platform mutex/condition/once/TLS](../platform/include/salts/thread.h), [C11 atomics](../concurrency/include/salts/atomic.h), [execution primitives contract](EXECUTION_PRIMITIVES.md). None alone certifies the entire corresponding ACE pattern.
- **ACT starting point:** [NativeIO request, operation and completion identity](../native-io/include/salts/native_io.h) and [NativeIO tests](../native-io/tests/native_io_test.c); `user_data` is a copied association value, **not** a lease or ownership transfer.

## Acceptance contract for each gap

Every missing or unqualified pattern must specify: **Intent → ACE participants → CMeta native type/Interface/Function relation → authoritative runtime owner → ownership/borrows → concurrency/execution rules → error/cancel/terminal behavior → composition edges → positive and negative conformance**.

- The smallest complete executable slice wins. Use existing APIs, typed C11 functions, finite macros and explicit immutable descriptors.
- Only introduce a CMeta public helper when it proves a previously unexpressed native type/ownership relation or removes demonstrably unsafe repeated glue. **No pattern-named aliases solely for aesthetic coverage.**
- Qualified C11 and C++17 compiled tests for public typed declarations; compile-negative mismatch checks for exact FunctionAbi / Interface mappings. Cross-platform tests and installed-SDK consumer if an exported contract changes.
- Must preserve CNet/NativeIO I/O and terminal authority, CFlow scheduling authority, Concurrency/Platform mutex/TLS authority, Component composition authority and Plugin sole module lease/loader authority.
- All bounded admission, no implicit retries, no hidden queues/thread pools, no global mutable CMeta registry. Do not dispatch every network packet through pattern reflection.
- Documentation-only evidence changes do not reopen ACE functional freeze. **Any new source/public ABI work is post-freeze and requires separate approval, a precise issue/regression test, and the appropriate CI/SDK gates; do not silently merge into the frozen 2.3 candidate.**

## Post-freeze implementation candidates — not yet qualified

The five patterns below have **actual source plus executable fixtures** on the
existing ACE branch. All remain **GAP** in the qualification totals until the
*latest source head* compiles and the acceptance gates are reviewed. Existing
frozen SHA `02b741a` does **not** qualify these new commits.

| Pattern | Source / conformance | Remaining gate |
|---|---|---|
| Interceptor | [typed finite chain](include/cmeta/ace_interceptor.h) + C11/C++17 [pattern cases](tests/cmeta_pattern_cases.h) | Exact native FunctionAbi alignment, callback/Plugin lifetime and latest-head CI |
| ACT | [NativeIO typed token](../native-io/include/salts/native_io_ace_token.h) + [real read/cancel tests](../native-io/tests/native_io_test.c) + [C++ test](../native-io/tests/native_io_header_cpp_test.cpp) | Further close/race, installed consumer and latest-head CI |
| Strategized Locking | [fully reflected Lockable Interface](include/cmeta/ace_synchronization.h) + [mutex/RW tests](tests/cmeta_ace_sync_cases.h) | Additional policy-admission/lock ordering checks, CI |
| Thread-Safe Interface | [typed guarded public call](include/cmeta/ace_synchronization.h) + [concurrent counter](tests/cmeta_ace_sync_cases.h) | Negative native ABI and thread-safety contract/TSan, CI |
| Monitor Object | [real guarded monitor fixture](tests/cmeta_ace_sync_cases.h), using Platform condition | More cancellation/timeout/fairness cases, CI |

No new Reactor, Proactor, Actor, worker pool or CMeta scheduler was created.
Leader/Followers remains a separate scoped design under #1064.

### ACT / Plugin lease test (post-freeze)

[`cmeta_native_component_plugin_publication_test`](../component-plugin/tests/component_plugin_publication_test.c)
now combines the existing real Plugin DSO + Component generation with a
**synthetic** typed ACT terminal identity. It demonstrates that closing a
generation does not release the DSO while the caller still holds a Scope;
a stale completion cannot settle the token, and terminal settlement alone
does not implicitly release the Scope or its module lease. After explicit
Scope release, generation drain frees the sole module lease. Real NativeIO
read/cancel completions are tested separately under NativeIO; this test does
not claim a synthetic completion is real kernel I/O.

## Active pattern-gap tracking

Umbrella: [#1058 — POSA2 17-pattern coverage and conformance](https://github.com/qigao/salts/issues/1058). Missing semantic/composition slices:

| Pattern | Issue | Scope |
|---|---|---|
| Interceptor | [#1059](https://github.com/qigao/salts/issues/1059) | Exact FunctionAbi, before/after/error and borrowed callback lifetime |
| Asynchronous Completion Token | [#1060](https://github.com/qigao/salts/issues/1060) | Typed terminal association and once-only context settlement |
| Strategized Locking | [#1061](https://github.com/qigao/salts/issues/1061) | Typed lock policy plus scoped guard |
| Thread-Safe Interface | [#1062](https://github.com/qigao/salts/issues/1062) | Synchronized entry/private operations and self-deadlock prevention |
| Monitor Object | [#1063](https://github.com/qigao/salts/issues/1063) | Predicate/condition waiting, synchronized access and teardown |
| Leader/Followers | [#1064](https://github.com/qigao/salts/issues/1064) | Genuine leader handoff on an optional non-SG execution topology |

Primitive-only conformance (Wrapper Facade / Scoped Locking / safe Once-DCL / Thread-Specific Storage) is separately tracked in #1058. **Neither this issue list nor documentation changes the frozen CMeta source ABI.**

## Proposed investigation / implementation order

1. **Typed asynchronous boundaries:** Interceptor and ACT (FunctionAbi, callback borrow, cancellation/settlement).
2. **Synchronization semantics:** Thread-Safe Interface + Strategized Locking + Monitor Object, composed with actual Scoped Locking.
3. **Thread topology:** Leader/Followers, tested on separate opt-in executor topology; retain fixed CNet SG owner.
4. **Complete the primitive-only acceptance:** Wrapper Facade, Scoped Locking, safe once/DCL, Thread-Specific Storage.
5. **Pattern language smoke:** at least one realistic composed host exercise uses Component Configurator + Extension Interface + Interceptor + existing Reactor/Proactor/Acceptor-Connector + ACT; one synchronization/concurrency exercise uses typed Guard + Thread-Safe Interface + Monitor Object or Active Object. Never force all 17 patterns into one contrived runtime.

## Cross-cutting non-POSA2 patterns

Strategy = canonical Interface; Factory = FunctionDesc/FunctionAbi + ownership; Adapter = exact typed Interface mapping; Observer = callback Interface; Pipes and Filters = CFlow Graph. These remain essential **composition elements**, but do **not** increase the POSA2 17-pattern count.
