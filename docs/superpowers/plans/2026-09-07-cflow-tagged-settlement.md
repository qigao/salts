# CFlow Tagged Macrostep Settlement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Report exactly one correlated terminal outcome for every accepted nonzero-tagged external Statechart Event after its macrostep settles.

**Architecture:** Extend the existing size/version-gated Statechart hook table with a V5 settlement observer. The instance keeps the in-flight token explicitly, reuses the bounded external-token ring while terminal cancellation notifications drain, and serializes callbacks outside its mutex without adding an unbounded queue.

**Tech Stack:** C11, Salts CFlow/CMeta, TinyTest, CMake Presets, MSVC Release and ASan profiles.

**Spec:** https://github.com/qigao/salts/issues/234

## Global Constraints

- Preserve V4 source and binary callers by reading only the declared V4 prefix.
- Preserve all existing enum values, Statechart transition semantics, mailbox admission results, and aggregate counters.
- Never invoke an application hook while holding the instance mutex.
- A successful mailbox admission with a nonzero token produces exactly one settlement: completed, dropped, failed, or cancelled.
- Token zero preserves current behavior and produces no settlement callback.
- Pending storage stays bounded by the configured external mailbox capacity; no overflow allocation or silent loss is allowed.

## Data and lifecycle protocol

- **Data unit:** one value-only settlement record containing token, outcome, instance status, configuration version, and an optional borrowed stable first-error pointer.
- **Fact source:** the external token ring owns queued-token order; `external_in_flight_origin_token` owns the popped Event token until settlement.
- **Ownership:** the instance owns token storage. The observer receives a call-scoped settlement record; error text remains borrowed from the instance until destroy.
- **Topology:** external senders are MPSC, semantic execution is one SerialExecutor owner, and settlement callbacks are serialized by an instance delivery gate but may run on the thread that wins settlement.
- **Ordering:** in-flight settlement precedes queued terminal cancellations; accepted external FIFO is preserved.
- **Capacity:** the existing fixed token ring stores pending tokens and one fixed in-flight record stores the current settlement. Maximum unsettled work is mailbox capacity plus one in-flight Event.
- **Backpressure:** mailbox FULL remains an admission failure and therefore creates no settlement.
- **Failure:** failure settles the in-flight/first claimed Event as failed and all remaining admitted Events as cancelled. No token is reused before notification.
- **Shutdown:** close/cancel stops admission, records all terminal outcomes under the mutex, drains callbacks outside the mutex, then permits quiescent destroy.
- **Observability:** the callback is the exact correlated view; existing aggregate stats remain unchanged.

---

### Task 1: Public V5 settlement contract

**Files:**
- Modify: `cflow/include/cflow/statechart_instance.h`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Test: `cflow/tests/cflow_statechart_instance_test.c`

**Interfaces:**
- Consumes: existing V4 `cflow_statechart_instance_hooks` prefix and `cflow_statechart_instance_status`.
- Produces: `CFLOW_STATECHART_INSTANCE_HOOKS_ABI_V5`, `cflow_statechart_external_settlement_kind`, `cflow_statechart_external_settlement`, and `cflow_statechart_external_settlement_fn`.

- [x] **Step 1: Write a failing success-settlement test**

Add a settlement probe callback to the existing Statechart runtime fixture, configure hooks with ABI V5, submit token `77`, wait for completion, and assert one record with token `77`, outcome `COMPLETED`, status `OK`, and the final configuration version.

- [x] **Step 2: Verify RED**

Run `cmake --build --preset win-release-user --target cflow_statechart_instance_test` from a VS developer environment. Expected: compilation fails because the V5 settlement declarations do not exist.

- [x] **Step 3: Add the minimal public declarations**

Define explicit stable outcome values `COMPLETED = 1`, `DROPPED = 2`, `FAILED = 3`, and `CANCELLED = 4`. Add the record and callback after the instance-status enum, append `on_external_settlement` to the hook table, and document callback thread, borrow, reentrancy, ordering, and token-zero rules.

- [x] **Step 4: Add ABI compile checks**

Assert standard layout for the record/hook table and the callback signature in `cflow_header_cpp_test.cpp`. Keep the V4 constant and add V5 without renumbering existing enums.

- [x] **Step 5: Commit only after GREEN in Task 2**

Commit message: `feat(cflow): expose tagged statechart settlement hook`.

### Task 2: Exact settlement delivery

**Files:**
- Modify: `cflow/src/statechart_instance.c`
- Test: `cflow/tests/cflow_statechart_instance_test.c`

**Interfaces:**
- Consumes: V5 hook callback and existing external token ring.
- Produces: serialized outside-lock delivery for normal, DROP, failure, close, cancel, and queued terminal outcomes.

- [x] **Step 1: Implement the minimum success path**

Store the popped token in `external_in_flight_origin_token`, construct a completed record when `settle_external_locked(...OK)` runs, and drain it outside the mutex from `finish_terminal_side_effects`.

