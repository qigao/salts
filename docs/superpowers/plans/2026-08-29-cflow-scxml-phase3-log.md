# CFlow SCXML Phase 3 Log Executable Content Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Execute null-data-model SCXML `log` elements through tlog DEBUG records without changing Statechart success, rollback, or public APIs.

**Architecture:** Extend the private SCXML block IR with a program-owned NUL-terminated label step. Emit it only in the SCXML contextual callback through `tlog_peek_default()`, while keeping native CFlow free of XML/tlog and accounting the retained label in the existing string budget.

**Tech Stack:** C11, CFlow Statechart, TurboUtils XML parser, TurboUtils tlog, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-log-design.md`

## Global Constraints

- Keep all tlog calls inside `cflow-scxml`; do not change CFlow runtime headers or implementation.
- Use DEBUG and `tlog_peek_default()`; never initialize, flush, retry, sample, or destroy a logger in production SCXML code.
- Logger absence, filtering, backpressure, and dropped records must not alter Statechart state or errors.
- Support only the null-model label; reject every present `expr` at compile time.
- Copy labels into bounded program-owned storage and include their terminators in `max_name_bytes`.
- Preserve deterministic document order, checked arithmetic, transactional cleanup, and installed-package behavior.

---

### Task 1: Specify observable log execution with RED tests

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `cflow-scxml/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `cflow_scxml_compile`, runtime executable bindings, tlog callback sinks.
- Produces: behavioral tests for DEBUG emission, order, and non-interference.

- [x] **Step 1: Add a real tlog capture fixture**

Use `tlog_create`, `turbo_sink_callback_create`, `tlog_set_default`, and
`tlog_flush` only in the test boundary. Capture `level`, `component`, and
message bytes for two records, restore the prior default logger before
destroying the test logger, and keep the statechart executor/instance real.
Link the test target explicitly to `TurboUtils::Core` so its direct use of the
public tlog API does not depend on transitive include paths.

- [x] **Step 2: Add the first failing behavior test**

Compile an onentry block containing `<log label='entered'/>` followed by a
supported `raise`. Assert compilation succeeds, the machine reaches final,
and the captured record is DEBUG with component `cflow.scxml` and message
`entered`. This fails before implementation with
`CFLOW_SCXML_UNSUPPORTED_FEATURE`.

- [x] **Step 3: Run focused RED**

```powershell
cmake --build --preset win-release-user --target cflow_scxml_test --parallel
ctest --preset win-release-user -R ^cflow_scxml_test$ --output-on-failure
```

Expected: the new test fails because `log` is unsupported; existing tests pass.

- [x] **Step 4: Add order, conditional, and no-default-logger RED cases**

Use two labels around a conditional/raise path and literal expected messages
to detect skipped, duplicated, reordered, or wrong-branch emission. Separately
set the default logger to NULL and assert the same document completes without
runtime error.

### Task 2: Admit and execute bounded label-only log steps

**Files:**
- Modify: `cflow-scxml/src/scxml.c`
- Modify: `cflow-scxml/CMakeLists.txt`
- Test: `cflow-scxml/tests/cflow_scxml_test.c`

**Interfaces:**
- Consumes: XML attribute views, checked allocation helpers, immutable block steps, `TurboUtils::Core` tlog API.
- Produces: internal `SCXML_STEP_LOG` with a stable program-owned `const char *label`.

- [x] **Step 1: Add XML admission and checked label counting**

Recognize `log`; allow only `label` and `expr`; reject present `expr` as
unsupported for the null data model; reject non-comment children; and add
`label.size + 1` to a checked `log_label_bytes` count and the common retained
string budget.

- [x] **Step 2: Allocate, copy, transfer, and free label storage**

Allocate exactly the admitted label bytes before emission. Copy decoded label
bytes plus a NUL terminator, store stable pointers in log steps, transfer the
buffer into `cflow_scxml_program_impl`, and cover every build/program cleanup
path.

- [x] **Step 3: Execute DEBUG output without affecting runtime status**

Add the log branch to `execute_scxml_range` and call
`TURBO_LOG_DEBUG(tlog_peek_default(), "cflow.scxml", step->label)`. Do not
inspect delivery, flush, or modify `out_error`. Mark containing executable
descriptors with `CMETA_EFFECT_IO` while preserving their existing stateful and
may-fail effects.

- [x] **Step 4: Add the private/link-only Core dependency**

Link `cflow_scxml` privately to `TurboUtils::Core` and update its exact CMake
dependency-contract assertion without exposing `tlog.h` from public headers.

- [x] **Step 5: Run focused GREEN and adjacent tlog regression**

```powershell
cmake --fresh --preset win-release-user -DCFLOW_ENABLE_SCXML=ON
cmake --build --preset win-release-user --target cflow_scxml_test test_tlog --parallel
ctest --preset win-release-user -R "^(cflow_scxml_test|test_tlog)$" --output-on-failure
```

- [x] **Step 6: Commit executable support**

```bash
git add cflow-scxml/src/scxml.c cflow-scxml/CMakeLists.txt cflow-scxml/tests/cflow_scxml_test.c
git commit -m "feat: execute SCXML log actions"
```

### Task 3: Prove diagnostics, limits, documentation, and packaging

**Files:**
- Modify: `cflow-scxml/tests/cflow_scxml_test.c`
- Modify: `cflow/README.md`
- Modify: `tests/install_consumer/consumer.c`
- Modify: `docs/superpowers/specs/2026-08-29-cflow-scxml-phase3-log-design.md`
- Modify: `docs/superpowers/plans/2026-08-29-cflow-scxml-phase3-log.md`

**Interfaces:**
- Consumes: label-only log compiler behavior and package export graph.
- Produces: stable diagnostics, shared retention limit coverage, and installed consumer proof.

- [x] **Step 1: Add failing diagnostic and boundary tests**

Assert `expr` reports `CFLOW_SCXML_UNSUPPORTED_FEATURE` at the attribute,
children/unknown attributes report invalid structure, and a label that makes
state/event/label retained bytes exceed a reduced `max_name_bytes` reports
`CFLOW_SCXML_LIMIT_EXCEEDED` without publishing a program.

- [x] **Step 2: Make diagnostics and shared limit tests GREEN**

Preserve first-error document order and use the existing diagnostic helper and
transactional cleanup. Run the focused SCXML test after each adjustment.

- [x] **Step 3: Extend installed consumer behavior**

Add a label-only log to the existing SCXML consumer source. It must compile,
link, execute, and reach clean completion without installing or configuring a
logger, proving the link-only Core dependency and no-op logger path.

- [x] **Step 4: Document exact supported grammar and dependency**

Describe label-only DEBUG behavior, null-model `expr` rejection, program-owned
label lifetime, shared byte limit, no logger lifecycle, and the fact that
`TurboUtils::CFlow` itself remains tlog-free.

- [x] **Step 5: Run package and full verification**

Run SCXML enabled and disabled installed-package targets, the complete MSVC
Release build, and all CTest cases. Sync CodeGraph and run `git diff --check`.

- [x] **Step 6: Commit docs and package proof**

```bash
git add cflow/README.md tests/install_consumer/consumer.c docs/superpowers
git commit -m "docs: describe SCXML log execution"
```

### Task 4: Review and integration

- [ ] **Step 1: Request code review focused on dependency direction, label ownership, log non-interference, and limit accounting**
- [ ] **Step 2: Apply verified findings and rerun affected tests**
- [ ] **Step 3: Use verification-before-completion and finishing-a-development-branch for handoff**
- [ ] **Step 4: After merge only, mark `Implement logging` complete in #122**
