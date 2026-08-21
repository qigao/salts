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

Turbo does not depend on CFlow.