- [x] **Step 2: Verify the success test turns GREEN**

Build and run `cflow_statechart_instance_test --filter "reports one tagged settlement after macrostep quiescence"`.

- [x] **Step 3: Write failing outcome tests**

Add separate TinyTest cases for host DROP, guard/action/host failure, close/cancel of an in-flight Event, and executor cancellation after admission. Assert exact token, exact outcome/status, and one callback.

- [x] **Step 4: Verify each new test is RED for the intended missing outcome**

Run each case by TinyTest filter and confirm it fails on the outcome/count assertion rather than setup.

- [x] **Step 5: Implement outcome mapping**

Track whether the current external Event was dropped; map non-OK failures to `FAILED`, task cancellation to `CANCELLED`, and successful dropped macrosteps to `DROPPED`. Capture the final configuration version and stable first error.

- [x] **Step 6: Write a failing terminal-queue test**

Admit multiple tagged Events, force the first Event to remain in flight, cancel the instance, and assert FIFO notifications: first in-flight cancelled, then every queued token cancelled exactly once.

- [x] **Step 7: Verify RED and implement bounded pending delivery**

Retain queued tokens in the existing fixed ring after mailbox cancellation, record a cancellation cursor/count, and drain them after the in-flight record. Add a delivery-active gate so concurrent/reentrant finish paths cannot invoke callbacks concurrently or reorder records.

- [x] **Step 8: Verify focused GREEN**

Run the filtered tagged-settlement group and confirm every case passes with no warning or framework error.

### Task 3: V4 compatibility and validation

**Files:**
- Modify: `cflow/src/statechart_instance.c`
- Modify: `cflow/tests/cflow_statechart_instance_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: V4 prefix size through `on_host_transaction` and full V5 hook size.
- Produces: safe copying/validation of old and new hook tables.

- [x] **Step 1: Write failing compatibility tests**

Pass a V4 hook with `struct_size = offsetof(cflow_statechart_instance_hooks, on_external_settlement)` and assert initialization succeeds. Pass truncated V4 and V5 tables and assert `INVALID_ARGUMENT`. Pass V5 with settlement-only callback and assert success.

- [x] **Step 2: Verify RED**

Run the compatibility filters and confirm the old-prefix or V5 cases fail under current exact-size validation.

- [x] **Step 3: Implement prefix-safe validation and copy**

Validate only fields covered by the declared ABI/size, zero the destination hook table, and copy exactly the V4 prefix or V5 required size. Ignore appended bytes for V4. Require `on_host_transaction` for V4 and at least one known callback for V5.

- [x] **Step 4: Verify GREEN and mutation cases**

Run the compatibility tests, then temporarily reason through wrong ABI, one-byte-short size, both callbacks NULL, and a V4 table carrying extra bytes; each must follow the documented result.

### Task 4: Regression, sanitizers, documentation, and commit

**Files:**
- Modify: `cflow/include/cflow/statechart_instance.h`
- Modify: `docs/superpowers/plans/2026-09-07-cflow-tagged-settlement.md`

**Interfaces:**
- Consumes: completed V5 implementation.
- Produces: verified public ownership/lifecycle contract and one reviewable commit.

- [x] **Step 1: Run focused Release verification**

Run `cmake --build --preset win-release-user --target cflow_statechart_instance_test cflow_header_cpp_test cflow_actor_test cflow_statechart_actor_test`, then `ctest --preset win-release-user -R "cflow_(statechart_instance|header_cpp|actor|statechart_actor)_test" --output-on-failure`.

- [x] **Step 2: Run the full CFlow regression set**

Build `cflow/tests/all`, then run the core CFlow CTest range and record the
exact pass/fail count. Extension modules and examples are separate targets.

- [x] **Step 3: Run ASan-focused verification**

Configure `win-dev-user`, build `cflow_statechart_instance_test`, confirm the ASan DLL resolves through the preset/VS PATH, and run the focused CTest preset without copying DLLs.

- [x] **Step 4: Review ownership and ABI diff**

Use `git diff --check`, inspect the complete diff, and verify `.codegraph/`, build outputs, and generated files are not staged.

- [x] **Step 5: Mark this plan complete and commit**

Check completed steps, commit the header, implementation, tests, and plan with `feat(cflow): expose tagged statechart settlement hook`, then push the feature branch and open a PR linked to #234.

## Completion evidence

- Initial V5 test failed at initialization under the V4-only validator; the
  queued-cancellation test then failed with zero notifications instead of two.
- The callback/destroy race test failed before the reservation fix because
  destroy returned while the callback was blocked.
- Release core CFlow regression: 40/40 CTest cases passed.
- MSVC AddressSanitizer: all 157 Statechart instance tests and the C++ public
  header test passed.
- The final independent review found no remaining HIGH or MED issue.
