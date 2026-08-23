# CFlow Execution Foundation Completion Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Close the evidence and regression gaps left after the CFlow execution-foundation implementation without changing its public API or runtime semantics.

**Architecture:** Treat the current Platform → Concurrency → CFlow → Core implementation as the source of truth. Add behavior-level coverage for the Platform primitives that the design names, add a reusable external installed-package consumer for every exported foundation target, make Linux/Windows CI exercise those boundaries, and update the historical documents with evidence instead of rewriting their original red/green history.

**Tech Stack:** C11, TinyTest, CMake 3.20+, CMake Presets, GitHub Actions, TurboUtils Platform/Concurrency/CMeta/CFlow/STL/Core targets.

**Spec:** `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md`

## Global Constraints

- Preserve the public `cflow_scheduler`, Platform thread API, legacy include paths, and current target names.
- Platform depends only on Threads/OS facilities; Concurrency depends only on Platform; CFlow never depends on Core.
- CFlow deadlines and relative waits remain monotonic; virtual time remains deterministic.
- Test module boundaries by compiling and linking real consumers; do not add source-spelling tests.
- Use Release presets, run the smallest relevant test first, then expand to the full repository.
- Keep macOS and Android automation outside this patch: current repository CI has no macOS/Android execution job, and Android uses a host-local NDK/vcpkg preset contract. Record both as unverified host evidence rather than claiming coverage.

---

### Task 1: Complete Platform primitive behavior coverage

**Files:**
- Modify: `platform/tests/platform_thread_test.c`

**Interfaces:**
- Consumes: `turbo_thread_create`, `turbo_thread_join`, `turbo_thread_destroy`, `turbo_mutex_*`, `turbo_cond_*`, `turbo_rwlock_*`, `turbo_once`, `TURBO_THREAD_LOCAL`, `turbo_thread_yield`, `turbo_sleep_ms`, and `turbo_cpu_count` from `<turbo/thread.h>`.
- Produces: focused behavior evidence for the Platform ownership list in the design.

- [x] **Step 1: Add independent fixtures for synchronization behavior**

Add `stdatomic.h` and small file-local state objects. Use a mutex/condition handshake so detached-thread tests never retain pointers after the fixture is destroyed:

```c
typedef struct thread_gate {
  turbo_mutex_t mutex;
  turbo_cond_t changed;
  int entered;
  int released;
  int completed;
} thread_gate;

static atomic_int once_count;
static turbo_once_t once_guard = TURBO_ONCE_INIT;
static TURBO_THREAD_LOCAL int tls_value;

static void count_once(void) { atomic_fetch_add(&once_count, 1); }
```

The gate worker must set `entered`, wait in a `while (!released)` loop, then set `completed` and broadcast before returning. The test owns and destroys the mutex/condition only after observing `completed`.

- [x] **Step 2: Add one behavior per TinyTest case**

Add cases with these exact observable contracts:

```c
it("keeps detached work alive after destroying its handle") { /* handshake, destroy, release, completion */ }
it("serializes a write against a read lock") { /* writer updates under write lock; reader observes under read lock */ }
it("runs once initialization exactly once across threads") { /* four threads; once_count == 1 */ }
it("keeps thread-local values isolated") { /* two workers retain distinct values; main remains zero */ }
it("reports at least one CPU and accepts yield and sleep") { /* cpu_count > 0; yield; sleep_ms(0) */ }
```

Use typed `check_*` assertions where TinyTest provides them. Keep the existing create/join and monotonic timed-wait cases unchanged.

- [x] **Step 3: Build and run the focused test**

Run from the Windows VS developer environment:

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target platform_thread_test
ctest --preset win-release-user -R "^platform_thread_test$" --output-on-failure
```

Expected: `platform_thread_test` passes with no warning or timeout; the detached worker reaches completion before fixture cleanup.

- [x] **Step 4: Run the adjacent owner regressions**

```bat
cmake --build --preset win-release-user --target platform_clock_test thread_pool_test cflow_execution_test cflow_runtime_test
ctest --preset win-release-user -R "^(platform_|thread_pool_test$|cflow_execution_test$|cflow_runtime_test$)" --output-on-failure
```

Expected: all selected tests pass; Platform synchronization changes are not required.

- [x] **Step 5: Commit the behavior coverage**

```bash
git add platform/tests/platform_thread_test.c
git commit -m "test(platform): complete primitive behavior coverage"
```

---

### Task 2: Add reusable installed-package target consumers

**Files:**
- Create: `tests/install_consumer/CMakeLists.txt`
- Create: `tests/install_consumer/consumer.c`
- Create: `cmake/VerifyInstalledPackage.cmake`
- Modify: `CMakeLists.txt`

**Interfaces:**
- Consumes: installed `TurboUtilsConfig.cmake` and imported targets `TurboUtils::Platform`, `TurboUtils::Concurrency`, `TurboUtils::CMeta`, `TurboUtils::CFlow`, `TurboUtils::STL`, and `TurboUtils::Core`.
- Produces: build target `verify_installed_package`, which installs the current build into `${CMAKE_BINARY_DIR}/package-smoke/install`, configures an external consumer in `${CMAKE_BINARY_DIR}/package-smoke/consumer`, and builds all six consumers.

- [x] **Step 1: Verify the package-smoke target is absent**

```bat
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: build-tool failure stating that `verify_installed_package` is unknown. This establishes that installed-target verification is not currently part of the build graph.

