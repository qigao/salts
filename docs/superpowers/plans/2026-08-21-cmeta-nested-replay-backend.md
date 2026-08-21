# CMeta Nested Replay Backend Proof Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove that nested Producer semantics are lane-free and validate a strict-C11 deferred-expansion backend that can replay the same producer recursively without arity-specific A/B/C families.

**Architecture:** Extend the existing Producer formal model with Cartesian nested replay laws. Add one expected-failure strict-C11 source proving direct same-producer macro nesting is suppressed, plus one positive strict-C11 witness proving distinct-producer nesting and deferred same-producer nesting through an indirect thunk and bounded EVAL rescans. Production `pp.h` remains untouched.

**Tech Stack:** Lean 4.30, strict C11, CMake formal probes, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-nested-replay-backend-design.md`

## Global Constraints

- Do not modify production `cmeta/include/cmeta/pp.h`.
- Do not use C23 `__VA_OPT__`.
- Do not claim unbounded recursion; only a finite evaluation budget is proved.
- Distinguish semantic nested product from preprocessor self-suppression mechanics.
- Formal proof files may not contain `axiom`, `constant`, `sorry`, or `admit`.

---

### Task 1: Formal nested Producer semantics

**Files:**
- Create: `formal/CMeta/NestedReplay.lean`
- Modify: `formal/CMeta.lean`

**Produces:** lane-free `nestedReplay` semantics and cardinality laws.

- [ ] Import `CMeta.NestedReplay` before the file exists and run CI to observe RED.
- [ ] Define `nestedReplay f xs ys` as an ordered Cartesian map without lane/arity data.
- [ ] Prove empty-left, empty-right, general cardinality, and same-producer square cardinality.
- [ ] Run `lake build --wfail` through the formal workflow and require GREEN.

---

### Task 2: Strict-C11 suppression and deferred replay witnesses

**Files:**
- Create: `formal/cmeta_nested_replay_direct_fail.c`
- Create: `formal/cmeta_nested_replay_deferred_witness.c`
- Modify: `formal/CMakeLists.txt`
- Modify: `.github/workflows/lean.yml`

**Produces:** negative evidence for direct self-nesting and positive evidence for the smaller deferred backend.

- [ ] Add a CMake `try_compile`/configure check requiring `cmeta_nested_replay_direct_fail.c` to fail compilation under exact C11. If it compiles, configuration must fail.
- [ ] Add a positive witness with exact C11 and these fixed helpers: `EMPTY`, `DEFER`, `OBSTRUCT`, finite `EVAL` tiers, and one `P_INDIRECT() P` thunk.
- [ ] Verify direct nesting of distinct producers works without defer.
- [ ] Verify same two-element producer replayed through deferred recursion yields 4, 8, and 16 mapped leaves at depths 2, 3, and 4.
- [ ] Poison `CMETA_PP_NARG` and public `CMETA_PP_FOR_EACH` in the positive witness so the candidate path cannot accidentally use arity machinery.
- [ ] Wire the executable witness into the existing applicability step and require full workflow GREEN.

---

### Task 3: Record proof boundary

**Files:**
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-nested-replay-backend-design.md`
- Modify: `docs/superpowers/specs/README.md`
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-producer-replay-algebra-design.md`

- [ ] Record exact CI evidence only after the latest formal head is green.
- [ ] State that A/B/C × arity families are backend candidates for replacement, not yet deleted.
- [ ] Keep production migration and complete consumer audit explicitly open.

## Self-Review

- The negative witness proves the limitation rather than merely describing it.
- The positive witness changes the scaling dimension from element arity to finite rescan/nesting budget.
- No production macro API is changed by this plan.
