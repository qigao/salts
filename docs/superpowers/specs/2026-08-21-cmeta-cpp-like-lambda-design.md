# CMeta C++-Like Lambda Design

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Goal

CMeta Extend may expose a C++-like lambda surface syntax, but lambda semantics remain a CMeta Core callable/closure capability. The frontend must lower finite explicit capture lists and finite typed parameter lists to the existing `cmeta_callable` representation without introducing a second callable ABI, arity-specific semantic families, unrestricted templates, compile-time execution, ownership semantics, or a C++ runtime dependency.

The semantic target is one finite-arity model:

```text
Callable<Args, Result>
Lambda<Env, Args, Result>
```

where `Args` is a finite ordered type schema and `Env` is the finite capture environment.

The target property is:

```text
surface lambda syntax
        ↓ lowering
Lambda<Env, Args, Result>
        ↓ erasure
cmeta_callable
```

with invocation and signature preservation for every admitted finite arity.

## 2. Existing semantic baseline

The current Lean model already represents unary and binary closures as:

```text
capture environment + typed body
```

and proves beta reduction, callable erasure semantics, signature preservation, and bind/closure equivalence. The current C runtime already stores a by-value capture payload in `cmeta_callable.capture` and invokes through an erased adapter receiving:

```c
const void *const *args
```

Therefore the erased runtime ABI is already naturally finite-N-ary. The current `Callable1` / `Callable2` and `lambda1` / `lambda2` forms are implementation-era specializations, not a reason to make arity part of the permanent semantic model.

## 3. Architectural ownership

```text
CMeta Extend
  parser / source lambda syntax
  capture-list parsing
  finite parameter-schema parsing
  contextual type resolution
  lowering / generated C
        │
        ▼
CMeta Core
  Callable<Args, R>
  Lambda<Env, Args, R>
  cmeta_callable ABI
  signature / effects / properties
        ▲
        │
CFlow / State / Exec / other consumers
```

CFlow must not own the general lambda abstraction. Existing `cflow/meta.h` `lambda1/lambda2/lambda_gen` macros remain compatibility/consumer-specific spellings while Core converges on the generic callable model.

Removing CMeta Extend must remove only the C++-like spelling, not the semantic ability to construct an equivalent closure through strict C11 APIs.

## 4. One callable model, not `Callable1/2/3/...`

The formal semantic model must use one finite argument schema.

Conceptually:

```lean
inductive CType where
  | ...

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

Arity is derived information:

```text
arity = Args.length
```

and is not a separate semantic type family.

Examples:

```text
Callable<[int], long>
Callable<[int, double], long>
Callable<[A, B, C, D], R>
```

are all instances of the same abstraction.

The project must not add public semantic families such as:

```text
Callable3
Callable4
Lambda3
Lambda4
...
```

## 5. Compatibility aliases

The current unary/binary names may remain temporarily as aliases so existing proofs and CFlow code can migrate incrementally:

```lean
abbrev Callable1 (A R : CType) := Callable [A] R
abbrev Callable2 (A B R : CType) := Callable [A, B] R

abbrev Lambda1 (Env : Type) (A R : CType) := Lambda Env [A] R
abbrev Lambda2 (Env : Type) (A B R : CType) := Lambda Env [A, B] R
```

These aliases are compatibility surfaces only. New semantic theorems must be stated over `Callable` and `Lambda`, not duplicated by arity.

The C implementation may also keep internal arity-specialized adapters where strict C requires concrete function-pointer types, but those specializations are backend machinery rather than public semantic APIs.

## 6. Callable protocol is separate from arity

Generator semantics must not be modeled as `Lambda3` merely because the current C adapter has input, output pointer, and cursor parameters.

Logical protocol and logical arguments are separate concepts.

The long-term semantic signature shape is:

```lean
inductive CallableProtocol where
  | value
  | generator

structure Signature where
  protocol : CallableProtocol
  params : List CType
  result : CType
