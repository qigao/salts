# CBind Enum and Variant Decode Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development to implement this plan task-by-task.

**Goal:** Add format-neutral, transactional enum and tagged-variant decoding to TurboUtils CBind without guessing native C storage layout.

**Architecture:** CMeta gains append-only versioned storage lifecycle adapters and checked facades. CBind preflights enum/variant graphs, consumes exact CSerde scalar/array tokens, and reuses its semantic-zero rollback model. TurboParser remains outside this change.

**Tech Stack:** C11, CMeta, CSerde, CBind, TinyTest, CMake Presets, MSVC/Ninja.

**Spec:** `docs/superpowers/specs/2026-08-24-cbind-enum-variant-decode-design.md`

## Global Constraints

- Work only in `feature/cbind-variant-decode`, based on latest
  `origin/master`.
- Do not modify `utils/vendor`.
- Add tests before each production behavior and observe the expected failure.
- Preserve descriptor ABI through `struct_size` field-end checks.
- Run Windows commands through `VsDevCmd.bat` and repository presets.
- Do not add parser, format, DOM, encode, replace, or incremental-decoder APIs.

## Task 1: Add checked CMeta enum storage adapters

**Files:**

- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Modify: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

1. Add TinyTest cases proving that a complete enum adapter can query zero,
   assign/read a declared value, restore zero, reject undeclared values, reject
   mismatched storage, and restore zero after provider failure.
2. Build `cmeta_data_test` and observe compilation failure because the enum ops
   ABI and facades do not exist.
3. Append `enum_ops` to `cmeta_data_desc`, define the v1 ops record and checked
   facade declarations, and implement buffer-style ABI/storage validation.
4. Build and run `cmeta_data_test`; expect all enum adapter cases to pass.
5. Extend the C++ header test with standard-layout assertions and build/run it.
6. Commit: `feat(cmeta): add checked enum storage adapters`.

## Task 2: Add checked CMeta variant lifecycle adapters

**Files:**

- Modify: `cmeta/include/cmeta/data.h`
- Modify: `cmeta/src/data.c`
- Modify: `cmeta/tests/cmeta_data_test.c`
- Modify: `cmeta/tests/cmeta_header_cpp_test.cpp`

1. Add tests for select/active-tag/restore-zero, unknown tags, storage mismatch,
   provider failure rollback, and successful-callback postcondition violation.
2. Build `cmeta_data_test` and observe compilation failure for missing variant
   ops/facades.
3. Append `variant_ops`, implement the checked facades, and preserve shallow
   generic descriptor validation.
4. Build and run CMeta C/C++ tests; expect success.
5. Commit: `feat(cmeta): add checked variant lifecycle adapters`.

## Task 3: Implement enum preflight and decode

**Files:**

- Create: `cbind/tests/cbind_enum_decode_test.c`
- Modify: `cbind/tests/CMakeLists.txt`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/README.md`

1. Add an enum fixture and tests for text, symbol, signed, and unsigned tokens,
   plus unknown, oversized, float, wrong-token, callback-failure, rollback, and
   destination-not-empty behavior.
2. Fresh-configure because a test source is added; build the new target and
   observe behavioral failures (`CBIND_UNSUPPORTED`) against real CBind.
3. Add enum adapter preflight before reader consumption.
4. Add slice-safe enum lookup and exact integer decode, followed by checked
   CMeta assignment.
5. Add enum semantic-zero/reset dispatch.
6. Run the focused enum test and existing scalar/transaction tests.
7. Commit: `feat(cbind): decode semantic enums`.

## Task 4: Implement variant graph preflight

**Files:**

- Create: `cbind/tests/cbind_variant_decode_test.c`
- Modify: `cbind/tests/CMakeLists.txt`
- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/src/struct.c`
- Create: `cbind/src/variant.c`
- Modify: `cbind/CMakeLists.txt`

1. Add preflight tests for missing/mismatched ops, bad offsets, invalid tag
   ranges, unsupported direct container cases, struct/variant cycles, depth,
   and nested-struct scratch limits. Each test records that the reader was not
   consumed.
2. Fresh-configure, build the new target, and observe `CBIND_UNSUPPORTED` or
   missing-symbol failures.
3. Add variant validation declarations and implementation. Reuse validation
   frames across struct/variant edges and compute maximum active struct bitmap
   scratch across every case path.
4. Add `variant.c` to the target's existing explicit source list and fresh
   configure.
5. Run focused preflight tests and existing struct/preflight-order tests.
6. Commit: `feat(cbind): preflight variant graphs`.

## Task 5: Implement transactional variant decode

**Files:**

- Modify: `cbind/src/internal.h`
- Modify: `cbind/src/decode.c`
- Modify: `cbind/src/scalar.c`
- Modify: `cbind/src/struct.c`
- Modify: `cbind/src/variant.c`
- Modify: `cbind/tests/cbind_variant_decode_test.c`
- Modify: `cbind/README.md`

1. Add success tests for scalar, buffer, struct-with-container, enum, and nested
   variant payloads using the exact two-element array grammar.
2. Add failure tests for wrong begin/end, missing tag/payload/end, extra item,
   unknown tag, select failure, dirty selected payload, payload failure, and
   source failure. Assert semantic-zero rollback and exact error attribution.
3. Run the focused test and observe failures before adding decode dispatch.
4. Implement strict tag parsing, checked select, selected-payload zero
   verification, struct-container preparation, payload decode, and exact end
   token checking.
5. Add variant semantic-zero/reset dispatch so root, struct, and nested rollback
   converge on the provider lifecycle.
6. Run the focused variant, buffer, container, struct, transaction, and error
   attribution tests.
7. Commit: `feat(cbind): decode tagged variants transactionally`.

## Task 6: Verify ABI, docs, and regressions

**Files:**

- Modify if required: `tests/install_consumer/consumer.c`
- Modify: `cbind/README.md`
- Review: all files changed by Tasks 1-5

1. Fresh configure with `win-release-user`.
2. Build CMeta, CSerde, CBind, all related tests, and install-consumer targets.
3. Run `ctest --preset win-release-user -R "^(cmeta_|cserde_|cbind_|install_)" --output-on-failure`.
4. Run the repository's broader Release test preset if focused tests pass.
5. Scan changed files for placeholders, debug-only focused tests, private
   parser dependencies, and `utils/vendor` changes.
6. Review `git diff --check`, `git diff --stat`, and the exact diff against
   `origin/master`.
7. Commit any final documentation-only correction as
   `docs(cbind): document enum and variant decode`.
8. Push the branch and create a PR targeting `master`, including exact test
   evidence and the explicit TurboParser follow-up boundary.
