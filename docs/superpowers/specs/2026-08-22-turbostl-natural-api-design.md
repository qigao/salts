# TurboSTL Natural API Naming Design

## Goal

Make TurboSTL expose one canonical, natural C API with no `turbo_` prefix on container types, functions, status values, or TurboSTL-internal public macros.

This is an intentional breaking API/ABI cleanup. The final installed TurboSTL headers must not preserve `turbo_*` aliases.

## Scope

The rename covers every TurboSTL container and supporting API currently owned by `TurboUtils::STL`, including:

- vec
- deque
- list
- stack
- queue
- heap / priority queue
- set / hash set
- map / hash map
- multimap
- btree / bplus tree
- sort
- shared status and metadata-generation vocabulary

The module/directory identity remains `turbostl`, and the CMake target remains `TurboUtils::STL`. This change removes the redundant symbol prefix, not the module identity.

## Canonical naming

Container types use the container name directly:

```c
turbo_list_t       -> list_t
turbo_list_iter_t  -> list_iter_t
turbo_map_t        -> map_t
turbo_hash_map_t   -> hash_map_t
turbo_btree_t      -> btree_t
```

Functions follow the same namespace:

```c
turbo_list_init(...)       -> list_init(...)
turbo_list_push_back(...)  -> list_push_back(...)
turbo_map_put(...)         -> map_put(...)
turbo_hash_map_get(...)    -> hash_map_get(...)
turbo_btree_remove(...)    -> btree_remove(...)
```

Source files follow the public container name:

```text
src/turbo_list.c       -> src/list.c
src/turbo_map.c        -> src/map.c
src/turbo_btree.c      -> src/btree.c
```

Public header filenames are already natural (`list.h`, `map.h`, `btree.h`, ...), so they remain unchanged.

## Status naming

A completely generic `status` type would be too collision-prone, so the shared module status keeps an STL-level namespace:

```c
turbo_stl_status             -> stl_status
TURBO_STL_OK                 -> STL_OK
TURBO_STL_INVALID_ARGUMENT   -> STL_INVALID_ARGUMENT
TURBO_STL_OUT_OF_MEMORY      -> STL_OUT_OF_MEMORY
TURBO_STL_CAPACITY_EXCEEDED  -> STL_CAPACITY_EXCEEDED
TURBO_STL_EMPTY              -> STL_EMPTY
TURBO_STL_NOT_FOUND          -> STL_NOT_FOUND
TURBO_STL_TYPE_MISMATCH      -> STL_TYPE_MISMATCH
TURBO_STL_TRAIT_MISSING      -> STL_TRAIT_MISSING
```

This preserves a clear module namespace without repeating `turbo_` on every API symbol.

## CMeta / generated facade vocabulary

TurboSTL metadata-generation identifiers also lose the `TURBO_` prefix.

Examples:

```text
TURBO_META_VEC_METHODS       -> STL_META_VEC_METHODS
TURBO_META_MAP_METHODS       -> STL_META_MAP_METHODS
TURBO_STL_KIND_ROW_Vec       -> STL_KIND_ROW_Vec
TURBO_STL_INVALID_ARGUMENT   -> STL_INVALID_ARGUMENT
```

CMeta itself retains its own `CMETA_*` namespace. The rename must not move TurboSTL policy into CMeta or create new CMeta aliases.

## Compatibility policy

There is no permanent `turbo_*` compatibility API.

The implementation migration is atomic at repository level:

1. introduce the new canonical names;
2. migrate all TurboSTL implementation files, tests, examples, CFlow adapters, Core consumers, and turbo_serial consumers;
3. remove the old `turbo_*` declarations and definitions before the refactor is considered complete;
4. install only the natural API.

Temporary aliases may exist only inside an intermediate development commit when necessary to keep a mechanical rename buildable. They must not remain in the final PR tree and must not be installed.

## Dependency boundary

This naming refactor must not change the approved module dependency contract.

In particular:

```text
TurboSTL -> CMeta
```

remains valid for the base STL target. TurboSTL must not gain a dependency on Core, Platform, or Concurrency merely to make old compatibility headers compile.

`TurboUtils::STLStream` may continue to depend on `TurboUtils::CFlow` as its separate adapter target.

Any existing accidental include of Core compatibility headers from TurboSTL must be removed rather than satisfied by adding a lower-level dependency.

## API shape examples

Before:

```c
turbo_list_t list = {0};
turbo_stl_status rc = turbo_list_init(&list, &cmeta_type_int, 128u);
if (rc == TURBO_STL_OK) {
    int value = 7;
    turbo_list_push_back(&list, &value, NULL);
}
turbo_list_destroy(&list);
```

After:

```c
list_t list = {0};
stl_status rc = list_init(&list, &cmeta_type_int, 128u);
if (rc == STL_OK) {
    int value = 7;
    list_push_back(&list, &value, NULL);
}
list_destroy(&list);
```

The same rule applies consistently to every container.

## Implementation constraints

- Do not create a second long-lived API surface.
- Do not retain `turbo_*` aliases in installed headers.
- Do not solve compile failures by adding Platform/Core dependencies to TurboSTL.
- Do not add source-spelling/grep tests that merely assert the absence of a token.
- Preserve runtime behavior; this work is naming and ownership cleanup, not a container algorithm rewrite.
- Keep C11 compatibility and C++17 header compatibility.

## Verification

Verification is behavior/compile/link based:

- each container's existing tests are migrated to the natural API and must still pass;
- C11 and C++17 public-header tests compile against the natural API;
- `TurboUtils::STL` builds with its declared `CMeta` dependency only;
- `TurboUtils::STLStream` continues to compile through its explicit CFlow adapter dependency;
- Core and turbo_serial consumers compile after migration;
- full Linux and Windows builds pass;
- no completion claim is made until a fresh CI run on the final head succeeds.

Absence of legacy `turbo_*` TurboSTL API is verified by final diff/API review, not by source-text tests.

## Out of scope

- renaming the repository/module directory `turbostl`;
- renaming the CMake target `TurboUtils::STL`;
- changing container algorithms or storage models;
- changing CMeta naming policy;
- changing CFlow execution semantics.
