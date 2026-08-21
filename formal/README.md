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

## What is proved

- finite generation has an exact structural cardinality;
- trait lookup/inference is deterministic once the trait environment is fixed;
- typed unary/binary/generator callables preserve their declared signatures;
- capturing lambdas are closures (`environment + body`) and erasure preserves ABI signature;
- `cmeta_bind` is ordinary last-argument partial application;
- higher-order unary composition is representable without a new callable ABI;
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
- `CMeta.Callable`: `cmeta_fn`, `cmeta_callable`, resolved `cmeta_sig`;
- `CMeta.Lambda`: `lambda1`, `lambda2`, `cmeta_bind` capture semantics;
- `CMeta.Dispatch`: CFlow signature lists and generated dispatch.

The proof intentionally abstracts away some compiler/ABI mechanics such as raw object layout and preprocessor token implementation. Real C witnesses provide implementation-conformance evidence at those boundaries.

Run locally:

```sh
cmake --preset formal-linux
cmake --build --preset build-formal-linux
cd formal
lake build --wfail
```
