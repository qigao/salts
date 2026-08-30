# CFlow Reactive Public API Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the generic CFlow Source/Run/Sink public surface with a breaking Publisher/Subscriber/Subscription Reactive API and remove the obsolete concepts from user documentation and benchmarks.

**Architecture:** The existing bounded execution engine remains the single implementation and is renamed behind the Reactive contract rather than wrapped by a second state owner. Publisher is the moved input role, Subscriber is the borrowed output role, and opaque Subscription owns demand, wait, cancellation, errors, and live graph state. Actor remains independent; the I/O Publisher is an adapter over Actor and NativeIO.

**Tech Stack:** C11, CMeta interfaces, CFlow Graph and Scheduler, NativeIO, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-30-cflow-reactive-public-api-redesign.md`

## Global Constraints

- This is CFlow ABI v4; do not add aliases, compatibility wrappers, deprecated headers, or fallback paths.
- Preserve Publisher move ownership, bounded demand, first-error, cancellation, and quiescent-close semantics.
- Preserve `CFlow -> CMeta/NativeIO/Platform`; NativeIO must not depend on CFlow.
- Public user terminology is Direct, Actor, Reactive, Publisher, Subscriber, and Subscription.
- Statechart/Machine executions are Instances; Graph input and non-Reactive
  source-code/origin meanings remain unchanged.
- Execute inline; do not dispatch sub-agents.

---

### Task 1: Define the breaking Reactive header contract

**Files:**
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Replace: `cflow/include/cflow/reactive.h`
- Rename: `cflow/include/cflow/runtime.h` into the Reactive implementation contract
- Rename: `cflow/include/cflow/sources.h` to `cflow/include/cflow/publishers.h`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`

**Interfaces:**
- Produces: `cflow_publisher`, `cflow_subscriber`, opaque `cflow_subscription`, `cflow_subscribe()`, `cflow_subscription_request/cancel/close/is_done/is_cancelled/error/status/outstanding_demand/wake()`.
- Produces: `cflow_publisher_from_array/range/timer/channel/readiness()`.

- [ ] **Step 1: Change the aggregate C++ header test to instantiate `cflow_publisher`, `cflow_subscriber`, `cflow_subscription`, and `cflow_publish_context`, and to type-check `cflow_subscribe()` with the signature from the spec.**

- [ ] **Step 2: Build `cflow_header_cpp_test` and verify RED because the Publisher contract and new headers do not exist.**

  Run: `cmake --build --preset win-release-user --target cflow_header_cpp_test`

- [ ] **Step 3: Replace the public protocol declarations and aggregate includes, remove installed obsolete headers, and bump `CFLOW_LIBRARY_VERSION` to `4.0.0` with ABI `4`.**

- [ ] **Step 4: Build `cflow_header_cpp_test` and verify it passes with the new public layout.**

### Task 2: Rename the single execution engine and Publisher factories

**Files:**
- Rename: `cflow/src/runtime.c` to `cflow/src/subscription.c`
- Rename: `cflow/src/runtime_internal.h` to `cflow/src/subscription_internal.h`
- Rename: `cflow/src/sources.c` to `cflow/src/publishers.c`
- Rename: `cflow/src/sources_internal.h` to `cflow/src/publishers_internal.h`
- Remove: `cflow/src/reactive.c`
- Modify: all CFlow core callers and tests selected by `rg.exe -l "cflow_run|cflow_source|cflow_sink" cflow`
- Rename: `cflow/tests/cflow_runtime_test.c` to `cflow/tests/cflow_reactive_test.c`

**Interfaces:**
- Consumes: Task 1 Reactive types.
- Produces: the existing execution behavior under only the new symbol family.

- [ ] **Step 1: Rename the lifecycle test expectations to Subscription move, demand, wait/wake, cancellation, first-error, and close behavior; build the target and verify RED on missing new symbols.**

- [ ] **Step 2: Mechanically rename exact C identifiers and constants, then manually resolve Subscription ownership, callback, and internal include boundaries without semantic branches.**

- [ ] **Step 3: Build and run `cflow_reactive_test`, `cflow_pipeline_test`, `cflow_stream_execution_test`, `cflow_adapter_lifecycle_test`, and `cflow_calculus_conformance_test`.**

### Task 3: Migrate I/O, readiness, temporal, Machine, and Statechart adapters

