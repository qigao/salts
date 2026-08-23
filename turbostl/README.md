# TurboSTL

TurboSTL is TurboUtils' C11 standard-library-style collection module. It owns
the concrete data-structure algorithms, while CMeta supplies finite typed
facades and metadata around those raw algorithms.

Link the installed CMake target and include either the aggregate header or a
focused component header:

```cmake
target_link_libraries(my_target PRIVATE TurboUtils::STL)
```

```c
#include <turbostl.h>
#include <turbostl/typed.h>
```

## Self-describing initialization

TurboSTL provides declaration and expression forms. Both bind the container's
canonical CMeta descriptor and type arguments without allocating storage:

```c
Vec(int, declared_values);
Map(int, long, declared_scores);

vec_t values = VecOf(int);
map_t scores = MapOf(int, long);
```

Expression initializers are ordinary C11 expressions, so they also work in
assignments, return statements, and function arguments:

```c
static map_t new_scores(void) {
    return MapOf(int, long);
}

static void inspect_map(map_t map) {
    (void)map;
}

values = VecOf(int);
scores = new_scores();
inspect_map(MapOf(int, long));
```

Initialize each handle before use and destroy it afterward:

```c
int value = 42;
vec_t values = VecOf(int);

if (vec_init(&values, 8u) != STL_OK)
    return 1;
if (vec_push(&values, &value) != STL_OK) {
    vec_destroy(&values);
    return 1;
}

vec_destroy(&values);
```

The expression form creates the same zero-storage handle as the declaration
form. Once initialized, a handle owns its container storage and must not be
copied as a value.

## Supported kinds

```text
Vec        Deque      List
Stack      Queue      Heap
Set        HashSet
HashMap    Map        MultiMap
BTree      BPlusTree
```

Unary kinds use `VecOf(T)`, `DequeOf(T)`, `ListOf(T)`, `StackOf(T)`,
`QueueOf(T)`, `HeapOf(T)`, `SetOf(T)`, and `HashSetOf(T)`. Associative kinds
use `HashMapOf(K, V)`, `MapOf(K, V)`, `MultiMapOf(K, V)`, `BTreeOf(K, V)`, and
`BPlusTreeOf(K, V)`.

Ordered kinds obtain comparison from the registered CMeta type descriptor;
their typed declarations do not take a parallel comparator token. `Map` is an
ordered red-black-tree map and requires key `COMPARE`. `HashMap` is an independent
open-addressed hash table and requires key `HASH` and `EQUAL`. `List` is an
independent node-based doubly-linked list with stable iterators across insertion.

The public handle types (`vec_t`, `map_t`, and peers) have one ABI per container
kind. Type arguments live in CMeta descriptors instead of producing a new C
type for every combination. The concrete allocation, probing, linking, and
balancing algorithms remain compiled C in TurboSTL sources.

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

TurboSTL does not depend on CFlow.