- [x] **Step 2: Create the external consumer project**

Create `tests/install_consumer/CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.20)
project(TurboUtilsInstallConsumer LANGUAGES C)

find_package(TurboUtils CONFIG REQUIRED)

function(add_turboutils_consumer name target contract)
  add_executable(${name} consumer.c)
  target_compile_features(${name} PRIVATE c_std_11)
  target_compile_definitions(${name} PRIVATE ${contract}=1)
  target_link_libraries(${name} PRIVATE ${target})
endfunction()

add_turboutils_consumer(consume_platform TurboUtils::Platform CONSUME_PLATFORM)
add_turboutils_consumer(consume_concurrency TurboUtils::Concurrency CONSUME_CONCURRENCY)
add_turboutils_consumer(consume_cmeta TurboUtils::CMeta CONSUME_CMETA)
add_turboutils_consumer(consume_cflow TurboUtils::CFlow CONSUME_CFLOW)
add_turboutils_consumer(consume_stl TurboUtils::STL CONSUME_STL)
add_turboutils_consumer(consume_core TurboUtils::Core CONSUME_CORE)
```

Create `consumer.c` with mutually exclusive branches. Each branch must call a non-inline symbol when the target provides one:

```c
#if defined(CONSUME_PLATFORM)
#include <turbo/clock.h>
int main(void) { return turbo_hrtime() == 0u; }
#elif defined(CONSUME_CONCURRENCY)
#include <turbo/thread_pool.h>
int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return 0;
}
#elif defined(CONSUME_CMETA)
#include <cmeta/cmeta.h>
int main(void) { return cmeta_type_equal(&cmeta_type_int, &cmeta_type_int) ? 0 : 1; }
#elif defined(CONSUME_CFLOW)
#include <cflow/clock.h>
int main(void) {
  cflow_clock clock = {0};
  if (!cflow_clock_system_init(&clock)) return 1;
  cflow_clock_destroy(&clock);
  return 0;
}
#elif defined(CONSUME_STL)
#include <turbostl/typed.h>
int main(void) {
  Vec(int, values);
  if (vec_init(&values, 1u) != STL_OK) return 1;
  vec_destroy(&values);
  return 0;
}
#elif defined(CONSUME_CORE)
#include <platform.h>
#include <turbo_thread.h>
int main(void) {
  turbo_threadpool_t *pool = turbo_threadpool_create(1);
  if (!pool) return 1;
  turbo_threadpool_destroy(pool);
  return turbo_hrtime() == 0u;
}
#else
#error "one TurboUtils consumer contract is required"
#endif
```

- [x] **Step 3: Add the cross-platform verification driver**

`cmake/VerifyInstalledPackage.cmake` must fail fast unless `SOURCE_DIR`, `BUILD_DIR`, `CMAKE_COMMAND_PATH`, and `BUILD_CONFIG` are present. Derive both disposable directories below `${BUILD_DIR}/package-smoke`; remove only that derived directory, install the current build there, configure `tests/install_consumer` with the current generator and `CMAKE_PREFIX_PATH`, then build it. Check every `execute_process(RESULT_VARIABLE ...)` and issue `message(FATAL_ERROR ...)` with the failed phase.

Register the target after all module subdirectories in the root `CMakeLists.txt`:

```cmake
add_custom_target(verify_installed_package
  COMMAND ${CMAKE_COMMAND}
    -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
    -DBUILD_DIR=${CMAKE_BINARY_DIR}
    -DCMAKE_COMMAND_PATH=${CMAKE_COMMAND}
    -DBUILD_CONFIG=$<CONFIG>
    -DBUILD_GENERATOR=${CMAKE_GENERATOR}
    -P ${CMAKE_SOURCE_DIR}/cmake/VerifyInstalledPackage.cmake
  DEPENDS turbo_platform turbo_concurrency turbo_cmeta turbo_cflow turbo_stl turbo_utils
  VERBATIM)
set_target_properties(verify_installed_package PROPERTIES FOLDER "cmake")
```

- [x] **Step 4: Reconfigure and run installed-package verification**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: the install succeeds and all six external executables compile and link while naming only their own imported target.

- [x] **Step 5: Commit the package contract**

```bash
git add CMakeLists.txt cmake/VerifyInstalledPackage.cmake tests/install_consumer
git commit -m "test(cmake): verify installed foundation targets"
```

---

### Task 3: Put foundation boundaries on Linux/Windows CI

**Files:**
- Modify: `.github/workflows/cmeta.yml`

