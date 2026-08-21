# CMeta Unified Lambda Formal Model Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace arity-specific Lean `Callable1/Callable2` and `Lambda1/Lambda2` with one finite-arity `Callable<Args,R>` and `Lambda<Env,Args,R>` model, with no compatibility aliases.

**Architecture:** Introduce a dependent heterogeneous argument list `HArgs : List CType → Type`. `Callable` consumes one `HArgs Args`; `Lambda` adds one environment and erases to `Callable`. Existing unary/binary C signature constructors remain temporarily as backend/formal compatibility so this slice stays focused on callable/lambda semantics rather than rewriting every CFlow signature proof.

**Tech Stack:** Lean 4.30.0, Lake, existing CMeta formal modules.

**Spec:** `docs/superpowers/specs/2026-08-21-cmeta-cpp-like-lambda-design.md`

## Global Constraints

- Do not define `Callable1`, `Callable2`, `Lambda1`, or `Lambda2`, even as aliases.
- Do not introduce `Callable3/4/...` or `Lambda3/4/...`.
- Keep generator protocol separate from value-callable arity.
- Preserve existing unary/binary `Signature` constructors in this focused slice unless compilation requires otherwise.
- No `axiom`, `constant`, `sorry`, or `admit`.
- `lake build --wfail` must pass.

---

### Task 1: Replace typed callable family with finite `Args`

**Files:**
- Modify: `formal/CMeta/Callable.lean`

**Interfaces:**
- Produces: `HArgs`, `Callable`, `Callable.invoke1`, `Callable.invoke2`, `Callable.compose`, `eraseValue`.
- Consumes: existing `CType`, `Signature`, `CallableDesc` semantics from `Traits.lean`.

- [ ] **Step 1: Replace `Callable1/Callable2` with `HArgs` and `Callable`**

```lean
inductive HArgs : List CType → Type where
  | nil : HArgs []
  | cons {t ts} : t.denote → HArgs ts → HArgs (t :: ts)

structure Callable (Args : List CType) (R : CType) where
  run : HArgs Args → R.denote
```

- [ ] **Step 2: Add unary/binary invocation helpers without arity-specific types**

```lean
def Callable.invoke1 (f : Callable [A] R) (x : A.denote) : R.denote :=
  f.run (.cons x .nil)

def Callable.invoke2 (f : Callable [A, B] R)
    (a : A.denote) (b : B.denote) : R.denote :=
  f.run (.cons a (.cons b .nil))
```

- [ ] **Step 3: Express composition on `Callable [A] R`**

```lean
def Callable.compose (g : Callable [B] R) (f : Callable [A] B) : Callable [A] R :=
  ⟨fun xs =>
    match xs with
    | .cons x .nil => g.invoke1 (f.invoke1 x)⟩
```

- [ ] **Step 4: Preserve runtime erasure for currently admitted value arities through one function**

```lean
def eraseValue : {Args : List CType} → Callable Args R → Option CallableDesc
  | [a], _ => some ⟨.unary a R⟩
  | [a, b], _ => some ⟨.binary a b R⟩
  | _, _ => none
```

- [ ] **Step 5: Push and verify expected RED**

Expected: `Lambda.lean` fails because old `Callable1/Callable2` names no longer exist. This confirms the hard cut-over is not masked by aliases.

---

### Task 2: Replace lambda family with `Lambda<Env,Args,R>`

**Files:**
- Modify: `formal/CMeta/Lambda.lean`

**Interfaces:**
- Consumes: `HArgs`, `Callable`, `Callable.invoke1`, `Callable.invoke2`, `eraseValue`.
- Produces: `Lambda`, `Lambda.invoke`, `Lambda.asCallable`, generalized beta/erasure theorems, generalized `bindLast`.

- [ ] **Step 1: Replace `Lambda1/Lambda2` with one `Lambda`**

```lean
structure Lambda (Env : Type) (Args : List CType) (R : CType) where
  capture : Env
  body : Env → HArgs Args → R.denote
```

- [ ] **Step 2: Define generic invocation and erasure**

```lean
def Lambda.invoke (f : Lambda Env Args R) (xs : HArgs Args) : R.denote :=
  f.body f.capture xs

def Lambda.asCallable (f : Lambda Env Args R) : Callable Args R :=
  ⟨fun xs => f.body f.capture xs⟩
```

- [ ] **Step 3: Prove the arity-independent closure laws**

```lean
theorem Lambda.beta (f : Lambda Env Args R) (xs : HArgs Args) :
    f.invoke xs = f.body f.capture xs := rfl

theorem Lambda.erasure_semantics (f : Lambda Env Args R) (xs : HArgs Args) :
    f.asCallable.run xs = f.invoke xs := rfl
```

- [ ] **Step 4: Keep concrete backend-signature theorems as corollaries, not lambda types**

For unary/binary lambdas, prove `eraseValue f.asCallable` yields the current `.unary`/`.binary` descriptor using `rfl`/`simp`, while `Lambda` itself remains arity-independent.

- [ ] **Step 5: Generalize bind over a finite prefix**

Define list append-aware argument append helpers and state the closure equivalence over `Callable (Args ++ [B]) R`. If Lean dependent-list manipulation makes this disproportionately large, retain `bindLast` only as a theorem over `Callable [A,B] R` in this slice, but do not reintroduce arity-specific callable/lambda types. Document the broader theorem as the next formal strengthening.

- [ ] **Step 6: Run `lake build --wfail`**

Expected: all existing downstream modules compile because they depend on `Signature`/`Dispatch`, not the removed callable/lambda type names.

---

### Task 3: Lock the hard cut-over and document evidence

**Files:**
- Modify: `docs/superpowers/specs/README.md`
- Modify: `formal/README.md`

**Interfaces:**
- Consumes: completed Task 1/2 formal model.
- Produces: explicit architecture/documentation statement that no Lean arity-specific callable/lambda aliases remain.

- [ ] **Step 1: Update docs**

State:

```text
Callable<Args,R>
Lambda<Env,Args,R>
arity = Args.length
```

and explicitly state that `Callable1/2` and `Lambda1/2` were removed rather than retained as aliases.

- [ ] **Step 2: Verify source absence**

Search formal Lean sources for:

```text
Callable1
Callable2
Lambda1
Lambda2
Lambda3
Lambda4
```

Expected: zero semantic definitions/usages. Documentation may mention them only when stating that they are removed/rejected.

- [ ] **Step 3: Run final CI**

Required evidence:

```text
proof-placeholder guard PASS
lake build --wfail PASS
existing C/Lean snapshots PASS
TypeId applicability probes PASS
```

- [ ] **Step 4: Commit**

Commit message:

```text
formal: unify callable and lambda arity model
```
