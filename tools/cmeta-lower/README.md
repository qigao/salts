# cmeta-lower

`cmeta-lower` is the host-side source lowerer for the small CMeta source
extensions that must compile to ordinary C.

It is deliberately **not** a general C parser or a runtime reflection
interpreter. The accepted source surface is finite and fail-closed. Public CMeta source extensions in this frontend use the `cmeta_` prefix to avoid claiming ordinary C identifiers.

## Current lowering

Version 6 generalizes ownership lifecycle discovery with explicit
`CMETA_LIFECYCLE(Type, accessor)` bindings. Lifecycle-only types do not gain
typed receiver syntax or a generic-container owner; the binding supplies
cleanup authority only.

### Ordinary typed operations

Container/data-structure operations remain ordinary concrete C calls in both
`.c` and `.cmeta.c`:

```c
IntList_init(&list, 16);
IntList_add(&list, 10);
IntList_destroy(&list);
```

`cmeta-lower` does not provide receiver syntax (`list.add(...)`) or generic
operation syntax (`List_add(...)`). CMeta source lowering is reserved for
semantic features that C cannot express directly, such as ownership state and
automatic lifecycle placement.

### Ownership-state proof

Version 2 adds the first compiler-private ownership-state slice. Version 3
binds every admitted cmeta_owned typed value to the generated canonical DataDesc
accessor from its `typed(...)` declaration:

```c
cmeta_owned(IntList) source = {0};
IntList destination = {0};

destination = cmeta_move(source);
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

Lexical shadowing uses the ownership symbol stack, so an inner cmeta_owned
variable has independent move state from an outer variable with the same name.

## Canonical lifecycle binding

A source cmeta_owned type must now be a concrete type discovered from a supported
`typed(...)` declaration. That declaration generates one uniform accessor:

```c
IntList_cmeta_data()
IntMap_cmeta_data()
```

The accessor returns the already-existing canonical `cmeta_data_desc`;
there is no compiler-private lifecycle registry and no runtime lookup.

The lowerer records that accessor with the cmeta_owned symbol. A source such as
`cmeta_owned(MissingResource) value` fails closed because the compiler cannot prove
which lifecycle provider owns cleanup.

Non-container providers can bind the same canonical authority explicitly:

```c
CMETA_LIFECYCLE(MyOwner, MyOwner_cmeta_data);

cmeta_owned(MyOwner) value;
```

`CMETA_LIFECYCLE(Type, accessor)` is compile-time source metadata only. The
lowerer records the explicit `cmeta_data_desc` accessor and generated cleanup
calls it directly; no runtime lifecycle registry or string lookup is introduced.
A conflicting `typed(...)`/lifecycle binding fails closed.

## Straight-line deterministic cleanup

Version 4 adds the first cleanup-lowering slice.

An admitted cmeta_owned declaration is normalized into a cleanup-safe canonical zero
slot:

```c
cmeta_owned(IntList) values;
```

becomes:

```c
IntList values = {0};
```

The source may also write explicit `= {0}`; other initializers fail closed in
this first slice.

At normal lexical scope exit, every still-LIVE cmeta_owned value is destroyed in
reverse declaration order through the lifecycle accessor already recorded from
`typed(...)`:

```c
cmeta_data_value_destroy(IntList_cmeta_data(), &values);
```

A source consumed by `cmeta_move(name)` is MOVED and receives no source-scope
cleanup.

This is deliberately a **straight-line ownership subset**. While a LIVE cmeta_owned
value exists, the lowerer rejects ownership-relevant control flow it cannot yet
join soundly: early return, outer conditional/loop/switch flow, goto,
break/continue, and short-circuit/ternary expressions. cmeta_owned values scoped
entirely inside a branch or loop body remain admissible when their lifetime
does not escape that lexical path.

The compiler decides **when** cleanup occurs; canonical CMeta DataDesc lifecycle
decides **how** cleanup occurs. No destructor name or allocator policy is
inferred.

## Invariants

- strings, comments, and preprocessor text are preserved;
- invalid admitted ownership syntax fails closed;
- generated output remains portable C;
- normal cmeta_owned cleanup is reverse lexical order;
- cmeta_moved sources are not cleaned by the source scope;
- unsupported ownership-sensitive control flow fails closed;
- receiver/generic operation lowering is not part of this tool;
- runtime CMeta method resolution is never introduced by this tool.
