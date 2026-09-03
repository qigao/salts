# CFlow Windowed I/O Source Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an opt-in bounded Windowed I/O Source that uses Runtime demand to keep multiple independent Actor requests in flight and emits authoritative completions without changing the existing capacity-one Source behavior.

**Architecture:** Runtime writes its current downstream-value demand into `cflow_resume_ctx` before every resume. The existing I/O Source implementation is generalized to a fixed array of at most 64 entries, while the old constructor selects capacity one and a new constructor selects an explicit window. Actor request state remains authoritative; adapter entries only reserve demand and retain encoded results until Runtime consumes them.

**Tech Stack:** C11, CMeta type descriptors, CFlow Runtime/Source/Waitable, CFlow I/O Actor and manual Executor, Salts mutex/condition primitives, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-27-cflow-windowed-io-source-design.md`

## Global Constraints

- Preserve `cflow_source_from_io_actor()` capacity-one behavior, callback ownership, error mapping, completion order and shutdown behavior.
- Runtime remains the only downstream-demand fact source; the Adapter never maintains an independent demand counter.
- Window capacity is in `[1, 64]` and bounds Actor requests, commands, Executor jobs, adapter entries and result slots.
- No runtime allocation, unbounded growth, retry, polling thread, silent drop, overwrite or fallback is permitted.
- Every accepted operation reaches exactly one Actor completion/delivery/acknowledge/release terminal path.
- Result emission order is authoritative completion delivery order.
- User/backend callbacks, Source wakers and drive callbacks run outside the Adapter gate.
- Keep production changes only if capacity 8 or 32 improves the synthetic control workload by at least 30% over capacity 1 with zero errors/rejections/stale completions.
- Configure, build and test through `win-release-user` or `win-dev-user` under `VsDevCmd.bat`.

---

### Task 1: Expose Runtime downstream demand to resumables

**Files:**
- Modify: `cflow/include/cflow/runtime.h`
- Modify: `cflow/src/runtime.c`
- Modify: `cflow/tests/cflow_runtime_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: `run_impl.demand`, `demand_get()`, existing Source/continuation resume calls.
- Produces: `cflow_resume_ctx.downstream_demand`, an exact pre-resume snapshot or zero for direct callers without a Runtime.

- [x] **Step 1: Write a demand-snapshot RED test**

  Add a real Source probe whose resume records the context and returns literal values:

  ```c
  typedef struct runtime_demand_probe {
      size_t seen[4];
      size_t calls;
  } runtime_demand_probe;

  static cflow_step runtime_demand_probe_resume(
      void *self, cflow_resume_ctx *ctx, void *out) {
      runtime_demand_probe *probe = self;
      probe->seen[probe->calls++] = ctx->downstream_demand;
      *(int *)out = (int)probe->calls;
      return (cflow_step){
          probe->calls == 4u ? CFLOW_STEP_VALUE_AND_DONE
                             : CFLOW_STEP_VALUE,
          {0}, NULL};
  }
  ```

  Open it through a real identity Run, call `cflow_run_request(&run, 4u)`, drain the
  deterministic scheduler, and assert literal snapshots `[4, 3, 2, 1]`. Add a filter-drop
  case where the first Source item is rejected by the filter and the second resume still sees
  the same demand. The production mutation caught is failing to refresh demand before each
  resume or decrementing it on Source input instead of downstream output.

- [x] **Step 2: Run RED**

  Run:

  ```text
  cmake --build --preset win-release-user --target cflow_runtime_test
  build/Msvc-Release/bin/cflow_runtime_test.exe --filter "passes downstream demand to source resume"
  ```

  Expected: compilation fails because `cflow_resume_ctx` has no `downstream_demand` member.

