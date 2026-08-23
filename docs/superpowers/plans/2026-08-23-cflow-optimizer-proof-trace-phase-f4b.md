# CFlow Optimizer Proof Trace Phase F-4B Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Emit and validate a bounded C optimizer trace that lets AOT Stage IR account for Lean-proved duplicate-idempotent Map deletion.

**Architecture:** Optimization optionally owns one opaque contiguous trace allocation and records exact source coordinates for every semantic deletion. A certificate-aware AOT matcher replays those coordinates against the original normalized Graph, filters only justified Stage IR rows, and exactly matches the optimized Graph.

**Tech Stack:** ISO C11, CMeta contracts, CFlow Graph/optimizer/AOT IR, TinyTest, CMake Presets.

**Spec:** `docs/superpowers/specs/2026-08-23-cflow-optimizer-proof-trace-phase-f4b-design.md`

## Global Constraints

- Preserve existing optimizer and matcher APIs and all Direct/Plan/Kernel hot paths.
- A trace is optional, bounded, owned and transactionally committed.
- Graph identity and version are both required; no structural-clone fallback.
- Only duplicate declared-idempotent endomap deletion is certified in F-4B.
- C validates declared contracts and exact rule application; Lean owns the mathematical law theorem.

---

### Task 1: Lock trace and AOT certificate behavior with failing tests

**Files:**
- Modify: `cflow/tests/cflow_direct_test.c`

**Interfaces:**
- Produces requirements for `cflow_opt_trace`, `cflow_opt_rewrite_event`,
  `cflow_graph_optimize_with_trace` and
  `cflow_aot_pipeline_ir_match_optimized_graph`.

- [x] Add a generated Direct pipeline containing the same declared-idempotent
  `int -> int` clamp Map twice.
- [x] Require one exact trace event and a successful certificate-aware match
  after the existing matcher rejects the shortened optimized Graph.
- [x] Require Direct, source Kernel and optimized Kernel output parity.
- [x] Require a cloned optimized Graph to fail binding validation and clear the
  new witness.
- [x] Build `cflow_direct_test` and verify compilation fails because the new API
  does not exist.

### Task 2: Implement bounded optimizer trace ownership

**Files:**
- Modify: `cflow/include/cflow/opt.h`
- Modify: `cflow/src/opt.c`
- Modify: `cflow/include/cflow/property.h`
- Modify: `cflow/src/property.c`

**Interfaces:**
- Produces stable rule/event types, opaque trace lifecycle/accessors,
  `cflow_graph_optimize_with_trace`, and
  `cflow_callable_declares_idempotent_endomap`.

- [x] Add explicit rule value 1, event coordinates and zero-state opaque trace.
- [x] Count logical source callables with checked arithmetic and allocate one
  contiguous trace object only when tracing is requested.
- [x] Record retained/removed source coordinates exactly when the existing
  optimizer removes a duplicate.
- [x] Commit trace binding only after output validation; destroy all partial
  state on failure.
- [x] Delegate the old optimizer API to the no-trace core and verify focused
  optimizer tests remain green.

### Task 3: Replay the certificate in the AOT matcher

**Files:**
- Modify: `cflow/include/cflow/direct.h`
- Modify: `cflow/src/direct.c`

**Interfaces:**
- Produces `cflow_aot_optimized_equivalence_witness` and
  `cflow_aot_pipeline_ir_match_optimized_graph`.

- [x] Match Stage IR exactly against the normalized source Graph and collect at
  most 16 logical source coordinates.
- [x] Validate trace binding and replay events in order; reject wrong rule,
  coordinate, adjacency, callable or declared-contract premise.
- [x] Filter certified removed stages into a fixed 16-row local IR and reuse the
  exact existing Graph matcher for the optimized Graph.
- [x] Commit both versions, original stage count and rewrite count only after all
  checks succeed; clear the witness on every failure.
- [x] Build and run `cflow_direct_test` to verify GREEN.

### Task 4: Verify compatibility and performance

**Files:**
- Modify this plan only to record completed checks.

**Interfaces:**
- Produces reproducible MSVC/Clang correctness and Direct hot-path evidence.

- [x] Run the complete MSVC Release CMeta/CFlow CTest filter.
- [x] Run the complete Clang Release CMeta/CFlow CTest filter.
- [x] Run five Direct benchmark samples per compiler and compare medians to F-3.
- [x] Run `git diff --check` and confirm no unowned files entered the diff.
- [ ] Commit and push F-4B to Draft PR #35.

Paired same-session throughput medians (ops/s), baseline `0ca17d6` versus
F-4B:

| Compiler | Path | Baseline | F-4B | Delta |
|---|---|---:|---:|---:|
| MSVC | Direct | 2,299,893,558 | 2,500,732,607 | +8.73% |
| MSVC | Raw predecoded | 325,824,315 | 330,242,257 | +1.36% |
| MSVC | Materialized Plan | 126,277,013 | 127,658,259 | +1.09% |
| MSVC | Plan | 284,859,072 | 295,230,357 | +3.64% |
| Clang | Direct | 2,089,079,704 | 1,955,668,965 | -6.39% |
| Clang | Raw predecoded | 317,475,672 | 324,741,015 | +2.29% |
| Clang | Materialized Plan | 129,453,668 | 126,339,613 | -2.41% |
| Clang | Plan | 266,034,835 | 272,808,990 | +2.55% |

All unchanged execution paths remain within the 10% regression gate. Trace
allocation and replay are optimizer/AOT control-plane work and do not enter the
per-item Direct, Plan or Kernel loops.
