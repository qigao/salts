# Separate CFlow and transport benchmark implementation plan

> **For Codex:** Execute this plan in the existing isolated worktree with test-driven changes and verification before completion.

**Goal:** Remove invalid cross-layer Actor/Reactive comparisons from the NativeIO/CNet/libuv benchmark workflow and add an independently selectable Linux io_uring benchmark job.

**Architecture:** Keep `cnet_io_benchmark` as a transport data-plane comparison using one explicitly selected NativeIO backend for both direct NativeIO and CNet. Keep Actor and Reactive performance tests in CFlow, where each workload defines its own sender/receiver topology, demand, scheduling, and denominator; do not compare those numbers with transport drivers.

**Tech stack:** C11, CMake Presets, TinyTest, GitHub Actions, PowerShell/bash runner steps.

---

### Task 1: Add a tested benchmark-backend selector

**Files:**
- Create: `cnet/benchmarks/cnet_io_benchmark_config.h`
- Create: `cnet/benchmarks/cnet_io_benchmark_config.c`
- Create: `cnet/tests/cnet_io_benchmark_config_test.c`
- Modify: `cnet/benchmarks/CMakeLists.txt`
- Modify: `cnet/tests/CMakeLists.txt`
- Modify: `cnet/benchmarks/cnet_io_benchmark.c`

1. Add tests for the platform default, every platform-supported explicit backend, and invalid or platform-incompatible values.
2. Build the test before implementing the selector and confirm the expected unresolved implementation failure.
3. Implement a private fail-fast selector for `CNET_IO_BENCHMARK_BACKEND` without changing public CNet or NativeIO APIs.
4. Pass one selected backend through both NativeIO and CNet fixtures and print that backend in the report.
5. Build and run `cnet_io_benchmark_config_test`.

### Task 2: Add a dedicated io_uring CI matrix entry

**Files:**
- Modify: `.github/workflows/native-io-release-benchmarks.yml`

1. Add an Ubuntu 24.04 `io_uring` matrix entry beside the existing `epoll` entry.
2. Pass the matrix backend through `CNET_IO_BENCHMARK_BACKEND`.
3. Give every matrix entry a unique artifact suffix so the two Linux jobs cannot collide.
4. Keep NativeIO/CNet/libuv contract tests and benchmarks in this workflow; remove CFlow paths, targets, tests, and benchmark steps.

### Task 3: Remove the invalid mixed benchmark contract

**Files:**
- Delete: `cflow/benchmarks/cflow_native_io_adapter_benchmark.c`
- Delete: `.github/scripts/cflow-benchmark-stats.ps1`
- Delete: `.github/tests/cflow-benchmark-stats-test.ps1`
- Modify: `cflow/benchmarks/CMakeLists.txt`
- Modify: `cflow/README.md`
- Modify: `native-io/README.md`

1. Remove the target that compares NativeIO direct, Actor/NativeIO, and Reactive/NativeIO under one denominator.
2. Remove the unused statistics helper that encodes Actor/Source and Direct/Actor comparisons.
3. Document that adapter contract tests verify integration correctness only.
4. Document that Actor and Reactive benchmarks require separate topology-specific methods and are not transport-driver comparisons.

### Task 4: Verify and update the pull request

1. Build and run the selector test and adjacent CNet/NativeIO contract tests.
2. Run the Release `cnet_io_benchmark` with the platform default; on Linux also run explicit `epoll` and `io_uring` selections.
3. Run the full configured CTest suite and whitespace checks.
4. Commit and push the correction, update PR #238 title/body, and require the exact new SHA's CI results before merge.

## Compatibility and rollback

No public API, ABI, protocol, or production runtime behavior changes. The environment variable is private to the benchmark executable and rejects invalid values instead of falling back. Removing the mixed benchmark changes only developer benchmark targets and CI artifacts; the CFlow NativeIO adapter correctness test remains available in the normal CFlow test suites. The correction can be reverted as one benchmark/CI commit.
