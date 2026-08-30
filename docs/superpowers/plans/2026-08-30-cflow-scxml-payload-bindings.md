# CFlow SCXML bounded payload bindings implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> `superpowers:executing-plans` to implement this plan task-by-task.

**Goal:** Carry ordered, bounded CMeta scalar payloads from SCXML `send` and
`invoke` into opt-in v2 adapters without changing v1 ABI or behavior.

**Architecture:** Add one format-neutral public payload view and independent
v2 callback tables. Compile `namelist`/`param` into immutable expression spans,
materialize them in session-owned scratch immediately before prepare, and keep
the existing effect journal and invocation registry as the only publication
authorities.

**Tech stack:** C11, CFlow Statechart/SCXML, CMeta descriptors and expression
programs, TinyTest, CMake Presets.

**Spec:**
`docs/superpowers/specs/2026-08-30-cflow-scxml-payload-bindings-design.md`

## Global constraints

- Preserve all v1 public layouts, entry points, and accepted behavior.
- Use the official SCXML 1.0 ordering and exclusivity rules.
- Keep payloads bounded and allocation-free during execution.
- Keep inline text/XML, `idlocation`, internal object data, and late binding
  unsupported in this batch.
- Add no transport or serialization dependency.

### Task 1: Specify the additive v2 public contract with failing tests

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Add tests that construct v2 Event I/O/invoke adapters, require exact ABI
  and capability admission, and prove a payload program rejects a v1-only
  session.
- [x] Add compile tests for legal and illegal `namelist`, `param`, and scalar
  content combinations; run the focused target and observe the expected
  failures before implementation.
- [x] Define the typed scalar, ordered payload view, v2 request/adapter tables,
  v2 adapter bundle, requirement/capability bits, and v2 session entry points.

### Task 2: Compile ordered bounded payload descriptors

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Recognize and validate `<param>`.
- [x] Count `namelist` tokens and parameter children with checked arithmetic;
  retain duplicate entries and document order.
- [x] Emit immutable name/expression descriptor spans for `send` and `invoke`.
- [x] Compile external scalar content for both operations and retain existing
  immediate-internal scalar behavior.
- [x] Destroy all new expression programs on every compile failure and program
  destruction path.
- [x] Run the focused compile tests.

### Task 3: Materialize send payloads through Event I/O v2

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Normalize v1/v2 Event I/O configuration into one private session table.
- [x] Allocate one exact-size session scratch array at initialization and free
  it during all normal/failure teardown paths.
- [x] Evaluate named/scalar payload values exactly once against staged state,
  call v2 prepare with a callback-scoped view, and preserve ticket rollback.
- [x] Cover ordering, duplicates, scalar kinds, full/error results, accepted
  ticket validation, and v1 behavior.
- [x] Build and run the focused CMeta SCXML target.

### Task 4: Materialize invoke payloads through invocation v2

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/tests/cflow_scxml_cmeta_test.c`

- [x] Normalize v1/v2 invocation configuration into the private session table.
- [x] Evaluate invocation payload values once against the stable committed
  state and pass them to v2 prepare_start.
- [x] Preserve registry states, statistics, adapter error events, cancellation,
  forwarding, close, and quiescence behavior.
- [x] Cover named and scalar invoke payloads plus evaluation/adapter failures.
- [x] Build and run the focused CMeta SCXML target.

### Task 5: Document and verify the compatibility boundary

**Files:**
- Modify: `cflow/README.md`
- Modify: `cflow-scxml/tests/w3c/manifest.tsv` only when a matching fixture is
  executable under this exact profile.

- [x] Document v2 ownership, borrow invalidation, capability admission,
  ordering, unsupported content forms, and migration from v1.
- [x] Run all `cflow_scxml` tests, then the full configured suite.
- [x] Run `codegraph sync .`, `git diff --check`, and inspect status/diff.
- [x] Record any remaining conformance items without claiming text/XML,
  `idlocation`, or late-binding completion.