**Files:**
- Rename: `cflow/include/cflow/io_source.h` to `cflow/include/cflow/io_publisher.h`
- Rename: `cflow/src/io_source.c` to `cflow/src/io_publisher.c`
- Rename: `cflow/src/io_source_internal.h` to `cflow/src/io_publisher_internal.h`
- Rename: `cflow/tests/cflow_io_source_test.c` to `cflow/tests/cflow_io_publisher_test.c`
- Rename: `cflow/tests/cflow_io_source_init_failure_test.c` to `cflow/tests/cflow_io_publisher_init_failure_test.c`
- Modify: `cflow/include/cflow/readiness.h`, `temporal.h`, `machine_instance.h`, `machine_hierarchy.h`, `statechart_instance.h`
- Modify: corresponding `cflow/src/*.c` and focused tests

**Interfaces:**
- Produces: `cflow_io_publisher_owner/config/stats/window_stats`, `cflow_publisher_from_io_actor[_windowed]()`, readiness Publisher ownership, temporal Publisher decorators, Machine/Statechart Instance APIs, and their Publisher adapters.

- [ ] **Step 1: Convert focused I/O and readiness tests to the new API and verify RED on the missing Publisher factories.**

- [ ] **Step 2: Rename adapters and preserve one authoritative Actor/native completion state, fixed window capacity, explicit backpressure, and close ordering.**

- [x] **Step 3: Build and run `cflow_io_publisher_test`, `cflow_io_native_adapter_test`, `cflow_readiness_test`, `cflow_temporal_test`, `cflow_machine_instance_test`, and `cflow_statechart_instance_adapter_test`.**

### Task 4: Migrate extensions, examples, and benchmarks

**Files:**
- Rename: `cflow-fs/include/cflow/fs_watch_source.h` to `cflow-fs/include/cflow/fs_watch_publisher.h`
- Rename: `cflow-fs/src/fs_watch_source.c` to `cflow-fs/src/fs_watch_publisher.c`
- Rename: `cflow-fs/tests/cflow_fs_watch_source_test.c` to `cflow-fs/tests/cflow_fs_watch_publisher_test.c`
- Modify: `cflow/examples/*.c`, `cflow/minicoro/include/cflow/minicoro.h`
- Modify: `cflow/benchmarks/cflow_native_io_adapter_benchmark.c`
- Rename: obsolete Source-named CFlow benchmark targets and files to Reactive names
- Modify: related CMake files and workflow target names

**Interfaces:**
- Consumes: public Reactive and I/O Publisher contracts.
- Produces: examples and extensions with no dependency on removed headers or symbols; benchmark rows `NativeIO Direct`, `Actor`, and `Reactive`.

- [ ] **Step 1: Change the NativeIO benchmark fixture to create a Publisher and call `cflow_subscribe()`; build and verify RED before implementation migration.**

- [ ] **Step 2: Migrate extension and example targets, deleting obsolete Source-named installed headers and target names.**

- [ ] **Step 3: Build and run CFlow-FS tests, CFlow examples, `native_io_pipe_benchmark`, and `cflow_native_io_adapter_benchmark`.**

### Task 5: Rewrite user documentation and verify installed surface

**Files:**
- Modify: `ARCHITECTURE.md`
- Modify: `cflow/README.md`
- Modify: `cflow/examples/README.md`
- Modify: `cflow-fs/README.md`
- Modify: `native-io/README.md`
- Modify: CFlow public header documentation

**Interfaces:**
- Produces: one public conceptual model—Direct, Actor, Reactive—with exact Publisher, Subscriber, and Subscription lifecycle documentation.

- [ ] **Step 1: Rewrite overview, architecture diagrams, selection tables, I/O sections, examples, ownership tables, and benchmark explanations without presenting implementation-only execution mechanics as user models.**

- [ ] **Step 2: Run `rg.exe` over current user documentation and inspect every remaining generic `Source` or `Runtime` occurrence; retain only non-Reactive meanings and exact historical/spec references.**

- [ ] **Step 3: Configure/build the installed-package verification targets and confirm obsolete headers are absent from the install tree.**

- [ ] **Step 4: Run Windows Release focused tests, remote Linux Release focused tests through `root@eu`, and `git diff --check`; report exact assertions and any unverified platform risk.**
