# CFlow Stream terminal implementation plan

1. Add public terminal declarations and an opaque owned `find_first` result.
2. Add RED tests for scalar results, empty input, errors, short circuit,
   repeated evaluation, C++ headers, and managed values.
3. Generalize the synchronous adapter driver only enough to recognize
   terminal-owned short-circuit cancellation.
4. Implement typed predicate validation, checked count, retained first value,
   and visitor invocation.
5. Add TurboSTL-prefixed convenience wrappers and update user documentation.
6. Run focused Release tests, all CFlow/TurboSTL tests, focused ASan tests,
   installed-package verification, `git diff --check`, and CodeGraph sync.
