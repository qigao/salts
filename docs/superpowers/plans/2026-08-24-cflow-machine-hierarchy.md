# CFlow Machine Hierarchy Implementation Plan

> **For Codex:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add a validated hierarchical Machine declaration that lowers to the existing flat Machine IR, plus a runtime wrapper whose state-scoped Timer Events are canceled on state exit.

**Architecture:** `machine_hierarchy.c` owns hierarchy metadata, expands inherited transitions into a flat `cflow_machine`, and stores route metadata aligned to flat transition rows. A private Machine-runtime commit hook observes the selected row without creating a second state source. Timer Event scope is private to the hierarchy wrapper and preserves all existing public behavior.

**Tech Stack:** C11, CMeta descriptors, TurboUtils concurrency, CFlow Machine/Mailbox/Timer Event, TinyTest, CMake Presets, Lean 4.

---

### Task 1: Specify failing hierarchy build tests

**Files:**
- Create: `cflow/tests/cflow_machine_hierarchy_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

1. Add TinyTest cases for a two-level hierarchy, initial descent, leaf-first bubbling, same-node priority, composite target descent, exit/entry route order, DONE/ERROR leaves, and validation failures.
2. Register `cflow_machine_hierarchy_test` and build it to confirm failure because the API is absent.
3. Keep fixtures homogeneous in state type and assert exact flat transition order.

### Task 2: Implement immutable hierarchy normalization

**Files:**
- Create: `cflow/include/cflow/machine_hierarchy.h`
- Create: `cflow/src/machine_hierarchy.c`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

1. Define the additive public declarations, status values, owning handle, and borrowed route query.
2. Copy and validate nodes with checked size arithmetic; build parent/depth and initial-leaf tables without recursion.
3. Expand each declared transition over descendant source leaves, resolve target composites, order candidates by depth then declared priority, and call `cflow_machine_build` atomically.
4. Build immutable route arrays aligned with the normalized flat transitions and publish only after all allocations and validation succeed.
5. Run the hierarchy and C++ header tests; add boundary assertions until they pass.

### Task 3: Add scoped Timer Event internals test-first

**Files:**
- Modify: `cflow/src/timer_event_internal.h`
- Modify: `cflow/src/timer_event.c`
- Modify: `cflow/tests/cflow_timer_event_test.c`

1. Add internal tests for scoped scheduling, canceling all pending timers in one state scope, retaining other scopes, and FIRE_WON behavior.
2. Extend private slots with a scope ID. Route existing public schedule calls through scope zero.
3. Add private schedule-with-scope and cancel-scope functions; keep one lock acquisition per batch cancellation and preserve statistics.
4. Run `cflow_timer_event_test` and confirm existing public cases remain unchanged.

### Task 4: Add private commit hook and hierarchy runtime wrapper

**Files:**
- Modify: `cflow/src/machine_runtime_internal.h`
- Modify: `cflow/src/machine_runtime.c`
- Modify: `cflow/include/cflow/machine_hierarchy.h`
- Modify: `cflow/src/machine_hierarchy.c`
- Modify: `cflow/tests/cflow_machine_hierarchy_test.c`

1. Add failing tests for nested transition execution, state query delegation, scoped timer cancellation on exit, equal-deadline FIFO, terminal/error propagation, close, and cancel.
2. Add an internal initializer accepting a non-user commit hook and selected normalized transition index; keep public initialization as a null-hook wrapper.
3. Implement the hierarchy instance as ownership composition of one Machine instance and one Timer Event queue.
4. Validate timer scopes against hierarchy nodes and bracket target commit so exit-scope cancellation is atomically visible to the Timer data plane.
5. Run hierarchy, Machine-runtime, Actor, and Timer Event tests.

### Task 5: Formalize normalization and trace equivalence

**Files:**
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/CFlow/MachineHierarchy.lean`
- Create: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus/Proofs/MachineHierarchy.lean`
- Create: `formal/cmeta_cflow_calculus/Test/PhaseATests/MachineHierarchy.lean`
- Modify: `formal/cmeta_cflow_calculus/CMetaCFlowCalculus.lean`
- Modify: `formal/cmeta_cflow_calculus/Test/PhaseATests.lean`

1. Model ancestry, initial descent, descendant expansion, bubbling keys, and LCA routes over finite declaration rows.
2. Prove child-first selection equivalence between hierarchical candidates and flattened priority rows.
3. Prove route exit and entry order and terminal-kind preservation under target descent.
4. Add executable examples mirroring the C fixture and run `lake test`.

### Task 6: Add hierarchy Release benchmark and regressions

**Files:**
- Create: `cflow/benchmarks/cflow_machine_hierarchy_benchmark.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`

1. Benchmark flat build versus hierarchy normalization separately.
2. Benchmark equivalent steady-state flat and hierarchy-wrapper event execution without counting construction.
3. Configure/build with `win-release-user`, run the benchmark, and record observed output in the PR description rather than hard-coding claims.
4. Run the complete focused CFlow test set before moving to temporal adapters.
