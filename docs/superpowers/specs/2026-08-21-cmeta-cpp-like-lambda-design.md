# CMeta C++-Like Lambda Design

Status: draft for user review  
Date: 2026-08-21  
Branch: `leanv4`  
Parent architecture: `2026-08-21-cmeta-hexagonal-architecture-design.md`

## 1. Goal

CMeta Extend may expose a C++-like lambda surface syntax, but lambda semantics remain a CMeta Core callable/closure capability. The frontend must lower finite explicit capture lists and typed parameters to the existing `cmeta_callable` representation without introducing a second callable ABI, unrestricted templates, compile-time execution, ownership semantics, or a C++ runtime dependency.

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

Therefore this design does not invent closure semantics. It generalizes the formal capture environment and gives CMeta Extend a source spelling for those semantics.

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

V1 supports explicit by-value and init captures:

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
```

V1 requires:

```text
explicit capture list
explicit parameter types
explicit return type
finite capture count
capture environment fitting the Core closure-storage contract
```

This avoids requiring the frontend to implement full C free-variable analysis, C return-type inference, or body-level identifier rewriting.

The spelling is intentionally close to C++ lambda syntax, but semantic compatibility is defined by this specification, not by the full C++ standard.

## 5. Capture semantics

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

The generated storage is conceptually:

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

## 6. Body-preserving lowering

V1 deliberately avoids rewriting captured identifiers inside the C body.

For:

```c
[factor, offset](int x) -> long {
    return (long)x * factor + offset;
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
    int x)
{
    return (long)x * factor + offset;
}
```

The invoke adapter unpacks the environment and calls:

```c
__cmeta_lambda_body_N(env.factor, env.offset, x)
```

Therefore the original body source can remain unchanged. The frontend only needs to recognize the lambda boundary and its explicit declaration surface; it does not need free-variable discovery or general C identifier substitution for v1.

Generated helper symbol names are implementation details and do not enter callable semantic identity.

## 7. V1 exclusions

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

Reference capture is not rejected as a long-term capability. It is postponed until Extend has a token/scope-aware body transformation capable of preserving C++-like lvalue spelling without unsafe textual macros. Its lifetime and synchronization safety will remain ordinary C obligations even then.

## 8. Contextual `auto` after v1

CMeta may later support contextual `auto` where the consumer already determines exactly one type.

Example:

```c
stream<int>
  |> map([](auto x) -> long { return x * 2; });
