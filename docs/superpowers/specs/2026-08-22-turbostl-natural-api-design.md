# TurboSTL Self-Describing Natural API Design

## Goal

TurboSTL exposes one natural user API with no `turbo_` prefix and no user-visible generated names such as `IntVec`, `IntList`, `IntMap`, `IntVec_init`, or `UserMap_put`.

The canonical user model is:

```c
Vec(int, numbers);
List(int, items);
Map(int, int, scores);

vec_init(&numbers, 128u);
vec_push(&numbers, &value);

list_init(&items, 128u);
list_push_back(&items, &value);

map_init(&scores, 128u);
map_put(&scores, &id, &score);
```

The concrete element/key/value types are bound by the declaration DSL and carried by the container object itself. Users do not pass a generated type token to operations.

This is an intentional breaking API/ABI cleanup.

## Module identity

The directory/module remains `turbostl` and the CMake target remains `TurboUtils::STL`.

The base dependency contract remains:

```text
TurboSTL -> CMeta
```

The base STL target must not gain Core, Platform, Concurrency, or CFlow dependencies. `TurboUtils::STLStream` remains the explicit optional `TurboUtils::STL + TurboUtils::CFlow` adapter target.

## Self-describing handles

There is exactly one runtime handle type per container kind:

```c
vec_t
list_t
map_t
hash_map_t
set_t
...
```

`Vec(int, numbers)` does **not** create `IntVec`. It declares an ordinary `vec_t` whose CMeta element descriptor is pre-bound:

```c
#define Vec(T, name) \
    vec_t name = { .element_type = CMETA_TYPEOF(T) }
```

The exact implementation macro may use an internal initializer helper, but the observable contract is that `numbers` has type `vec_t` and carries the descriptor resolved for `T` before `vec_init()`.

Two-type containers bind both descriptors:

```c
#define Map(K, V, name) \
    map_t name = { .key_type = CMETA_TYPEOF(K), \
                   .value_type = CMETA_TYPEOF(V) }
```

Equivalent declaration macros exist for all supported kinds:

```text
Vec(T, name)
Deque(T, name)
List(T, name)
Stack(T, name)
Queue(T, name)
Heap(T, name)
Set(T, name)
HashSet(T, name)
Map(K, V, name)
HashMap(K, V, name)
MultiMap(K, V, name)
BTree(K, V, name)
BPlusTree(K, V, name)
```

These macros bind type metadata only. They do not allocate storage and do not initialize the runtime container.

## CMeta type-resolution boundary

Current strict-C11 `CMETA_TYPEOF(T)` is intentionally finite: it resolves the types registered in CMeta's `_Generic` type list and returns `NULL` for an unknown application type.

TurboSTL must not silently invent memcpy/no-op lifecycle semantics for an unknown type merely to make `Vec(User, users)` compile.

Therefore the first self-describing API contract is:

- if CMeta resolves `T`, `Vec(T, value)` / `Map(K,V,value)` pre-bind those descriptors;
- if any required descriptor is unresolved, typed `*_init()` returns `STL_INVALID_ARGUMENT` without mutation;
- application-defined types require a proper CMeta type-registration path before they can use the inferred typed initialization path;
- raw-byte entry points remain available when the caller intentionally supplies size/alignment/comparator information.

Extending CMeta's application-type registration mechanism is a separate concern and must not be faked inside TurboSTL.

## Natural operation API

Users call one stable operation vocabulary per container kind:

```c
vec_init(&v, limit);
vec_push(&v, &value);
vec_pop(&v, &out);
vec_destroy(&v);

list_init(&list, limit);
list_push_back(&list, &value);
list_destroy(&list);

map_init(&map, limit);
map_put(&map, &key, &value);
map_get(&map, &key);
map_destroy(&map);
```

There is no public overload taking a generated type token:

```c
/* Not public API */
vec_init(IntVec, &v, limit);
map_put(UserMap, &m, key, value);
```

There is also no public generated method vocabulary:

```c
/* Not public API */
IntVec_init(...);
IntVec_push(...);
IntMap_put(...);
```

## Initialization contract

Typed initialization reads the type binding already stored in the handle.

For example:

```c
stl_status vec_init(vec_t *vec, size_t element_limit);
stl_status list_init(list_t *list, size_t element_limit);
stl_status map_init(map_t *map, size_t entry_limit);
```

A typed initializer returns `STL_INVALID_ARGUMENT` when the required binding is absent or invalid.

Raw-byte entry points remain explicit and separate, for example:

```c
vec_init_bytes(...);
map_init_bytes(...);
```

They are not part of type inference and continue to accept explicit size/alignment/comparator information where required.

`from_array`/`from_arrays` typed entry points follow the same rule: they use the handle's pre-bound descriptors rather than receiving descriptors as normal user arguments.

## Binding lifetime

Type bindings are configuration, not runtime storage.

`clear()` and `destroy()` release/reset runtime contents while preserving declaration-time type bindings. Therefore this remains valid:

```c
Vec(int, values);
vec_init(&values, 32u);
vec_destroy(&values);
vec_init(&values, 64u);  /* still bound to int */
```

A destroyed self-describing handle can be initialized again without repeating a type token.

## CMeta integration

CMeta remains the source of type descriptors and traits:

