# TurboSTL Generic API Implementation Plan

> **For Codex:** Execute this plan task-by-task with test-driven development and fresh verification before completion.

**Goal:** Restore TurboSTL's finite CMeta Generic API as the single typed application surface and make Stream terminals explicitly type their output container.

**Architecture:** CMeta continues to route `typed(kind, ...)`; a TurboSTL-owned schema generates concrete wrappers over compiled raw handles. Semantic operation macros dispatch through the declared type token. CFlow execution remains unchanged and receives a collector created by the generated output type.

**Tech Stack:** C11, CMeta Schema/macros, TurboSTL, CFlow, CMake Presets, TinyTest.

**Spec:** `docs/superpowers/specs/2026-08-23-turbostl-generic-api-design.md`

---

### Task 1: Establish the failing public contract

**Files:**
- Modify: `turbostl/tests/install_consumer/consumer.c`
- Modify: `turbostl/tests/turbostl_stream_test.c`

- [x] Replace instance declarations with `typed(Vec, InstalledInts, int)` and representative `typed(List, ...)`/`typed(Map, ...)` declarations.
- [x] Exercise semantic type-token operations and four-argument `to_list`/`collect`.
- [x] Build the focused targets and confirm they fail because TurboSTL kinds and typed terminals are unavailable.

### Task 2: Restore TurboSTL-owned finite facade generation

**Files:**
- Create: `turbostl/include/turbostl/detail/typed_facade.h`
- Modify: `turbostl/include/turbostl/typed.h`

- [x] Move the TurboSTL-specific kind schema and generator adapters behind a TurboSTL detail header.
- [x] Adapt initialization/destruction rows to current `*_raw_*` storage bridges while retaining existing compiled algorithms.
- [x] Register all thirteen kinds with CMeta and remove the competing instance declaration macros.
- [x] Add semantic type-token operations without adding an inference fallback.
- [x] Build and run the focused Generic consumer test.

### Task 3: Restore the typed Stream terminal contract

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify: `turbostl/tests/turbostl_stream_test.c`

- [x] Restore `collector(Type, out, limit)`, `collect(stream, Type, out, limit)`, and `to_list(stream, Type, out, limit)`.
- [x] Preserve explicit capacity, transactional abort, and borrowed-source semantics.
- [x] Build and run `turbostl_stream_test`.

### Task 4: Migrate typed-facing coverage and documentation

**Files:**
- Modify: `turbostl/tests/turbostl_header_test.c`
- Modify: `turbostl/tests/turbostl_header_typed_test.c`
- Modify: `turbostl/tests/turbostl_typed_test.c`
- Modify: `turbostl/tests/turbostl_entry_test.c`
- Modify: `turbostl/tests/turbostl_generic_identity_test.c`
- Modify: `turbostl/tests/turbostl_semantic_projection_test.c`
- Modify: `turbostl/tests/turbostl_list_test.c`
- Modify: `turbostl/tests/turbostl_map_test.c`
- Modify: `turbostl/tests/turbostl_tree_test.c`
- Modify: `turbostl/README.md`
- Modify: `cmeta/LANGUAGE_REFERENCE.md`

- [x] Migrate typed-facing declarations to named generated wrapper types.
- [x] Keep raw algorithm tests on a test-only compatibility header where appropriate.
- [x] Document the unique Generic surface, semantic calls, ownership, errors, and Stream usage.
- [x] Run all TurboSTL tests plus adjacent CMeta/CFlow tests.

### Task 5: Verify installation, review, and publish

**Files:**
- Modify as required by review findings.

- [x] Run the repository's MSVC preset configure/build and focused CTest suites from a clean environment.
- [x] Run the install consumer workflow.
- [x] Inspect the diff for unintended raw ABI/storage changes.
- [ ] Commit, request an independent code review, address important findings, and rerun verification.
- [ ] Push `feat/turbostl-generic-api` and open a PR against `master` with the design, compatibility impact, and exact verification evidence.