```

Examples:

```text
value     [int]          -> long
value     [int, double]  -> long
generator [Input]        -> Output
```

For a generator, output-buffer and cursor parameters are runtime protocol mechanics, not logical user arguments.

This keeps the model orthogonal:

```text
arity      = params.length
protocol   = value | generator
```

No protocol is encoded by inventing another lambda arity.

## 7. Surface syntax v1

V1 supports explicit by-value and init captures with any admitted finite parameter count:

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
finite parameter count
finite capture count
capture environment fitting the Core closure-storage contract
resolved signature admitted by the target consumer/policy
```

This avoids requiring the frontend to implement full C free-variable analysis, C return-type inference, or body-level identifier rewriting.

The spelling is intentionally close to C++ lambda syntax, but semantic compatibility is defined by this specification, not by the full C++ standard.

## 8. Capture semantics

### 8.1 Empty capture

```c
[](int x) -> int { ... }
```

lowers to a closure with a unit/empty environment. Semantically this is equivalent to an ordinary `Callable<[int], int>` with the same body and must not require a distinct runtime ABI.

### 8.2 By-value capture

```c
[x, y](int z) -> int { ... }
```

captures the values at closure-construction time. Later mutation of the original variables does not affect the captured snapshot.

The generated storage is conceptually:

```c
typedef struct {
    X x;
    Y y;
} __cmeta_capture_N;
```

The environment is copied into the closure value.

### 8.3 Init capture

```c
[x = expr](int y) -> int { ... }
```

means `expr` is evaluated once at closure construction and the resulting value becomes the captured field `x`.

This is ordinary runtime C expression evaluation, not compile-time CMeta evaluation.

## 9. Body-preserving lowering

V1 deliberately avoids rewriting captured identifiers inside the C body.

For:

```c
[factor, offset](int x, long y) -> long {
    return (long)x * factor + y + offset;
}
```

Extend generates an environment plus a helper whose leading parameters use the capture names:

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

The invoke adapter unpacks the environment and finite argument array, then calls:

```c
__cmeta_lambda_body_N(
    env.factor,
    env.offset,
    x,
    y);
```

Therefore the original body source can remain unchanged. The frontend only needs to recognize the lambda boundary and its explicit declarations; it does not need free-variable discovery or general C identifier substitution for v1.

Generated helper symbol names are implementation details and do not enter callable semantic identity.

## 10. V1 exclusions

The following C++ lambda features are intentionally excluded from v1:

```text
[&x] reference capture
[=] / [&] default capture
mutable value captures
implicit parameter/result type inference
generic template lambdas using unconstrained auto
constexpr / consteval lambda semantics
C++ object lifetime / ownership rules
implicit this capture
variadic template parameter packs
arbitrary overloaded operator() generation
```

These exclusions preserve the finite CMeta calculus and avoid requiring a complete C/C++ semantic frontend.

Reference capture is not rejected as a long-term capability. It is postponed until Extend has token/scope-aware body transformation capable of preserving C++-like lvalue spelling without unsafe textual macros. Its lifetime and synchronization safety will remain ordinary C obligations even then.

## 11. Contextual `auto` after v1

CMeta may later support contextual `auto` where the consumer already determines exactly one concrete type.

Example:

```c
stream<int>
  |> map([](auto x) -> long { return x * 2; });
```

If the target operator resolves exactly one signature, the frontend may replace `auto` with that concrete type before Core lowering.

This is permitted only when resolution is finite and unique. It must lower to one concrete `Callable<Args, R>`. It must not create a C++-style generic closure with a template call operator.

The existing trait principle remains authoritative: unsupported or ambiguous type recovery is rejected.

## 12. Generalized invocation semantics

`Lambda` invocation is defined once for all finite arities:

```lean
def Lambda.invoke (f : Lambda Env Args R) (xs : HArgs Args) : R.denote :=
  f.body f.capture xs

/-- Environment erasure yields an ordinary typed callable. -/
def Lambda.asCallable (f : Lambda Env Args R) : Callable Args R :=
  ⟨fun xs => f.body f.capture xs⟩
```

The central beta law is therefore independent of arity:

