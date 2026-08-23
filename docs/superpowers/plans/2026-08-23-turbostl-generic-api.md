# TurboSTL Generic API Implementation Plan

> **For Codex:** Execute this plan task-by-task with test-driven development and fresh verification before completion.

**Goal:** Restore TurboSTL's finite CMeta Generic API while retaining PR #53's self-describing raw-handle initializers and make typed Stream terminals explicitly name their output container.

**Architecture:** CMeta continues to route `typed(kind, ...)`; a TurboSTL-owned schema generates concrete wrappers over compiled raw handles. PR #53 declaration/expression forms remain erased-handle initializers. Generated `Type_method` functions and distinctly named typed Stream terminals avoid shadowing raw List/Map calls or three-argument Stream terminals.

**Tech Stack:** C11, CMeta Schema/macros, TurboSTL, CFlow, CMake Presets, TinyTest.

**Spec:** `docs/superpowers/specs/2026-08-23-turbostl-generic-api-design.md`

---

### Task 1: Establish the failing public contract

**Files:**
- Modify: `tests/install_consumer/consumer.c`
- Modify: `turbostl/tests/install_consumer/consumer.c`
- Modify: `turbostl/tests/turbostl_stream_test.c`

- [x] Replace instance declarations with `typed(Vec, InstalledInts, int)` and representative `typed(List, ...)`/`typed(Map, ...)` declarations.
- [x] Preserve and exercise PR #53's `Vec(...)`/`Map(...)` and
  `VecOf(...)`/`MapOf(...)` erased-handle initializers alongside finite Generic
  kinds.
- [x] Exercise generated `Type_method` operations and explicitly typed Stream terminals.
- [x] Require generated collector factories to accept their concrete wrapper pointer type.
- [x] Build the focused targets and confirm they fail because TurboSTL kinds and typed terminals are unavailable.

### Task 2: Restore TurboSTL-owned finite facade generation

**Files:**
- Create: `turbostl/include/turbostl/detail/typed_initializers.h`
- Create: `turbostl/include/turbostl/detail/typed_facade.h`
- Modify: `turbostl/include/turbostl/typed.h`

- [x] Move the TurboSTL-specific kind schema and generator adapters behind a TurboSTL detail header.
- [x] Adapt initialization/destruction rows to current `*_raw_*` storage bridges while retaining existing compiled algorithms.
- [x] Register all thirteen kinds with CMeta while retaining PR #53's initializer macros.
- [x] Keep raw List/Map functions unshadowed and use generated `Type_method` operations.
- [x] Build and run the focused Generic consumer test.

### Task 3: Restore the typed Stream terminal contract

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify: `turbostl/tests/turbostl_stream_test.c`

- [x] Add `collector(Type, out, limit)`, `collect_typed(stream, Type, out, limit)`, and `to_list_typed(stream, Type, out, limit)`.
- [x] Retain three-argument `collect(stream, out, limit)` and `to_list(stream, out, limit)` for self-describing raw handles.
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

- [x] Add generated wrapper coverage without dropping declaration/expression initializer coverage.
- [x] Keep raw algorithm tests on the production compatibility surface.
- [x] Document generated Generic and erased-handle initializer roles, ownership, errors, and both Stream usages.
- [x] Run all TurboSTL tests plus adjacent CMeta/CFlow tests.

### Task 5: Verify installation, review, and publish

**Files:**
- Modify as required by review findings.

- [x] Run the repository's MSVC preset configure/build and focused CTest suites from a clean environment.
- [x] Run the install consumer workflow.
- [x] Inspect the diff for unintended raw ABI/storage changes.
- [x] Commit, request an independent code review, address important findings, and rerun verification.
- [x] Push `feat/turbostl-generic-api` and open a PR against `master` with the design, compatibility impact, and exact verification evidence.
