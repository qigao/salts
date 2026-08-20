#ifndef CONTAINER_TYPES_H
#define CONTAINER_TYPES_H

#include <container/typed.h>

int container_test_int_compare(const void *a, const void *b, void *ctx);

/* CMeta value kinds: one-shot, header only. */
typed(Pair, IntLongPair, int, long);
typed(Tuple, Point3, double, double, double);
typed(Tuple, MixedTuple, int, long, double, bool);
typed(Tuple, Tuple16, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int, int);
typed(Option, MaybeInt, int);
typed(Result, IntResult, int, int);

/* Typed algorithmic containers: complete one-shot instantiation. */
Containers(
    (Vec, IntVec, int),
    (Deque, IntDeque, int),
    (List, IntList, int),
    (Stack, IntStack, int),
    (Queue, IntQueue, int),
    (Heap, IntHeap, int, container_test_int_compare),
    (Set, IntSet, int),
    (HashSet, IntHashSet, int),
    (HashMap, IntHashMap, int, long),
    (Map, IntMap, int, long),
    (MultiMap, IntMultiMap, int, long),
    (BTree, IntBTree, int, long, container_test_int_compare),
    (BPlusTree, IntBPlusTree, int, long, container_test_int_compare)
);

#endif
