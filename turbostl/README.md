# TurboSTL

TurboSTL is TurboUtils' C11 standard-library-style collection module. It owns
the concrete data-structure algorithms, while CMeta supplies finite typed
facades and metadata around those raw algorithms.

Link the installed CMake target and include the typed header (the aggregate
header includes it as well):

```cmake
target_link_libraries(my_target PRIVATE TurboUtils::STL)
```

```c
#include <turbostl/typed.h>
```

## One declaration is enough

For a single typed container:

```c
typed(List, IntList, int);
```

For several:

```c
typed(Vec, IntVec, int);
typed(List, IntList, int);
typed(HashSet, IntSet, int);
typed(HashMap, IntLongHashMap, int, long);
typed(Map, IntLongMap, int, long);
typed(BTree, IntTree, int, long);
```

No `implement(...)`, `DeclareContainers(...)`, or `ImplementContainers(...)` call is required or exposed for typed containers.

## Self-describing raw handles

PR #53's declaration and expression initializers remain supported:

```c
Vec(int, declared_values);
Map(int, long, declared_scores);

vec_t values = VecOf(int);
map_t scores = MapOf(int, long);
turbostl_stack_t pending = StackOf(int);
```

`turbostl_stack_t` is the canonical raw Stack handle. The historical
`stack_t` alias remains available outside Darwin unless
`TURBOSTL_NO_LEGACY_STACK_T` is defined. Darwin reserves `stack_t` for its
signal-stack API, so portable code must use `turbostl_stack_t` or the
`Stack(T, name)` declaration facade.

These forms bind CMeta descriptors to erased TurboSTL handles without
allocating storage or generating a concrete C type. They are raw-handle
initializers, not additional CMeta Generic instantiations. Initialize them with
the ordinary raw operations and destroy them after use:

```c
if (vec_init(&values, 8u) != STL_OK)
    return 1;
vec_destroy(&values);
```

The `*Of(...)` compound literals also remain valid in assignments, returns,
and function arguments. Once an initialized raw handle owns storage, it must
not be copied by value.

## What is header-only?

Only the generated typed layer:

```text
IntList wrapper type
IntList_* thin forwarding functions
container descriptor
Range factories / traits
```

The forwarding functions are `static inline`, and descriptors are header-local metadata. The actual algorithms remain compiled C in TurboSTL sources:

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

Ordered kinds obtain comparison from the registered CMeta type descriptor;
their typed declarations do not take a parallel comparator token. `Map` is an
ordered red-black-tree map and requires key `COMPARE`. `HashMap` is an independent
open-addressed hash table and requires key `HASH` and `EQUAL`. `List` is an
independent node-based doubly-linked list with stable iterators across insertion.

## Typed operations

Generated operations keep the concrete Generic type visible in the function
name:

```c
IntList values = {0};
IntLongMap index = {0};

IntList_init(&values, 100u);
IntList_push_back(&values, 10);
IntList_push_back(&values, 20);

IntLongMap_init(&index, 100u);
IntLongMap_put(&index, 7, 70L);

IntLongMap_destroy(&index);
IntList_destroy(&values);
```

These generated `Type_method` names are the complete typed ABI. Raw names such
as `map_init(&scores, 100u)`, `map_put(&scores, &key, &value)`, and
`map_destroy(&scores)` remain ordinary functions; typed declarations never
intercept or reinterpret their C expressions.

## CFlow bridge

Link `TurboUtils::STLStream` for the optional CFlow facade. Sequence/set wrappers
derive Range information in the same declaration, so CFlow can bind an
initialized typed object directly:

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

Collection names its output type and retains an explicit capacity bound:

```c
IntList output = {0};
turbostl_collect_result result =
    to_list_typed(&s, IntList, &output, 100u);
```

`collect_typed(&s, Type, &output, limit)` has the same typed terminal contract.
The generated public collector takes `Type *`, so a mismatched output wrapper
is diagnosed before CFlow receives its erased collector context.

Self-describing raw outputs retain the three-argument terminal:

```c
list_t raw_output = ListOf(int);
turbostl_collect_result raw_result = to_list(&s, &raw_output, 100u);
```

The erased form validates its descriptor at runtime; the typed form provides
the stronger compile-time output pointer check. Collection is transactional.
On failure, a generated wrapper is reset to zero. An erased raw handle releases
its storage while retaining the descriptor and type binding supplied by its
declaration or `*Of(...)` initializer, so it can be initialized again.
The core `TurboUtils::STL` target does not depend on CFlow.
