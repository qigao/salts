# CMeta Producer/Replay Algebra Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove that a producer macro, not raw `__VA_ARGS__`, is a sufficient semantic representation of a finite zero-or-more CMeta sequence and validate the model with a strict-C11 witness.

**Architecture:** Add a focused Lean module modeling finite producer sequences as lists and deriving replay, append, count, storage, guard, and map-composition laws. Add one strict-C11 witness that uses real producer-style macros to validate zero/non-zero replay and the two independent count derivations, without touching production `pp.h` or `fmt`.

**Tech Stack:** Lean 4.30, strict C11, CMake formal witness targets, GitHub Actions `Lean proofs` workflow.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-producer-replay-algebra-design.md`

## Global Constraints

- Do not modify `cmeta/include/cmeta/pp.h` in this proof slice.
- Do not modify `utils/include/fmt.h` or `utils/src/fmt.c`.
- Do not use C23 `__VA_OPT__`.
- Do not claim arbitrary raw empty `__VA_ARGS__` is portable strict-C11 list representation.
- Keep nested replay lanes explicitly out of scope.
- Formal proof files must contain no `axiom`, `constant`, `sorry`, or `admit`.

---

### Task 1: Formal Producer Algebra

**Files:**
- Create: `formal/CMeta/Producer.lean`
- Modify: `formal/CMeta.lean`

**Interfaces:**
- Consumes: ordinary Lean `List` operations and the existing formal build graph.
- Produces: `Producer.replay`, `Producer.append`, `Producer.count`, `Producer.storage`, `Producer.storageCount`, `Producer.canRead` and their laws.

- [ ] **Step 1: Write the new Lean module with the target statements first**

Define:

```lean
namespace CMeta.Producer

def replay (map : α → β) (xs : List α) : List β := xs.map map

def append (xs ys : List α) : List α := xs ++ ys

def count (xs : List α) : Nat := (replay (fun _ => 1) xs).foldl Nat.add 0

def storage (map : α → β) (sentinel : β) (xs : List α) : List β :=
  replay map xs ++ [sentinel]

def storageCount (storage : List β) : Nat := storage.length - 1

def canRead (storage : List β) (i : Nat) : Prop := i < storageCount storage
```

State the eight laws from the spec, including append/replay homomorphism, producer count, storage count agreement, guard equivalence, and map composition.

- [ ] **Step 2: Import the unfinished module from `formal/CMeta.lean` and run the formal workflow to observe RED**

Expected: Lean kernel build fails only because the new theorem bodies are intentionally incomplete or missing. Do not use proof placeholders forbidden by CI; use theorem declarations only after supplying bodies, so RED may instead be produced by temporarily referencing a not-yet-defined theorem name from a small local `example` and then removed in the next step.

- [ ] **Step 3: Implement the minimal proofs**

Use structural list induction and standard `simp`/`List.map_append` facts. Keep the model extensional and independent of C token mechanics.

Required theorem shapes:

```lean
theorem replay_empty ...
theorem replay_single ...
theorem replay_append ...
theorem count_eq_length ...
theorem storage_length ...
theorem storage_count_eq_count ...
theorem canRead_iff ...
theorem map_composition ...
```

- [ ] **Step 4: Run the workflow and require `lake build --wfail` to pass**

Expected: all existing formal modules plus `CMeta.Producer` build successfully.

- [ ] **Step 5: Commit**

```bash
git add formal/CMeta/Producer.lean formal/CMeta.lean
git commit -m "formal: prove CMeta producer replay algebra"
```

---

### Task 2: Strict-C11 Producer Applicability Witness

**Files:**
- Create: `formal/cmeta_producer_replay_witness.c`
- Modify: `formal/CMakeLists.txt`
- Modify: `.github/workflows/lean.yml`

**Interfaces:**
- Consumes: strict C11 preprocessor semantics only; may include `cmeta/pp.h` for existing public `Replay` spelling but must not rely on raw empty `FOR_EACH`.
- Produces: executable witness `cmeta_producer_replay_witness`.

- [ ] **Step 1: Write the witness cases**

Define producer macros such as:

```c
#define PRODUCER_EMPTY(M)
#define PRODUCER_ONE(M) M(11)
#define PRODUCER_THREE(M) M(11) M(22) M(33)
```

Use separate mappers to derive:

```text
ordered replay values
count = 0 + 1 + ...
storage = mapped items plus explicit sentinel
storage_count = ARRAY_COUNT(storage) - 1
```

Check empty, one, and three-item producers with explicit `CHECK` returning non-zero on failure.

- [ ] **Step 2: Wire it as a `c_std_11` formal witness and execute it in the existing applicability probe step**

Add:

```cmake
cmeta_add_formal_witness(cmeta_producer_replay_witness
  cmeta_producer_replay_witness.c)
```

and include the target in the workflow build list plus execution in `Execute applicability probes`.

- [ ] **Step 3: Verify strict-C11 behavior and full formal workflow**

Expected output contains:

```text
cmeta producer replay applicability: ok
```

and the final workflow completes successfully.

- [ ] **Step 4: Commit**

```bash
git add formal/cmeta_producer_replay_witness.c formal/CMakeLists.txt .github/workflows/lean.yml
git commit -m "formal: witness strict-C11 producer replay"
```

---

### Task 3: Architecture Status Sync

**Files:**
- Modify: `docs/superpowers/specs/2026-08-21-cmeta-producer-replay-algebra-design.md`
- Modify: `docs/superpowers/specs/README.md`

**Interfaces:**
- Consumes: successful latest-head Lean/C witness evidence.
- Produces: precise statement of what is proved and what remains backend work.

- [ ] **Step 1: Record proven status only after the latest workflow is green**

Document that Producer/Replay is a sufficient formal zero-or-more semantic list representation and that strict-C11 producer macros demonstrate applicability. Explicitly retain these open items:

```text
raw variadic empty adapter
nested replay lane simplification
production pp.h migration
```

- [ ] **Step 2: Update the specs index and trigger one final workflow from a formal-path touch only if needed**

If the final status commit touches docs only, rely on the immediately preceding latest-head formal run and do not claim a docs-only workflow was triggered.

- [ ] **Step 3: Commit**

```bash
git add docs/superpowers/specs/2026-08-21-cmeta-producer-replay-algebra-design.md docs/superpowers/specs/README.md
git commit -m "docs: record producer replay proof boundary"
```

## Self-Review

- Spec coverage: every Producer law and the strict-C11 applicability boundary has a corresponding task.
- Placeholder scan: no TBD/TODO/implementation placeholders.
- Type consistency: `count`, `storageCount`, and `canRead` all derive from one producer/storage model; raw `__VA_ARGS__` remains outside the semantic model.
