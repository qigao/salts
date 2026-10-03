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

## Straight-line deterministic cleanup

Version 4 adds the first cleanup-lowering slice.

An admitted owned declaration is normalized into a cleanup-safe canonical zero
slot:

```c
owned(IntList) values;
```

becomes:

```c
IntList values = {0};
```

The source may also write explicit `= {0}`; other initializers fail closed in
this first slice.

At normal lexical scope exit, every still-LIVE owned value is destroyed in
reverse declaration order through the lifecycle accessor already recorded from
`typed(...)`:

```c
cmeta_data_value_destroy(IntList_cmeta_data(), &values);
```

A source consumed by `move(name)` is MOVED and receives no source-scope
cleanup.

This is deliberately a **straight-line ownership subset**. While a LIVE owned
value exists, the lowerer rejects ownership-relevant control flow it cannot yet
join soundly: early return, outer conditional/loop/switch flow, goto,
break/continue, and short-circuit/ternary expressions. Owned values scoped
entirely inside a branch or loop body remain admissible when their lifetime
does not escape that lexical path.

The compiler decides **when** cleanup occurs; canonical CMeta DataDesc lifecycle
decides **how** cleanup occurs. No destructor name or allocator policy is
inferred.

## Invariants

- strings, comments, and preprocessor text are preserved;
- invalid admitted ownership syntax fails closed;
- generated output remains portable C;
- normal owned cleanup is reverse lexical order;
- moved sources are not cleaned by the source scope;
- unsupported ownership-sensitive control flow fails closed;
- runtime CMeta method resolution is never introduced by this tool.
