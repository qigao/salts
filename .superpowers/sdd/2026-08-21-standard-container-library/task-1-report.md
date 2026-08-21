# Task 1 report — Container target and public surface

## Result

Established the independent `TurboUtils::Container` target from the accepted
untracked `container/` baseline.  Its exported public interface now depends
only on `TurboUtils::CMeta`; it has no Core link or include dependency.

## RED

1. Added C11 and C++17 smoke tests for `<turbo/container.h>` before wiring the
   Container target.
2. Ran a fresh `win-release-user` configure followed by
   `cmake --build --preset win-release-user --target container_header_test`.
3. Observed the expected failure: Ninja reported the unknown target
   `container_header_test`, because the Container target and its tests were
   not yet added to the root build graph.

## GREEN

1. A fresh `win-release-user` configure succeeded after adding `turbo_container`.
2. Built `container_header_test` and `container_header_cpp_test` successfully.
3. `ctest --preset win-release-user -R "^container_header" --output-on-failure`
   passed both tests.
4. Clang 21.1.1 syntax-only C11 and C++17 checks passed for the two public
   aggregate-header tests.
5. `git diff --check` passed.
6. Generated export evidence shows `TurboUtils::Container` has
   `INTERFACE_LINK_LIBRARIES "TurboUtils::CMeta"` and no `TurboUtils::Core`;
   its exported include directory is Container's own `include` tree.

## Files

- Added `container/CMakeLists.txt`, `container/tests/CMakeLists.txt`, and C/C++ header tests.
- Added `container/include/turbo/container.h` plus the Container-owned
  `export.h` and `status.h` boundary headers.
- Moved every former flat `container/include/turbo_*.h` public header into
  `container/include/turbo/container/` without compatibility forwarding headers.
- Migrated Container headers and sources from Core platform/error symbols to
  `CONTAINER_API` and `CONTAINER_*` statuses. Empty pop operations now report
  `CONTAINER_EMPTY`; associative/tree missing-key cases report
  `CONTAINER_NOT_FOUND`.
- Updated root order to `cmeta`, `cflow`, `container`, `utils`, `turbo_serial`
  and updated package configuration dependency setup.

## Scope and risk

- No Task 2 ownership, generation, or explicit-limit API work was performed.
- No empty examples or benchmark directories were added; the baseline has none.
- Existing accepted `stream/` and `turbo/` deletions remain untouched.
- The baseline raw APIs retain their existing `int` return signatures while
  returning the new `CONTAINER_*` values; later ownership/typed-facade tasks
  can make any intentional public signature refinements together with their
  API migration.

## Commit

`build(container): establish standard container target`
