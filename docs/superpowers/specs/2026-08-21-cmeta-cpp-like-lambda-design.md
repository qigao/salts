# CMeta C++-Like Lambda Design

Status: architecture baseline — direct finite-arity model approved  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Goal

CMeta Extend may expose C++-like lambda syntax, but lambda semantics belong to CMeta Core. The frontend must lower finite explicit capture lists and finite typed parameter lists to the existing `cmeta_callable` runtime ABI without introducing a second callable ABI, arity-specific semantic families, unrestricted templates, compile-time execution, ownership semantics, or a C++ runtime dependency.

The Core semantic target is exactly:

```text
Callable<Args, Result>
Lambda<Env, Args, Result>
```

where `Args` is one finite ordered type schema and `Env` is one finite capture environment.

Arity is derived only as:

```text
arity = Args.length
```

It is not part of the semantic type family name.

## 2. Hard cut-over rule

The formal/Core model must migrate directly to the unified model.

Do not retain semantic compatibility aliases such as:

```text
Callable1
Callable2
Lambda1
Lambda2
```

and do not introduce:

```text
Callable3
Callable4
Lambda3
Lambda4
...
```

Existing CFlow C macros named `lambda1`, `lambda2`, or `lambda_gen` are consumer/backend implementation details until separately migrated. They must not determine Core formal semantics and must not cause arity-specific Lean types to remain.

## 3. Architecture

```text
CMeta Extend
  [captures](typed args) -> R { C body }
          │
          │ lower
          ▼
CMeta Core
  Lambda<Env, Args, R>
          │ erase environment
          ▼
  Callable<Args, R>
          │ erase runtime typing
          ▼
  cmeta_callable
          ▲
          │ consumed by
CFlow / State / Exec / other modules
```

Removing Extend removes only the source spelling. Equivalent strict-C11 closure construction remains possible through Core/runtime facilities.

## 4. Unified formal model

The target formal shape is:

```lean
/-- Heterogeneous values matching an ordered finite CType schema. -/
inductive HArgs : List CType → Type where
  | nil : HArgs []
  | cons : t.denote → HArgs ts → HArgs (t :: ts)

structure Callable (Args : List CType) (R : CType) where
  run : HArgs Args → R.denote

structure Lambda (Env : Type) (Args : List CType) (R : CType) where
  capture : Env
  body : Env → HArgs Args → R.denote
```

Examples:

```text
Callable<[int], long>
Callable<[int, double], long>
Callable<[A, B, C, D], R>
```

all use the same `Callable` type.

`Env : Type` is a semantic abstraction only. Accepted source lambdas still have a finite explicit capture list and must lower to finite C storage satisfying the Core capture-storage contract.

## 5. Unified invocation

Lambda invocation is defined once:

```lean
def Lambda.invoke (f : Lambda Env Args R) (xs : HArgs Args) : R.denote :=
  f.body f.capture xs

def Lambda.asCallable (f : Lambda Env Args R) : Callable Args R :=
  ⟨fun xs => f.body f.capture xs⟩
```

The central theorem is independent of arity:

```lean
theorem Lambda.beta
    (f : Lambda Env Args R)
    (xs : HArgs Args) :
    f.invoke xs = f.body f.capture xs := rfl
```

No `beta1`, `beta2`, `beta3`, etc. theorem family is permitted in the target model.

## 6. Protocol is separate from arity

Generator semantics are not a three-argument lambda.

The semantic signature direction is orthogonal:

```text
protocol = value | generator
params   = finite Args schema
result   = R
```

Conceptually:

```lean
inductive CallableProtocol where
  | value
  | generator

structure Signature where
  protocol : CallableProtocol
  params : List CType
  result : CType
```

Thus:

```text
value     [int]         -> long
value     [int,double]  -> long
generator [Input]       -> Output
```

A generator's output pointer and cursor are runtime protocol mechanics, not logical user parameters.

The lambda hard-cutover slice may leave the existing signature representation in place temporarily if changing it would expand the implementation into an unrelated whole-formal-model migration. It must not reintroduce arity-specific `Callable` or `Lambda` semantic types.

## 7. Surface syntax v1

V1 accepts explicit by-value and init captures with an admitted finite parameter count:

```c
[](int x) -> int {
    return x * x;
}

[factor](int x) -> long {
    return (long)x * factor;
}

[a, b](int x, long y) -> long {
    return (long)x * a + y + b;
}

[factor = config.scale](int x) -> long {
    return (long)x * factor;
}
```

V1 requires:

```text
explicit capture list
explicit parameter types
explicit return type
finite capture count
finite parameter count
capture environment within Core storage limits
one concrete admitted callable signature
```

## 8. Capture semantics

Empty capture uses an empty/unit environment.

By-value capture snapshots values at closure construction time:

```c
[x, y](int z) -> int { ... }
```

conceptually stores:

```c
typedef struct {
    X x;
    Y y;
} __cmeta_capture_N;
```

Init capture:

```c
[x = expr](int y) -> int { ... }
```

evaluates `expr` exactly once at closure construction. This is ordinary runtime C evaluation, not CMeta compile-time evaluation.

## 9. Body-preserving lowering

