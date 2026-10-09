# ACE/POSA2 completion contract — opt-in Leader/Followers and primitive conformance

Status: **post-freeze design** on `feature/cmeta-ace-patterns`. This is not executable qualification, a new Salts 2.3 public ABI, or release approval. See #1058, #1064 and #1018.

## Non-negotiable architectural split

| Concern | Authority | CMeta role |
|---|---|---|
| Readiness/completion, request generation, terminal status | NativeIO / CNet | typed callback/request descriptors only |
| Connection owner, shard execution and handoff | CNet Sharded Graph | borrowed handler and immutable policy metadata |
| Worker creation, mutex, condition variable, TLS, once | Platform/Concurrency | strongly typed role/strategy declarations |
| Actor scheduling and bounded mailbox | CFlow | typed producer/action/receiver contracts |
| DSO load/unload, generation scope and leases | ComponentPlugin | typed borrowed invocation, never implicit retention |

An owner-affine CNet SG lane **cannot** become a Leader/Followers pool by changing a pattern label. Existing Reactor/Proactor already implement their ACE semantics; no duplicate I/O loop is permitted.

## Leader/Followers (#1064): optional CPU event executor, **not** an SG I/O topology

Only implement this pattern if an actual consumer requests an event source where any equivalent worker may process any event. No owner-affine endpoint, TLS-bound callback, borrowed stack buffer, or live NativeIO request may be transferred through this path.

```text
bounded event ingress (owned message copies)
     |
     v
single elected leader --take one event--> elect successor under mutex
     |                                      |
     v                                      v
process event outside mutex              next leader waits for ingress
     |
     v
exactly-one settlement + owned payload release
```

### Role and state contract

- Caller constructs an explicit pool with `capacity > 0`, worker count in `{1,2,4}`, and one fixed typed event contract. This is an opt-in consumer facility; **do not install in CMeta, CNet or NativeIO by default**.
- The producer retains ownership on rejected admission. On acceptance, the pool owns the copied event until exactly one completion/cancellation settlement; no retry, hidden resubmission or unbounded queue.
- Synchronization authority is one existing Platform mutex and condition variable. Under that lock keep `leader_id`, `closing`, `queued`, `active` and one bounded FIFO. Only a designated leader removes an event. The leader elects a waiting follower (or preserves its role if no follower is available) **before** processing that event outside the lock. A successor cannot take the same event.
- Stop under the mutex sets `closing`, rejects subsequent submission, broadcasts all waiters, and drains accepted work by a documented policy (either finish or explicit cancelled settlement). Destruction is forbidden while `queued + active > 0` or workers remain live.
- Worker cancellation/error cannot abandon leadership: release or elect a successor while holding the mutex and wake a follower. Never invoke application handlers, Plugin callbacks or CMeta reflection while holding the election mutex.
- CMeta encodes a canonical, compile-time typed `Event -> Result/Status` Callable or Interface. Provider and borrowed callback lifetimes must cover *all* admitted events, and ComponentPlugin generation scope must be held externally until pool shutdown. This declaration does not create scheduler or ownership semantics.
- No fairness claim stronger than eventual handoff with runnable followers and an unblocked event source. No migration of CNet endpoint owner, duplicate backend polling or replacement for SG.

### Executable admission before marking qualified

1. Deterministic 1/2/4-worker tests: one active leader at any instant, baton exchange while the previous worker processes, and exactly one settlement for every admitted event.
2. Queue-full rejection preserves caller payload; FIFO dequeue within the single event ingress; callback errors and worker cancellation leave capacity and counts consistent.
3. Close during blocked wait and active processing: wake all, reject late admission and join before storage/provider teardown; explicit generation-scope busy/drain test with a real DSO.
4. C11 and C++17 exact typed Callable declarations, compile-negative wrong signature, ASan+UBSan and TSan where supported.
5. Independent installed-SDK consumer **if** anything public is exported, plus a workload-specific comparison against ordinary SG. No speedup claims without measured data.

**Decision gate:** until a concrete non-SG event-source consumer is identified, #1064 stays DESIGN ONLY / unsupported execution topology rather than a fake 17/17 completion.

## Remaining primitive-to-pattern obligations (#1058 phase B)

| Pattern | Actual contract to prove | Required failure/negative test |
|---|---|---|
| Wrapper Facade | Typed external/native resource open/use/close; status propagation; borrowed handle invalid after close | double-close, operation after close, failed-open leaves outputs unchanged; separate C11/C++17 consumer |
| Scoped Locking | Existing Platform mutex/RW lock acquired before guarded call and released at **every** return / C++ unwind | acquisition failure, early return, reversed nested release, no copyable active guard |
| Safe Once / DCL intent | Platform once or portable acquire-release published initialization; no historical data-racy DCL | simultaneous first-use, failed initialization semantics, stale/non-published reads |
| Thread-Specific Storage | Real `SALTS_THREAD_LOCAL` per-thread construction/use/teardown with typed CMeta view | cross-thread pointer borrow rejected, thread exit releases own data, address reuse not identity |

For all four: type exactness, owned/borrowed lifetimes, C11/C++17 conformance and installed consumer when public. Presence of `cmeta_scope` or `cmeta_local_type` **alone** does not count as qualification.

## Phase A gap acceptance and actual CI authority

- Interceptor (#1059): actual loaded DSO and borrowed ComponentPlugin Scope at `6185834`, full CI run 37869377431 passed; pending new fixture sanitizer and final candidate.
- ACT (#1060): genuine epoll/kqueue NativeIO plus Plugin Scope and Windows IOCP evidence exist on the branch; exact-head and targeted sanitizer acceptance remain distinct.
- Strategized Locking (#1061), Thread-Safe Interface (#1062), Monitor (#1063): tested reflected ports, actual mutex/RW/condition and concurrent fixtures exist; exact HEAD, negative cases and sanitizer completion must be independently verified.
- Leader/Followers (#1064): **not implemented or qualified**; above design is deliberately not a claim of functionality.

## Ordering

1. Close Phase A tests at their exact source SHA. Distinguish host success, sanitizer success and published installed SDK.
2. Implement Phase B *only* as thin CMeta composition over existing Platform primitives and concrete resource operations. Avoid pattern-named API aliases.
3. Decide #1064 via a named non-SG consumer before implementing any workers or queue.
4. Update `ACE_PATTERN_COVERAGE.md` only when the corresponding executable evidence exists; keep #1013 Draft and #1018 release admission independent.