```lean
theorem Lambda.beta
    (f : Lambda Env Args R)
    (xs : HArgs Args) :
    f.invoke xs = f.body f.capture xs := rfl
```

This theorem replaces duplicated `beta1`, `beta2`, `beta3`, etc.

## 13. Unified signature semantics

Typed callable signature is derived from the finite parameter schema:

```lean
def Callable.signature (_ : Callable Args R) : Signature :=
  { protocol := .value, params := Args, result := R }
```

The erasure theorem is stated once:

```text
signature(erase(f))
=
{ protocol = value, params = Args, result = R }
```

The old `.unary` / `.binary` constructors may remain temporarily as compatibility representations while the formal model migrates, but they are not the target architecture.

The real C backend may still map a finite `Args` schema to concrete generated function-pointer families when necessary. That mapping is a backend specialization and must preserve the unified semantic signature.

## 14. Core semantic theorems

The unified model must prove at least the following.

### 14.1 Invocation preservation

For every admitted finite argument schema:

```text
invoke(lower(L), args)
=
surfaceEval(L, args)
```

No separate theorem is required for unary, binary, ternary, or higher arity.

### 14.2 Signature preservation

```text
signature(erase(lower(L)))
=
resolved surface signature
```

Lambda syntax must never create a callable whose erased signature differs from the type-resolved surface declaration.

### 14.3 Empty-capture equivalence

A closure with an empty environment is observationally equivalent, for invocation and signature, to an ordinary `Callable<Args, R>` with the same body.

### 14.4 Capture packaging transparency

Packing finite capture values into an environment and unpacking them as hidden helper parameters must preserve the body result:

```text
body(c1, c2, ..., args)
=
body(unpack(pack(c1, c2, ...)), args)
```

This is the central theorem connecting explicit C++-like capture lists to the Core environment model.

### 14.5 Capture-name irrelevance at Core level

Source capture names are an Extend binding concern. Once lowered, Core semantics depend on environment values/types and body binding, not source identifier spellings or generated helper names.

### 14.6 General partial-application equivalence

Binding the last logical argument is closure formation over the remaining finite schema.

Conceptually:

```text
f : Callable<Args ++ [B], R>
b : B

bindLast(f, b) : Callable<Args, R>
```

and:

```text
bindLast(f, b)
≃
[b](Args... xs) -> R { return f(xs..., b); }
```

The existing binary `lambda_bind_same_shape` theorem becomes the `[A] ++ [B]` corollary of this general law.

## 15. Capture storage policy

The current runtime has an inline capture buffer. V1 lowering must respect the existing Core capture-size contract rather than invent a new lambda-specific storage policy.

If the finite environment exceeds the supported inline capacity, construction must fail at compile/lowering time unless a future Core closure-storage extension is designed independently.

The frontend must not silently allocate heap-owned closure environments because that would introduce new ownership/lifetime semantics.

## 16. Effects and properties

Lambda syntax does not infer semantic effects merely from the C body in v1.

The callable contract must be supplied by context or explicit annotation according to existing CMeta/Core rules. The frontend may reject a lambda when the target operator requires a contract that cannot be established.

Capture syntax alone does not determine purity, determinism, totality, or failure behavior.

## 17. Parser boundary

For v1 the frontend needs to parse only enough lambda syntax to identify:

```text
explicit capture declarations
finite parameter declarations
explicit return type
balanced body source span
```

The lambda body remains ordinary C source text emitted inside the generated helper body. No free-variable analysis and no captured-identifier rewriting is required for by-value/init capture because capture names become hidden leading helper parameters.

The frontend still needs normal lexical balancing for comments, strings, braces, and tokens so it can locate the body boundary safely. This is compatible with the planned `re2c + handwritten parser` island-grammar approach and does not require a full C grammar.

Source locations must be preserved through source maps or `#line` directives.

## 18. Strict-C11 equivalence

Every accepted surface lambda must have an equivalent strict-C11 construction path using Core callable/closure facilities.

This is a mandatory architecture invariant:

