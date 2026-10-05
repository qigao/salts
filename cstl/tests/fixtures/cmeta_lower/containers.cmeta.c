#include <cstl/typed.h>
#include <string.h>

typed(Vec, IntVec, int);
typed(List, IntList, int);
typed(Set, IntSet, int);
typed(Map, IntMap, int, int);

typedef IntList ExternalList;
CMETA_LIFECYCLE(ExternalList, IntList_cmeta_data);

int main(void) {
    const char *literal = "list.add(99); List_add(&list, 99);";
    const char *ownership_literal =
        "owned(IntList) fake = {0}; move(fake);";
    IntList list = {0};
    IntVec vec = {0};
    IntSet set = {0};
    IntMap map = {0};
    const int *value;

    /* list.add(77); List_add(&list, 77); */
    /* owned(IntList) fake = {0}; move(fake); */

    if (strcmp(literal, "list.add(99); List_add(&list, 99);") != 0)
        return 1;
    if (strcmp(ownership_literal,
               "owned(IntList) fake = {0}; move(fake);") != 0)
        return 90;

    if (IntList_init(&list, 8u) != STL_OK ||
        IntVec_init(&vec, 8u) != STL_OK ||
        IntSet_init(&set, 8u) != STL_OK ||
        IntMap_init(&map, 8u) != STL_OK)
        return 2;

    if (list.add(10) != STL_OK) return 3;
    if (List_add(&list, 20) != STL_OK) return 4;

    if (vec.push(10) != STL_OK) return 5;
    if (Vec_push(&vec, 20) != STL_OK) return 6;

    if (set.add(10) != STL_OK) return 7;
    if (Set_add(&set, 20) != STL_OK) return 8;

    if (map.put(1, 10) != STL_OK) return 9;
    if (Map_put(&map, 2, 20) != STL_OK) return 10;

    {
        IntVec list = {0};
        if (IntVec_init(&list, 4u) != STL_OK) return 11;
        if (list.push(30) != STL_OK) return 12;
        if (Vec_push(&list, 40) != STL_OK) return 13;
        if (IntVec_size(&list) != 2u) return 14;
        IntVec_destroy(&list);
    }

    if (list.add(50) != STL_OK) return 15;

    {
        owned(ExternalList) external_cleanup;
        (void)IntList_init(&external_cleanup, 2u);
        (void)IntList_add(&external_cleanup, 52);
    }

    {
        owned(IntList) cleanup_first;
        owned(IntVec) cleanup_second = {0};
        owned(IntMap) cleanup_map;
        int cleanup_first_size;
        int cleanup_second_size;
        size_t cleanup_map_size;

        (void)IntList_init(&cleanup_first, 2u);
        (void)IntVec_init(&cleanup_second, 2u);
        (void)IntMap_init(&cleanup_map, 2u);
        (void)cleanup_first.add(53);
        (void)cleanup_second.push(54);
        (void)cleanup_map.put(1, 55);
        cleanup_first_size = (int)IntList_size(&cleanup_first);
        cleanup_second_size = (int)IntVec_size(&cleanup_second);
        cleanup_map_size = IntMap_size(&cleanup_map);
        (void)cleanup_first_size;
        (void)cleanup_second_size;
        (void)cleanup_map_size;
    }

    {
        owned(IntList) moved_source;
        IntList moved_sink = {0};

        (void)IntList_init(&moved_source, 2u);
        (void)moved_source.add(56);
        moved_sink = move(moved_source);
        if (IntList_size(&moved_sink) != 1u) return 98;
        IntList_destroy(&moved_sink);
    }

    {
        owned(IntList) transfer;
        IntList received = {0};
        int inner_size;

        (void)IntList_init(&transfer, 4u);
        (void)transfer.add(60);

        {
            owned(IntList) inner_transfer;
            IntList inner_received = {0};

            (void)IntList_init(&inner_transfer, 2u);
            (void)inner_transfer.add(70);
            inner_received = move(inner_transfer);
            inner_size = (int)IntList_size(&inner_received);
            IntList_destroy(&inner_received);
        }

        (void)transfer.add(61);
        received = move(transfer);

        if (inner_size != 1) return 99;
        if (IntList_size(&received) != 2u) return 100;
        IntList_destroy(&received);
    }

    if (IntList_size(&list) != 3u ||
        IntVec_size(&vec) != 2u ||
        IntSet_size(&set) != 2u ||
        IntMap_size(&map) != 2u)
        return 16;

    value = IntMap_get_const(&map, 2);
    if (value == NULL || *value != 20)
        return 17;

    IntMap_destroy(&map);
    IntSet_destroy(&set);
    IntVec_destroy(&vec);
    IntList_destroy(&list);
    return 0;
}
