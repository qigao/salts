#include "stream_turbo_containers.h"

#include <stdio.h>

static int int_cmp(const void *left, const void *right, void *ctx)
{
    const int lhs = *(const int *)left;
    const int rhs = *(const int *)right;
    (void)ctx;
    return lhs < rhs ? -1 : (lhs > rhs ? 1 : 0);
}

static bool int_is_even(const void *value)
{
    return (*(const int *)value % 2) == 0;
}

static bool int_is_pos(const void *value)
{
    return *(const int *)value > 0;
}

static bool int_equal(const void *left, const void *right)
{
    return *(const int *)left == *(const int *)right;
}

TURBO_VEC_DEFINE(int_vec_t, int)
TURBO_DEQUE_DEFINE(int_deque_t, int)
TURBO_LIST_DEFINE(int_list_t, int)
TURBO_SET_DEFINE(int_set_t, int)
TURBO_MAP_DEFINE(int_map_t, int, int)
TURBO_MULTI_MAP_DEFINE(int_multi_map_t, int, int)
TURBO_HEAP_DEFINE(int_heap_t, int, int_cmp)
TURBO_TREE_MAP_DEFINE(int_tree_map_t, int, int, int_cmp)
TURBO_BPLUS_TREE_DEFINE(int_bplus_map_t, int, int, int_cmp)

static void print_int(const void *value)
{
    printf("%d ", *(const int *)value);
}

int main(void)
{
    int_vec_t vec;
    int_deque_t deque;
    int_list_t list;
    int_set_t set;
    int_map_t map;
    int_multi_map_t multimap;
    int_heap_t heap;
    int_tree_map_t tree;
    int_bplus_map_t bpt;
    stream_t stream;

    if (int_vec_t_from(&vec, (int[]){1, 2, 3, 4, 5}, 5) != TURBO_OK) {
        return 1;
    }
    if (int_deque_t_from(&deque, (int[]){10, 11, 12}, 3) != TURBO_OK) {
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_list_t_from(&list, (int[]){20, 21, 22}, 3) != TURBO_OK) {
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_set_t_from(&set, (int[]){2, 4, 2, 8}, 4) != TURBO_OK) {
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_map_t_from(
            &map,
            (const int_map_t_entry[]){{1, 10}, {3, 30}, {2, 20}},
            3) != TURBO_OK) {
        int_set_t_destroy(&set);
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_multi_map_t_from(
            &multimap,
            (const int_multi_map_t_entry[]){
                {1, 10}, {2, 20}, {1, 11}, {3, 30}},
            4) != TURBO_OK) {
        int_map_t_destroy(&map);
        int_set_t_destroy(&set);
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_heap_t_from(&heap, (int[]){7, 2, 9, 1, 5}, 5) != TURBO_OK) {
        int_multi_map_t_destroy(&multimap);
        int_map_t_destroy(&map);
        int_set_t_destroy(&set);
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_tree_map_t_from(
            &tree,
            (const int_tree_map_t_entry[]){{5, 50}, {1, 10}, {3, 30}},
            3) != TURBO_OK) {
        int_heap_t_destroy(&heap);
        int_multi_map_t_destroy(&multimap);
        int_map_t_destroy(&map);
        int_set_t_destroy(&set);
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }
    if (int_bplus_map_t_from(
            &bpt,
            (const int_bplus_map_t_entry[]){{6, 60}, {2, 20}, {8, 80}},
            3) != TURBO_OK) {
        int_tree_map_t_destroy(&tree);
        int_heap_t_destroy(&heap);
        int_multi_map_t_destroy(&multimap);
        int_map_t_destroy(&map);
        int_set_t_destroy(&set);
        int_list_t_destroy(&list);
        int_deque_t_destroy(&deque);
        int_vec_t_destroy(&vec);
        return 1;
    }

    puts("vector even:");
    stream_t *s = STREAM_FROM_TURBO_VEC(&stream, &vec.raw);
    if (!s || s->filter(s, int_is_even)->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("deque positive:");
    s = STREAM_FROM_TURBO_DEQUE(&stream, &deque.raw);
    if (!s || s->filter(s, int_is_pos)->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("map values:");
    s = STREAM_FROM_TURBO_MAP_VALUES(&stream, &map.raw);
    if (!s || s->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("multimap keys:");
    s = STREAM_FROM_TURBO_MULTIMAP_KEYS(&stream, &multimap.raw);
    if (!s ||
        s->distinct(s, 16, int_equal)->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("heap internal order:");
    s = STREAM_FROM_TURBO_HEAP(&stream, &heap.raw);
    if (!s || s->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("tree keys:");
    s = STREAM_FROM_TURBO_TREE_MAP_KEYS(&stream, &tree.raw);
    if (!s || s->for_each(s, print_int) != STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    puts("b+ values:");
    s = STREAM_FROM_TURBO_BPLUS_TREE_VALUES(&stream, &bpt.raw);
    if (!s || s->for_each(s, print_int) !=
        STREAM_END) {
        puts("unexpected");
        goto cleanup;
    }
    puts("");

    goto destroy;

cleanup:
    int_bplus_map_t_destroy(&bpt);
    int_tree_map_t_destroy(&tree);
    int_heap_t_destroy(&heap);
    int_multi_map_t_destroy(&multimap);
    int_map_t_destroy(&map);
    int_set_t_destroy(&set);
    int_list_t_destroy(&list);
    int_deque_t_destroy(&deque);
    int_vec_t_destroy(&vec);
    return 1;

destroy:
    int_bplus_map_t_destroy(&bpt);
    int_tree_map_t_destroy(&tree);
    int_heap_t_destroy(&heap);
    int_multi_map_t_destroy(&multimap);
    int_map_t_destroy(&map);
    int_set_t_destroy(&set);
    int_list_t_destroy(&list);
    int_deque_t_destroy(&deque);
    int_vec_t_destroy(&vec);
    return 0;
}
