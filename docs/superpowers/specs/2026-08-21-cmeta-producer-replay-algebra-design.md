# CMeta Producer/Replay Algebra Design

## Status

**Implemented and formally verified as a semantic/proof slice.**

The production C preprocessor API has not been migrated. The verified result is that `Producer`/`Replay`, rather than raw `__VA_ARGS__`, is sufficient as the semantic representation of a finite zero-or-more sequence in CMeta.

Authoritative verification:

```text
GitHub Actions: Lean proofs
run #367
run id: 32447896972
head: 7879d525412c9159894d740cb7c49a07b0d7ea24
result: completed / success
```

## Goal

Make `Producer` rather than raw `__VA_ARGS__` the semantic representation of a finite zero-or-more sequence in CMeta. `Replay` consumes a producer without first counting its arity.

## Core idea

A finite sequence `S = [x0, ..., xn-1]` is modeled extensionally by the ordered sequence of mapper applications it produces:

```text
Replay([], M) = ε
Replay(x :: xs, M) = M(x) ⊕ Replay(xs, M)
```

where `ε` is empty output and `⊕` is ordered concatenation.

In strict C11, a producer is represented by a function-like macro accepting a mapper:

```c
#define EmptySchema(M)
#define ThreeSchema(M) M(A) M(B) M(C)
#define Replay(schema, M) schema(M)
```

This representation supports zero elements without asking the C11 preprocessor to distinguish an empty raw variadic argument list.

## Formal model

`formal/CMeta/Producer.lean` defines one finite producer algebra:

```text
replay       : mapper -> Producer -> output sequence
append       : Producer -> Producer -> Producer
count        : Producer -> Nat
storage      : mapper -> sentinel -> Producer -> storage
storageCount : storage -> Nat
canRead      : storage -> index -> Prop
```

The kernel-checked laws are:

1. `Replay(Empty, M) = []`.
2. `Replay(Single(x), M) = [M(x)]`.
3. `Replay(Append(A, B), M) = Replay(A, M) ++ Replay(B, M)`.
4. `Count(S) = length(S)` where count is the fold of constant one over replay.
5. `Storage(S) = Replay(S, ARG) ++ [NONE]` has `length(Storage(S)) = Count(S) + 1`.
6. `StorageCount(S) = length(Storage(S)) - 1 = Count(S)`.
7. `CanRead(S, i) := i < StorageCount(S)` iff `i < length(S)`.
8. A logically readable index is physically inside normalized storage.
9. The sentinel index is rejected by the logical guard.
10. Mapping composes: `map g (map f S) = map (g ∘ f) S` at the semantic sequence level.

These laws establish a single source of truth:

```text
Producer S
   ├── Replay(mapper) -> generated output
   ├── Count          -> logical length
   └── Storage        -> mapped values ++ sentinel
                         └── length - 1 -> same logical length
```

Therefore arity is a derived property of a producer, not a semantic dispatch dimension.

## Strict-C11 applicability evidence

`formal/cmeta_producer_replay_witness.c` verifies the producer spelling with empty, one-element, three-element, and appended producers.

The witness is deliberately stronger than a normal compile smoke test:

```c
#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "producer replay applicability witness must compile as exact C11"
#endif

#undef CMETA_PP_NARG
#define CMETA_PP_NARG(...) CMETA_PRODUCER_WITNESS_FORBIDS_NARG
#undef CMETA_PP_FOR_EACH
#define CMETA_PP_FOR_EACH(...) CMETA_PRODUCER_WITNESS_FORBIDS_FOR_EACH
```

So the verified `Replay(PRODUCER_*, mapper)` path cannot obtain its zero/non-zero or arity behavior from the existing `CMETA_PP_NARG` or public `CMETA_PP_FOR_EACH` backend.

The same producer is replayed independently to derive:

```text
ordered values
producer count       = 0 + 1 + ...
normalized storage   = replayed values + sentinel
storage-derived count = ARRAY_COUNT(storage) - 1
```

The witness checks that producer-derived count and storage-derived count agree and that producer append preserves order.

Run #367 passed both `Build C conformance witnesses` and `Execute applicability probes`, followed by the complete Lean 4.30 `lake build --wfail` kernel check.

## What this proves for CMeta

The semantic finite-sequence kernel can be expressed as:

```text
Producer
Replay
Append
Map
Fold / Count
Storage / Guard derivations
```

The following mechanisms are therefore **not semantic primitives**:

```text
NARG
FOR_EACH_1..N arity families
HAS_ARGS / IS_EMPTY style raw-token detection
consumer-specific WRAP_0..N dispatch
```

They may still be useful implementation mechanics for adapters that accept inline raw `__VA_ARGS__`, but higher-level CMeta domains do not need to define their semantics in terms of them.

This gives the desired architecture split:

```text
CMeta semantic core
    Producer / Replay algebra

Adapters / backends
    raw __VA_ARGS__ -> producer-compatible expansion
    one-or-more FOR_EACH implementation
    optional newer-standard __VA_OPT__ adapter
    nested expansion lanes
```

Named Schema/Enum/Struct/Operator/container sources, generated metadata, and future generated callable/capture schemas can therefore be producer-first without carrying an arity family in their semantic model.

## Strict-C11 applicability boundary

This result does **not** claim a fully generic `CMETA_PP_FOR_EACH(M,C)` with an empty raw `__VA_ARGS__` can be implemented portably in strict C11 without auxiliary information.

Raw variadic lists remain an adapter/backend concern. In particular, an API spelling such as:

```c
fmt(...)
fmt(..., a)
fmt(..., a, b)
```

still needs a strict-C11 raw-varargs adapter if that exact source form is retained. The Producer theorem shows that this difficulty does not belong in CMeta's semantic list model.

## Nested replay boundary

This proof does not remove the need for distinct expansion lanes (`A/B/C`) currently used to avoid macro self-suppression during nested replay. Zero-or-more representation and nested same-macro expansion are separate problems.

The result justifies treating arity counting and `FOR_EACH_1..N` families as backend/adaptor mechanics. It does not yet prove that every such implementation macro can be deleted from the current `pp.h`.

## Remaining work

The following are deliberately still open:

```text
raw variadic empty adapter
nested replay lane simplification
production pp.h migration
FMT public raw-varargs spelling
```

Any production simplification should be a separate change driven by these proved Producer laws rather than by ad-hoc preprocessor tricks.
