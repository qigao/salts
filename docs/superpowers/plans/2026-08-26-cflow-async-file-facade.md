# CFlow Bounded Async File Facade Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an owning, bounded CFlow regular-file facade with synchronous control-plane open and native asynchronous offset I/O.

**Architecture:** `cflow_io_file` is a thin facade over the existing native file Actor strategy. It owns the compatible native handle, backend, manual Executor, Actor, and fixed operation slots; callers retain buffers through completion callback return. Explicit backend selection and operation capability checks preserve fail-fast behavior without readiness or worker fallback.

**Tech Stack:** C11, CFlow I/O Actor, Windows IOCP, Linux io_uring, Salts threads, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-26-cflow-async-file-facade-design.md`

## Global Constraints

- `salts_fs` remains synchronous and independent from CFlow.
- Path open is synchronous control-plane work; read/write/flush are native asynchronous data-plane work.
- Backend selection is explicit and unsupported behavior never falls back.
- `request_capacity` is the exact operation-slot bound; submission performs no allocation.
- Accepted buffers remain caller-owned and borrowed through completion callback return.
- Submission/cancellation are MPSC; drive, callback, close, and destroy have exactly one owner thread.
- Destroy never closes a handle before all terminal callbacks are acknowledged.
- Existing native socket, pipe, and file APIs remain source- and ABI-compatible.

---

### Task 1: Public Contract and Compile Coverage

**Files:**
- Create: `cflow/include/cflow/io_file.h`
- Modify: `cflow/include/cflow/cflow.h`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`
- Create: `cflow/tests/cflow_io_file_test.c`
- Modify: `cflow/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cflow_io_native_backend_kind`, `cflow_io_native_file_operation_kind`, I/O Actor IDs and completions.
- Produces: the exact public types and function signatures in the design specification.

- [x] **Step 1: Add the failing public-contract tests**

Add `cflow_io_file_test.c` with a TinyTest case that zero-initializes
`cflow_io_file`, fills every `cflow_io_file_config` and stats field, references
each enum, and uses C11 `_Generic` expressions to verify every public function
signature. Extend the C++ header test with the same type names. Register the new
test target:

```cmake
cmake_add_test(cflow_io_file_test
  SOURCES cflow_io_file_test.c
  LIBS Salts::CFlow Salts::Platform Salts::TinyTest
  FOLDER "cflow/tests")
```

- [x] **Step 2: Run RED**

```powershell
cmd.exe /D /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --fresh --preset win-release-user && cmake --build --preset win-release-user --target cflow_io_file_test cflow_header_cpp_test'
```

Expected: compilation fails because `cflow/io_file.h` and its public names do
not exist.

- [x] **Step 3: Add the minimal public header**

Declare the opaque handle, flags, submit result, completion callback, config,
stats, and lifecycle functions exactly as specified. Include the header from
`cflow/cflow.h`; do not add implementation or platform headers yet.

- [x] **Step 4: Run GREEN for header compilation**

Build the two targets again. Expected: both public-header contract targets are
green because the C test uses declarations only and does not odr-use a facade
function before Task 2 supplies its implementation.

### Task 2: Open, Capability, and Owning Lifecycle

**Files:**
- Create: `cflow/src/io_file.c`
- Modify: `cflow/CMakeLists.txt`
- Modify: `cflow/tests/cflow_io_file_test.c`

**Interfaces:**
- Consumes: native backend init/support/shutdown/destroy, manual Executor, I/O Actor file strategy.
- Produces: `cflow_io_file_open`, capability query, close, quiescence, stats, and destroy.

- [x] **Step 1: Write lifecycle behavior tests**

Add cases proving:

```c
check_equal(cflow_io_file_open(NULL, path, &config), SALTS_EINVAL);
check_equal(cflow_io_file_open(&file, path, &unsupported), SALTS_ENOTSUP);
check_false(file_exists_after_unsupported_open);
check_equal(cflow_io_file_open(&file, path, &config), SALTS_OK);
check_equal(cflow_io_file_destroy(&file), SALTS_EBUSY);
check_equal(cflow_io_file_close(&file), SALTS_OK);
check_true(cflow_io_file_is_quiescent(&file));
check_equal(cflow_io_file_destroy(&file), SALTS_OK);
check_null(file.impl);
```

Cover unknown flags, no access bit, CREATE/TRUNCATE without WRITE, invalid mode,
zero capacities, null callback, nonzero destination, and missing path.

- [x] **Step 2: Run RED**

