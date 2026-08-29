# CFlow SCXML Phase 3 Executable Block and Raise Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Compile bounded SCXML executable blocks containing `raise` into native CFlow Statechart actions and provide borrowed runtime bindings that execute the resulting internal Events.

**Architecture:** `TurboUtils::CFlowScxml` retains immutable block bytecode and binding rows inside the owning program; the native Statechart keeps only typed executable declarations and ordered action references. One frontend callback interprets a block on the existing single-owner Statechart runtime and stages raised Events through the native transactional queue API.

**Tech Stack:** C11, TurboUtils XML parser, CMeta type/effect descriptors, CFlow Statechart IR/runtime, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-raise-design.md`

## Global Constraints

- Preserve the null data model and its one-byte `cmeta_type_bool` false witness.
- Do not expose system variables because W3C SCXML 1.0 makes them inaccessible in the null data model.
- Keep `TurboUtils::CFlow` independent of XML and SCXML; all new behavior remains in optional `TurboUtils::CFlowScxml`.
- Preserve transactional compilation, deterministic document order, exact source diagnostics, bounded allocation, and fail-fast unsupported-feature behavior.
- The program must outlive every native runtime instance using its borrowed executable binding users.
- Do not claim completion of a Phase 3 checkbox from this increment alone.

---

### Task 1: Publish the borrowed runtime-binding contract

**Files:**
- Modify: `cflow-scxml/include/cflow/scxml.h`
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Consumes: existing opaque `cflow_scxml_program` ownership and `cflow_statechart_executable_binding` from `cflow/statechart_runtime.h`.
- Produces: `bool cflow_scxml_program_runtime_bindings(const cflow_scxml_program *, const cflow_statechart_executable_binding **, size_t *)`.

- [x] **Step 1: Write the failing zero-binding and invalid-argument tests**

```c
const cflow_statechart_executable_binding *bindings =
    (const cflow_statechart_executable_binding *)(uintptr_t)1u;
size_t binding_count = SIZE_MAX;

check_true(cflow_scxml_program_runtime_bindings(
    &program, &bindings, &binding_count));
check_null(bindings);
check_equal(binding_count, (size_t)0u);
check_false(cflow_scxml_program_runtime_bindings(
    NULL, &bindings, &binding_count));
```

- [x] **Step 2: Build the focused target and verify RED**

Run from a VS developer environment:

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
```

Expected: compilation fails because `cflow_scxml_program_runtime_bindings` is not declared.

- [x] **Step 3: Add the public declaration and empty-program implementation**

```c
bool cflow_scxml_program_runtime_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count);
```

The implementation validates all pointers, publishes `NULL/0` for a valid
Phase 2 program, and documents the borrowed lifetime through program destroy.

- [x] **Step 4: Build and run the focused test to verify GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: the target builds and the focused test passes.

- [x] **Step 5: Commit the public contract**

```powershell
git add cflow-scxml/include/cflow/scxml.h cflow-scxml/src/scxml.c cflow-scxml/tests/cflow_scxml_test.c
git commit -m "feat: expose SCXML runtime bindings"
```

### Task 2: Compile and execute onentry raise blocks

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Consumes: Task 1 runtime-binding accessor and native `cflow_statechart_raise_fn`.
- Produces: immutable program-owned block/step storage, native executable declarations, state-action references, and bindings for nonempty `onentry` blocks.

- [x] **Step 1: Write a failing runtime trace test**

```c
static const char source[] =
    "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
    "<state id='start'><onentry><raise event='advance'/></onentry>"
    "<transition event='advance' target='done'/></state>"
    "<final id='done'/></scxml>";

check_equal(compile_status(source, &program, &diagnostic), CFLOW_SCXML_OK);
check_true(cflow_scxml_program_runtime_bindings(
    &program, &bindings, &binding_count));
check_equal(binding_count, (size_t)1u);
```

Initialize the real native Statechart runtime with those bindings and assert
that initialization processes the raised internal Event and reaches the
top-level final state.

- [x] **Step 2: Run the focused test and verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: the compiler returns `CFLOW_SCXML_UNSUPPORTED_FEATURE` for `raise`.

- [x] **Step 3: Implement counted block/step admission and lowering**

Add `SCXML_ELEMENT_RAISE`, an XML `NMTOKEN` validator, checked block/step/action
counts, raised-event occurrence collection, and storage for:

```c
typedef enum scxml_step_kind {
    SCXML_STEP_RAISE = 1
} scxml_step_kind;

typedef struct scxml_step {
    scxml_step_kind kind;
    cflow_event_id event;
} scxml_step;
```

Emit one executable declaration and one entry action for the nonempty block.
The shared binding callback copies the null state byte and calls
`raise_internal` for each step, stopping on the first failure.

