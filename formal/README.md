# CMeta Lean proofs

This directory is a formalization spike for the finite, typed meta-programming
model already visible in CMeta/CFlow.

## What is proved

- finite generation has an exact structural cardinality;
- trait lookup/inference is deterministic once the trait environment is fixed;
- typed unary/binary/generator callables preserve their declared signatures;
- capturing lambdas are closures (`environment + body`) and erasure preserves ABI signature;
- `cmeta_bind` is ordinary last-argument partial application;
- higher-order unary composition is representable without a new callable ABI;
- finite operator dispatch is sound: a successful result comes from a matching rule;
- a policy-respecting dispatch table cannot select a signature outside the operator policy.

## Mapping to the C implementation

- `CMeta.Calculus`: `cmeta/pp.h`, schema replay, signature product generation;
- `CMeta.Traits`: `_Generic`/descriptor-based type recovery assumption;
- `CMeta.Callable`: `cmeta_fn`, `cmeta_callable`, resolved `cmeta_sig`;
- `CMeta.Lambda`: `lambda1`, `lambda2`, `cmeta_bind` capture semantics;
- `CMeta.Dispatch`: CFlow signature lists and generated dispatch.

The proof intentionally abstracts away C object layout, `memcpy`, `sizeof`,
`_Alignof`, and preprocessor token mechanics.  Those remain compiler/ABI
obligations.  The formal claim is about the semantic capability obtained once
traits resolve types and the runtime erasure layer validates the corresponding
signature.

Run locally:

```sh
cd formal
lake build --wfail
```
