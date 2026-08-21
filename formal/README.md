# CMeta Lean proofs

This directory contains the formal model and the hand-written C conformance witnesses for the finite, typed CMeta/CFlow semantics.

## Source / generated boundary

The source tree contains only hand-written proof and witness sources:

```text
formal/
  CMeta/*.lean
  *_witness.c
  type_identity_*.c
```

Files named `*_witness.c` are ordinary checked-in C programs. They execute the real CMeta/CFlow implementation and emit Lean snapshot text; they are not generated C sources.

All transient generated outputs belong to the CMake binary tree:

```text
${CMAKE_CURRENT_BINARY_DIR}/generated/
```

For the `formal-linux` preset this is:

```text
build/formal-linux/formal/generated/
```

CI writes generated Lean snapshots there and compares them with the checked baselines under `formal/CMeta/*GeneratedC.lean`.

Generated C sources, if introduced by future CMeta lowering/code generation, must follow the same rule: they are emitted below the corresponding binary directory and are never written into the source tree.

## Unified callable and lambda model

The Core formal model now uses one finite argument schema:

```text
HArgs<Args>
Callable<Args,R>
Lambda<Env,Args,R>
arity = Args.length
```

There are no Lean `Callable1/2/3/...` or `Lambda1/2/3/...` semantic types or compatibility aliases. CI rejects those names from the formal source tree.

Unary and binary helpers such as `Callable.ofUnary`, `Callable.invoke1`, `Callable.ofBinary`, and `Callable.invoke2` are construction/invocation adapters over the one `Callable` type. They do not create separate semantic callable families.

`bindLast` is defined over an arbitrary finite prefix:

```text
Callable<Args ++ [B],R> + B
        ↓
Callable<Args,R>
```

using heterogeneous argument `append/snoc`, so partial application is no longer modeled as a special binary-only semantic operation.

Generator remains a separate protocol. Its output-buffer/cursor implementation parameters do not make it a higher-arity value lambda.

## What is proved

- finite generation has an exact structural cardinality;
- trait lookup/inference is deterministic once the trait environment is fixed;
- one finite-arity typed callable model covers zero, unary, binary, and higher finite argument schemas;
- current unary/binary C backend signature erasure is a projection from that unified model;
- capturing lambda is `environment + HArgs body`, and environment erasure preserves invocation;
- lambda beta semantics is proved once for arbitrary finite `Args`;
- `bindLast` is ordinary closure formation over `Args ++ [B]`;
- higher-order unary composition is a specialization of the unified callable model;
- finite operator dispatch is sound: a successful result comes from a matching rule;
- a policy-respecting dispatch table cannot select a signature outside the operator policy;
- structural TypeId and descriptor-bridge semantics are modeled separately from descriptor addresses;
- descriptor semantic equality is reflexive, symmetric and transitive in the formal model;
- real multi-TU witnesses demonstrate descriptor-address independence.

## Mapping to the C implementation

- `CMeta.Calculus`: `cmeta/pp.h`, schema replay, signature product generation;
- `CMeta.TypeIdentity`: structural TypeId and finite generic constructor identity;
- `CMeta.DescriptorBridge`: semantic descriptor equality and legacy/structural isolation;
- `CMeta.Traits`: `_Generic`/descriptor-based type recovery assumption;
- `CMeta.Callable`: unified finite-argument semantics over the existing `cmeta_callable` erased ABI;
- `CMeta.Lambda`: general closure/environment semantics; CFlow `lambda1/lambda2` macros are consumer-side C spellings rather than formal type families;
- `CMeta.Dispatch`: CFlow signature lists and generated dispatch.

The proof intentionally abstracts away some compiler/ABI mechanics such as raw object layout and preprocessor token implementation. Real C witnesses provide implementation-conformance evidence at those boundaries.

Run locally:

```sh
cmake --preset formal-linux
cmake --build --preset build-formal-linux
cd formal
lake build --wfail
```

## Lean module-system migration status

Plan A (M1–M6) of the Lean 4.30 module-system migration is implemented and exact-head verified. The CFlow semantic spine now uses explicit module visibility, `CMeta.PublicProof` exposes the curated semantic vocabulary plus six stable end-to-end wrapper theorems, and representative Graph, Lowering, Optimize, Execution, and EndToEnd proof plumbing is not visible to a downstream client importing only `CMeta.PublicProof`.

The real-C generated Lean snapshots for the direct, structured, and optimizer conformance paths are module-framed by their authoritative C witnesses and remain protected by byte-for-byte CI regeneration checks. `CType.denote` is intentionally `@[expose] public` because executable conformance models must reduce the logical CType universe to host value types across module boundaries.

Plan B is still pending. It will migrate the independent Producer / Replay / Registry / LanguageSpec tree, create the final internal build aggregator, and convert the root `CMeta` module without reducing kernel-check coverage.