Build and run `cflow_io_file_test`; expect unresolved facade symbols.

- [x] **Step 3: Implement resource construction and cleanup**

Allocate one implementation and exactly `request_capacity` slots. Initialize
the native backend, manual Executor, and Actor before pathname mutation. Open
Windows files with `FILE_FLAG_OVERLAPPED`; open POSIX files with `O_CLOEXEC`.
Publish `file->impl` only after complete success. Track partial initialization
with explicit booleans and one cleanup path.

- [x] **Step 4: Implement close and destroy ordering**

Close Actor admission without blocking. Reject destroy until Actor quiescence.
After quiescence: stop the native backend worker, destroy Actor, close the
native handle, forget the closed identity, destroy backend, shut down/destroy
Executor, destroy the slot mutex, free storage, and zero the public handle.
Preserve the first useful cleanup error while releasing remaining owned
resources.

- [x] **Step 5: Run GREEN**

Run `cflow_io_file_test` and `cflow_io_native_test`.

### Task 3: Bounded Submission, Drive, Completion, and Automatic Ack

**Files:**
- Modify: `cflow/src/io_file.c`
- Modify: `cflow/tests/cflow_io_file_test.c`

**Interfaces:**
- Consumes: native file operation validation, Actor try-submit/cancel/run-one/acknowledge.
- Produces: read-at, write-at, flush, cancel, and single-driver `run_ready`.

- [x] **Step 1: Write real-file operation tests**

Use a real temporary file and literals to verify:

- write `"payload"` at offset 7 and read exactly those 7 bytes back;
- the completion callback observes the correct operation kind, lease, byte
  count, and terminal status;
- a read-only facade rejects write, and a write-only facade rejects read;
- null/zero buffers and `INT64_MAX` range overflow are invalid;
- IOCP flush is unsupported before Actor admission;
- capacity 1 rejects a second live request as FULL and accepts it after the
  first callback is automatically acknowledged;
- close before driving a queued request yields one terminal cancellation and a
  reusable zero-state object after destroy.

- [x] **Step 2: Run RED**

Run the focused test; expect submission functions to report invalid/default
results and no callback.

- [x] **Step 3: Implement fixed-slot admission**

Claim one free slot under a Salts mutex, fill its native operation, and submit
it as a move-only Actor operation. Map every Actor submit status one-to-one.
On rejection, return the slot immediately. On accepted submit, only the Actor's
release callback may return the slot.

- [x] **Step 4: Implement completion and drive**

The facade callback invokes the user callback, then marks that slot delivered.
`run_ready` performs at most `max_steps` total actions by prioritizing one
automatic acknowledgement, one Actor transition, or one manual Executor task
per iteration. It returns `SALTS_EBUSY` for a concurrent/reentrant driver and
reports the exact number of progressed actions.

- [x] **Step 5: Run GREEN and adjacent regression**

```powershell
cmd.exe /D /S /C 'call "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 >nul && cmake --build --preset win-release-user --target cflow_io_file_test cflow_io_native_test cflow_io_actor_test test_salts_fs && ctest --preset win-release-user -R "^(cflow_io_file_test|cflow_io_native_test|cflow_io_actor_test|test_salts_fs)$" --output-on-failure'
```

### Task 4: Documentation and Final Verification

**Files:**
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/specs/2026-08-26-cflow-async-file-facade-design.md`
- Modify: `docs/superpowers/plans/2026-08-26-cflow-async-file-facade.md`

**Interfaces:**
- Consumes: verified facade behavior.
- Produces: migration guidance and reproducible validation evidence.

- [x] **Step 1: Document the layer boundary and runnable usage**

Document that `salts_fs` is synchronous, facade open is synchronous, file data
operations are native asynchronous, and readiness/worker fallback is absent.
Include a complete open → submit → drive → close → drain → destroy example.

- [x] **Step 2: Run formatting and symbol checks**

Run `git diff --check`; search for removed `salts_fs_*_async` symbols and verify
none were restored. Confirm the CFlow facade appears in the aggregate header.

- [x] **Step 3: Run final Windows Release verification**

Run the Task 3 build/CTest command plus the C++ header test. Record test counts
and any platform coverage not executable locally.

- [x] **Step 4: Review compatibility and lifecycle**

Inspect the diff for public ABI changes, fixed-capacity arithmetic, rejection
ownership, callback borrow boundaries, close ordering, and cleanup on every
partial-init failure. Update issue #109 acceptance items only from observed
evidence.
