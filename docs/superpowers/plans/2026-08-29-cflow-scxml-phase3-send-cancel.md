# CFlow SCXML Phase 3 Session, Send, and Cancel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete Increment A of the remaining Phase 3 design by adding an owning SCXML session, recoverable `error.execution` / `error.communication` Events, and literal null-model `send` / `cancel` with bounded transactional adapter effects.

**Architecture:** Keep one immutable inline step program and one native Statechart configuration fact source. Add generic bounded native effect tickets so callbacks reserve external work but publish only after the native microstep commits. An owning `cflow_scxml_session` supplies per-instance bindings, delayed-send identity, adapter ownership boundaries, and internal adapter-error ingress while adapter-free programs retain the existing direct binding path.

**Tech Stack:** C11, Salts XML parser, CMeta descriptors, CFlow Statechart runtime, Salts mutexes, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`

## Global constraints and data-path protocol

- Public changes are additive and versioned. Existing `cflow_statechart_instance_config` zero-initializers and adapter-free SCXML programs retain their behavior.
- The Statechart runtime stays format-neutral: it knows only generic effect tickets and typed Events, never XML, SCXML targets, serializers, or transports.
- A prepared effect is a fixed-size move-only ticket copied into native bounded storage. `prepare` reserves adapter capacity and has no external visibility; native success calls `commit` exactly once after state publication, while every rollback/cancel/failure path calls `discard` exactly once.
- The SerialExecutor is the only producer and consumer of the staged-effect journal. Capacity is a required positive session setting for adapter-dependent programs; full returns an explicit runtime limit error and never grows or retries.
- Adapter requests borrow program strings only during `prepare`; the adapter must copy everything retained. Ticket callback state remains adapter-owned until exactly one terminal callback.
- Adapter-result ingress is MPSC copy admission into a bounded native internal mailbox. It is drained on the SerialExecutor before external Events. Full, closed, invalid Event, and type mismatch remain distinguishable.
- A session owns its copied binding users and bounded delayed-send rows. Program storage and borrowed adapter context must outlive session destruction. Session destruction stops admission, closes the adapter, requires adapter quiescence, destroys the native instance, discards any uncommitted ticket, then frees session storage.
- `#_internal` and `_internal` are built-in aliases. A zero-delay internal send stages an internal Event directly and needs no external adapter ticket. Delayed internal sends and all other targets/types require a matching Event I/O adapter capability.
- Literal null-model forms reject expression, payload/content, unsupported target/type, malformed delay, missing required cancel ID, and unknown attributes at compile time whenever the error is statically knowable.
- Runtime adapter `ERROR_EXECUTION` / `ERROR_COMMUNICATION` aborts only the current executable block, stages the matching internal Event, preserves earlier successful steps, and returns native action success. Infrastructure/capacity/contract failures remain fatal.

---

### Task 1: Add generic transactional effect tickets to the native runtime

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`

**Interfaces:**
- Produces `cflow_statechart_effect_ticket`, `cflow_statechart_stage_effect_fn`, the call-scoped `stage_effect` members on `cflow_statechart_executable_context`, and `effect_capacity` on `cflow_statechart_instance_config`.
- `effect_capacity == 0` disables effects and preserves existing callers; a callback can stage only when capacity is nonzero.

- [x] **Step 1: Write failing native behavior tests**

Add real Statechart fixtures proving: successful initial entry and transition actions commit prepared tickets after the published configuration changes; a later action failure discards all earlier tickets; internal-queue-full and cancellation discard; capacity+1 fails without commit; destroy never leaks a prepared ticket. Ticket callbacks record the configuration version they observe so the test catches pre-publication commit.

- [x] **Step 2: Build and run the focused test to verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test --parallel
ctest --preset win-release-user -R ^cflow_statechart_runtime_test$ --output-on-failure
```

Expected: compilation fails because the ticket and staging API do not exist.

- [x] **Step 3: Implement checked storage and exact terminal transitions**