- [x] **Step 3: Add the field and refresh helper**

  Extend the public context without reordering scheduler:

  ```c
  typedef struct cflow_resume_ctx {
      cflow_scheduler *scheduler;
      /* Exact outstanding downstream-value demand before this resume.
       * Zero means a direct caller did not provide a Runtime snapshot. */
      size_t downstream_demand;
  } cflow_resume_ctx;
  ```

  Add and use one helper before Source and continuation resume calls:

  ```c
  static cflow_resume_ctx *run_resume_ctx(run_impl *run) {
      run->resume_ctx.downstream_demand = demand_get(run);
      return &run->resume_ctx;
  }
  ```

  Replace `&r->resume_ctx` only at the two Runtime-owned resume call sites with
  `run_resume_ctx(r)`. Direct tests and examples that initialize only scheduler retain a zero
  hint.

- [x] **Step 4: Run GREEN and public-header coverage**

  Run `cflow_runtime_test` and `cflow_header_cpp_test`. Add a C++17 assertion that aggregate
  zero state has null scheduler and zero demand, while `{&scheduler}` still initializes the
  original first field.

- [x] **Step 5: Commit the independently testable Runtime contract**

  ```text
  feat(cflow): expose downstream demand to resumables
  ```

### Task 2: Lock the opt-in Windowed Source public contract

**Files:**
- Modify: `cflow/include/cflow/io_source.h`
- Modify: `cflow/tests/cflow_io_source_test.c`
- Modify: `cflow/tests/cflow_header_cpp_test.cpp`

**Interfaces:**
- Consumes: existing `cflow_io_source_config`, owner and stats APIs.
- Produces: `CFLOW_IO_SOURCE_MAX_WINDOW`, `cflow_source_from_io_actor_windowed()`, `cflow_io_source_window_stats`, and `cflow_io_source_owner_get_window_stats()`.

- [x] **Step 1: Write invalid-capacity and compatibility RED tests**

  Add tests using the existing real fake backend fixture:

  ```c
  check_equal(cflow_source_from_io_actor_windowed(
      &source, &owner, &config, 0u), SALTS_EINVAL);
  check_false(cflow_source_valid(&source));
  check_null(owner.impl);

  check_equal(cflow_source_from_io_actor_windowed(
      &source, &owner, &config,
      CFLOW_IO_SOURCE_MAX_WINDOW + 1u), SALTS_EINVAL);
  check_false(cflow_source_valid(&source));
  check_null(owner.impl);
  ```

  Construct capacity 1 through both constructors in separate fixtures, request one value, and
  assert identical prepare/submit/encode/release counts, emitted value, Actor stats and close
  result. This catches a compatibility wrapper that selects a different state machine.

- [x] **Step 2: Run RED**

  Build `cflow_io_source_test`. Expected: compilation fails on the missing constant, type and
  functions, while all old API names continue compiling.

- [x] **Step 3: Add the complete header declarations**

  Declare exactly:

  ```c
  enum { CFLOW_IO_SOURCE_MAX_WINDOW = 64u };

  typedef struct cflow_io_source_window_stats {
      size_t capacity;
      size_t occupied;
      size_t demand_reserved;
      size_t results_ready;
      size_t peak_occupied;
  } cflow_io_source_window_stats;

  int cflow_source_from_io_actor_windowed(
      cflow_source *out,
      cflow_io_source_owner *owner,
      const cflow_io_source_config *config,
      size_t window_capacity);

  bool cflow_io_source_owner_get_window_stats(
      const cflow_io_source_owner *owner,
      cflow_io_source_window_stats *out);
  ```

  Copy the spec's ownership, demand, completion-order, capacity, error, thread and close
  contracts into the header comments. Do not add fields to `cflow_io_source_config` or change
  existing function signatures.

- [x] **Step 4: Advance RED to missing implementation**

  Rebuild the focused target. Expected: header and C++ aggregate checks compile, then link fails
  on the two new exported functions.

- [x] **Step 5: Commit the public contract and RED tests**

  ```text
  test(cflow): define windowed IO source contract
  ```

### Task 3: Generalize Adapter storage and bounded admission

**Files:**
- Modify: `cflow/src/io_source_internal.h`
- Modify: `cflow/src/io_source.c`
- Modify: `cflow/tests/cflow_io_source_test.c`

