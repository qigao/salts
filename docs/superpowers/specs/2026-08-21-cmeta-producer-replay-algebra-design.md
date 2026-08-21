# CMeta Producer/Replay Algebra Design

## Status

Approved architecture spike. This document defines the formal target only; it does not change the production C preprocessor API.

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

## Semantic laws

The formal model must prove, for every finite producer sequence:

1. `Replay(Empty, M) = []`.
2. `Replay(Single(x), M) = [M(x)]`.
3. `Replay(Append(A, B), M) = Replay(A, M) ++ Replay(B, M)`.
4. `Count(S) = length(S)` where count is the fold of constant one over replay.
5. `Storage(S) = Replay(S, ARG) ++ [NONE]` has `length(Storage(S)) = Count(S) + 1`.
6. `StorageCount(S) = length(Storage(S)) - 1 = Count(S)`.
7. `CanRead(S, i) := i < StorageCount(S)` iff `i < length(S)`.
8. Mapping composes: `map g (map f S) = map (g ∘ f) S` at the semantic sequence level.

These laws establish a single source of truth:

```text
Producer S
   ├── Replay(mapper) -> generated output
   ├── Count          -> logical length
   └── Storage        -> mapped values ++ sentinel
                         └── length - 1 -> same logical length
```

## Strict-C11 applicability boundary

The design does **not** claim a fully generic `CMETA_PP_FOR_EACH(M,C)` with an empty raw `__VA_ARGS__` can be implemented portably in strict C11 without auxiliary information. Raw variadic lists remain an adapter/backend concern.

The applicability witness must instead demonstrate that producer macros themselves are valid under strict C11 for both zero and non-zero sequences and that the same producer can derive ordered replay, count, and storage facts.

## Nested replay boundary

This proof does not remove the need for distinct expansion lanes (`A/B/C`) used to avoid macro self-suppression during nested replay. Zero-or-more representation and nested same-macro expansion are separate problems.

The result of this work may justify moving arity counting and `FOR_EACH_1..N` families out of the semantic Core, but it does not by itself prove those backend mechanisms can all be deleted from `pp.h`.

## Non-goals

- No production changes to `cmeta/include/cmeta/pp.h`.
- No changes to `utils/include/fmt.h` or `utils/src/fmt.c`.
- No C23 `__VA_OPT__` dependency.
- No claim that arbitrary raw `__VA_ARGS__` is a semantic finite-list representation.
- No redesign of nested replay lanes in this slice.

## Verification

The proof slice is complete only when:

- the Lean model contains the laws above without `axiom`, `constant`, `sorry`, or `admit`;
- a strict-C11 witness demonstrates empty and non-empty producer macros, ordered replay, producer-derived count, and storage-derived count;
- the existing formal GitHub workflow completes successfully on the latest head.