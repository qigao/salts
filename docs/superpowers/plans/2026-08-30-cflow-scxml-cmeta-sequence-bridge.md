# CFlow SCXML CMeta Sequence Bridge Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a private, allocation-free bridge that resolves one reflected CMeta sequence location and opens a validated sized, ordered borrowed Range for later SCXML `<foreach>` execution.

**Architecture:** Compile a dotted NCName path against the existing `cmeta_data_struct_shape` and its paired `cmeta_struct_desc` layout. The compiled private program retains only descriptor pointers, the root-relative field offset, the concrete container storage type, and the declared element type. Runtime opening validates the actual container's semantic sequence projection, concrete generic application, Range element type, and `SIZED | ORDERED` capabilities without depending on Container types.

**Tech Stack:** C11, CMeta data/declared-type/cstl/Range protocols, CFlowScxml private adapters, TinyTest, CMake presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

## Global Constraints

- Preserve all public CMeta and CFlowScxml structures and functions in this increment.
- The program borrows the immutable root schema, layout metadata, storage type, and element type until program destruction.
- The opened Range borrows the staged root container and is valid only until that container is mutated or destroyed; the bridge retains neither Range elements nor cursor state.
- Execution is single-threaded through the existing Statechart SerialExecutor. Concurrent container mutation is outside the contract and must surface through the Range version protocol.
- Require exact sequence semantics plus sized, stable document order; set/map and unordered/unsized ranges fail fast.
- Snapshot only the reported length. No element storage is allocated or retained by the bridge.
- Bounds checks use the root storage size, reflected layout field size, checked addition, and semantic type equality.
- Do not add `<foreach>` admission or a public iteration-limit field in this increment.
- Do not commit or push this dirty worktree.

---

### Task 1: Specify the private sequence bridge behavior

**Files:**
- Create: `cflow-scxml/tests/cflow_scxml_cmeta_sequence_test.c`
- Modify: `cflow-scxml/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `Struct(TYPE(Vec, int), ...)`, `cmeta_data_sequence`, and the existing raw Container Vec Range provider.
- Produces: behavioral requirements for `cflow_scxml_cmeta_sequence_compile()` and `cflow_scxml_cmeta_sequence_open()`.

- [x] **Step 1: Write the failing success-path test**

  Declare a reflected root containing `TYPE(Vec, int) values`; compile `values`, open an initialized Vec, assert snapshot length two, and consume literal values `17` and `29` in order.

- [x] **Step 2: Write the failing rejection tests**

  Assert compile-time rejection of missing, malformed, unknown, scalar, and set locations. Assert runtime rejection of an unbound handle, a mismatched element type, and a sequence Range missing `SIZED` or `ORDERED`.

- [x] **Step 3: Run the new target and record RED**

  Configure `win-dev-user`, build `cflow_scxml_cmeta_sequence_test`, and confirm the test cannot link because the private sequence bridge is absent.

### Task 2: Implement the allocation-free reflected sequence bridge

**Files:**
- Create: `cflow-scxml/src/cmeta_sequence.h`
- Create: `cflow-scxml/src/cmeta_sequence.c`
- Modify: `cflow-scxml/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_scxml_cmeta_sequence_program`, `cflow_scxml_cmeta_sequence_compile()`, and `cflow_scxml_cmeta_sequence_open()`.
- `compile()` returns the existing CMeta expression status/diagnostic family and leaves output zero on failure.
- `open()` returns a validated borrowed `cmeta_range` and an immutable length snapshot only on success; failure leaves both outputs unchanged.

- [x] **Step 1: Resolve a dotted reflected path without retaining source text**

  Parse UTF-8 NCName segments, traverse concrete struct descriptors, pair each data field with its layout field, and use the final declared container storage/element types for the abstract sequence descriptor.

- [x] **Step 2: Validate runtime container and Range contracts**

  Check storage bounds, semantic `CMETA_DATA_SEQUENCE`, valid unary type application, declared/runtime element type equality, default Range creation, exact Range element equality, and `SIZED | ORDERED` plus size callback.

- [x] **Step 3: Keep borrowed outputs transactional**

  Build local Range/length values first and publish caller outputs only after every invariant succeeds; allocate no memory and retain no cursor or element.

- [x] **Step 4: Run the focused target to GREEN**

  Build and run `cflow_scxml_cmeta_sequence_test` through `win-dev-user` and confirm every success/error/lifetime contract passes.

### Task 3: Document and verify the bridge boundary

**Files:**
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`
- Modify: `cflow/README.md`

**Interfaces:**
- Consumes: the private bridge implementation and tests.
- Produces: an accurate distinction between available sequence traversal foundation and still-unimplemented `<foreach>` semantics.

- [x] **Step 1: Update the sequence foundation status**

  Document the paired data/layout resolution, exact runtime type check, sized/ordered borrowed Range, length snapshot, and invalidation boundary. Keep `<foreach>`, indices, and public iteration-limit configuration explicitly unsupported.

- [x] **Step 2: Run adjacent Debug ASan tests**

  Run `cflow_scxml_cmeta_sequence_test`, `cflow_scxml_cmeta_test`, and `cmeta_core_test` through `win-dev-user`.

- [x] **Step 3: Run Release regression and consistency checks**

  Build through `win-release-user`, run the registered CTest suite, then run `codegraph sync .` and `git diff --check`; record exact results without committing or pushing.

## Verification Record

- RED: the new `cflow_scxml_cmeta_sequence_test` target failed at compile time because the requested private `cmeta_sequence.h` bridge did not exist.
- Focused GREEN: `cflow_scxml_cmeta_sequence_test` passed 1/1 through `win-dev-user`.
- Debug ASan adjacent regression: `cmeta_core_test`, `cflow_scxml_cmeta_test`, and `cflow_scxml_cmeta_sequence_test` passed 3/3 through `win-dev-user`.
- Release regression: the registered `win-release-user` CTest suite passed 200/200.
- Structural consistency: `codegraph sync .` indexed 3 changed files; CodeGraph identified the bridge's CMeta cstl/type/range dependencies, while manual `rg.exe` confirmed the dedicated test call sites because `codegraph affected` did not associate the new private source with a test target.
- Repository consistency: `git diff --check` completed without diagnostics after the final plan update.
