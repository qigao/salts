# CMeta static inspection contract

CMeta reflection descriptors are already the canonical compile/control-plane
inspection model. Tooling and compilers inspect these immutable descriptors
directly and use canonical validation/equality/derived queries where semantic
meaning is not equivalent to pointer or field comparison.

This is **not** a second `MetaType` / `MetaFunction` object model and it is not
runtime RTTI.

## Inspection rule

```text
immutable public descriptor fields
        +
canonical valid/equal/derived queries
        =
static inspection surface
```

Do not add an accessor merely to mirror every public descriptor member. Add a
query when it owns semantic validation, equality, or derived meaning that
multiple consumers would otherwise reconstruct.

Inspection belongs to build/start/control plane when an immutable executable
artifact can be produced. Hot-path execution continues to use direct C,
exact callables/vtables, CFlow plans, or another explicit execution authority.

## Type identity

### `cmeta_type_desc`

Direct immutable facts include:

- name;
- size/alignment;
- kind;
- pointee type;
- traits;
- semantic identity.

Canonical queries:

```c
cmeta_type_desc_valid(...)
cmeta_type_equal(...)
cmeta_type_identity_of(...)
```

`cmeta_type_equal()` is semantic. Descriptor address is not type identity
across translation units or DSOs.

### `cmeta_type_identity` / `cmeta_generic_desc`

Canonical queries:

```c
cmeta_type_identity_valid(...)
cmeta_type_identity_equal(...)
cmeta_generic_desc_valid(...)
cmeta_generic_desc_equal(...)
cmeta_generic_accepts_arity(...)
cmeta_type_application_valid(...)
```

A generic constructor is identified by canonical `cmeta_generic_desc`
semantics, not source spelling or descriptor address.

## Declared generic types

`cmeta_declared_type` is declaration/source-level generic application metadata
attached to a concrete native storage type.

```text
storage_type   = native representation identity
constructor    = semantic generic constructor
arguments[]    = semantic generic arguments
construction   = optional provider capability
```

Canonical queries:

```c
cmeta_declared_type_valid(...)
cmeta_declared_type_argument(...)
cmeta_declared_type_application_equal(...)
cmeta_declared_type_constructible(...)
```

`cmeta_declared_type_application_equal()` compares only the semantic generic
application:

```text
constructor + arguments
```

It intentionally does **not** compare:

- `storage_type` — use `cmeta_type_equal()` separately when representation
  identity is part of the consumer contract;
- `construction` — construction is provider capability, not generic identity.

A compiler should not instantiate a runtime container merely to rediscover
declaration generic metadata.

## Data semantics

`cmeta_data_desc` is canonical native data/lifecycle meaning.

Canonical base queries include:

```c
cmeta_data_desc_valid(...)
cmeta_data_desc_equal(...)
cmeta_data_value_traits_supported(...)
cmeta_data_value_copy_supported(...)
cmeta_data_value_move_supported(...)
```

Kind-specific provider-neutral queries include existing collection/map/buffer,
enum, variant, fixed-value and construction accessors such as:

```c
cmeta_data_collection_element_data(...)
cmeta_data_map_key_data(...)
cmeta_data_map_value_data(...)
cmeta_data_construct_ops_of(...)
```

Shape fields remain immutable reflection data. Consumers must not replace
`cmeta_data_desc_equal()` with local approximations such as
`kind + storage pointer`.

Lifecycle callbacks are semantic authority for values. Their presence does not
make Reflection an allocator or ownership runtime.

## Struct fields

`cmeta_field_desc` exposes native field type/layout and optional
`declared_type`.

Canonical structural lookup uses:

```c
cmeta_struct_find_field(...)
```

For a generic field:

```text
field.type
    = concrete native storage TypeDesc

field.declared_type
    = source/declaration generic application
```

Do not conflate those identities.

## Functions and ABI

`cmeta_function_desc` owns semantic function facts:

- parameters and exact reflected parameter type;
- direction/ownership/nullability flags;
- result type and result ownership;
- effects/properties.

`cmeta_function_abi_desc` owns exact ABI carrier facts when present.

Canonical queries include:

```c
cmeta_param_desc_valid(...)
cmeta_function_desc_valid(...)
cmeta_function_desc_equal(...)
cmeta_function_abi_desc_valid(...)
cmeta_function_abi_desc_equal(...)
cmeta_function_param(...)
cmeta_function_param_abi(...)
```

Do not infer a semantic pointee value merely because a parameter is spelled
`T *`. Dereferencing a pointer parameter is a domain/compiler ABI convention
unless canonical metadata explicitly says otherwise.

Reflection never grants generic execution authority. Dynamic execution still
requires an exact admitted callable/invokable adapter.

## Receiver methods

`cmeta_receiver_method_set` publishes:

- exact receiver type;
- optional canonical generic owner;
- receiver methods;
- canonical FunctionDesc/FunctionAbi where reflected.

Canonical queries include:

```c
cmeta_receiver_method_set_valid(...)
cmeta_receiver_method_find(...)
cmeta_receiver_method_resolve(...)
```

For generic operations, `set->owner` is a canonical
`cmeta_generic_desc *`. Compare it with `cmeta_generic_desc_equal()`, never
with descriptor address or source-name strings.

## Interfaces

`cmeta_interface_desc` is a typed protocol description. It does not replace
the exact `{ self, vtable }` execution carrier.

Canonical queries include:

```c
cmeta_interface_desc_valid(...)
cmeta_interface_desc_equal(...)
cmeta_interface_method_reflection_valid(...)
cmeta_interface_method_owns_self(...)
cmeta_interface_desc_has_owning_method(...)
```

Function-reflected interface methods reuse canonical FunctionDesc/FunctionAbi
semantics.

## ObjectRef

`cmeta_object_ref` is runtime-selected native identity/lifetime state, not a
compile-time reflection wrapper.

Use it only when runtime selection is required. Static inspection of known
declarations should consume descriptors directly.

```c
cmeta_object_ref_valid(...)
```

ObjectRef -> Interface capability projection remains an explicit provider
relationship; descriptor inspection alone never invents executable authority.

## Plugin / DSO lifetime

Plugin exports Function/Interface capabilities whose reachable descriptor graph
may include TypeDesc, TypeIdentity, GenericDesc, DataDesc and related metadata.

All provider-owned descriptors/callbacks/code are borrowed from one explicit
outer Plugin lease:

```text
descriptor/view lifetime <= Plugin lease lifetime
```

Semantic equality may compare host/provider descriptor graphs across different
addresses. Copying a descriptor pointer does not retain the module.

There is no standalone TYPE/DATA/GENERIC Plugin registry unless a future real
consumer proves independent discovery is required.

## Compiler-private state

The following do **not** belong in reflection descriptors:

- lexical scope;
- CFG/control-flow state;
- move state;
- cleanup insertion points;
- borrow regions;
- generated temporary ownership state.

Those remain compiler-private proof/lowering state.

## Negative rules

Do not introduce:

- `MetaType` / `MetaFunction` wrapper universes;
- one getter per immutable descriptor field;
- pointer-address equality across TUs/DSOs;
- method-name runtime invocation;
- generic vtable interpretation;
- libffi;
- ownership inference from C spelling;
- hot-path reflection lookup;
- descriptor-embedded Plugin leases.

## Current compiler/tooling consumers

The same inspection authority is intended for:

```text
cmeta-lower
SaltsUtils IDL/DataBind compiler
TurboScript lowering
CHttp/OpenAPI generation
diagnostics/docs/code generation
```

Domain consumers may apply their own admitted conventions after validating
canonical CMeta facts. Those conventions do not become universal CMeta
semantics.