Measure ticket storage in `max_storage_bytes`, allocate only for nonzero capacity, reset the staged count at each microstep, and centralize `commit_staged_effects()` / `discard_staged_effects()`. Publish configuration and queues under the runtime lock, release the lock, then commit in document order. Discard outside the runtime lock on every failure/cancel path and during teardown.

- [x] **Step 4: Run the focused native test to verify GREEN**

Use the Step 2 commands. Existing zero-initialized config fixtures must still pass unchanged.

### Task 2: Add prioritized bounded internal adapter ingress

**Files:**
- Modify: `cflow/include/cflow/statechart_runtime.h`
- Modify: `cflow/src/statechart_runtime.c`
- Test: `cflow/tests/cflow_statechart_runtime_test.c`

**Interfaces:**
- Produces `adapter_internal_event_capacity`, `cflow_statechart_instance_try_send_internal()`, and explicit admission statuses compatible with existing typed Event validation.
- The copied MPSC mailbox is drained after committed current internal/completion work and before the external mailbox.

- [x] **Step 1: Write failing priority, boundary, and shutdown tests**

Prove adapter-internal Event beats an already admitted external Event, capacity 1 rejects the second Event without ownership transfer, unknown/type-mismatched Events fail distinctly, and admission after close is rejected.

- [x] **Step 2: Verify RED with the focused native target**

Use the Task 1 focused build/test commands.

- [x] **Step 3: Implement the optional mailbox and driver priority**

Reuse the existing copied typed mailbox primitive. Include its fixed slot/payload storage in the instance byte ceiling, schedule the existing driver after successful admission, and drain it only from the SerialExecutor. Do not expose configuration storage or call user code under the runtime mutex.

- [x] **Step 4: Verify GREEN and adjacent native regressions**

```powershell
cmake --build --preset win-release-user --target cflow_statechart_runtime_test cflow_statechart_test --parallel
ctest --preset win-release-user -R "^(cflow_statechart_runtime_test|cflow_statechart_test)$" --output-on-failure
```

### Task 3: Publish program requirements and the owning SCXML session contract

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Produces a bitmask requirements query; Event I/O adapter v1 ABI header, capabilities, request/result/ticket structs; `cflow_scxml_session_config`; owning session init/send/stats/error/report/destroy functions.
- Adapter-dependent programs make `cflow_scxml_program_runtime_bindings()` fail without modifying outputs; adapter-free programs remain usable through it.

- [x] **Step 1: Write failing header, validation, lifetime, and compatibility tests**

Cover NULL/empty handles, ABI version and `struct_size`, missing callbacks, capability mismatch, zero capacities, session initialization, adapter close/quiescence, installed C/C++ consumption, and unchanged direct bindings for current raise/log/conditional programs.

- [x] **Step 2: Verify RED in SCXML and installed-consumer targets**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test verify_installed_package --parallel
```

- [x] **Step 3: Implement the smallest owning session and requirements surface**

Copy program binding rows into session-owned rows whose users combine immutable block data with mutable session state. Validate the versioned ops table once, retain only borrowed adapter context, mirror native try-send/stats/error behavior, and use one cleanup path for partial initialization.

- [x] **Step 4: Verify GREEN before admitting new XML syntax**

Run the focused SCXML test and installed consumer. At this checkpoint all old documents behave identically through either direct bindings or the session wrapper.

### Task 4: Compile literal send/cancel descriptors and reserved errors

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Extends the inline step IR with `SEND` / `CANCEL` descriptor indices and program-owned literal storage.
- Adds `error.execution` / `error.communication` Event declarations only when required, after author-event first-occurrence IDs.

- [x] **Step 1: Write failing admission and deterministic mapping tests**

Cover onentry/onexit/transition nesting, document order, `#_internal` and `_internal`, literal event/target/type/id/sendid/delay, milliseconds and seconds, malformed/overflow delay, mutually exclusive expression attributes, payload/content rejection, unknown attributes/children, and reserved-name deduplication without shifting existing event IDs.