```text
remove cmc / Extend
→ lose C++-like spelling
→ do not lose closure semantics
```

CFlow `lambda1/lambda2` can serve as early compatibility evidence but must not remain the architectural owner of the closure ABI.

No strict-C11 public API should grow as `lambda3`, `lambda4`, and so on. A Core construction API must accept one finite parameter schema or generated descriptor rather than exposing arity as the API family name.

## 19. C backend specialization

Strict C function-pointer typing may require generated adapter specializations for concrete arities/signatures.

That is acceptable internally:

```text
Callable<Args, R>
      ↓ Args.length / resolved signature
backend adapter specialization
      ↓
ordinary strict-C11 function pointer + erased cmeta_callable adapter
```

The following distinction is mandatory:

```text
semantic API          one Callable / Lambda model
backend codegen       finite concrete specializations as needed
```

Backend specialization must not leak into user-facing lambda naming or formal theorem duplication.

## 20. C implementation conformance target

A future conformance witness should exercise at least:

```text
zero logical arguments
one logical argument
multiple logical arguments
empty capture
single by-value capture
multi-field by-value capture
init capture
bind-equivalent lambda
oversized-capture rejection
```

For each accepted witness:

```text
surface/lowered expectation
→ generated ordinary C
→ real cmeta_callable invocation
→ checked result/signature snapshot
→ Lean conformance theorem
```

The multi-argument witness must prove that adding arity does not require a new semantic lambda family.

Generated outputs must follow the project build-layout rule and live under the corresponding CMake binary `generated/` directory, not in the source tree.

Reference capture gets a separate later applicability gate because it requires stronger source binding/lowering semantics.

## 21. Applicability boundary

The project may claim "finite C++-like lambda v1 syntax is applicable" only when all of the following are demonstrated:

- generalized `Callable<Args,R>` Lean model proves invocation/signature preservation;
- generalized `Lambda<Env,Args,R>` proves closure beta/erasure laws for arbitrary finite `Args`;
- no `Lambda3/Lambda4/...` semantic definitions are needed;
- protocol is modeled independently from arity;
- capture pack/unpack transparency is formally established;
- real Core closure construction supports equivalent finite environments;
- multi-field captures are exercised by real C witnesses;
- multiple logical argument counts are exercised by real C witnesses;
- init capture is evaluated exactly once at construction in a real witness;
- CFlow consumes the resulting Core callable without lambda-specific runtime logic;
- generated C is valid strict C11;
- GCC, Clang, and MSVC-compatible lowering rules are preserved;
- generated artifacts remain in the CMake binary tree;
- no full C++ compiler/runtime dependency is introduced.

## 22. Rejected designs

Rejected:

- implementing lambda as a CFlow-only feature;
- introducing `Lambda3`, `Lambda4`, or an unbounded public arity-name family;
- introducing `Callable3`, `Callable4`, or duplicated arity-specific semantic theorems;
- modeling generator as a three-argument value lambda;
- introducing a second callable ABI for Extend lambdas;
- treating C++ generic lambda templates as Core generics;
- using preprocessor tricks to parse `[capture](args){body}` in ordinary `.c` files;
- requiring full free-variable analysis for v1;
- textual macro substitution of captured identifiers;
- implicit heap allocation for oversized captures;
- claiming reference lifetime safety without an ownership/lifetime system;
- implementing `mutable` by silently mutating a `const cmeta_callable` capture buffer;
- making generated symbol names part of lambda semantic identity.

## 23. Success criterion

The design succeeds when one commuting property covers every admitted finite arity:

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
      | erase
      v
cmeta_callable
```

and when finite capture packaging is transparent:

```text
explicit captures
   ↓ pack
finite Env
   ↓ unpack in adapter
hidden helper parameters
   ↓
original C body
```

Finally:

```text
finite explicit captures
+ finite concrete Args schema
+ one concrete result type
+ one explicit protocol
+ ordinary C body
→ one finite Core callable
```

C++-like lambda syntax therefore increases source ergonomics, not CMeta Core expressive power, and lambda arity does not expand the semantic API surface.
