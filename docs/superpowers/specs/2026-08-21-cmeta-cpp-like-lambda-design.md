# CMeta C++-Like Lambda Design

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Goal

CMeta Extend may expose a C++-like lambda surface syntax, but lambda semantics remain a CMeta Core callable/closure capability. The frontend must lower finite capture lists and typed parameters to the existing `cmeta_callable` representation without introducing a second callable ABI, unrestricted templates, compile-time execution, ownership semantics, or a C++ runtime dependency.

The target property is:

```text
surface lambda syntax
        ↓ lowering
finite typed closure
        ↓ erasure
cmeta_callable
```

with invocation and signature preservation.

## 2. Existing semantic baseline

The current Lean model already represents one- and two-argument closures as:

```text
capture environment + typed body
```

and proves beta reduction, callable erasure semantics, and signature preservation. The current C runtime already stores a by-value capture payload in `cmeta_callable.capture` and invokes it through erased adapters.

Therefore this design does not invent closure semantics. It generalizes the formal capture model and gives CMeta Extend a source spelling for those semantics.

## 3. Architectural ownership

```text
CMeta Extend
  parser / source lambda syntax
  capture-list parsing
  contextual type resolution
  lowering / generated C
        │
        ▼
CMeta Core
  typed closure semantics
  cmeta_callable ABI
  signature / effects / properties
        ▲
        │
CFlow / State / Exec / other consumers
```

CFlow must not own the general lambda abstraction. Existing `cflow/meta.h` `lambda1/lambda2/lambda_gen` macros remain compatibility/consumer-specific spellings until a Core-level closure construction API replaces their duplicated mechanics.

Removing CMeta Extend must remove only the C++-like spelling, not the semantic ability to construct an equivalent closure through strict C11 APIs.

## 4. Surface syntax v1

Supported forms:

```c
[](int x) -> int {
    return x * x;
}

[factor](int x) -> long {
    return (long)x * factor;
}

[a, b](int x) -> long {
    return (long)x * a + b;
}

[factor = config.scale](int x) -> long {
    return (long)x * factor;
}

[&state](Event e) -> Result {
    return handle(state, e);
}
```

V1 requires an explicit capture list, explicit parameter types, and an explicit return type. This avoids requiring the frontend to implement full C free-variable analysis or general C return-type inference.

The spelling is intentionally close to C++ lambda syntax, but semantic compatibility is defined by this specification, not by the full C++ standard.

## 5. Capture modes

### 5.1 Empty capture

```c
[](int x) -> int { ... }
```

lowers to a closure with a unit/empty environment. Semantically this is equivalent to an ordinary typed callable and must not require a distinct runtime ABI.

### 5.2 By-value capture

```c
[x, y](int z) -> int { ... }
```

captures the values at closure-construction time. Later mutation of the original variables does not affect the captured snapshot.

The generated representation is conceptually:

```c
typedef struct {
    X x;
    Y y;
} __cmeta_capture_N;
```

The environment is copied into the closure value.

### 5.3 Init capture

```c
[x = expr](int y) -> int { ... }
```

means `expr` is evaluated once at closure construction and the resulting value becomes the captured field `x`.

This is ordinary runtime C expression evaluation, not compile-time CMeta evaluation.

### 5.4 Reference capture

```c
[&state](Event e) -> Result { ... }
```

lowers to a captured pointer/reference representation. CMeta does not infer or prove lifetime safety. The lifetime and synchronization obligations remain ordinary C obligations.

The formal claim is only that reference capture is represented as a value-captured reference/pointer and invocation preserves that representation.

## 6. V1 exclusions

The following C++ lambda features are intentionally excluded from v1:

```text
[=] / [&] default capture
mutable value captures
generic template lambdas using unconstrained auto
constexpr / consteval lambda semantics
C++ object lifetime / ownership rules
implicit this capture
variadic template parameter packs
arbitrary overloaded operator() generation
```

These exclusions preserve the finite CMeta calculus and avoid requiring a complete C/C++ semantic frontend.

## 7. Contextual `auto`

CMeta may later support contextual `auto` where the consumer already determines exactly one type.

Example:

```c
stream<int>
  |> map([](auto x) -> long { return x * 2; });
```

If the target operator requires exactly `int -> long`, the frontend may resolve `auto` to `int` before Core lowering.

This is permitted only when resolution is finite and unique. It must lower to one concrete callable signature. It must not create a C++-style generic closure with a template call operator.

The existing trait principle remains authoritative: unsupported or ambiguous type recovery is rejected.

## 8. Formal capture environment

The current formal model uses a single `CType` capture parameter. That is adequate for proving closure shape but not for directly representing a heterogeneous C++-style capture list.

The generalized semantic model should distinguish the capture environment type from callable argument/result `CType`s:

```lean
structure Lambda1 (Env : Type) (A R : CType) where
  capture : Env
  body : Env → A.denote → R.denote

structure Lambda2 (Env : Type) (A B R : CType) where
  capture : Env
  body : Env → A.denote → B.denote → R.denote
```

This is the minimal Core formal generalization. A concrete finite capture list can lower to a finite product/record `Env` without requiring the Core theorem to understand source variable names.

A later Extend-specific formal layer may model capture schemas explicitly as a finite typed list if parser/lowering conformance needs field-order/name proofs.

## 9. Core semantic theorems

The generalized model must retain the existing closure laws.

### 9.1 Invocation preservation

For a lowered lambda `L` and argument `x`:

```text
invoke(lower(L), x)
=
evalBody(L.capture, x)
```