- [x] **Step 2: Verify RED**

Build and run `cflow_scxml_test`; the new valid forms should currently return `CFLOW_SCXML_UNSUPPORTED_FEATURE`.

- [x] **Step 3: Implement two-pass counted lowering**

Use checked arithmetic for descriptor/string counts and offsets, retain no XML pointers, preserve exact source diagnostics, and mark external/delayed sends plus all cancels as session requirements. Keep unsupported expression forms fail-fast under the null model.

- [x] **Step 4: Verify GREEN plus current parser regressions**

Run `cflow_scxml_test` including the existing raise, conditional, log, and condition fixtures.

### Task 5: Execute recoverable send/cancel semantics through tickets

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Consumes native `stage_effect`, `raise_internal`, and adapter-internal ingress.
- Produces block outcomes `CONTINUE`, `BLOCK_ABORTED`, and `FATAL`, plus bounded session delayed-send rows.

- [x] **Step 1: Write failing end-to-end behavior tests with a strict fake adapter**

The fake copies complete requests and exposes observable committed effects, but assertions target session behavior. Prove zero-delay internal sends require no adapter, external requests preserve literal fields/order, delayed send then cancel works in one block, only same-session IDs cancel, success commits once, later fatal action discards once, and adapter full/closed/error results map exactly.

- [x] **Step 2: Write failing block-abort and async-error tests**

Prove an adapter error stages the matching internal Event, skips later steps in that block, allows later independent executable blocks, preserves earlier committed steps, prioritizes asynchronous adapter errors over external Events, and fails fatally only when the required internal error Event cannot be represented.

- [x] **Step 3: Verify RED**

Build and run the focused SCXML test.

- [x] **Step 4: Implement ticket wrappers, registry, and block outcomes**

Keep the registry single-source under a small session mutex; do no adapter callback while holding it. A delayed row transitions `FREE -> PREPARED -> LIVE -> CANCEL_PREPARED -> FREE`, with one authoritative delivery/cancel winner. Reset the bounded per-microstep wrapper journal only after all native ticket terminal callbacks. Convert semantic adapter failures into internal error Events and return native success after block abort; propagate infrastructure failures.

- [x] **Step 5: Verify GREEN and mutation boundaries**

Run focused SCXML/native tests, then mentally mutate commit/discard, priority, alias handling, block skipping, and capacity comparisons; every mutation must be caught by a named test.

### Task 6: Document, package, and run complete verification

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-remaining-design.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify only if needed: public export/CMake files already owning these headers and targets

- [x] **Step 1: Document exact supported profile and ownership**

Describe adapter-free versus session paths, ABI/capability validation, ticket ownership, capacities/backpressure, close/quiescence, internal target aliases, literal-only limits, error ordering, and why `assign`/`foreach` remain unsupported in the null model. Change the spec status from proposed to implemented only for Increment A.

- [x] **Step 2: Run formatting/static checks and focused verification**

```powershell
git diff --check
cmake --build --preset win-release-user --target cflow_scxml_test cflow_statechart_runtime_test cflow_statechart_test verify_installed_package --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_runtime_test|cflow_statechart_test|verify_installed_package)$" --output-on-failure
```

- [x] **Step 3: Run complete Release verification and installation**

```powershell
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user --parallel
```

- [x] **Step 4: Record residual platform risk**

Local Windows Release is required before completion. Linux/macOS CI remains required before changing the matching #122 conformance checkboxes; do not infer cross-platform success from MSVC alone.

## Self-review

- Every external effect has exactly one owner and one terminal callback; no callback is made under the native runtime or session registry mutex.
- All queues, journals, registries, strings, and adapter result paths are hard-bounded and included in checked storage accounting.
- Program, session, adapter context, ticket, Event payload, and borrowed string lifetimes are explicit.
- Native configuration remains the only active-state fact source. The session adds no configuration mirror or fallback interpreter.
- Existing adapter-free public behavior remains the compatibility baseline; new syntax is admitted only when its required execution path exists.