**Interfaces:**
- Consumes: Task 2 API and existing Actor/manual Executor.
- Produces: fixed window construction, demand-bounded multi-prepare, O(1) lease-to-entry mapping, and window stats.

- [x] **Step 1: Write the window-fill RED test**

  Create a capacity-4 owner, request 6 downstream values, and run only the Runtime scheduler
  until it waits. Assert four prepare calls, no fifth call, four accepted requests, Actor request
  and command capacity 4, window `occupied == demand_reserved == 4`, `results_ready == 0`, and
  backend maximum active count 4 after the owner driver submits them. No completion is injected
  in this test. The production mutation caught is filling beyond demand/capacity or retaining the
  old one-request gate.

- [x] **Step 2: Run RED and confirm capacity-one behavior remains GREEN**

  Run the new filter and then the existing test that requests two values. The new filter must
  fail at one prepare; the old sequential-demand test must still pass.

- [x] **Step 3: Replace the single result fields with fixed entries**

  Define the internal entry and bounded state:

  ```c
  typedef struct cflow_io_source_entry {
      cflow_value_slot result;
      cflow_io_request_id request_id;
      cflow_io_lease_id lease_id;
      uint64_t delivery_sequence;
      cflow_read_status result_status;
      const char *result_error;
      bool occupied;
      bool submission_in_progress;
      bool result_encoding;
      bool result_ready;
      bool completion_delivered;
      bool acknowledged;
      bool demand_reserved;
  } cflow_io_source_entry;
  ```

  Replace the state-wide single request/result fields with:

  ```c
  cflow_io_source_entry *entries;
  size_t window_capacity;
  size_t occupied;
  size_t demand_reserved;
  size_t results_ready;
  size_t peak_occupied;
  uint64_t next_delivery_sequence;
  bool prepare_done;
  bool terminal_delivery_seen;
  ```

  Allocate the entry array with `calloc` only after checking
  `window_capacity <= SIZE_MAX / sizeof(*entries)`. Initialize every value slot transactionally;
  on failure destroy initialized slots in reverse ownership order. Initialize Actor request/
  command and manual Executor capacity from the same window value. The old constructor calls:

  ```c
  return cflow_source_from_io_actor_windowed(
      out, owner, config, 1u);
  ```

- [x] **Step 4: Implement demand-bounded preparation**

  At the start of a no-result resume compute:

  ```c
  const size_t hinted = ctx != NULL && ctx->downstream_demand != 0u
      ? ctx->downstream_demand : 1u;
  const size_t target = hinted < state->window_capacity
      ? hinted : state->window_capacity;
  ```

  While `demand_reserved < target`, reserve a FREE entry under the gate, assign
  `lease_id = index + 1`, increment occupied/peak, then call prepare outside the gate. Before
  Actor submit set the entry's demand reservation under the gate so synchronous completion can
  observe it. ACCEPTED stores/verifies request id; every rejection releases the operation and
  publishes the existing stable Source error. `prepare DONE` sets `prepare_done`; `prepare ERROR`
  sets terminal error and closes Actor admission.

- [x] **Step 5: Implement stats and run GREEN**

  `get_window_stats()` copies the five counters under the Adapter gate. Existing stats derives
  its booleans from aggregate entry counts and still merges Actor active requests conservatively.
  Run the capacity 0/1/4/64/65 tests, full `cflow_io_source_test`, and `cflow_io_actor_test`.

- [x] **Step 6: Commit bounded window admission**

  ```text
  feat(cflow): add demand-bounded IO source window
  ```

### Task 4: Deliver, order, terminate and drain multiple completions

**Files:**
- Modify: `cflow/src/io_source.c`
- Modify: `cflow/tests/cflow_io_source_test.c`

**Interfaces:**
- Consumes: Task 3 entries and window counters.
- Produces: completion-order emission, independent emit/ack settlement, terminal cutoff, lossless wake and close drain.

