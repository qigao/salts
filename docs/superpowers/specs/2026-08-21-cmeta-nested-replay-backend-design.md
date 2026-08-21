# CMeta Nested Replay Backend Design

## Status

Approved proof slice. This document defines the target for proving whether the current A/B/C replay lanes are semantic necessities or replaceable strict-C11 backend machinery. It does not change production `cmeta/include/cmeta/pp.h`.

## Goal

Prove that nested producer semantics are lane-free while strict-C11 macro self-suppression is a backend constraint, then determine whether the current `FEA/FEB/FEC × 1..16` families can be replaced by a smaller deferred-expansion mechanism.

## Semantic model

For finite producers `A` and `B`, nested replay is the ordered Cartesian map:

```text
NestedReplay(A, B, f)
  = concatMap (a -> map (b -> f(a,b)) B) A
```

The semantic result does not contain a lane, expansion family, or arity dispatch parameter.

Required laws:

```text
NestedReplay([], B, f) = []
NestedReplay(A, [], f) = []
length(NestedReplay(A,B,f)) = length(A) * length(B)
length(NestedReplay(A,A,f)) = length(A)^2
```

Nested same-producer replay is therefore semantically ordinary finite product construction.

## Strict-C11 backend fact

ISO-style preprocessors suppress a macro while rescanning that macro's own replacement context. Therefore direct same-producer nesting such as:

```c
#define P(M) M(1) M(2)
#define INNER(x) + 1
#define OUTER(x) + (0 P(INNER))
enum { N = 0 P(OUTER) };
```

leaves the inner `P(INNER)` unexpanded and is not valid C. This is a preprocessor expansion-state limitation, not a Producer semantic limitation.

Different producer identities do not have this conflict:

```c
P(OUTER_USING_Q)
```

may replay `Q` directly while `P` is active because `Q` is not the disabled macro.

## Deferred same-producer candidate

A smaller backend can postpone the recursive producer call until the outer producer is no longer disabled. The candidate mechanism uses:

```text
EMPTY
DEFER
OBSTRUCT
EVAL_k
producer indirect thunk
```

For producer `P`:

```c
#define P(M) M(1) M(2)
#define P_INDIRECT() P
```

A mapper can emit an obstructed `P_INDIRECT()(... )` call. Repeated evaluation rescans it after the outer `P` context has ended, allowing the same producer to replay again.

The important complexity change is:

```text
current backend size ~ lane_count * supported_arity
candidate backend size ~ fixed defer/eval machinery + one tiny indirect thunk per self-nestable producer
```

The evaluation budget depends on required nesting depth, not producer element count.

## Applicability obligations

The proof slice must demonstrate all of the following under exact strict C11:

1. direct same-producer nesting fails as expected;
2. direct distinct-producer nesting succeeds;
3. deferred same-producer nesting succeeds;
4. the same two-element producer gives counts 4, 8, and 16 for nesting depths 2, 3, and 4;
5. no `NARG` or arity-specific `FOR_EACH_N` facility is needed by the deferred producer path;
6. existing production `pp.h` remains unchanged in this slice.

## Architecture consequence if obligations hold

The semantic Core remains:

```text
Producer / Replay / finite product
```

while nested expansion becomes a backend concern. The current A/B/C × arity families would no longer be justified as semantic structure and would become candidates for replacement by a depth-bounded deferred expansion backend.

This proof does **not** yet authorize deleting A/B/C from production. Before production migration, the candidate must be checked against every existing nested schema consumer and the required maximum nesting depth must be made explicit.

## Non-goals

- No production `pp.h` edits.
- No claim of unbounded preprocessor recursion.
- No C23 `__VA_OPT__` dependency.
- No raw-empty-variadic redesign.
- No migration of existing Schema/Enum/Struct consumers in this slice.
