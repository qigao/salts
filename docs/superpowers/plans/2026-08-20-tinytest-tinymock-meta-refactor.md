# TinyTest/TinyMock Meta Refactor Implementation Plan

**Goal:** Stabilize PR #2's TinyTest build, remove duplicated C11 type-routing between TinyTest and TinyMock, and add first-phase Matcher + Answer behavior without coupling either framework to the full CMeta runtime.

## Architecture boundary

- Keep `TurboUtils::TinyTest` source-compatible and header-consumable.
- Preserve the current large implementations byte-for-byte as `tinytest_impl.h` and `tinymock_impl.h` during this migration.
- Put reusable C scalar-family routing in `tinytest_meta.h`; it is inspired by CMeta's small routing layer but has no CMeta include/link dependency.
- Keep ordinary mock arguments as typed values. Matcher and Answer are explicit callable objects only where behavior is required.
- Do not move the runner/state machine into a compiled library in this phase. The wrapper/implementation split establishes that seam first; a later phase can move stable runtime functions behind a static target without changing public macros again.

## Tasks

1. Add `tinytest_meta.h` and route C11 generic assertion maps through it.
2. Preserve the existing TinyTest implementation as `tinytest_impl.h`; add a small public wrapper that fixes GCC/Clang diagnostic macro statement boundaries without rewriting the implementation blob.
3. Preserve the existing TinyMock implementation as `tinymock_impl.h`; add a public wrapper that shares meta routing.
4. Add `TINYMOCk_ARG_THAT` / `TINYMOCk_MATCH` predicate matching.
5. Add `TINYMOCk_ANSWER` for dynamic return computation from invocation arguments.
6. Add/extend TinyTest and TinyMock self-tests and include implementation/meta headers in installation.
7. Validate Linux/GCC plus the PR's existing Windows/macOS/Android preset CI before considering the runtime static-library extraction.