- [x] **Step 1: Write completion-order and lease-reuse RED tests**

  Extend the fake backend to retain the real `(actor, request_id, lease_id)` tuple for four
  requests. Complete tuple indices 2, 0, 3, 1 with literal encoder values 30, 10, 40, 20; drain
  owner and scheduler; assert sink values `[30, 10, 40, 20]`, four unique releases, Actor
  acknowledged 4, window counters zero, and no stale/rejected completion. Then request four more
  and assert leases are reusable only after the prior result emit and acknowledge both settled.

- [x] **Step 2: Run RED**

  Expected: the current single `backend_request_id` fixture cannot retain or deliver the four
  requests and the Source emits at most one value.

- [x] **Step 3: Map completion by lease and publish delivery sequence**

  In completion callback validate:

  ```c
  index = (size_t)(lease_id - 1u);
  entry = index < state->window_capacity ? &state->entries[index] : NULL;
  valid = entry != NULL && entry->occupied && entry->lease_id == lease_id &&
          (entry->request_id == 0u || entry->request_id == request_id) &&
          !entry->result_encoding && !entry->completion_delivered;
  ```

  Reserve encoding under the gate, call encoder outside it, then publish result status/error and
  `delivery_sequence = ++next_delivery_sequence` under the gate. Increment `results_ready` only
  for a published result; after a terminal delivery is recorded, later completions skip encoder
  and become drain-only. Take one Source waker on the empty-to-nonempty result edge and invoke it
  outside the gate.

- [x] **Step 4: Emit the smallest delivery sequence and settle entries**

  Scan at most 64 entries for the smallest ready nonzero sequence. Copy/reset VALUE storage,
  clear its demand reservation, decrement ready/reserved, and free the entry only when Actor ack
  has also completed. Owner driving scans for `completion_delivered && !acknowledged`, calls Actor
  acknowledge outside the Adapter gate, marks ACKNOWLEDGED on RELEASED, and frees an already
  emitted/discarded entry. Each successful acknowledge counts as one `progressed` transition.

- [x] **Step 5: Add terminal and close RED/GREEN cases**

  Add separate cases for prepare DONE with three outstanding values, prepare ERROR after two
  accepted operations, encoder DONE, VALUE_AND_DONE and ERROR with other operations live, and
  cancellation before/after completion. Assert the exact prefix of emitted values, one terminal
  notification, later completion drain without encode/value, release count equal accepted count,
  `owner_close == SALTS_EBUSY` before quiescence, and final close success. Preserve the existing
  completion-before-arm and two driver-tail barrier cases.

- [x] **Step 6: Run focused race and adjacent tests**

  Run full Source once, the tail-window and concurrent-completion filters 1000 times, then Actor,
  Runtime, readiness, native and file tests. Any lost wake, duplicate release, stale result or
  capacity-one behavior change blocks the task.

- [x] **Step 7: Commit multi-completion lifecycle**

  ```text
  feat(cflow): drain windowed IO completions in delivery order
  ```

### Task 5: Benchmark gate, documentation and repository verification

**Files:**
- Create: `cflow/benchmarks/cflow_io_source_benchmark.c`
- Modify: `cflow/benchmarks/CMakeLists.txt`
- Modify: `cflow/README.md`
- Modify: `docs/superpowers/plans/2026-08-27-cflow-windowed-io-source.md`

**Interfaces:**
- Consumes: completed capacity 1/windowed adapters and TinyTest benchmark APIs.
- Produces: capacity 1/8/32 control-plane A/B evidence and the keep-or-revert decision.

- [x] **Step 1: Add a correctness-first synthetic backend**

  The benchmark backend retains submitted tuples in a fixed array of 32 and completes the entire
  submitted batch through the real `cflow_io_actor_complete()` API. Each operation owns a small
  fixed token whose release increments an exact counter. Before timing, assert requested values,
  emitted values, releases, acknowledgements, window peak, errors, rejections and stale
  completions all match literal expected counts.

