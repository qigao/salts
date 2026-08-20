# Container

`container/` is the first-class concrete data-structure module. Raw containers are ordinary C and do not depend on CMeta or CFlow.

## Raw C API

Use namespaced headers and `container_*` symbols:

```c
#include <container/vec.h>

container_vec_t values;
container_vec_init(&values, sizeof(int));
container_vec_push(&values, &(int){42});
container_vec_destroy(&values);
```

The raw C namespace deliberately keeps the `container_` prefix. Short names such as `list_init()` are reserved for the optional typed ergonomic facade.

With CMake, raw consumers link:

```cmake
target_link_libraries(app PRIVATE TurboUtils::Container)
```

The raw module uses the repository's existing platform/error support (`TURBO_OK`, `TURBO_EINVAL`, and related status values). Those names belong to the later core extraction and are intentionally not renamed here.

## CMeta typed integration

Typed integration is isolated in:

```c
#include <container/typed.h>
```

This header depends on CMeta and provides the registered container kinds used by `typed(...)` and `Containers(...)`. Raw headers such as `<container/vec.h>` and `<container/list.h>` contain no CMeta dependency.

## One declaration is enough

For a single typed container:

```c
typed(List, IntList, int);
```

For several:

```c
Containers(
    (Vec, IntVec, int),
    (List, IntList, int),
    (HashSet, IntSet, int),
    (HashMap, IntLongMap, int, long),
    (BTree, IntTree, int, long, int_compare)
);
```

No `implement(...)`, `DeclareContainers(...)`, or `ImplementContainers(...)` call is required or exposed for typed containers.

## What is header-only?

Only the generated typed layer:

```text
IntList wrapper type
IntList_* thin forwarding functions
container descriptor
Range factories / traits
```

The forwarding functions are `static inline`, and descriptors are header-local metadata. The actual algorithms remain compiled C in Container sources:

```text
vector growth
list allocation/linking
hash probing
heap operations
B-tree/B+tree balancing
```

So "header-only typed facade" does not mean "header-only container algorithms".

## Supported kinds

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

`Heap`, `BTree`, and `BPlusTree` take an explicit comparator.

## Generated low-level typed ABI

```c
IntList values;
IntList_init(&values);
IntList_push_back(&values, 10);
IntList_push_back(&values, 20);
int *p = IntList_at(&values, 0);
IntList_destroy(&values);
```

These generated concrete names remain a typed ABI. A later ergonomic `_Generic` facade can expose `list_init/list_push/list_at` without changing the underlying instantiation model.

## CFlow bridge

Sequence/set wrappers derive Range information in the same declaration. CFlow can therefore bind an initialized typed object directly:

```c
cflow_stream s;
stream(&values, &s);
```

Associative types use explicit descriptor views:

```c
stream_keys(&map, &s);
stream_values(&map, &s);
stream_entries(&map, &s);
```

Container raw algorithms depend on neither CMeta nor CFlow. Typed integration is isolated in `<container/typed.h>`.
