# Container Module Extraction Design

## Goal

Promote the experimental `turbo/` data-structure implementation into a first-class top-level `container/` module and remove container-specific `turbo_*` naming. The module remains in the `turbo-utils` monorepo.

## Scope

This change does:

- rename `turbo/` to `container/`;
- move public headers under `container/include/container/`;
- rename raw C symbols from `turbo_<kind>_*` and `turbo_<kind>_t` to `container_<kind>_*` and `container_<kind>_t`;
- rename container meta/typed integration headers to `container/meta.h` and `container/typed.h`;
- rename internal container meta macros from `TURBO_META_*` / `TURBO_*_DEFINE` to `CONTAINER_META_*` / `CONTAINER_*_DEFINE`;
- add a first-class `TurboUtils::Container` CMake target;
- update tests, examples, docs, Makefile, CMeta typed registrations, and CFlow container integration references.

This change does not:

- extract a new `core/` module;
- rename project-wide error values such as `TURBO_OK`, `TURBO_EINVAL`, or `TURBO_ENOMEM`;
- redesign container algorithms;
- implement the optional generic `list_*` registry layer;
- remove the old independent root `stream/` subsystem in the real repository (that is a separate consolidation change).

## Public layout

```text
container/
├── CMakeLists.txt
├── README.md
├── include/container/
│   ├── vec.h
│   ├── deque.h
│   ├── list.h
│   ├── stack.h
│   ├── queue.h
│   ├── heap.h
│   ├── set.h
│   ├── hash_set.h
│   ├── hash_map.h
│   ├── map.h
│   ├── multimap.h
│   ├── btree.h
│   ├── bplus_tree.h
│   ├── meta.h
│   └── typed.h
└── src/
```

The raw API is intentionally namespaced:

```c
container_list_t list;
container_list_init(&list, sizeof(User));
```

The generated typed ABI remains type-specific:

```c
typed(List, UserList, User);
UserList users;
UserList_init(&users);
```

This leaves the short `list_init(&users)` namespace available for the future optional ergonomic typed facade.

## Dependencies

Raw container algorithms remain independent of CFlow and CMeta except for the dedicated typed/meta integration layer.

```text
container raw  ---> existing project platform/error support
container/typed.h ---> CMeta + container raw
CFlow ---> CMeta
```

`CMeta` does not depend on CFlow. CFlow does not depend on raw container implementation symbols.

## CMake

`container/CMakeLists.txt` builds a `turbo_container` library exported as `TurboUtils::Container`. It installs the `container/...` headers. Root CMake adds `add_subdirectory(container)` before `cmeta`/`cflow` consumers that need it.

## Compatibility

There are no compatibility aliases for `turbo_vec_t`, `turbo_list_init`, `<turbo_vec.h>`, or the `turbo/` directory. The project has no external users for this experimental API, so the rename is intentionally clean.

Project-wide status/error names (`TURBO_OK`, etc.) are temporarily retained because they belong to the later `core/` extraction, not to this module identity.

## Verification

- strict C11 container smoke tests;
- randomized BTree property test;
- typed container + callable coexistence;
- Range and multi-TU descriptor tests;
- CMeta Range -> CFlow Stream test;
- GCC/Clang O0/O2 matrix;
- ASan/UBSan;
- CMake target/build/install smoke;
- repository scan proving no container-specific `turbo_*` symbols or legacy `turbo/` paths remain in the migrated module/tests/docs.