For binary callables the analogous theorem applies to both arguments.

### 9.2 Signature preservation

```text
signature(erase(lower(L)))
=
inferred/resolved surface signature
```

Lambda syntax must never create a callable whose erased signature differs from the type-resolved surface declaration.

### 9.3 Empty-capture equivalence

A closure with an empty environment is observationally equivalent, for invocation and signature, to an ordinary typed callable with the same body.

### 9.4 Bind equivalence

Partial application of a binary function and a lambda capturing the bound value remain the same closure shape:

```text
bindLast(f, b)
≃
[b](A a) -> R { return f(a, b); }
```

The existing `lambda_bind_same_shape` theorem is the baseline for this property.

### 9.5 Capture-name irrelevance

Source capture field names do not participate in callable semantic identity. Renaming a captured local while preserving environment value/type and body binding must not change invocation semantics.

## 10. Surface lowering

For:

```c
[factor, offset](int x) -> long {
    return (long)x * factor + offset;
}
```

Extend conceptually produces:

```c
typedef struct {
    int factor;
    long offset;
} __cmeta_capture_N;

static long __cmeta_lambda_body_N(
    __cmeta_capture_N env,
    int x)
{
    return (long)x * env.factor + env.offset;
}
```

plus Core closure construction metadata/adapters equivalent to:

```text
capture = { factor, offset }
signature = int -> long
body = __cmeta_lambda_body_N
```

The generated C symbol names are implementation details and do not enter semantic identity.

## 11. Capture storage policy

The current runtime has an inline capture buffer. V1 lowering must respect the existing Core capture-size contract rather than invent a new lambda-specific storage policy.

If the finite environment exceeds the supported inline capacity, construction must fail at compile/lowering time unless a future Core closure-storage extension is designed independently.

The frontend must not silently allocate heap-owned closure environments because that would introduce new ownership/lifetime semantics.

## 12. Effects and properties

Lambda syntax does not infer semantic effects merely from the C body in v1.

The callable contract must be supplied by context or explicit annotation according to existing CMeta/Core rules. The frontend may reject a lambda when the target operator requires a contract that cannot be established.

Reference capture does not automatically imply a particular effect flag; effects describe callable behavior, not capture syntax alone.

## 13. Parser boundary

The frontend parser must understand enough syntax to identify:

```text
capture list
parameter declarations
return type
body source span
```

It does not need to become a full C parser for v1. With explicit capture lists and explicit types, the lambda body may remain an ordinary C source region subject to generated-name rewriting for captured variables.

The frontend must preserve source locations for diagnostics through source maps or `#line` directives.

## 14. Rewriting captured identifiers

Within the lambda body, a captured identifier resolves to the corresponding environment field.

Conceptually:

```text
factor
↓
env.factor
```

This requires lexical identifier resolution for explicitly declared captures, not unrestricted free-variable discovery.

The frontend must distinguish local parameter/local declarations from captured names so shadowing behaves deterministically. If v1 cannot resolve a shadowing case safely, it must reject it rather than perform textual substitution.

## 15. Strict-C11 equivalence

Every accepted surface lambda must have an equivalent strict-C11 construction path using Core callable/closure facilities.

This is a mandatory architecture invariant:

```text
remove cmc / Extend
→ lose C++-like spelling
→ do not lose closure semantics
```

CFlow `lambda1/lambda2` can serve as early compatibility evidence but must not remain the architectural owner of the closure ABI.

## 16. C implementation conformance target

A future conformance witness should exercise at least:

```text
empty capture
single by-value capture
multi-field by-value capture
init capture
reference/pointer capture
binary lambda
bind-equivalent lambda
```

For each witness:

```text
surface/lowered expectation
→ generated ordinary C
→ real cmeta_callable invocation
→ checked result/signature snapshot
→ Lean conformance theorem
```

Generated outputs must follow the project build-layout rule and live under the corresponding CMake binary `generated/` directory, not in the source tree.

## 17. Applicability boundary

The project may claim "C++-like lambda syntax is applicable" only when all of the following are demonstrated:

- generalized Lean environment model proves invocation/signature preservation;
- real Core closure construction supports equivalent environments;
- multi-field captures are exercised by real C witnesses;
- CFlow consumes the resulting Core callable without lambda-specific runtime logic;
- generated C is valid strict C11;
- GCC, Clang, and MSVC-compatible lowering rules are preserved;
- no full C++ compiler/runtime dependency is introduced.

## 18. Rejected designs

Rejected:

- implementing lambda as a CFlow-only feature;
- introducing a second callable ABI for Extend lambdas;
- treating C++ generic lambda templates as Core generics;
- using textual macro tricks to parse `[capture](args){body}` in `.c` files;
- implicit heap allocation for oversized captures;
- claiming reference lifetime safety without an ownership/lifetime system;
- inferring `[=]`/`[&]` through incomplete free-variable analysis;
- implementing `mutable` by silently mutating a `const cmeta_callable` capture buffer;
- making generated symbol names part of lambda semantic identity.

## 19. Success criterion

The design succeeds when the following commuting property is formally and operationally true:

```text
             surface evaluation
SurfaceLambda --------------------> Result
      |                                ^
      | lower                          | equal
      v                                |
Typed Core Closure ---- invoke --------+
      |
      | erase
      v
cmeta_callable
```

and when the surface feature remains only a finite syntax adapter:

```text
finite explicit captures
+ finite concrete parameter types
+ one concrete result type
+ ordinary C body
→ one finite Core callable
```

C++-like lambda syntax therefore increases source ergonomics, not CMeta Core expressive power.