V1 must not require captured-identifier rewriting inside the C body.

For:

```c
[factor, offset](int x, long y) -> long {
    return (long)x * factor + y + offset;
}
```

Extend may generate:

```c
typedef struct {
    int factor;
    long offset;
} __cmeta_capture_N;

static long __cmeta_lambda_body_N(
    int factor,
    long offset,
    int x,
    long y)
{
    return (long)x * factor + y + offset;
}
```

The invoke adapter unpacks capture storage plus logical arguments and calls the helper. The original body can therefore remain ordinary C source text.

Generated helper names are not semantic identity.

## 10. V1 exclusions

Excluded from v1:

```text
[&x]
[=]
[&]
mutable captures
implicit parameter/result type inference
unconstrained generic `auto` lambda templates
constexpr / consteval lambda semantics
implicit `this`
C++ ownership/lifetime semantics
variadic template parameter packs
arbitrary overloaded operator()
```

Reference capture can be reconsidered only with a token/scope-aware lowering design. Lifetime and synchronization remain ordinary C obligations.

Contextual `auto` may be added later only when one concrete target signature uniquely determines the type before Core lowering. It must still lower to one concrete finite `Callable<Args,R>`.

## 11. Core proof obligations

The unified model must prove once for arbitrary finite `Args`:

### Invocation preservation

```text
invoke(lower(L), args)
=
surfaceEval(L, args)
```

### Signature preservation

```text
signature(erase(lower(L)))
=
resolved surface signature
```

### Empty-capture equivalence

An empty-environment lambda is observationally equivalent to an ordinary callable with the same body.

### Capture packaging transparency

```text
body(c1, c2, ..., args)
=
body(unpack(pack(c1, c2, ...)), args)
```

### Capture-name irrelevance

Core semantics depend on environment values/types and body binding, not source capture names or generated helper symbols.

### General partial application

For:

```text
f : Callable<Args ++ [B], R>
b : B
```

binding `b` forms a closure over the remaining `Args` and is semantically equivalent to a lambda capturing `b`.

The existing binary bind/lambda theorem becomes only a historical special case and must not remain the primary theorem shape.

## 12. C backend specialization

Strict C may require concrete generated function-pointer/adaptor specializations for specific admitted signatures.

That is backend machinery:

```text
Callable<Args,R>
      ↓ resolve finite signature
concrete C adapter
      ↓
cmeta_callable
```

Backend arity specialization must never leak into the Core semantic API or force public `lambdaN`/`CallableN` families.

## 13. Capture storage

The current inline capture buffer remains the v1 storage contract.

Oversized finite environments must be rejected at compile/lowering time. Extend must not silently heap-allocate closure environments, because that would introduce new ownership/lifetime semantics.

## 14. Effects and properties

V1 does not infer effects/properties from arbitrary C body text. Contracts come from context or explicit annotations under existing CMeta rules.

Capture syntax alone does not imply purity, determinism, totality, failure, or mutation properties.

## 15. Parser boundary

V1 parsing needs only:

```text
explicit capture declarations
finite parameter declarations
explicit return type
balanced body source span
```

The body remains ordinary C text inside the generated helper. No full free-variable analysis or capture identifier rewriting is required.

Generated artifacts must live under the corresponding CMake binary-tree `generated/` directory, never the source tree.

## 16. Implementation gate

Before claiming the unified Core lambda model implemented:

- `Callable1` and `Callable2` are removed from Lean formal code;
- `Lambda1` and `Lambda2` are removed from Lean formal code;
- no alias with those names remains;
- `HArgs`, `Callable`, and `Lambda` cover zero, one, two, and higher finite argument schemas semantically;
- beta/erasure/signature theorems are stated on the unified model;
- the existing bind equivalence is generalized or restated without arity-specific lambda types;
- every downstream Lean module builds without relying on old callable/lambda names;
- `lake build --wfail` passes;
- proof-placeholder guard remains green.

CFlow C macro cleanup is a separate implementation slice because it changes public strict-C11 source spelling, whereas this gate changes Core formal semantic ownership.

## 17. Rejected designs

Rejected:

- `Lambda1/2/3/4/...` as the semantic model;
- `Callable1/2/3/4/...` as the semantic model;
- retaining Lean compatibility aliases after hard cut-over;
- generator modeled as a three-argument value lambda;
- CFlow owning general closure semantics;
- a second callable ABI for Extend;
- C++ generic-lambda template semantics;
- preprocessor parsing of `[capture](args){body}` in ordinary `.c` files;
- full free-variable analysis for v1;
- textual capture substitution;
- implicit heap allocation for captures;
- generated symbol names as semantic identity.

## 18. Success criterion

One commuting property must cover every admitted finite arity:

```text
             surface evaluation
SurfaceLambda --------------------> Result
      |                                ^
      | lower                          | equal
      v                                |
Lambda<Env,Args,R> ---- invoke --------+
      |
      | erase
      v
Callable<Args,R>
      |
      | runtime erase
      v
cmeta_callable
```

Therefore C++-like lambda syntax increases source ergonomics, not Core expressive power, and adding callable arity does not expand the semantic API surface.