- [x] **Step 4: Run the focused test to verify GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: the raised Event drives the machine to final state during initial
stabilization and all existing SCXML tests pass.

- [x] **Step 5: Commit onentry raise execution**

```powershell
git add cflow-scxml/src/scxml.c cflow-scxml/tests/cflow_scxml_test.c
git commit -m "feat: execute SCXML raise blocks"
```

### Task 3: Complete raise placement, order, diagnostics, and bounded failure

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`
- Create: `cflow-scxml/tests/fixtures/raise_trace.scxml`
- Create: `cflow-scxml/tests/fixtures/raise_trace.expected`

**Interfaces:**
- Consumes: Task 2 block/step compiler and the existing native state/transition action phases.
- Produces: onexit, ordinary transition, initial-transition, and history-transition raise lowering with shared block IDs and deterministic trace fixtures.

- [x] **Step 1: Write failing placement and trace tests**

Add literal fixtures whose expected trace proves exit actions run before
transition actions, transition actions before entry actions, and several raises
within one block preserve document order. Add focused cases for initial and
history transition actions and for a multi-event transition sharing one block.

```text
exit.raised
transition.raised
entry.raised
```

- [x] **Step 2: Write failing diagnostic and queue-boundary tests**

Use independent literals for missing, empty, whitespace-containing, and invalid
`event` values; raise child content; unknown unqualified attributes; and an
unsupported `log` sibling. Assert status plus the owning element/attribute byte
offset. Configure internal-event capacity below a two-raise block and assert
`CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL`, no committed raised Event, and
no committed configuration change.

- [x] **Step 3: Run the focused test and verify RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: placement cases remain unsupported or lack action references, and
the new trace/diagnostic expectations fail for those missing behaviors.

- [x] **Step 4: Extend lowering and cleanup transactionally**

Associate one compiled block with every supported nonempty executable parent,
duplicate only transition-action references for multi-token native rows, keep
all rows in document order, enforce `CFLOW_STATECHART_MAX_ACTION_REFS`, and free
temporary/program executable storage on every failure and on program destroy.

- [x] **Step 5: Run focused and adjacent tests to verify GREEN**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test cflow_statechart_test cflow_statechart_runtime_test --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_test|cflow_statechart_runtime_test)$" --output-on-failure
```

Expected: all three tests pass with deterministic traces and stable failures.

- [x] **Step 6: Commit complete raise lowering**

```powershell
git add cflow-scxml/src/scxml.c cflow-scxml/tests
git commit -m "test: cover SCXML raise execution semantics"
```

### Task 4: Document and verify the optional installed boundary

**Files:**
- Modify: `cflow/README.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-raise-design.md`

**Interfaces:**
- Consumes: installed `TurboUtils::CFlowScxml` header/target and Task 1 binding accessor.
- Produces: documented lifecycle example and installed-consumer compilation of the new public API.

- [x] **Step 1: Add installed-consumer compilation coverage**

```c
const cflow_statechart_executable_binding *bindings = NULL;
size_t binding_count = 0u;
if (!cflow_scxml_program_runtime_bindings(
        &program, &bindings, &binding_count)) {
    return 1;
}
```

- [x] **Step 2: Build package verification with the public accessor**

```powershell
cmake --build --preset win-release-user --target verify_installed_package --parallel
```

Expected: the verification target stages the updated header/library, then the
external consumer configures and compiles the accessor without source-tree
include paths.

- [x] **Step 3: Document binding use and program lifetime**

Update the CFlow README example to place the borrowed bindings into
`cflow_statechart_instance_config`, keep the program alive through instance
destroy, and state that only `raise` is supported in executable blocks under
the null data model.

- [x] **Step 4: Run focused, complete, install, and feature-off verification**

```powershell
cmake --build --preset win-release-user --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|cflow_statechart_test|cflow_statechart_runtime_test|verify_installed_package)$" --output-on-failure
ctest --preset win-release-user --output-on-failure
cmake --build --preset install-win-release-user --parallel
```

Also configure the documented SCXML-disabled package tree and run its package
consumer to prove that no `CFlowScxml` target or header leaks when the option is
off.

- [x] **Step 5: Commit documentation and package verification**

```powershell
git add cflow/README.md tests/install_consumer/consumer.c docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-raise-design.md
git commit -m "docs: describe SCXML raise runtime bindings"
```

## Self-review

- Spec coverage: all design sections map to Tasks 1-4; system variables and
  non-null data models are explicitly excluded rather than silently omitted.
- Placeholder scan: every implementation and verification step names concrete
  files, interfaces, commands, outcomes, and failure behavior.
- Type consistency: every task uses the exact
  `cflow_scxml_program_runtime_bindings` signature and existing native
  `cflow_statechart_executable_binding` type.
