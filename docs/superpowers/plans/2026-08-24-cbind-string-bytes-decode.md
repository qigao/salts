# CBind String/Bytes Decode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement bounded, transactional root and struct decoding for owned
and borrowed CMeta STRING/BYTES storage.

**Architecture:** CMeta exposes a versioned, format-neutral buffer adapter and
checked facade. TurboUtils Core supplies `tstr` and `vstr` providers. CBind
validates descriptor/context/lifetime rules, invokes the facade, and extends
its existing whole-root rollback without linking Core.

**Tech Stack:** C11, CMeta, CSerde, CBind, TurboUtils Core (`tstr`/`vstr`),
TinyTest, CMake user presets, MSVC.

**Design:** `docs/superpowers/specs/2026-08-24-cbind-string-bytes-decode-design.md`

## Task 1: CMeta buffer adapter contract

**Files:**

- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Modify: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

- [x] Add behavior tests for operation-table validation, semantic type and
  ownership mismatch, null/length rejection, zero-state precondition,
  assignment failure preserving zero, and restore-to-zero.
- [x] Build `cmeta_data_test` and capture RED failures caused by missing API.
- [x] Append `buffer_ops` to `cmeta_data_desc`, define the versioned operations,
  and add checked facade declarations.
- [x] Implement prefix-safe adapter discovery and facade validation in
  `data.c`; retain the old descriptor-validity prefix.
- [x] Build and run `cmeta_data_test` plus `cmeta_header_cpp_test` until GREEN.

## Task 2: TurboUtils standard tstr/vstr adapters

**Files:**

- Create: `utils/include/turbo_cmeta_data.h`
- Create: `utils/tests/test_turbo_cmeta_data.c`
- Create: `utils/tests/test_turbo_cmeta_data_cpp.cpp`

- [x] Add real-provider tests for tstr exact-byte copy, embedded NUL, zero
  state, restore, and invalid assignment; add vstr stable-address borrow,
  canonical empty state, and restore tests.
- [x] Reconfigure so the existing source/test collection sees the new files;
  build the tests and capture RED compile/link failures.
- [x] Define semantically identified, header-local tstr/vstr type descriptors
  and immutable adapter tables that remain usable in Windows C static
  initializers.
- [x] Implement adapters using `tstr_new_len`, `tstr_freep`, and direct vstr
  state transitions; preserve zero state on failure.
- [x] Build and run both provider tests until GREEN.

## Task 3: CBind public context and preflight

**Files:**

- Modify: `cbind/include/cbind/context.h`
- Modify: `cbind/tests/cbind_header_cpp_test.cpp`
- Create: `cbind/tests/cbind_buffer_decode_test.c`
- Modify: `cbind/tests/CMakeLists.txt`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/src/struct.c`

- [x] Add compile/runtime tests showing old context initializers retain zero
  defaults and the new initializer exposes `max_buffer_bytes`.
- [x] Add recording-reader preflight tests proving missing/malformed adapters,
  custom ownership, storage mismatch, old context prefixes, and buffer
  container elements are rejected without consuming input.
- [x] Build `cbind_buffer_decode_test` and capture RED failures.
- [x] Append `max_buffer_bytes`, add `CBIND_CONTEXT_WITH_BUFFERS_INIT`, and
  preserve existing macros with an explicit zero tail.
- [x] Teach graph and reflected-struct validation to recognize root/field
  buffer adapters while leaving container scalar validation unchanged.
- [x] Run preflight tests until GREEN.

## Task 4: CBind root buffer decode

**Files:**

- Create: `cbind/src/buffer.c`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/tests/cbind_buffer_decode_test.c`

- [x] Add root tests for owned STRING/BYTES with transient/stable tokens,
  borrowed stable tokens, borrowed transient rejection, exact token-kind
  mismatch, zero/exact/over-limit sizes, non-empty destination, and adapter
  error attribution.
- [x] Run the focused test and verify the new behavioral cases fail for the
  intended unsupported/missing path.
- [x] Implement exact-kind/lifetime/limit checks and adapter invocation.
- [x] Route root emptiness/reset and decode through buffer helpers.
- [x] Run the focused test until GREEN.

## Task 5: Struct buffer fields and whole-root rollback

**Files:**

- Modify: `cbind/src/struct.c`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/tests/cbind_buffer_decode_test.c`

- [x] Add a struct fixture containing scalar, owned string/bytes, and borrowed
  string fields.
- [x] Add success tests plus failures after one or more fields have committed:
  transient borrowed input, over-limit payload, wrong token, source error, and
  provider failure. Assert every field returns to semantic zero and error
  attribution names the failing field.
- [x] Run the focused test and capture RED rollback failures.
- [x] Extend struct emptiness/reset recursion and value dispatch for buffer
  fields; reuse the CMeta facade as the only target-state fact source.
- [x] Run all CBind tests until GREEN.

## Task 6: Documentation and compatibility surfaces

**Files:**

- Modify: `cmeta/README.md`
- Create: `cbind/README.md`
- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cbind/include/cbind/context.h`
- Modify: `utils/include/turbo_cmeta_data.h`

- [x] Document adapter ownership, zero state, failure atomicity, and single-
  owner rules.
- [x] Document CSerde borrowed-lifetime constraints and the per-buffer limit.
- [x] Provide complete root and struct descriptor/context examples using the
  standard providers.
- [x] Confirm no example advertises container buffer elements or custom
  ownership as implemented.

## Task 7: Verification and handoff

**Files:**

- Verify all changed files and generated target surfaces.

- [x] Run `cmake --fresh --preset win-release-user` under `VsDevCmd.bat`.
- [x] Build focused targets: CMeta data/header tests, Core adapter tests, all
  CBind tests, and existing tstr/vstr tests.
- [x] Run focused CTest regex with `--output-on-failure`.
- [x] Build all Release targets and run the full Release CTest preset.
- [x] Build the `install-win-release-user` preset.
- [x] Configure/build/run a small external C and C++ consumer against the
  installed `TurboUtils::CMeta`, `TurboUtils::Core`, and `TurboUtils::CBind`
  package targets.
- [x] Inspect `git diff --check`, `git status`, and the final diff; confirm no
  `.codegraph`, build, or vcpkg artifact is tracked.
- [ ] Commit the verified changes, push `feat/cbind-string-bytes-decode`, and
  open a PR with exact verification evidence.
