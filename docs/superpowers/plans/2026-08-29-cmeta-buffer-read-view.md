# CMeta Buffer Read View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a bounded borrowed read-view contract for reflected CMeta `STRING` and `BYTES` values without changing existing assignment behavior.

**Architecture:** Extend the existing versioned `cmeta_data_buffer_ops` adapter by appending an optional read callback. A checked CMeta facade validates the descriptor, callback result, and caller-supplied byte ceiling before publishing outputs; TurboUtils `tstr` and `vstr` adapters provide thin layout-aware implementations. Existing adapters without the appended callback remain valid for assignment and fail reads with `CMETA_TRAIT_MISSING`.

**Tech Stack:** C11, CMeta descriptors, TurboUtils `tstr`/`vstr`, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

## Global Constraints

- The returned byte view is borrowed; it remains valid only while the source object and provider-owned storage remain alive and unmodified.
- A successful zero-length view may use a null data pointer; a non-empty successful view must not.
- `max_bytes` is a hard ceiling checked by the facade before outputs are published.
- Failure never modifies caller output pointers or sizes.
- Existing `cmeta_data_buffer_ops_of`, assignment, restore, and legacy/truncated adapter behavior remain unchanged.
- The change adds no allocation and no SCXML public data-model admission.

---

### Task 1: Checked CMeta Buffer Read Contract

**Files:**
- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Test: `cmeta/tests/cmeta_data_test.c`
- Test: `cmeta/tests/cmeta_header_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `cmeta_data_desc`, `cmeta_data_buffer_ops`, and `cmeta_status` contracts.
- Produces: `cmeta_data_buffer_read_fn` and `cmeta_data_buffer_read(const cmeta_data_desc *, const void *, size_t, const unsigned char **, size_t *)`.

- [x] **Step 1: Write failing facade tests**

  Add provider-backed tests for a successful borrowed binary view, an empty view, capacity rejection, malformed non-empty null data, provider failure, missing read trait, invalid arguments, and unchanged outputs on every failure.

- [x] **Step 2: Verify the focused test fails for the missing interface**

  Build `cmeta_data_test`; expect compilation to fail because `cmeta_data_buffer_read_fn`, the appended callback member, and `cmeta_data_buffer_read` do not exist.

- [x] **Step 3: Implement the minimal checked facade**

  Append `read` after `restore_zero`, retain the old prefix validation for existing operations, and implement read-specific trait validation. Invoke the provider into local variables, reject a non-empty null pointer with `CMETA_CALLBACK_ERROR`, reject oversized views with `CMETA_CAPACITY_EXCEEDED`, and publish caller outputs only on `CMETA_OK`.

- [x] **Step 4: Verify C and C++ CMeta tests pass**

  Build and run `cmeta_data_test` and `cmeta_header_cpp_test` using `win-release-user`.

### Task 2: Turbo String Adapters and Cross-TU Compatibility

**Files:**
- Modify: `utils/include/turbo_cmeta_data.h`
- Modify: `utils/src/turbo_cmeta_data.c`
- Modify: `utils/tests/test_turbo_cmeta_data.c`
- Modify: `utils/tests/test_turbo_cmeta_data_cpp.cpp`
- Modify: `utils/tests/turbo_cmeta_data_peer.c`

**Interfaces:**
- Consumes: the checked CMeta read contract from Task 1 and the existing `tstr`/`vstr` storage APIs.
- Produces: read-capable `turbo_tstr_cmeta_buffer_ops` and `turbo_vstr_cmeta_buffer_ops`; the UUID descriptor remains a valid write-only adapter because its object does not retain canonical source text.

- [x] **Step 1: Write failing adapter behavior tests**

  Assert exact binary-safe views for owned `tstr`, borrowed `vstr`, empty values, and byte ceilings. Assert that the exported UUID adapter remains valid across translation units and reports `CMETA_TRAIT_MISSING` for reads. Keep expected bytes and sizes literal.

- [x] **Step 2: Verify the adapter tests fail because read callbacks are absent**

  Build and run the focused Turbo CMeta targets; expect `CMETA_TRAIT_MISSING` or the new success assertions to fail before adapter implementation.

- [x] **Step 3: Implement thin adapter callbacks**

  Return borrowed data and exact lengths from `tstr` and `vstr` without allocation, copying, mutation, or lifetime extension. Do not synthesize a UUID string from its binary object or require a read callback in UUID provider identity validation.

- [x] **Step 4: Run focused and adjacent regression tests**

  Run `cmeta_data_test`, `cmeta_header_cpp_test`, `test_turbo_cmeta_data`, and `test_turbo_cmeta_data_cpp`, then the complete Release CTest preset and focused ASan tests.

### Task 3: Contract Documentation and Final Verification

**Files:**
- Modify: `cmeta/include/cmeta/data.h`
- Modify: `utils/include/turbo_cmeta_data.h`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-cmeta-data-model-selection.md`

**Interfaces:**
- Consumes: verified behavior from Tasks 1 and 2.
- Produces: explicit borrowed lifetime, invalidation, output-preservation, and compatibility documentation for the next private SCXML string-expression increment.

- [x] **Step 1: Document ownership and compatibility at the API boundary**

  State that views expire on mutation, assignment, restore, destruction, or provider storage invalidation; readers must not retain them across those boundaries or concurrent mutation.

- [x] **Step 2: Re-run format/build/test verification and inspect the diff**

  Run the repository formatter on modified C/C++ files using the existing command, rebuild affected targets, run focused plus full Release tests, run focused ASan tests, and inspect `git diff --check` and `git status --short`.
