# CFlow SCXML Invoke and Finalize Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Increment B of SCXML Phase 3 with bounded literal `invoke`, matching `finalize`, cancellation on committed state exit, returned-event routing, and `autoforward` while preserving the existing null-data-model and inline execution boundaries.

**Architecture:** Keep the native Statechart configuration as the sole active-state fact source. Add optional versioned native lifecycle hooks at stable macrostep and external-event preprocessing boundaries, plus an opaque tagged external-event admission API. Lower each SCXML invocation into immutable program descriptors and implicit inline entry/exit actions; an owning SCXML session maintains one bounded lifecycle row per declaration and calls a versioned invocation adapter without holding runtime or registry mutexes.

**Tech Stack:** C11, CFlow Statechart runtime, Salts XML parser, CMeta descriptors, Salts mutexes/atomics, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`

## Global constraints and data-path protocol

- Public changes are additive and versioned. Zero-initialized native configs and invocation-free SCXML programs keep their current behavior and storage requirements.
- The data unit is a copied typed external Event plus a nonzero `uint64_t` invocation token. A parallel fixed-capacity token ring is kept in exact lockstep with the existing external mailbox; token `0` denotes an ordinary external Event.
- The native published configuration is the sole active-state fact source. The SCXML session registry owns only invocation lifecycle (`INACTIVE`, `PENDING`, `ACTIVE`, `DONE`) and adapter tokens; it never mirrors state activity.
- Program invocation descriptors and retained strings are immutable and program-owned. Session rows and generated tokens are session-owned. Adapter users are borrowed until session destruction. Requests borrow strings and Event views only for the adapter call. Prepared effect tickets are move-only and receive exactly one `commit` or `discard` terminal callback.
- The SerialExecutor is the only semantic consumer of hooks and the invocation registry. Public event/report admission may be MPSC; copying and token order are serialized by the native instance mutex. User adapter callbacks never run while the native or session mutex is held.
- `external_event_capacity` bounds both Event slots and origin-token slots. `invocation_capacity` must cover every compiled invocation declaration. Native effect capacity covers transactional enter/exit lifecycle steps; adapter-internal capacity covers recoverable invocation errors. All byte calculations use checked arithmetic and participate in `max_storage_bytes`.
- Full queues, insufficient invocation/effect capacity, invalid ABI/capability, stale result tokens, adapter failure, close, and hook failure are distinct explicit outcomes. There is no retry, unbounded growth, silent drop, or fallback interpreter. The only intentional drop is a returned Event whose token is no longer live; it is counted as rejected.
- Stable callbacks run after eventless/internal/completion work drains and before the macrostep is exposed as quiescent. External preprocessing runs after dequeue and before transition selection. A callback may enqueue copied internal errors through its call-scoped context; borrowed configuration/Event views expire when it returns.
- On close, stop report admission, close Event I/O and invocation adapters exactly once, require both to become quiescent, destroy the native instance, then free session rows and copied binding users. Canceled invocation results remain rejectable until native work is drained.
- Observability is bounded counters: active, started, start failed, canceled, completed, returned accepted/rejected, forwarded, and forward failed. Counters do not become a second lifecycle fact source.

---

### Task 1: Add generic stable and external-preprocess hooks to native Statechart

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Produces a versioned optional runtime hook table copied by instance initialization.
- Produces call-scoped stable/external contexts with published configuration queries and bounded internal enqueue.
- Produces `cflow_statechart_instance_try_send_tagged()`; existing `try_send()` delegates with token `0`.

- [x] **Step 1: Write RED ordering, tagging, and compatibility tests**

Add native fixtures proving: stable hook runs only after all internal/eventless/completion work; a transiently entered/exited state is absent; hook-enqueued internal Events run before external dequeue; external preprocess observes the exact FIFO-aligned token; `DROP` prevents transition selection; hook failure is explicit; zero hooks and ordinary `try_send` preserve existing behavior. Add a compile-level installed-consumer check for the versioned public structs.

- [x] **Step 2: Build and run the focused target to verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test --parallel
ctest --preset win-release-user -R ^cflow_statechart_runtime_test$ --output-on-failure
```

