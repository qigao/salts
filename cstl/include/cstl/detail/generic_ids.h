#ifndef CSTL_DETAIL_GENERIC_IDS_H
#define CSTL_DETAIL_GENERIC_IDS_H

/*
 * Canonical CSTL generic constructor stable IDs.
 *
 * Keep this header dependency-free so CSTL runtime descriptor publication can
 * share one stable constructor identity across translation units. Public
 * cmeta_type(...) spelling is declaration syntax only; semantic generic-owner
 * equality is the stable ID.
 */
#define CSTL_GENERIC_STABLE_ID_Vec       "cstl.Vec"
#define CSTL_GENERIC_STABLE_ID_Deque     "cstl.Deque"
#define CSTL_GENERIC_STABLE_ID_List      "cstl.List"
#define CSTL_GENERIC_STABLE_ID_Stack     "cstl.Stack"
#define CSTL_GENERIC_STABLE_ID_Queue     "cstl.Queue"
#define CSTL_GENERIC_STABLE_ID_Heap      "cstl.Heap"
#define CSTL_GENERIC_STABLE_ID_Set       "cstl.Set"
#define CSTL_GENERIC_STABLE_ID_HashSet   "cstl.HashSet"
#define CSTL_GENERIC_STABLE_ID_HashMap   "cstl.HashMap"
#define CSTL_GENERIC_STABLE_ID_Map       "cstl.Map"
#define CSTL_GENERIC_STABLE_ID_MultiMap  "cstl.MultiMap"
#define CSTL_GENERIC_STABLE_ID_BTree     "cstl.BTree"
#define CSTL_GENERIC_STABLE_ID_BPlusTree "cstl.BPlusTree"

#define CSTL_GENERIC_STABLE_ID_I(kind) CSTL_GENERIC_STABLE_ID_##kind
#define CSTL_GENERIC_STABLE_ID(kind) CSTL_GENERIC_STABLE_ID_I(kind)

#define CSTL_GENERIC_OWNER_KINDS(X) \
    X(Vec) \
    X(Deque) \
    X(List) \
    X(Stack) \
    X(Queue) \
    X(Heap) \
    X(Set) \
    X(HashSet) \
    X(HashMap) \
    X(Map) \
    X(MultiMap) \
    X(BTree) \
    X(BPlusTree)

#endif /* CSTL_DETAIL_GENERIC_IDS_H */
