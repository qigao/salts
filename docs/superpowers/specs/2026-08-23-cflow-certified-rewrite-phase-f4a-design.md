# CFlow Certified Rewrite Phase F-4A Design

## Context

The v1 Lean calculus proves R1–R10 semantic rewrites and classifies R11–R14
as execution refinements. The C optimizer additionally removes an adjacent
duplicate Map when the callable is pure, total, endomorphic and declares the
`IDEMPOTENT` property. That implementation rule is not one of R1–R15 and had
no Lean theorem.

A CMeta property is a positive declaration, not a mathematical proof. Treating
the property bit alone as the equation `f (f x) = f x` would make the formal
model accept a dishonest callable such as `x + 1` merely because its metadata
claims idempotence.

## Decision

Keep the stable R1–R15 catalogue unchanged and add a separate proof-carrying
optimizer relation.

`IdempotentLaw meaning` is the semantic equation required for duplicate Map
elimination. `IdempotentEndomapPremises` packages the callable meaning, PURE
effect, TOTAL declaration, IDEMPOTENT declaration and the independent law.
Neither the declaration nor the law is inferred from the other.

`CertifiedRewrite before after` is an indexed relation over complete
`StreamResult` values. Its constructors are:

- `refl`, for an empty certificate;
- `idempotentMap`, for one adjacent duplicate Map elimination carrying complete
  premises; and
- `trans`, for certificate composition.

The theorem `map_idempotent_elimination` proves the single optimizer rule.
`certified_rewrite_preserves_observations` proves every finite composition by
induction. Its conclusion is equality of the complete `StreamResult`, which is
stronger than value-only equality: construction effects, ordered values,
terminal outcome and ownership-safety state are all retained.

## Trust Boundary

Lean proves the rule schema conditional on `IdempotentLaw`; it does not inspect
or verify C machine code. CMeta metadata remains a trusted declaration made by
the callable producer. A later C certificate checker must verify that the
optimizer applied the declared rule to the exact source and destination Graph
coordinates, while a trusted callable-law registry connects the C callable
identity to the corresponding Lean premise.

The eventual bridge is therefore:

```text
callable law witness
  + normalized Graph
  + optimizer trace
  + optimized Graph
      -> C certificate checker
      -> instance of Lean CertifiedRewrite
      -> observational equivalence theorem
```

The compiler's choice to inline a StaticTarget is outside this semantic trust
boundary. Lean proves that StaticTarget lowering preserves semantics; object-code
inspection and benchmarks determine whether inlining occurred and whether it is
profitable.

## Ownership and Complexity

The Lean objects are immutable proof terms and own no C storage. A certificate
does not execute callbacks or advance a stream. A trace with `n` rewrite steps
has O(n) proof size; the preservation proof is linear in the certificate
composition. The per-rule list proof is O(m) in the finite stream length used by
the denotation.

The later C trace is control-plane data. It must be bounded by source Graph
semantic stages, own its event storage, borrow Graphs only during validation,
and never enter Direct, Plan or Kernel item-processing loops.

## Compatibility

This phase adds Lean definitions, theorems and tests only. It does not renumber
R1–R15, change C ABI, alter optimizer behavior, change Graph ownership or add a
runtime fallback. Existing proofs remain source-compatible.

## Verification

- A real clamp endomap supplies both the declaration and the mathematical law.
- Two adjacent applications reduce to one by the single-rule theorem.
- Three applications reduce to one through two composed certificate steps.
- A successor function can declare IDEMPOTENT but is proved not to satisfy
  `IdempotentLaw`, demonstrating that metadata alone cannot construct the
  certificate premises.
- `lake test` builds all Phase A–F Lean targets without warnings.

## Deferred F-4B Work

The C optimizer still needs an owned proof trace, stable rule identifiers,
source/destination coordinate binding, stale-Graph rejection and an AOT matcher
entry point that consumes the checked certificate. F-4B must retain the old
optimizer API and Direct hot path unchanged, and must use differential execution
tests in addition to trace-structure assertions.

## Rollback

Remove the Phase F test import/file and the new law, premises, relation and
theorems. R1–R15 and all existing calculus phases remain intact. No C rollback
is required.
