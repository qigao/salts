# Turbo typed containers v50

Turbo owns the concrete data-structure algorithms. CMeta supplies finite typed facades and metadata around those raw algorithms.

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
    (BTree, IntTree, int, long)
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

The forwarding functions are `static inline`, and descriptors are header-local metadata. The actual algorithms remain compiled C in Turbo sources:

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

## Generated low-level typed ABI

```c
IntList values = {0};
IntList_init(&values, 100);
IntList_push_back(&values, 10);
IntList_push_back(&values, 20);
int *p = IntList_front(&values);
IntList_destroy(&values);
```

These generated concrete names remain a typed ABI. A later ergonomic `_Generic`
facade can expose initialization, insertion, and iterator operations without
changing the underlying instantiation model.

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

Turbo does not depend on CFlow.