**Interfaces:**
- Consumes: existing Release configure/build/test presets and `verify_installed_package` from Task 2.
- Produces: pull-request and master evidence for Platform, Concurrency, CMeta, CFlow, STL, Core compatibility, and installed exports on Linux and Windows.

- [x] **Step 1: Expand workflow path ownership**

Add these paths to both `pull_request.paths` and `push.paths`:

```yaml
      - "platform/**"
      - "concurrency/**"
      - "utils/**"
      - "cmake/**"
      - "tests/install_consumer/**"
```

This makes changes to the owners under test trigger the workflow.

- [x] **Step 2: Expand focused CTest selection**

Use the same filter in both jobs:

```text
^(platform_|concurrency_|thread_pool_test$|disruptor_test$|cmeta_|cserde_|cflow_|turbostl_|test_execution_compat$)
```

Keep the full build step unchanged; the filter narrows only execution.

- [x] **Step 3: Build the installed-package verification target**

After CTest, add on Linux:

```yaml
      - name: Verify installed package targets
        run: cmake --build --preset build-default-linux --target verify_installed_package
```

In the existing Windows `cmd` block, add:

```bat
          if errorlevel 1 exit /b 1
          cmake --build --preset build-release-windows --target verify_installed_package
```

- [x] **Step 4: Review the workflow diff and commit**

```bash
git diff --check
git diff -- .github/workflows/cmeta.yml
git add .github/workflows/cmeta.yml
git commit -m "ci(cflow): verify execution foundation boundaries"
```

Expected: workflow syntax preserves the existing VS developer environment and uses repository presets rather than duplicating compiler flags.

---

### Task 4: Reconcile historical documents with implementation evidence

**Files:**
- Modify: `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md`
- Modify: `docs/superpowers/plans/2026-08-22-cflow-execution-foundation.md`

**Interfaces:**
- Consumes: current source tree, focused/full test output, installed-package target output, and Linux/Windows CI result URLs.
- Produces: an auditable status record that distinguishes implemented contracts, verified hosts, and remaining host/toolchain evidence.

- [x] **Step 1: Preserve the historical red/green checklist**

Add a note under the old plan title. Do not mechanically convert its checkboxes, because the original RED observations cannot be reconstructed from the current tree:

```markdown
> **Historical execution record:** The implementation described here landed through the Platform, Concurrency, Clock, Executor, TimerQueue, scheduler, and Core-compatibility commit series. The unchecked boxes preserve the original step-by-step script; current completion evidence is maintained in `2026-08-23-cflow-execution-foundation-completion.md` and the design status table.
```

- [x] **Step 2: Add the 13-item acceptance evidence table**

Change the design status from “Approved architecture” to “Implemented; completion evidence tracked below”. Add columns `Criterion`, `Status`, and `Evidence`. Criteria 1–11 cite the owning CMake/source/header files; criterion 12 cites Platform/Concurrency/CFlow/Core compatibility tests; criterion 13 cites `tests/install_consumer` and `verify_installed_package`.

Use only these status values:

- `Implemented + locally verified` after the Windows Release commands pass.
- `Implemented + Linux/Windows CI verified` only after both jobs pass at the final commit.
- `Implementation present; macOS/Android host evidence absent` for the cross-host evidence row until those jobs exist and pass.

- [x] **Step 3: Record non-goals and residual work explicitly**

Add a short “Residual work outside this completion patch” section:

- Event/Mailbox/Machine/Actor/reactor/minicoro remain design non-goals, not missing foundation features.
- macOS Release and Android cross-build automation require their own host/toolchain plan before being claimed.
- ManualExecutor and TimerQueue capacity limits are a later resource-policy design because adding limits changes admission/error behavior; this patch does not silently impose a new bound.

- [x] **Step 4: Run final local verification**

```bat
cmake --fresh --preset win-release-user
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset win-release-user --target verify_installed_package
git diff --check
```

Expected: configure/build/package target exit zero; CTest reports zero failed tests; `git diff --check` reports no whitespace errors.

- [x] **Step 5: Commit the reconciled status**

```bash
git add docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md docs/superpowers/plans/2026-08-22-cflow-execution-foundation.md
git commit -m "docs(cflow): reconcile execution foundation status"
```

## Self-Review Result

- Spec criteria 1–11 already have implementation evidence and receive regression/documentation coverage here.
- Criterion 12 gains missing Platform primitive coverage and Linux/Windows owner-test execution.
- Criterion 13 gains a reusable external installed-package build for all six exported targets.
- macOS/Android host evidence is deliberately reported as absent rather than represented as complete; its NDK/vcpkg/runner contract is an independent CI/toolchain project.
- Public APIs, dependency direction, ownership, scheduler semantics, data formats, and runtime admission behavior remain unchanged.

## Execution Evidence

- Windows Release fresh configure and full build completed on 2026-08-23.
- Full CTest result: 101/101 passed.
- Focused Platform/Concurrency/CFlow regression result: 6/6 passed.
- Installed-package result: all six external consumers compiled and linked.
- Linux CI is configured by this plan but remains unverified until the branch workflow completes.
