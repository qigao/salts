# cmeta-lower

`cmeta-lower` is the host-side source lowerer for the small CMeta source
extensions that must compile to ordinary C.

It is deliberately **not** a general C parser or a runtime reflection
interpreter. The accepted source surface is finite and fail-closed.

## Current lowering

### Typed receiver calls

Given a CMeta typed declaration and a concrete local:

```c
typed(List, IntList, int);

IntList list = {0};
list.add(10);
```

the lowerer emits the direct typed call:

```c
IntList_add(&list, 10);
```

No runtime method lookup is generated.

### Ownership-state proof

Version 2 adds the first compiler-private ownership-state slice:

```c
owned(IntList) source = {0};
IntList destination = {0};

destination = move(source);
```

which lowers to ordinary C:

```c
IntList source = {0};
IntList destination = {0};

destination = source;
```

while the compiler state transitions:

```text
source: LIVE_OWNED -> MOVED
```

A second move or any later value use of `source` is rejected.

Lexical shadowing uses the same symbol stack as typed receiver lowering, so an
inner owned variable has independent move state from an outer variable with the
same name.

## Important Phase B boundary

The ownership syntax currently proves **who may still use the source value**.
It does not yet insert automatic cleanup and does not claim generic CMeta
`DataDesc` move construction.

In particular:

- `owned(Type)` currently accepts one simple named C type;
- `move(name)` lowers to the ordinary C expression `name`;
- no `destroy`, `release`, or Plugin lease release is inserted yet;
- no ownership is inferred from pointers, names, or ABI carriers.

This phase is appropriate for proving handle-like ownership transfer and the
compiler state machine. The RAII cleanup phase must consume canonical CMeta
lifecycle contracts rather than guessing cleanup from the source type spelling.

## Invariants

- strings, comments, and preprocessor text are preserved;
- invalid admitted ownership syntax fails closed;
- generated output remains portable C;
- runtime CMeta method resolution is never introduced by this tool.
