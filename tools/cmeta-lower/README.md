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

Version 2 adds the first compiler-private ownership-state slice. Version 3
binds every admitted owned typed value to the generated canonical DataDesc
accessor from its `typed(...)` declaration:

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

## Canonical lifecycle binding

A source-owned type must now be a concrete type discovered from a supported
`typed(...)` declaration. That declaration generates one uniform accessor:

```c
IntList_cmeta_data()
IntMap_cmeta_data()
```

The accessor returns the already-existing canonical `cmeta_data_desc`;
there is no compiler-private lifecycle registry and no runtime lookup.

The lowerer records that accessor with the owned symbol. A source such as
`owned(MissingResource) value` fails closed because the compiler cannot prove
which lifecycle provider owns cleanup.

## Important Phase C1 boundary

Lifecycle **binding** is now proven, but automatic cleanup is not inserted yet.

In particular:

- `owned(Type)` accepts one simple named concrete type with canonical typed
  lifecycle metadata;
- `move(name)` still lowers to the ordinary C expression `name`;
- no `destroy`, `release`, or Plugin lease release is inserted yet;
- no ownership/lifecycle is inferred from pointers, names, ABI carriers, or
  guessed `Type_destroy` symbols.

The next cleanup phase can emit canonical CMeta lifecycle calls through the
recorded DataDesc accessor rather than discovering a second cleanup model.

## Invariants

- strings, comments, and preprocessor text are preserved;
- invalid admitted ownership syntax fails closed;
- generated output remains portable C;
- runtime CMeta method resolution is never introduced by this tool.
