#include <cstl/typed.h>
#include <string.h>

typed(Vec, IntVec, int);
typed(List, IntList, int);
typed(Set, IntSet, int);
typed(Map, IntMap, int, int);

int main(void) {
    const char *literal = "list.add(99); List_add(&list, 99);";
    IntList list = {0};
    IntVec vec = {0};
    IntSet set = {0};
    IntMap map = {0};
    const int *value;

    /* list.add(77); List_add(&list, 77); */

    if (strcmp(literal, "list.add(99); List_add(&list, 99);") != 0)
        return 1;

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
