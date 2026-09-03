# CFlow Minicoro Resumable Adapter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an optional minicoro-backed implementation of the public `cflow_resumable` contract without changing core CFlow semantics or making CFlow headers depend on minicoro.

**Architecture:** A separate `Salts::CFlowMinicoro` static adapter target owns an opaque coroutine frame and translates explicit coroutine suspension calls into `VALUE`, `VALUE_AND_DONE`, `WAIT`, `DONE`, and `ERROR`. Its public header includes only `cflow/runtime.h`; its implementation privately compiles a static-symbol copy of the vendored minicoro header and selects the Fiber/ucontext fallback on the supported desktop hosts so fixed assembly labels do not escape the archive. Neither `Salts::CFlow` nor installed consumers need a minicoro target. The adapter is single-owner: `resume`, `cancel`, and `destroy` are serialized by the caller, while a returned CFlow waitable may wake through the existing scheduler contract without directly resuming the coroutine frame.

**Tech Stack:** C11, CFlow Resumable/Waitable APIs, vendored minicoro, CMake Presets, TinyTest, AddressSanitizer CI.

**Spec:** https://github.com/qigao/salts/issues/67

## Global Constraints

- `CFLOW_ENABLE_MINICORO` defaults to `OFF`.
- `Salts::CFlow`, `<cflow/cflow.h>`, and all existing CFlow headers retain their current source and link contract when the option is off.
- The optional public header is `<cflow/minicoro.h>` and exposes no `mco_*` or `coro_*` type.
- The adapter target is `Salts::CFlowMinicoro`; no private scheduler or demand counter is introduced.
- The coroutine frame and adapter state are owned by the returned `cflow_resumable` and are released only by its `destroy` operation.
- `name`, `output_type`, entry `user`, error text, and waitable state are borrowed for the documented lifetime; emitted values are copied into `out_value` before `resume` returns.
- A value pointer passed at a suspension point must remain valid only until that suspension produces its `cflow_step`; it is never retained across the next resume.
- The adapter supports only output descriptors with `TRIVIAL_COPY` and `TRIVIAL_DESTROY`, matching current retained-byte Resumable composition.
- `cancel` cancels an active waitable, marks the frame terminal, and prevents every later `resume` from entering minicoro.
- `destroy` performs cancel-before-frame-destruction-before-state-release. The caller must not concurrently call `resume`, `cancel`, or `destroy`.
- A stale CFlow waker can notify its owner but cannot call `mco_resume`; resumption remains an explicit owner operation, which returns `DONE` after cancellation.
- Do not commit or push until the user explicitly requests it.

---

### Task 1: Public Adapter Contract and Step Mapping

**Files:**
- Create: `cflow/minicoro/include/cflow/minicoro.h`
- Create: `cflow/minicoro/src/minicoro.c`
- Create: `cflow/minicoro/tests/cflow_minicoro_test.c`
- Create: `cflow/minicoro/CMakeLists.txt`
- Modify: `CMakeOptions.cmake`
- Modify: `cflow/CMakeLists.txt`

**Interfaces:**
- Consumes: `cflow_resumable`, `cflow_step`, `cflow_waitable`, `cflow_resume_ctx`, `cmeta_type_require_traits`, and private `vendor/minicoro/minicoro.h`.
- Produces:

```c
typedef struct cflow_minicoro cflow_minicoro;
typedef void (*cflow_minicoro_entry_fn)(cflow_minicoro *coroutine,
                                        void *user);
typedef void *(*cflow_minicoro_alloc_fn)(size_t size, void *allocator_data);
typedef void (*cflow_minicoro_dealloc_fn)(void *pointer,
                                          size_t size,
                                          void *allocator_data);

typedef struct cflow_minicoro_config {
    const char *name;
    const cmeta_type_desc *output_type;
    cflow_minicoro_entry_fn entry;
    void *user;
    size_t stack_size;
    cflow_minicoro_alloc_fn alloc;
    cflow_minicoro_dealloc_fn dealloc;
    void *allocator_data;
} cflow_minicoro_config;

bool cflow_resumable_from_minicoro(cflow_resumable *out,
                                    const cflow_minicoro_config *config);
bool cflow_minicoro_yield_value(cflow_minicoro *coroutine,
                                const void *value);
bool cflow_minicoro_return_value(cflow_minicoro *coroutine,
                                 const void *value);
bool cflow_minicoro_wait(cflow_minicoro *coroutine,
                         cflow_waitable waitable);
bool cflow_minicoro_fail(cflow_minicoro *coroutine,
                         const char *error);
cflow_resume_ctx *cflow_minicoro_resume_context(
    cflow_minicoro *coroutine);
```

- [x] **Step 1: Write the missing-feature tests**