- [x] **Step 2: Add explicit TinyTest benchmark units**

  For capacities 1, 8 and 32, process the same `VALUES_PER_SAMPLE` values and use:

  ```c
  benchmark_ops(title, samples, VALUES_PER_SAMPLE) {
      check_status = run_window_batch(&fixture, VALUES_PER_SAMPLE);
  }
  ```

  Keep setup/allocation outside the timed block; inside, request demand, drive owner/scheduler and
  recycle fixed operations. Do not place assertions in the timed block. Print one JSON record per
  capacity with `ns_per_value`, `ops_per_second`, wake/driver counts, peak occupied, errors,
  rejections and stale completions.

- [x] **Step 3: Run repeated Release A/B and apply the gate**

  Build with `win-release-user`; run 15 alternating sequences of capacities `1,8,32` and
  `32,8,1`. Compute independent medians and paired ratios:

  ```text
  gain(capacity) = median(ns_per_value_cap1) /
                   median(ns_per_value_capacity) - 1
  ```

  Keep production code only if capacity 8 or 32 has gain at least 0.30, zero correctness counters,
  no capacity-one latency regression over 10%, and measured retained memory agrees with the
  capacity formula within 20%. Otherwise use `apply_patch` to remove Tasks 1-4 production/API and
  their feature-only tests/benchmark, retaining only an evidence note in this plan.

- [x] **Step 4: Document only a passing implementation**

  If the gate passes, update `cflow/README.md` with the old sequential path, the opt-in windowed
  path, completion-order warning, demand/ownership example, memory formula, error behavior and
  shutdown order. If it fails, do not document or expose an unavailable feature.

- [x] **Step 5: Run full verification**

  Under `VsDevCmd.bat`, run focused Release tests, all CFlow Release tests, full
  `ctest --preset win-release-user`, install preset and installed-package consumer. Configure and
  run focused `win-dev-user` ASan tests when the runtime is available. Then run CodeGraph sync/
  affected analysis, `git diff --check`, inspect public ABI changes and verify `.codegraph/` is not
  staged.

- [x] **Step 6: Commit only after the performance and correctness gates pass**

  ```text
  perf(cflow): batch independent IO source completions
  ```

## Execution Evidence (2026-08-27)

Tasks 3-5 were intentionally integrated in commit `b7d6b39` after the
performance gate instead of creating intermediate production commits.

- **事实:** Release benchmark used 15 interleaved process runs per capacity,
  20 samples per run and 4096 values per sample. All runs reported zero errors,
  rejections and stale completions.
- **计算:** Median capacity-1 latency was `664.409 ns/value`; capacity 8 was
  `448.787 ns/value`. Latency reduction is
  `1 - 448.787 / 664.409 = 32.45%`; the plan's speedup gain is
  `664.409 / 448.787 - 1 = 48.03%` (`1.480x`). Capacity 32 measured
  `666.605 ns/value`, or `-0.33%` speedup gain, because bounded linear scans
  offset the lower driver count.
- **事实:** Capacity 8 reduced `driver_calls` from 86016 to 10752 and
  `drive_calls` from 172032 to 21504 for the same benchmark work. Peak
  occupancy was exactly the configured capacity for 1, 8 and 32.
- **事实:** Focused Release tests passed 7/7, driver-tail race filters passed
  1000 repetitions each, all CFlow tests passed 29/29, and the full Release
  suite passed 147/147. Install and installed-package consumers succeeded.
  Focused MSVC AddressSanitizer tests passed after running under `VsDevCmd` so
  the ASan runtime was on `PATH`.
- **事实:** Construction uses one checked fixed allocation per adapter entry
  array plus fixed per-entry value slots, and initializes Actor request,
  logical command and manual Executor capacities from the same configured
  capacity. Runtime adapter allocations are absent. OS allocator retained-byte
  telemetry was not added; caller/backend payload remains outside adapter
  ownership and must be budgeted separately.
- **推论:** The measured gain comes from amortizing Runtime/drive/control-plane
  transitions, not removing the existing typed-value `memcpy`. Capacity 32's
  result confirms that larger windows can lose the gain to O(capacity) scans.
