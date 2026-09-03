# CFlow Branching CSR Evaluation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Produce reproducible evidence that selects or rejects a private CSR
view for existing branching CFlow phases without changing production behavior.

**Architecture:** Implement five benchmark-only adjacency paths over the flat
edge fact source, test them differentially, and run them beside a valid nested
RELATION validation path. Keep all candidate storage private, bounded,
transactional and absent from the public Graph ABI.

**Tech Stack:** C11, CFlow Graph, Container HashMap, TinyTest benchmarks, CMake
Presets, GitHub Actions fixed Release hosts.

**Spec:** `docs/superpowers/specs/2026-08-24-cflow-branching-csr-evaluation-design.md`

## Global Constraints

- The flat edge array remains the sole topology fact source.
- Do not modify public Graph layouts, signatures or valid runtime behavior.
- Preserve outgoing-edge order within every source interval.
- Check endpoint, addition and multiplication bounds before allocation.
- Failed construction leaves a reusable zero-state output; no fallback.
- Adopt CSR only for a measured valid phase with greater than 30% improvement
  and no more than 20% retained/peak memory regression.
- Release shared-runner measurements are evidence, never timing gates.

---

### Task 1: Benchmark-only adjacency views

**Files:**

- Create: `cflow/benchmarks/cflow_branching_views.h`
- Create: `cflow/benchmarks/cflow_branching_views.c`

**Interfaces:**

- Consumes: borrowed `const cflow_subgraph *` and optional test allocator.
- Produces: flat observations plus build/traverse/destroy APIs for pointer,
  HashMap and CSR views, with `cflow_branching_memory` accounting.

- [x] **Step 1: Declare status, observation, memory and view contracts**

  Define distinct `OK`, invalid argument, invalid edge, overflow and allocation
  failure statuses. Each owned view stores the allocator needed by destroy.

- [x] **Step 2: Implement checked flat reference observation**

  Scan sources in node-id order and scan the edge fact source in storage order,
  mixing `(source, ordinal, target, ports)` into an order-sensitive digest.

- [x] **Step 3: Implement transactional pointer and CSR builders**

  Allocate exact `V`/`E` storage, validate endpoints while deriving, publish
  only after all allocations and fills succeed, and restore zero on destroy.

- [x] **Step 4: Implement bounded HashMap span adjacency**

  Reserve the exact non-empty-source count, group copied targets in stable
  order, and derive requested allocation/byte accounting from public capacity
  and stride fields.

### Task 2: Differential correctness through TDD

**Files:**

- Create: `cflow/tests/cflow_branching_views_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 1 benchmark-private view contracts.
- Produces: a TinyTest executable that protects ordering, CSR invariants,
  boundary validation and transactional cleanup.

- [x] **Step 1: Write the failing interleaved-edge differential test**

  Use literal edges `{2->3, 0->2, 2->1, 0->1}` and assert the expected per-source
  target order before the view implementation is linked.

- [x] **Step 2: Run the focused target and verify RED**

  Run `cmake --build --preset win-release-user --target cflow_branching_views_test`.
  The expected failure is an unresolved or missing benchmark-view contract, not
  a syntax or fixture error.

- [x] **Step 3: Link Task 1 and verify GREEN**

  Add `../benchmarks/cflow_branching_views.c` to the test target, link
  `Salts::CFlow`, `Salts::CSTL` and `Salts::TinyTest`, then run the
  executable through CTest.

- [x] **Step 4: Add one-behavior boundary tests**

  Cover empty, single edge, invalid endpoint, checked overflow, allocator
  failure after one CSR allocation, destroy/rebuild and exact memory formulas.

### Task 3: Release benchmark workloads

**Files:**

- Create: `cflow/benchmarks/cflow_branching_csr_benchmark.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`

**Interfaces:**

- Consumes: Task 1 view APIs and the existing cross-TU Graph-path identity
  callable.
- Produces: correctness-checked setup, traversal, memory and valid nested
  RELATION validation evidence in `cflow_direct_benchmark`.

- [x] **Step 1: Build bounded synthetic fixtures**

  Generate empty, single-edge, typical sparse, high-fan-out, skewed and peak
  source-grouped edge arrays with literal hard maxima of 4096 vertices and 8190
  edges.

- [x] **Step 2: Build a valid nested RELATION fixture**

  Compose linear branch Graphs with `cflow_graph_relation`, validate the final
  Graph once outside timing, and keep all branch storage alive until teardown.

- [x] **Step 3: Assert differential equivalence outside timing**

  Compare edge counts and digests for flat lookup, one-pass control, pointer,
  HashMap and CSR. Assert CSR offset invariants for every workload.

- [x] **Step 4: Add setup and traversal rows**

  Use `benchmark_batch` for complete build/destroy and `benchmark_ops` for
  complete immutable edge traversals. Publish the digest to a volatile sink.

- [x] **Step 5: Print deterministic memory accounting**

  Emit allocation count, allocated, retained and peak requested bytes for each
  representation and workload before timed rows.

### Task 4: Evidence, verification and delivery

**Files:**

- Modify: `docs/superpowers/specs/2026-08-24-cflow-branching-csr-evaluation-design.md`
- Modify: `docs/superpowers/plans/2026-08-24-cflow-branching-csr-evaluation.md`
- Inspect: `.github/workflows/cflow-release-benchmarks.yml`

**Interfaces:**

- Consumes: local MSVC output and four fixed-host workflow artifacts.
- Produces: an explicit adopt/reject decision tied to issue #46's two gates.

- [x] **Step 1: Run focused Release verification**

  Build the new test and `cflow_direct_benchmark`; run the new differential
  test, all CFlow tests and the combined benchmark.

- [x] **Step 2: Run adjacent repository verification**

  Run the full `win-release-user` build and CTest preset, then `lake test` for
  CFlow Lean obligations when the repository target is available.

- [x] **Step 3: Record the local decision inputs**

  Add the exact host/compiler, sample outputs, memory calculations and whether
  each adoption gate passed. Do not infer Linux results from Windows.

- [x] **Step 4: Request code review and resolve findings**

  Review benchmark semantics, ownership, arithmetic, source-order parity,
  memory accounting and the absence of production/public changes.

- [x] **Step 5: Commit, push and create the PR**

  Use a PR body that closes #46 only when the evidence document selects adopt
  or reject, and keep the worktree for CI/review fixes.

- [x] **Step 6: Inspect all fixed-host evidence**

  Confirm Ubuntu 22.04/24.04 and Windows 2022/2025 jobs succeeded and artifacts
  contain five raw runs plus metadata. Amend the evidence document if a host
  invalidates the local decision, then rerun verification before updating the
  PR.
