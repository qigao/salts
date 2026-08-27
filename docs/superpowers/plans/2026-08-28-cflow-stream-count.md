# CFlow Stream `count` Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a reusable, managed-value-safe Stream count terminal through the existing interpreted CFlow runtime.

**Architecture:** A checked bound Range Source feeds the existing normalized runtime helper. A terminal-local sink increments a checked accumulator and publishes it only after normal completion. TurboSTL exposes a prefixed result facade without a global C++-hostile `count` macro.

**Tech Stack:** C11/C++, CFlow runtime, CMeta Range, TurboSTL facade, TinyTest, CMake Presets.

---

### Task 1: Add failing public-contract tests

**Files:**
- Create: `cflow/tests/cflow_stream_count_test.c`
- Modify: `cflow/tests/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Modify: `turbostl/tests/turbostl_stream_test.c`
- Modify: `turbostl/tests/turbostl_stream_cpp_test.cpp`
- Modify: `turbostl/tests/turbostl_managed_stream_test.c`
- Modify: `tests/install_consumer/consumer.c`

- [x] Add CFlow tests for empty, transformed, repeated, invalid, and Range-error behavior.
- [x] Add TurboSTL C/C++ facade tests and a C++ `std::count` coexistence compile check.
- [x] Add a managed-value lifecycle test proving the terminal performs no copy or move.
- [x] Add installed-consumer use of the new exported API.
- [x] Build the focused targets and confirm failure is caused by the missing count API.

### Task 2: Implement the CFlow terminal

**Files:**
- Modify: `cflow/include/cflow/adapters.h`
- Modify: `cflow/src/adapters.c`
- Create: `cflow/src/adapters_internal.h`

- [x] Declare and document `cflow_eval_count` ownership, errors, and output rules.
- [x] Add a checked counting sink with overflow detection.
- [x] Probe terminal state after exact demand exhaustion and verify it through
  the internal bounded form.
- [x] Admit the bound Range through `cflow_source_from_range_checked` and execute with `cflow_eval_source`.
- [x] Publish the count only after successful runtime completion.
- [x] Build and run focused CFlow tests until green.

### Task 3: Add the TurboSTL facade and documentation

**Files:**
- Modify: `turbostl/include/turbostl/stream.h`
- Modify: `cflow/README.md`
- Modify: `turbostl/README.md`

- [x] Add `turbostl_count_result` and `turbostl_stream_count`.
- [x] Document empty, unbounded, managed, error, repeated-evaluation, and interpreted-only behavior.
- [x] Explain why there is no global `count` macro.
- [x] Build and run focused TurboSTL and installed-consumer tests until green.

### Task 4: Verify impact and regression scope

**Files:**
- Verify: all modified files

- [x] Run `codegraph affected` for the changed public headers and implementation.
- [x] Run focused Release CFlow/TurboSTL/header/install-consumer tests.
- [x] Run the full Release CTest suite if the focused suite passes.
- [x] Run `git diff --check`, inspect the diff, and scan for placeholders.
- [x] Record exact verification evidence and remaining platform/CI risk.