Expected: compilation fails only because the hook/tagged-event API is not present.

- [x] **Step 3: Implement bounded token storage and callback boundaries**

Append status/API values without renumbering existing enums. Include the token array in checked instance storage. Write/read it only under the same mutex and indices as the copied external mailbox. Release the mutex before invoking hooks; supply only call-scoped query/enqueue functions. After a stable hook, re-enter the driver if new internal work exists. Apply preprocess `CONTINUE`, `DROP`, or `FATAL` before guard selection.

- [x] **Step 4: Verify GREEN and adjacent native runtime behavior**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test cflow_statechart_test verify_installed_package --parallel
ctest --preset win-release-user -R "^(cflow_statechart_runtime_test|cflow_statechart_test|verify_installed_package)$" --output-on-failure
```

### Task 2: Admit and lower the null-profile invoke/finalize syntax

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Extends the immutable inline IR with invocation descriptors, deterministic generated IDs, `done.invoke.<id>` Events, finalize blocks, and implicit entry/exit lifecycle steps.
- Literal `id`, `type`, `src`, and boolean `autoforward` are admitted. Expression/data-bearing forms remain rejected under the null model.

- [x] **Step 1: Write RED admission, diagnostics, and deterministic-mapping tests**

Cover state/parallel placement, finalize-only-under-invoke, duplicate/malformed IDs, generated IDs, literal type/src, autoforward boolean parsing, child order, duplicate done-event names, and retained-name limits. Reject `idlocation`, `typeexpr`, `srcexpr`, `namelist`, `param`, `content`, unknown attributes/children, and finalize `raise`/`send`/`cancel`. Admit empty finalize plus null-safe `log` and nested `if In(id)`.

- [x] **Step 2: Run focused SCXML tests and verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: valid invocation documents currently fail admission as unsupported.

- [x] **Step 3: Implement two-pass counted lowering**

Count every descriptor, generated/retained byte, lifecycle step/action, finalize block, and done-event occurrence before allocation. Emit invocation entry actions after explicit `onentry` actions and exit actions after explicit `onexit` actions. Retain no XML pointers. Generated IDs use stable program-owned state/document identity and remain unique within the session.

- [x] **Step 4: Verify GREEN and existing inline-content regressions**

Run the Step 2 commands and confirm raise, conditional, log, send/cancel, transition-condition, and name-limit cases remain green.

### Task 3: Implement bounded invocation start/cancel lifecycle

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Produces invocation adapter ABI v1, capability bits, literal start/cancel/forward requests, `invocation_capacity`, invocation adapter/user config, and additive invocation stats.
- Consumes native transactional effects for entry/exit intent and the stable hook for committed starts.

- [x] **Step 1: Write RED ABI, capacity, ownership, and lifecycle tests**

Use a strict fake adapter that copies requests and exposes prepare/commit/discard/close/quiescent observations. Prove invalid version/size/callback/capability rejection; capacity below declaration count rejection; transient state never starts; stable entry starts once in document order; exit cancels after explicit onexit; exit+reentry cancels then starts with a fresh nonzero token; rollback discards lifecycle intents; close is exact-once and quiescence-gated.

- [x] **Step 2: Verify RED**

Build `cflow_scxml_test` and `verify_installed_package`; failure must be the missing public invocation surface or missing behavior.

- [x] **Step 3: Implement registry state transitions and stable starts**

Allocate one fixed row per immutable descriptor. Stage entry/exit intent as native effect tickets so only committed microsteps mutate rows. At stable hook, prepare and commit starts outside the session mutex in declaration order. Generate monotonically increasing nonzero execution tokens. Map semantic adapter errors to the existing internal SCXML error Events and mark the execution failed until a committed reentry; propagate infrastructure/capacity failures.

- [x] **Step 4: Verify GREEN and mutate lifecycle boundaries**

Run focused SCXML/native tests. Confirm named tests fail if start moves before stability, cancel moves before commit, callbacks move under the mutex, tokens are reused, or rollback commits a row.

### Task 4: Route returned Events through finalize and autoforward

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Produces `cflow_scxml_session_report_invoke_event()` using tagged external admission.
- Consumes external preprocessing to revalidate the token, execute only its owning finalize block, mark matching done Events complete, forward the exact external Event to live autoforward invocations, and optionally drop stale returned Events.

- [x] **Step 1: Write RED finalize, stale-result, and forwarding tests**

Prove finalize runs exactly once immediately before transition selection for the matching live token; nonmatching ordinary Events do not run it; finalize `log`/`if` order is observable; `done.invoke.<id>` marks the row complete; canceled/old-token results are rejected or dropped and counted; accepted results preserve Event type/payload; autoforward observes every external Event in declaration order; forward failure maps to the documented internal error without reordering the current Event.

- [x] **Step 2: Verify RED**

Run `cflow_scxml_test`; new report APIs may compile only after Task 3, but semantics must fail before preprocessing is wired.

- [x] **Step 3: Implement matching preprocess and forwarding**

Revalidate row/token at preprocess time to close the admission/cancel race. Execute only the descriptor's restricted finalize block, then forward the exact external Event to active autoforward rows outside the mutex. Return `DROP` for stale tagged results and increment rejected. Keep adapter prepare/commit/discard terminal ownership exact and enqueue recoverable errors through the hook context.

- [x] **Step 4: Verify GREEN and ordering invariants**

Run focused SCXML/native tests. Confirm named tests catch finalize after guard selection, finalize on unrelated tokens, forwarding before finalize, stale-event transition selection, or Event copy/type drift.

### Task 5: Documentation, packaging, and complete verification

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify only if required: existing CMake/export files owning the public headers/targets

- [x] **Step 1: Document the implemented profile and lifecycle contract**

Describe supported literal invoke attributes, restricted finalize, generated IDs, adapter ticket ownership, capacities/backpressure, token/stale-result behavior, stable start/cancel ordering, autoforward order, error semantics, close/quiescence, and why data-model expressions/params/content remain unsupported. Mark only Increment B as implemented in the accepted spec.

- [x] **Step 2: Run source and focused verification**

```powershell
git diff --check
cmake --build --preset win-release-user --target cflow_scxml_test cflow_statechart_runtime_test cflow_statechart_test verify_installed_package --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_runtime_test|cflow_statechart_test|verify_installed_package)$" --output-on-failure
```

- [x] **Step 3: Run complete Release build, tests, and install**

```powershell
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user --parallel
```

- [x] **Step 4: Record platform evidence before issue/PR state changes**

Local Windows Release evidence is required before commit/push. Linux/macOS CI remains required before marking the corresponding #122 conformance checkbox complete.

Windows Release evidence on 2026-08-29: complete build succeeded; CTest passed
197/197; `verify_installed_package` rebuilt, linked, and ran the installed
CFlow SCXML consumer; the install preset succeeded. Linux/macOS evidence is
pending CI, so no #122 platform/conformance checkbox is changed by this work.

## Self-review gates

- Every callback boundary is outside both native and SCXML session mutexes; every borrowed view has a call-scoped expiry.
- Every copied queue, token ring, invocation row, descriptor, string, effect, and adapter-error path is hard-bounded and checked.
- Native runtime remains format-neutral; SCXML invocation/finalize policy does not leak into CFlow Statechart APIs.
- Finalize is associated only by the authoritative execution token and runs before selection; late canceled results cannot select transitions.
- Existing APIs, enum numeric values, direct bindings, invocation-free sessions, Event ordering, and zero-initialized configs retain their behavior.