```

If the target operator requires exactly `int -> long`, the frontend may resolve `auto` to `int` before Core lowering.

This is permitted only when resolution is finite and unique. It must lower to one concrete callable signature. It must not create a C++-style generic closure with a template call operator.

The existing trait principle remains authoritative: unsupported or ambiguous type recovery is rejected.

## 9. Formal capture environment

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

`Env : Type` is a semantic abstraction, not permission for unbounded source generation. Extend may construct only finite explicit capture schemas, and each accepted schema lowers to one finite generated C record/product that satisfies the Core storage limit.

This is the minimal Core formal generalization. A later Extend-specific formal layer may model capture schemas explicitly as a finite typed list when parser/lowering conformance needs field-order/type proofs.

## 10. Core semantic theorems

The generalized model must retain and extend the existing closure laws.

### 10.1 Invocation preservation

For a lowered lambda `L` and argument `x`:

```text
invoke(lower(L), x)
=
evalBody(L.capture, x)
```

For binary callables the analogous theorem applies to both arguments.

### 10.2 Signature preservation

```text
signature(erase(lower(L)))
=
resolved surface signature
```

Lambda syntax must never create a callable whose erased signature differs from the type-resolved surface declaration.

### 10.3 Empty-capture equivalence

A closure with an empty environment is observationally equivalent, for invocation and signature, to an ordinary typed callable with the same body.

### 10.4 Bind equivalence

Partial application of a binary function and a lambda capturing the bound value remain the same closure shape:

```text
bindLast(f, b)
≃
[b](A a) -> R { return f(a, b); }
```

The existing `lambda_bind_same_shape` theorem is the baseline for this property.

### 10.5 Capture packaging transparency

Packing a finite set of capture values into an environment and unpacking them as hidden helper parameters must preserve the body result.

Conceptually:

```text
body(c1, c2, ..., x)
=
body(unpack(pack(c1, c2, ...)), x)
```

This is the central theorem connecting explicit C++-like capture lists to the generalized Core environment model.

### 10.6 Capture-name irrelevance at Core level

Source capture names are an Extend binding concern. Once lowered, Core semantics depend on environment values/types and body binding, not source identifier spellings or generated helper names.

## 11. Capture storage policy

The current runtime has an inline capture buffer. V1 lowering must respect the existing Core capture-size contract rather than invent a new lambda-specific storage policy.

If the finite environment exceeds the supported inline capacity, construction must fail at compile/lowering time unless a future Core closure-storage extension is designed independently.

The frontend must not silently allocate heap-owned closure environments because that would introduce new ownership/lifetime semantics.

## 12. Effects and properties

Lambda syntax does not infer semantic effects merely from the C body in v1.

The callable contract must be supplied by context or explicit annotation according to existing CMeta/Core rules. The frontend may reject a lambda when the target operator requires a contract that cannot be established.

Capture syntax alone does not determine purity, determinism, totality, or failure behavior.

## 13. Parser boundary

For v1 the frontend needs to parse only enough lambda syntax to identify:

```text
explicit capture declarations
parameter declarations
explicit return type
balanced body source span
```

The lambda body remains ordinary C source text emitted inside the generated helper body. No free-variable analysis and no captured-identifier rewriting is required for by-value/init capture because capture names become hidden leading helper parameters.

The frontend still needs normal lexical balancing for comments, strings, braces, and tokens so it can locate the body boundary safely. This is compatible with the planned `re2c + handwritten parser` island-grammar approach and does not require a full C grammar.

Source locations must be preserved through source maps or `#line` directives.

## 14. Strict-C11 equivalence

Every accepted surface lambda must have an equivalent strict-C11 construction path using Core callable/closure facilities.

This is a mandatory architecture invariant:

```text
remove cmc / Extend
→ lose C++-like spelling
→ do not lose closure semantics
```

CFlow `lambda1/lambda2` can serve as early compatibility evidence but must not remain the architectural owner of the closure ABI.

## 15. C implementation conformance target

A future conformance witness should exercise at least:

```text
empty capture
single by-value capture
multi-field by-value capture
init capture
binary lambda
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

Generated outputs must follow the project build-layout rule and live under the corresponding CMake binary `generated/` directory, not in the source tree.

Reference capture gets a separate later applicability gate because it requires stronger source binding/lowering semantics.

## 16. Applicability boundary

The project may claim "C++-like lambda v1 syntax is applicable" only when all of the following are demonstrated:

- generalized Lean environment model proves invocation/signature preservation;
- capture pack/unpack transparency is formally established;
- real Core closure construction supports equivalent finite environments;
- multi-field captures are exercised by real C witnesses;
- init capture is evaluated exactly once at construction in a real witness;
- CFlow consumes the resulting Core callable without lambda-specific runtime logic;
- generated C is valid strict C11;
- GCC, Clang, and MSVC-compatible lowering rules are preserved;
- generated artifacts remain in the CMake binary tree;
- no full C++ compiler/runtime dependency is introduced.

## 17. Rejected designs

Rejected:

- implementing lambda as a CFlow-only feature;
- introducing a second callable ABI for Extend lambdas;
- treating C++ generic lambda templates as Core generics;
- using preprocessor tricks to parse `[capture](args){body}` in ordinary `.c` files;
- requiring full free-variable analysis for v1;
- textual macro substitution of captured identifiers;
- implicit heap allocation for oversized captures;
- claiming reference lifetime safety without an ownership/lifetime system;
- implementing `mutable` by silently mutating a `const cmeta_callable` capture buffer;
- making generated symbol names part of lambda semantic identity.

## 18. Success criterion

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
+ finite concrete parameter types
+ one concrete result type
+ ordinary C body
→ one finite Core callable
```

C++-like lambda syntax therefore increases source ergonomics, not CMeta Core expressive power.