Add TinyTest cases whose production mutations are explicit:

- removing `yield_value` loses the first value;
- mapping natural entry return to anything except `DONE` breaks immediate completion;
- removing `return_value` loses `VALUE_AND_DONE`;
- mapping `fail` to any non-error step loses the borrowed error;
- retaining the callback value pointer until a later resume breaks a stack-local value trace;
- accepting a managed or invalid output descriptor violates current byte-storage admission.

The primary trace fixture drives both a hand-written native Resumable and the minicoro adapter and compares literal step kinds and integer values: `VALUE(3)`, `VALUE_AND_DONE(5)`.

- [x] **Step 2: Run the test to verify RED**

Run from the Visual Studio developer environment:

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=ON
cmake --build --preset win-release-user --target cflow_minicoro_test
```

Expected: compilation fails because `<cflow/minicoro.h>` and `cflow_resumable_from_minicoro` do not exist.

- [x] **Step 3: Implement the minimum step adapter**

Create an opaque adapter state containing the minicoro frame, borrowed config fields, current `cflow_resume_ctx`, pending signal, pending value pointer, pending waitable, error pointer, and terminal/cancel flags. Define `MCO_API static` and `MINICORO_IMPL` only in `minicoro.c` so all backend symbols are translation-unit private.

Map behavior exactly:

```text
first/subsequent ops->resume -> mco_resume until one adapter signal or entry return
yield_value(value)           -> VALUE, copy value before ops->resume returns
return_value(value)          -> VALUE_AND_DONE, never re-enter frame
wait(waitable)               -> WAIT with the same CFlow waitable
entry return                 -> DONE
fail(error)                  -> ERROR with borrowed error text
invalid adapter state        -> ERROR
resume after terminal/cancel -> DONE without mco_resume
```

Validate `out`, config, entry, descriptor size/alignment/traits, allocator pairing, and stack-size arithmetic before allocation. On construction failure, leave `*out` unchanged and release every successful partial allocation.

- [x] **Step 4: Run the focused test to verify GREEN**

```text
cmake --build --preset win-release-user --target cflow_minicoro_test
ctest --preset win-release-user -R "^cflow_minicoro_test$" --output-on-failure
```

Expected: one test executable passes all step-mapping and admission cases.

- [x] **Step 5: Inspect the uncommitted checkpoint**

Run `git diff --check` and `git status --short`; keep changes uncommitted pending user instruction.

### Task 2: WAIT, Cancellation, Destruction, and Allocation Failure

**Files:**
- Modify: `cflow/minicoro/include/cflow/minicoro.h`
- Modify: `cflow/minicoro/src/minicoro.c`
- Modify: `cflow/minicoro/tests/cflow_minicoro_test.c`

**Interfaces:**
- Consumes: Task 1 API and existing `cflow_waitable_arm` / `cflow_waitable_cancel`.
- Produces: the documented frame state machine and lifecycle guarantees required by issue #67.

- [x] **Step 1: Add lifecycle tests before implementation changes**

Add real fake-waitable and counting-allocator fixtures, with separate cases for:

- WAIT arms through the caller-provided waitable, wake notifies the probe, and the next explicit resume returns the terminal value;
- cancel while suspended calls the underlying waitable cancel exactly once and later resume returns `DONE` without executing code after the coroutine suspension;
- a saved stale waker invoked after cancel may notify the probe but cannot enter the frame or change the trace;
- a wake callback may cancel and destroy the suspended Resumable without executing code after WAIT;
- allocator failure on adapter-state allocation and minicoro-frame allocation leaves `out` zero and balances deallocation;
- 128 create/destroy cycles balance state and frame allocations;
- the callback sees exactly the caller's `cflow_resume_ctx` during execution and no internal scheduler is substituted.

- [x] **Step 2: Run the focused test to verify RED**

```text
cmake --build --preset win-release-user --target cflow_minicoro_test
ctest --preset win-release-user -R "^cflow_minicoro_test$" --output-on-failure
```

Expected: the newly added lifecycle assertion fails before the corresponding lifecycle handling exists.

- [x] **Step 3: Implement lifecycle handling**

Use this state machine:

```text
CREATED -> RUNNING -> VALUE_SUSPENDED -> RUNNING
                   -> WAIT_SUSPENDED  -> RUNNING
                   -> VALUE_DONE_TERMINAL
                   -> ERROR_TERMINAL
                   -> DONE_TERMINAL
