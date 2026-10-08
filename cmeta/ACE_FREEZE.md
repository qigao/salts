# Salts 2.3 — ACE / CMeta functional freeze contract

**Status:** functionally scoped and under exact-HEAD qualification on the
long-lived `feature/cmeta-ace-patterns` Draft PR
[#1013](https://github.com/qigao/salts/pull/1013).
**DO NOT MERGE. DO NOT PUBLISH.** This is a feature/API freeze contract,
not a claim that the 2.3.0 SDK release gates have passed.

## Frozen ownership graph

```text
CMeta: immutable Type / Interface / FunctionAbi / DataDesc / ObjectRef /
       Lifecycle / Manifest / typed macro + inline declarations
    |
    +--> Salts::Component: bounded static selection, graph/lifecycle
    |        |
    |        +--> Salts::ComponentPlugin: generation publication + scope admission
    |                    |
    |                    +--> Salts::Plugin: sole DSO load/unload + module lease
    |
    +--> CNet / NativeIO: transport, accept/connect, Reactor/Proactor owner
    |
    +--> CFlow / Actor: bounded mailbox, scheduler, Graph, Channel, Stream
```

**No new public ACE pattern DSL, runtime scheduler, dynamic service locator,
reflection registry, module loader or automatic retry.** Strategy is the
canonical typed `Interface {self,vtable}`; Factory is FunctionDesc/FunctionAbi
plus explicit result ownership; Adapter is exact typed Interface mapping;
Extension Interface borrows one ObjectRef with provider-authorized
projections. A macro/inline may be admitted *only* if it proves a new native
relation or prevents a real ownership error, not merely to rename an ACE
pattern. Public Component struct/layout is frozen for this 2.3 candidate
after test qualification; intentional changes restart ABI gates.

## Context, owned callbacks and borrowed views

```text
Context ZERO -> READY -> RESOLVED -> ACTIVE -> STOPPED
                                \-> FAILED (rollback complete)
```

- First use: `salts_component_context context =
  SALTS_COMPONENT_CONTEXT_INIT;` (C11 and C++17). Reading an indeterminate
  uninitialized stack context is outside the API contract; no unsafe
  "automatic initialization" compatibility path.
- Reinit in READY/RESOLVED/ACTIVE fails **without changing storage or losing
  ObjectRef destroy authority**. STOPPED or fully rolled-back FAILED reuse
  requires exclusive caller ownership and expired borrowed views.
- A failed `create()` that returned a valid owned ObjectRef is settled once;
  on `activate()` failure the current instance is destroyed but NOT
  `deactivate()`d. Provider activation must undo partial side effects itself
  or have them covered by owned ObjectRef destruction.
- Previously activated dependencies are deactivated/released in reverse
  topological order. On post-activation PROVIDES failure, the failed
  instance *is* deactivated before ObjectRef release.
- An Interface/descriptor/callback/service view borrows its containing
  ObjectRef/Component scope/Plugin lease and may not escape their lifetime.

## Generation publication, leases and Scope

```text
ZERO -> BUILT -> PUBLISHED -> DRAINING -> STOPPING -> DRAINED
                         \        \-> FAILED (explicit bounded recovery)
                          \-> new admission on next published generation
```

- Complete candidate graphs are built before publication. Publication,
  admission/release, close and drain claims are protected by the existing
  runtime mutex. Generation IDs strictly increase and at most **two**
  generations may remain attached; there is no third-generation fallback.
- A live `salts_component_plugin_scope` is **noncopyable, address-stable,
  thread-exclusive**. A shallow copy cannot release or borrow; stale and
  double release cannot decrement the original generation's scope count.
  Scope release is *fallible* and therefore is not silently cast to a
  no-fail lexical cleanup callback.
- `runtime_close` withdraws current admission. It is *not* the same as
  final `runtime_destroy`; a later explicitly published higher generation
  may reopen admission subject to capacity.
- A scope pins its generation's provider callback/storage until release,
  not an exclusive external socket/lock. Drain waits for all scopes, stops
  Components and releases owned ObjectRefs **before** Plugin module leases;
  unload is allowed only after Plugin reports quiescence. No second DSO
  owner/refcount is created.
- `cmeta_cleanup`, `cmeta::object_scope` and ObjectRef/result cleanup
  discharge only the ownership they were explicitly given; they **never**
  manufacture a Plugin lease or authorize long-lived borrowed views.

## Exclusive external resource, N -> N+1

A socket listener, writer, file lock or equivalent singleton resource
**belongs to the domain owner**, not either Component generation. It must
not be bound twice simply because a candidate is built before publication.

1. Domain binds the stable exclusive resource once; Generation N borrows the
   capability. Its domain fencing token is epoch N.
2. N+1 builds/activates using borrowed provider storage without acquiring a
   second listener/writer. Candidate failure cannot close the domain resource
   or revoke N.
3. ComponentPlugin publishes N+1, but the domain still decides when its
   resource epoch changes. A new N+1 scope is initially **not admitted for
   exclusive I/O**. After the domain explicitly switches the fencing epoch,
   N is no longer admitted for exclusive I/O although its old scopes still
   pin teardown/code lifetime.
4. N drains after its scopes are released; draining N **must not** close
   the domain resource now used by N+1. After close/drain of N+1 and all
   borrowed work, the domain closes the resource exactly once.

The epoch check and the associated resource operation must be serialized or
made atomic by the **domain** where concurrent I/O is possible. The
deterministic `component_plugin_exclusive_fence_test` proves the
state/ownership contract, not a lock-free socket-fencing algorithm.
A real CNet/CHttp listener transfer still requires consumer-specific
concurrency qualification; no hidden authority is added to CMeta.

## Qualified executable contracts / freeze gate

| Contract | Fixture |
| --- | --- |
| Canonical Strategy / FunctionAbi Factory / Extension Interface | `cmeta_pattern_test`, `cmeta_pattern_cpp_test` |
| Correct and incorrect Interface result ABI (C11, C++17) | configure-time `SALTS_ACE_*_COMPILES` checks |
| Owned/Borrowed result, exactly-once cleanup, noncopyable C++ scope | `cmeta_pattern_cases.h`, `cmeta_pattern_cpp_test.cpp` |
| Context states and activation rollback | `cmeta_native_component_test`, C++ header fixture |
| Plugin lease cleanup before DSO unload; owned ObjectRef discharged first | `cmeta_native_component_plugin_generation_test` / DSO tests |
| Copied/stale scope, 2-generation capacity, close/reopen | `cmeta_native_component_plugin_publication_test` |
| Exclusive domain resource fencing and single close | `cmeta_native_component_plugin_exclusive_fence_test` |
| Real CNet Acceptor/Connector and Handler lifetime | `cnet_ace_acceptor_connector_test` |
| Typed CFlow Active Object, bounded mailbox and stale producer | `cflow_actor_test` / installed Actor consumer |
| FILTER -> MAP demand, channel FULL, cancellation | `cflow_ace_pipes_filters_test` / installed Pipes consumer |
| Real TCP -> Channel -> Subscription -> Actor | `cflow_ace_half_sync_async_test` / installed CFlowCNet consumer |

**Functional freeze admission:** all applicable above tests must compile,
execute and pass in the branch's selected Linux complete CTest/Lean gate,
and no unresolved P1 correctness issue may contradict these invariants.
The last source edit must be the commit actually tested. Host-matrix
results improve confidence; older-green runs do not qualify a newer HEAD.

**Separately OPEN for 2.3 release** (not permission to add new CMeta
primitives): immutable SHA-attested package, native SONAME/ABI evidence,
final sanitizer and Windows/macOS downstream conformance, CHttp request
scope/deferred mount, TurboFlow durable-plan ownership, TurboSCXML
session/invocation generations, Android/iOS device runtime if required,
and the explicit release/merge decision in
[#1018](https://github.com/qigao/salts/issues/1018).

## Freeze change control

After functional freeze admission, accept only corrections accompanied by
an exact negative/regression test and an issue explaining the violated
invariant. New pattern convenience APIs, alternative schedulers, registries,
legacy aliases, speculative incremental graph rebuilds and runtime-state
persistence are outside this freeze. All subsequent source changes require
re-running the affected contract gate. **PR #1013 stays Draft and
DO NOT MERGE regardless of test results.**