```text
registered CMeta type
        ↓
CMETA_TYPEOF(T)
        ↓
container handle binding
        ↓
copy / move / destroy / compare / hash
```

TurboSTL no longer needs a named generated wrapper type merely to carry CMeta metadata.

Container Range/collector integration should read type metadata from the runtime handle. The previous `CMETA_CONTAINER1_DEFINE(IntVec, ...)` / `CMETA_CONTAINER2_DEFINE(UserMap, ...)` layer is not the user-facing TurboSTL model and should be removed from TurboSTL's public path where no longer required.

CMeta's generic container-generation infrastructure may remain for other users; this refactor does not require deleting generic facilities from CMeta itself.

## Stream integration

Removing generated user types simplifies CFlow integration because container *kinds* are finite even though element types are open-ended.

A public generic stream entry point can dispatch on ordinary handle types:

```c
_Generic((container_ptr),
    vec_t *: ...,
    list_t *: ...,
    map_t *: ...)
```

It does not need an association for `IntVec`, `UserVec`, or every application-defined container type.

Collectors likewise bind to a concrete output handle rather than a generated type token. The intended terminal shape is:

```c
to_list(&pipeline, &output, limit);
```

not:

```c
to_list(&pipeline, OutputList, &output, limit);
```

## Type-safety boundary

Strict C11 cannot recover an arbitrary application-defined element type from a `void *` payload argument and perform C++-style template deduction without compiler extensions or a finite `_Generic` registry.

Therefore the supported guarantee is:

- declaration-time type metadata is inferred for CMeta-resolved types from `Vec(T, ...)` / `Map(K,V,...)`;
- lifecycle/compare/hash behavior is driven by the bound CMeta descriptors;
- operations require no repeated type token;
- element/key/value payloads remain pointer-based (`&value`, `&key`) as in the underlying C API.

The design does not introduce GNU `typeof`, compiler-specific template emulation, or a user-maintained `_Generic` association table merely to make `vec_push(&v, 42)` possible.

## Natural naming

All TurboSTL public container/status names lose the `turbo_` prefix:

```text
turbo_vec_t        -> vec_t
turbo_list_t       -> list_t
turbo_map_t        -> map_t
turbo_hash_map_t   -> hash_map_t

turbo_vec_push     -> vec_push
turbo_list_init    -> list_init
turbo_map_put      -> map_put

turbo_stl_status   -> stl_status
TURBO_STL_OK       -> STL_OK
```

Source filenames also become natural (`vec.c`, `list.c`, `map.c`, ...).

There is no permanent `turbo_*` compatibility API in final installed TurboSTL headers.

## Typed/generated vocabulary policy

TurboSTL must not publish generated names such as `IntList_init()` as the normal end-user path.

`typed.h` must not define semantic macros that shadow natural functions. In particular, it must not own macros named `list_init`, `map_init`, `map_put`, etc.

As this self-describing model is implemented, TurboSTL's old `typed(Vec, IntVec, int)` registration/generation surface should be removed from the public entry point rather than maintained as a second API.

## Status naming

Shared status remains namespaced at the module level:

```c
typedef enum stl_status {
    STL_OK = 0,
    STL_INVALID_ARGUMENT,
    STL_OUT_OF_MEMORY,
    STL_CAPACITY_EXCEEDED,
    STL_EMPTY,
    STL_NOT_FOUND,
    STL_TYPE_MISMATCH,
    STL_TRAIT_MISSING
} stl_status;
```

A completely generic `status` name is intentionally avoided.

## Compatibility policy

The repository migration is atomic:

1. introduce self-describing handle bindings and declaration macros;
2. switch typed initializers to consume stored bindings;
3. migrate tests/examples/Core/turbo_serial/CFlow adapters to the natural API;
4. remove generated TurboSTL typed-wrapper usage from the public path;
5. remove temporary `turbo_*` aliases;
6. install only the self-describing natural API.

Temporary aliases may exist in intermediate commits only. They must not remain in the final PR tree.

## Verification

Verification is compile/link/runtime based:

- a C11 test declares `Vec(int, v)`, calls `vec_init(&v, limit)`, pushes/pops values, destroys, and reinitializes without a type token;
- equivalent declaration/init/operation tests cover List, Stack, Queue, Set/HashSet, Map/HashMap, MultiMap, BTree/BPlusTree using CMeta-resolved types;
- an unresolved type-binding test proves `*_init()` fails without mutation rather than guessing lifecycle semantics;
- user tests contain no `IntVec`, `IntList`, `IntMap`, `Type_method`, or generated container type token;
- C++17 can include all public headers;
- existing ownership, iterator invalidation, capacity, compare/hash, and sort behavior remains unchanged;
- `TurboUtils::STL` builds with CMeta only;
- `TurboUtils::STLStream` compiles with explicit CFlow dependency;
- full Linux and Windows builds pass;
- final completion requires a fresh CI run on the final head.

No grep/source-spelling test is added; API absence is established by final public-header/diff review.

## Out of scope

- changing container algorithms/storage models except fields required to retain type bindings;
- inventing an application-type CMeta registration mechanism inside TurboSTL;
- introducing C++ templates or compiler-specific `typeof` extensions;
- renaming the `turbostl` directory or `TurboUtils::STL` target;
- changing CMeta naming policy outside the integration required by TurboSTL;
- changing CFlow execution semantics.