any non-destroyed state --cancel--> CANCELLED_TERMINAL
any quiescent state      --destroy--> DESTROYED
```

`cancel` copies and clears the pending waitable before calling its cancel operation, then makes the frame permanently non-resumable. `destroy` calls the same cancellation path, destroys the suspended/dead minicoro frame, and finally uses the paired allocator to release state. No waker callback stores or receives the frame pointer.

- [x] **Step 4: Run focused and adjacent tests to verify GREEN**

```text
cmake --build --preset win-release-user --target cflow_minicoro_test cflow_runtime_test test_salts_coro
ctest --preset win-release-user -R "^(cflow_minicoro_test|cflow_runtime_test|test_salts_coro)$" --output-on-failure
```

Expected: all three test executables pass.

- [x] **Step 5: Inspect the uncommitted checkpoint**

Run `git diff --check` and review ownership comments against the state machine; keep changes uncommitted.

### Task 3: Optional Build and Installed-Package Boundary

**Files:**
- Modify: `cflow/minicoro/CMakeLists.txt`
- Modify: `cflow/CMakeLists.txt`
- Modify: `CMakeOptions.cmake`
- Modify: `CMakeLists.txt`
- Modify: `tests/install_consumer/CMakeLists.txt`
- Modify: `tests/install_consumer/consumer.c`

**Interfaces:**
- Consumes: Task 1 adapter library/header.
- Produces: optional exported target `Salts::CFlowMinicoro` and an installed consumer check that appears only when that target is installed.

- [x] **Step 1: Add package-consumer behavior before export wiring**

When `TARGET Salts::CFlowMinicoro` exists, add a consumer that includes `<cflow/minicoro.h>`, constructs an immediate-completion adapter, resumes it to literal `CFLOW_STEP_DONE`, and destroys it. The same consumer project must configure unchanged when the target is absent.

- [x] **Step 2: Verify the package consumer fails with feature ON before export wiring**

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=ON
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: installed consumer configure or link fails because `Salts::CFlowMinicoro` is not yet exported/installed.

- [x] **Step 3: Add optional install/export wiring**

Install the adapter archive and its header only when `CFLOW_ENABLE_MINICORO=ON`. Export it through `SaltsTargets` with public dependency `Salts::CFlow`. Keep the vendored include directory private and do not export `Salts::MinicoroVendor` or a raw minicoro include path. Add the optional adapter target to `verify_installed_package` dependencies only when enabled.

- [x] **Step 4: Verify feature ON package GREEN**

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=ON
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: install, consumer configure, compile, and link all succeed.

- [x] **Step 5: Verify feature OFF compatibility**

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=OFF
cmake --build --preset win-release-user --target salts_cflow cflow_header_cpp_test verify_installed_package
ctest --preset win-release-user -R "^(cflow_header_cpp_test|cflow_runtime_test)$" --output-on-failure
```

Expected: core CFlow and its installed consumer build without the adapter target or header dependency.

### Task 4: Cross-Host and Sanitizer Verification

**Files:**
- Modify: `.github/workflows/cmeta.yml`
- Modify: `docs/superpowers/plans/2026-08-24-cflow-minicoro-resumable.md`

**Interfaces:**
- Consumes: optional target and focused test from Tasks 1-3.
- Produces: CI evidence for Linux, Windows, macOS feature-on/off builds and Linux ASan execution.

- [x] **Step 1: Add CI feature-on checks without replacing default-off checks**

Keep each host's existing default configure/build/test as the feature-off proof. Add a reconfigure with `-DCFLOW_ENABLE_MINICORO=ON`, build `cflow_minicoro_test`, run its exact CTest filter, and verify the installed package target on Linux, Windows, and macOS. Add a Linux `linux-dev-user` configure/build/test of `cflow_minicoro_test` with `ENABLE_SANITIZER_ADDRESS=ON`.

- [x] **Step 2: Run fresh local feature-on verification**

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=ON
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: full Windows Release build and every registered test pass; installed consumer passes.

- [x] **Step 3: Run fresh local feature-off verification**

```text
cmake --preset win-release-user -DCFLOW_ENABLE_MINICORO=OFF
cmake --build --preset win-release-user
ctest --preset win-release-user --output-on-failure
cmake --build --preset win-release-user --target verify_installed_package
```

Expected: full Windows Release build and every core test pass without the adapter target.

- [x] **Step 4: Self-review and report evidence gaps**

Run `git diff --check`, inspect `git diff --stat` and `git status --short`, and compare every issue verification bullet with a named test or CI step. Report local Windows evidence as fact; report Linux/macOS/ASan only as configured CI coverage until those remote jobs run.

Local MSVC AddressSanitizer note: running a single WAIT lifecycle case passes, but running the complete adapter executable reports a false stack access after several Windows Fiber lifecycles. The vendored backend selects Windows Fibers for MSVC ASan while its explicit sanitizer fiber-switch hooks are limited to non-MSVC Windows. Linux ASan remains the required sanitizer execution path in CI; no adapter behavior is weakened to suppress the Windows toolchain report.
