# NativeIO Names and Threaded Reactive Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete the `native_io_*` public API migration and replace the serialized NativeIO Reactive helper with a Publisher/Subscriber two-worker execution path.

**Architecture:** NativeIO remains a single-owner, caller-driven backend. A dedicated one-worker Turbo thread pool owns Publisher/NativeIO progress, while a one-worker CFlow Worker Scheduler owns Subscription pumping and Subscriber callbacks. The two roles exchange only bounded control edges; NativeIO request slots, Actor request slots, and Subscription demand remain their respective authoritative states.

**Tech Stack:** C11/C++17 headers, NativeIO, CFlow Actor/Reactive, Turbo thread pool, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-31-cflow-threaded-reactive-native-io-design.md`

## Constraints

- Execute inline; do not dispatch sub-agents.
- Do not add compatibility aliases or fallback backends/Schedulers.
- One NativeIO backend has one data-plane owner; tests must not call it concurrently.
- Both worker queues are bounded and shutdown must settle all accepted tasks.

### Task 1: Lock the final NativeIO public spelling

**Files:**
- Modify: `native-io/tests/native_io_header_cpp_test.cpp`
- Modify: `native-io/include/turbo/native_io.h`
- Modify: `native-io/src/*.c`, tests, benchmarks, CFlow consumers, and current user documentation

- [x] Change the C++ header test to instantiate only `native_io_*` types, functions, and `NATIVE_IO_*` constants.
- [x] Build `native_io_header_cpp_test` and verify RED on missing names.
- [x] Rename the public declarations and all repository consumers without aliases.
- [x] Build and run `native_io_header_cpp_test` and `native_io_test`.

### Task 2: Separate Publisher drive from Subscriber scheduling

**Files:**
- Modify: `cflow/include/cflow/io_native_adapter.h`
- Modify: `cflow/src/io_native_adapter.c`
- Modify: `cflow/tests/cflow_io_native_adapter_test.c`

- [x] Replace the serialized Reactive test with a two-worker test that expects the new Publisher-only drive API and records distinct execution roles.
- [x] Build `cflow_io_native_adapter_test` and verify RED before implementation.
- [x] Replace the serialized helper with `drive_publisher()` and a coalesced cross-thread NativeIO wake protocol.
- [x] Run the focused adapter and I/O Publisher tests, including shutdown and cancellation cases.

### Task 3: Make the benchmark measure the real Reactive topology

**Files:**
- Modify: `cflow/benchmarks/cflow_native_io_adapter_benchmark.c`
- Modify: benchmark documentation in `cflow/README.md` and `native-io/README.md`

- [x] Give Reactive mode one long-lived Publisher pool task and a one-worker Subscriber Scheduler; route drive edges through a coalesced wake plus condition signal.
- [x] Keep Direct as the sole denominator and preserve identical payload/operation counts.
- [x] Assert worker admission, completion, release, acknowledgement, quiescence, and role separation outside timed blocks.
- [x] Build and run a Windows Release benchmark smoke pass.

### Task 4: Verify the breaking surface and adjacent regressions

**Files:**
- Modify: current public examples/docs that still use obsolete NativeIO names or serialized Reactive wording

- [x] Scan current public code and user documentation for obsolete NativeIO prefixes and the removed serialized helper name; require zero live public/consumer occurrences.
- [x] Build and run `native_io_test`, `native_io_header_cpp_test`, `cflow_io_native_adapter_test`, `cflow_io_publisher_test`, and `cflow_reactive_test`.
- [x] Run `git diff --check` and inspect the final diff for ownership, shutdown, and API consistency.
