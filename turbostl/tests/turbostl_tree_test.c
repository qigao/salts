#include <turbostl/typed.h>
#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typed(BTree, IntTree, int, long);
typed(BPlusTree, IntPlusTree, int, long);

static int raw_int_compare(const void *left, const void *right, void *context) {
    int lhs = *(const int *)left;
    int rhs = *(const int *)right;
    (void)context;
    return (lhs > rhs) - (lhs < rhs);
}

static size_t counted_compare_calls;

static int counted_int_compare(const void *left, const void *right,
                               void *context) {
    ++counted_compare_calls;
    return raw_int_compare(left, right, context);
}

static uint32_t tree_test_random_state = UINT32_C(0x5eed1234);

static uint32_t tree_test_random(void) {
    uint32_t value = tree_test_random_state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    tree_test_random_state = value;
    return value;
}

static size_t bplus_tree_height(const turbo_bplus_tree_t *tree) {
    const turbo_bplus_tree_node_t *node = tree->root;
    size_t height = 0u;
    while (node != NULL) {
        ++height;
        node = node->is_leaf ? NULL : node->children[0];
    }
    return height;
}

static bool bplus_metadata_is_valid(const turbo_bplus_tree_node_t *node) {
    size_t index;
    if (node == NULL) return true;
    if (node->is_leaf)
        return node->first_key ==
               (node->num_keys == 0u ? NULL : node->keys[0]);
    if (node->children[0] == NULL ||
        node->first_key != node->children[0]->first_key)
        return false;
    for (index = 0u; index <= node->num_keys; ++index) {
        if (node->children[index] == NULL ||
            !bplus_metadata_is_valid(node->children[index]))
            return false;
        if (index != 0u &&
            node->keys[index - 1u] != node->children[index]->first_key)
            return false;
    }
    return true;
}

typedef struct owned_tree_value {
    int *value;
} owned_tree_value;

static size_t owned_tree_live;
static bool owned_tree_fail_copy;

static owned_tree_value owned_tree_make(int value) {
    owned_tree_value result;
    result.value = (int *)malloc(sizeof(*result.value));
    if (result.value != NULL) {
        *result.value = value;
        ++owned_tree_live;
    }
    return result;
}

static int owned_tree_compare(const void *left_, const void *right_) {
    const owned_tree_value *left = (const owned_tree_value *)left_;
    const owned_tree_value *right = (const owned_tree_value *)right_;
    return (*left->value > *right->value) - (*left->value < *right->value);
}

static bool owned_tree_copy(void *destination_, const void *source_) {
    owned_tree_value *destination = (owned_tree_value *)destination_;
    const owned_tree_value *source = (const owned_tree_value *)source_;
    if (owned_tree_fail_copy || source == NULL || source->value == NULL)
        return false;
    *destination = owned_tree_make(*source->value);
    return destination->value != NULL;
}

static void owned_tree_move(void *destination_, void *source_) {
    owned_tree_value *destination = (owned_tree_value *)destination_;
    owned_tree_value *source = (owned_tree_value *)source_;
    destination->value = source->value;
    source->value = NULL;
}

static void owned_tree_destroy(void *value_) {
    owned_tree_value *value = (owned_tree_value *)value_;
    if (value != NULL && value->value != NULL) {
        free(value->value);
        value->value = NULL;
        --owned_tree_live;
    }
}

static const cmeta_type_traits owned_tree_traits = {
    CMETA_TRAIT_COMPARE | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
        CMETA_TRAIT_DESTROY,
    NULL, NULL, owned_tree_compare, owned_tree_copy, owned_tree_move,
    owned_tree_destroy
};

static const cmeta_type_desc owned_tree_type = {
    "owned_tree_value", sizeof(owned_tree_value), _Alignof(owned_tree_value),
    CMETA_T_OBJECT, NULL, &owned_tree_traits
};

static const cmeta_type_traits missing_compare_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, owned_tree_copy, owned_tree_move, owned_tree_destroy
};

static const cmeta_type_desc missing_compare_type = {
    "missing_compare", sizeof(owned_tree_value), _Alignof(owned_tree_value),
    CMETA_T_OBJECT, NULL, &missing_compare_traits
};

spec("TurboSTL trees") {
    it("splits BTree nodes and iterates entries in key order") {
        IntTree tree = {0};
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        IntTree_entry entry = {0};
        int key;

        check_equal(IntTree_init(&tree, 32u), TURBO_STL_OK);
        for (key = 31; key >= 0; --key)
            check_equal(IntTree_put(&tree, key, (long)key * 10L), TURBO_STL_OK);
        check_equal(IntTree_size(&tree), (size_t)32u);
        check_true(cmeta_container_range_view(&tree,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        for (key = 0; key < 32; ++key) {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor, &entry);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
            check_equal(entry.key, key);
            check_equal(entry.value, (long)key * 10L);
        }
        IntTree_destroy(&tree);
    }

    it("replaces before destroy removes by transfer and enforces tree limits") {
        IntPlusTree tree = {0};
        long out = 91L;

        check_equal(IntPlusTree_init(&tree, 1u), TURBO_STL_OK);
        check_equal(IntPlusTree_put(&tree, 1, 10L), TURBO_STL_OK);
        check_equal(IntPlusTree_put(&tree, 1, 20L), TURBO_STL_OK);
        check_equal(*IntPlusTree_get_const(&tree, 1), 20L);
        check_equal(IntPlusTree_put(&tree, 2, 30L),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(IntPlusTree_remove(&tree, 1, &out), TURBO_STL_OK);
        check_equal(out, 20L);
        check_true(IntPlusTree_empty(&tree));
        IntPlusTree_destroy(&tree);
    }

    it("rejects raw trees without explicit comparator and preserves live handles") {
        turbo_btree_t tree = {0};
        turbo_btree_t before;

        check_equal(turbo_btree_init_bytes(&tree, sizeof(int), _Alignof(int),
                                           sizeof(long), _Alignof(long), 2u,
                                           NULL, NULL),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(turbo_btree_init_bytes(&tree, sizeof(int), _Alignof(int),
                                           sizeof(long), _Alignof(long), 2u,
                                           raw_int_compare, NULL), TURBO_STL_OK);
        before = tree;
        check_equal(turbo_btree_init_bytes(&tree, sizeof(int), _Alignof(int),
                                           sizeof(long), _Alignof(long), 2u,
                                           raw_int_compare, NULL),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(memcmp(&tree, &before, sizeof(tree)), 0);
        turbo_btree_destroy(&tree);
    }

    it("builds a real split BPlusTree and invalidates its ordered Range") {
        IntPlusTree tree = {0};
        cmeta_range range;
        IntPlusTree_entry entry = {0};
        cmeta_range_cursor cursor = {0};
        cmeta_range_cursor before_cursor;
        uint64_t generation;
        int key;

        check_equal(IntPlusTree_init_with_order(&tree, 2u, 48u), TURBO_STL_OK);
        for (key = 47; key >= 0; --key)
            check_equal(IntPlusTree_put(&tree, key, (long)key + 100L),
                        TURBO_STL_OK);
        check_true(tree.raw.root != NULL && !tree.raw.root->is_leaf);
        check_true(cmeta_container_range_view(&tree,
                                              CMETA_CONTAINER_VIEW_ENTRIES,
                                              &range));
        for (key = 0; key < 48; ++key) {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor, &entry);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
            check_equal(entry.key, key);
            check_equal(entry.value, (long)key + 100L);
        }
        generation = turbo_bplus_tree_generation(&tree.raw);
        check_equal(IntPlusTree_put(&tree, 12, 999L), TURBO_STL_OK);
        check_equal(turbo_bplus_tree_generation(&tree.raw), generation + 1u);
        memset(&cursor, 0, sizeof(cursor));
        before_cursor = cursor;
        entry.key = -1;
        check_equal(cmeta_range_next(&range, &cursor, &entry),
                    CMETA_GEN_MUTATED);
        check_equal(memcmp(&cursor, &before_cursor, sizeof(cursor)), 0);
        check_equal(entry.key, -1);
        IntPlusTree_destroy(&tree);
    }

    it("shrinks a BPlusTree root without revisiting the retired root") {
        IntPlusTree tree = {0};
        turbo_bplus_tree_node_t *retired_root;
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        IntPlusTree_entry entry = {0};
        uint64_t generation;
        long out = -1L;
        int key;

        check_equal(IntPlusTree_init_with_order(&tree, 2u, 4u), TURBO_STL_OK);
        for (key = 0; key < 4; ++key)
            check_equal(IntPlusTree_put(&tree, key, (long)key + 100L),
                        TURBO_STL_OK);
        check_equal(bplus_tree_height(&tree.raw), (size_t)2u);
        retired_root = tree.raw.root;
        generation = turbo_bplus_tree_generation(&tree.raw);

        check_equal(IntPlusTree_remove(&tree, 0, &out), TURBO_STL_OK);
        check_equal(out, 100L);
        check_equal(turbo_bplus_tree_generation(&tree.raw), ++generation);
        check_equal(IntPlusTree_remove(&tree, 1, &out), TURBO_STL_OK);
        check_equal(out, 101L);
        check_equal(turbo_bplus_tree_generation(&tree.raw), ++generation);
        check_equal(bplus_tree_height(&tree.raw), (size_t)2u);
        check_equal(IntPlusTree_remove(&tree, 2, &out), TURBO_STL_OK);
        check_equal(out, 102L);
        check_equal(turbo_bplus_tree_generation(&tree.raw), ++generation);
        check_not_equal((const void *)tree.raw.root,
                        (const void *)retired_root);
        check_true(tree.raw.root != NULL && tree.raw.root->is_leaf);
        check_true(tree.raw.root->parent == NULL);
        check_equal(bplus_tree_height(&tree.raw), (size_t)1u);
        check_true(bplus_metadata_is_valid(tree.raw.root));

        range = IntPlusTree_entries_range(&tree);
        for (key = 3; key < 4; ++key) {
            cmeta_gen_status status = cmeta_range_next(&range, &cursor,
                                                       &entry);
            check_true(status == CMETA_GEN_VALUE ||
                       status == CMETA_GEN_VALUE_AND_DONE);
            check_equal(entry.key, key);
            check_equal(entry.value, (long)key + 100L);
        }
        check_equal(cmeta_range_next(&range, &cursor, &entry),
                    CMETA_GEN_DONE);

        check_equal(IntPlusTree_size(&tree), (size_t)1u);
        check_equal(*IntPlusTree_get_const(&tree, 3), 103L);
        IntPlusTree_destroy(&tree);
    }

    it("bounds BPlusTree separator maintenance to the modified path") {
        turbo_bplus_tree_t tree = {0};
        enum { ENTRY_COUNT = 4096, MAX_VISITS_PER_LEVEL = 5 };
        uint64_t visits_before;
        size_t height;
        int key;
        long value;

        check_equal(turbo_bplus_tree_init_bytes_with_order(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 2u, ENTRY_COUNT,
                        raw_int_compare, NULL), TURBO_STL_OK);
        for (key = 0; key < ENTRY_COUNT; ++key) {
            value = (long)key;
            check_equal(turbo_bplus_tree_put(&tree, &key, &value),
                        TURBO_STL_OK);
        }
        height = bplus_tree_height(&tree);
        visits_before = tree.maintenance_node_visits;
        key = ENTRY_COUNT - 1;
        check_equal(turbo_bplus_tree_remove(&tree, &key, NULL), TURBO_STL_OK);
        check_less_equal(tree.maintenance_node_visits - visits_before,
                         height * MAX_VISITS_PER_LEVEL);
        turbo_bplus_tree_destroy(&tree);
    }

    it("keeps single tree mutations on one logarithmic search path") {
        turbo_btree_t btree = {0};
        turbo_bplus_tree_t bplus = {0};
        enum { ENTRY_COUNT = 256, MAX_PATH_COMPARISONS = 128 };
        int key;
        long value;

        check_equal(turbo_btree_init_bytes_with_order(
                        &btree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 2u, ENTRY_COUNT + 1u,
                        counted_int_compare, NULL), TURBO_STL_OK);
        check_equal(turbo_bplus_tree_init_bytes_with_order(
                        &bplus, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 2u, ENTRY_COUNT + 1u,
                        counted_int_compare, NULL), TURBO_STL_OK);
        for (key = 0; key < ENTRY_COUNT; ++key) {
            value = (long)key;
            check_equal(turbo_btree_put(&btree, &key, &value), TURBO_STL_OK);
            check_equal(turbo_bplus_tree_put(&bplus, &key, &value),
                        TURBO_STL_OK);
        }

        key = ENTRY_COUNT / 2;
        value = 999L;
        counted_compare_calls = 0u;
        check_equal(turbo_btree_put(&btree, &key, &value), TURBO_STL_OK);
        check_true(counted_compare_calls < MAX_PATH_COMPARISONS);
        counted_compare_calls = 0u;
        check_equal(turbo_bplus_tree_put(&bplus, &key, &value), TURBO_STL_OK);
        check_true(counted_compare_calls < MAX_PATH_COMPARISONS);

        counted_compare_calls = 0u;
        check_equal(turbo_btree_remove(&btree, &key, NULL), TURBO_STL_OK);
        check_true(counted_compare_calls < MAX_PATH_COMPARISONS);
        counted_compare_calls = 0u;
        check_equal(turbo_bplus_tree_remove(&bplus, &key, NULL), TURBO_STL_OK);
        check_true(counted_compare_calls < MAX_PATH_COMPARISONS);

        turbo_bplus_tree_destroy(&bplus);
        turbo_btree_destroy(&btree);
    }

    it("builds raw trees by final live keys and commits once") {
        turbo_btree_t btree = {0};
        turbo_bplus_tree_t bplus = {0};
        int duplicate_keys[] = {1, 1};
        int distinct_keys[] = {2, 3};
        long duplicate_values[] = {10L, 11L};
        long distinct_values[] = {20L, 30L};

        check_equal(turbo_btree_from_arrays_bytes(
                        &btree, duplicate_keys, duplicate_values, 2u,
                        sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_OK);
        check_equal(turbo_btree_size(&btree), (size_t)1u);
        check_equal(*(const long *)turbo_btree_get_const(&btree,
                                                         &duplicate_keys[0]),
                    11L);
        check_equal(turbo_btree_generation(&btree), UINT64_C(1));
        check_equal(turbo_btree_from_arrays_bytes(
                        &btree, distinct_keys, distinct_values, 2u,
                        sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(turbo_btree_generation(&btree), UINT64_C(1));
        check_equal(*(const long *)turbo_btree_get_const(&btree,
                                                         &duplicate_keys[0]),
                    11L);

        check_equal(turbo_bplus_tree_from_arrays_bytes(
                        &bplus, duplicate_keys, duplicate_values, 2u,
                        sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_OK);
        check_equal(turbo_bplus_tree_size(&bplus), (size_t)1u);
        check_equal(*(const long *)turbo_bplus_tree_get_const(
                        &bplus, &duplicate_keys[0]), 11L);
        check_equal(turbo_bplus_tree_generation(&bplus), UINT64_C(1));
        check_equal(turbo_bplus_tree_from_arrays_bytes(
                        &bplus, distinct_keys, distinct_values, 2u,
                        sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(turbo_bplus_tree_generation(&bplus), UINT64_C(1));
        check_equal(*(const long *)turbo_bplus_tree_get_const(
                        &bplus, &duplicate_keys[0]), 11L);

        turbo_bplus_tree_destroy(&bplus);
        turbo_btree_destroy(&btree);
    }

    it("matches a bounded model across randomized split borrow and merge paths") {
        IntTree btree = {0};
        IntPlusTree bplus = {0};
        enum { KEY_COUNT = 128, OPERATION_COUNT = 2000 };
        bool present[KEY_COUNT] = {false};
        long model[KEY_COUNT] = {0};
        size_t live = 0u;
        int operation;

        tree_test_random_state = UINT32_C(0x5eed1234);
        check_equal(IntTree_init_with_order(&btree, 2u, KEY_COUNT),
                    TURBO_STL_OK);
        check_equal(IntPlusTree_init_with_order(&bplus, 2u, KEY_COUNT),
                    TURBO_STL_OK);
        for (operation = 0; operation < OPERATION_COUNT; ++operation) {
            int key = (int)(tree_test_random() % KEY_COUNT);
            if ((tree_test_random() & 3u) != 0u) {
                long value = (long)operation + 1000L;
                check_equal(IntTree_put(&btree, key, value), TURBO_STL_OK);
                check_equal(IntPlusTree_put(&bplus, key, value), TURBO_STL_OK);
                if (!present[key]) {
                    present[key] = true;
                    ++live;
                }
                model[key] = value;
            } else if (present[key]) {
                long btree_out = -1L;
                long bplus_out = -1L;
                check_equal(IntTree_remove(&btree, key, &btree_out),
                            TURBO_STL_OK);
                check_equal(IntPlusTree_remove(&bplus, key, &bplus_out),
                            TURBO_STL_OK);
                check_equal(btree_out, model[key]);
                check_equal(bplus_out, model[key]);
                present[key] = false;
                --live;
            } else {
                check_equal(IntTree_remove(&btree, key, NULL),
                            TURBO_STL_NOT_FOUND);
                check_equal(IntPlusTree_remove(&bplus, key, NULL),
                            TURBO_STL_NOT_FOUND);
            }

            if ((operation % 97) == 0) {
                int probe;
                check_equal(IntTree_size(&btree), live);
                check_equal(IntPlusTree_size(&bplus), live);
                check_true(bplus_metadata_is_valid(bplus.raw.root));
                for (probe = 0; probe < KEY_COUNT; ++probe) {
                    const long *left = IntTree_get_const(&btree, probe);
                    const long *right = IntPlusTree_get_const(&bplus, probe);
                    check_equal(left != NULL, present[probe]);
                    check_equal(right != NULL, present[probe]);
                    if (present[probe]) {
                        check_equal(*left, model[probe]);
                        check_equal(*right, model[probe]);
                    }
                }
            }
        }
        {
            cmeta_range btree_range = IntTree_entries_range(&btree);
            cmeta_range bplus_range = IntPlusTree_entries_range(&bplus);
            cmeta_range_cursor btree_cursor = {0};
            cmeta_range_cursor bplus_cursor = {0};
            IntTree_entry btree_entry = {0};
            IntPlusTree_entry bplus_entry = {0};
            size_t observed = 0u;
            int ordered_key;
            for (ordered_key = 0; ordered_key < KEY_COUNT; ++ordered_key) {
                if (!present[ordered_key]) continue;
                cmeta_gen_status btree_status = cmeta_range_next(
                    &btree_range, &btree_cursor, &btree_entry);
                cmeta_gen_status bplus_status = cmeta_range_next(
                    &bplus_range, &bplus_cursor, &bplus_entry);
                check_true(btree_status == CMETA_GEN_VALUE ||
                           btree_status == CMETA_GEN_VALUE_AND_DONE);
                check_true(bplus_status == CMETA_GEN_VALUE ||
                           bplus_status == CMETA_GEN_VALUE_AND_DONE);
                check_equal(btree_entry.key, ordered_key);
                check_equal(bplus_entry.key, ordered_key);
                check_equal(btree_entry.value, model[ordered_key]);
                check_equal(bplus_entry.value, model[ordered_key]);
                ++observed;
            }
            check_equal(observed, live);
            check_equal(cmeta_range_next(&btree_range, &btree_cursor,
                                         &btree_entry), CMETA_GEN_DONE);
            check_equal(cmeta_range_next(&bplus_range, &bplus_cursor,
                                         &bplus_entry), CMETA_GEN_DONE);
        }
        IntPlusTree_destroy(&bplus);
        IntTree_destroy(&btree);
    }

    it("keeps owning BTree mutations transactional and transfers removal") {
        turbo_btree_t tree = {0};
        owned_tree_value key = owned_tree_make(1);
        owned_tree_value value = owned_tree_make(10);
        owned_tree_value replacement = owned_tree_make(20);
        owned_tree_value failed_key = owned_tree_make(2);
        owned_tree_value failed_value = owned_tree_make(30);
        owned_tree_value out = {0};
        const owned_tree_value *stored;
        uint64_t generation;

        owned_tree_fail_copy = false;
        check_equal(turbo_btree_init(&tree, &owned_tree_type,
                                     &owned_tree_type, 2u), TURBO_STL_OK);
        check_equal(turbo_btree_put(&tree, &key, &value), TURBO_STL_OK);
        check_equal(turbo_btree_put(&tree, &key, &replacement), TURBO_STL_OK);
        stored = (const owned_tree_value *)turbo_btree_get_const(&tree, &key);
        check_true(stored != NULL && *stored->value == 20);

        generation = turbo_btree_generation(&tree);
        owned_tree_fail_copy = true;
        check_equal(turbo_btree_put(&tree, &failed_key, &failed_value),
                    TURBO_STL_OUT_OF_MEMORY);
        check_equal(turbo_btree_generation(&tree), generation);
        check_equal(turbo_btree_size(&tree), (size_t)1u);
        stored = (const owned_tree_value *)turbo_btree_get_const(&tree, &key);
        check_true(stored != NULL && *stored->value == 20);

        owned_tree_fail_copy = false;
        check_equal(turbo_btree_put(&tree, turbo_btree_key_at_const(&tree, 0u),
                                    turbo_btree_value_at_const(&tree, 0u)),
                    TURBO_STL_OK);
        check_equal(turbo_btree_remove(&tree, &key, &out), TURBO_STL_OK);
        check_true(out.value != NULL && *out.value == 20);
        check_true(turbo_btree_empty(&tree));
        turbo_btree_destroy(&tree);

        owned_tree_destroy(&out);
        owned_tree_destroy(&failed_value);
        owned_tree_destroy(&failed_key);
        owned_tree_destroy(&replacement);
        owned_tree_destroy(&value);
        owned_tree_destroy(&key);
        check_equal(owned_tree_live, (size_t)0u);
    }

    it("keeps owning BPlusTree split and from-arrays failure transactional") {
        turbo_bplus_tree_t tree = {0};
        owned_tree_value keys[8];
        owned_tree_value values[8];
        owned_tree_value out = {0};
        uint64_t generation;
        size_t index;

        owned_tree_fail_copy = false;
        for (index = 0u; index < 8u; ++index) {
            keys[index] = owned_tree_make((int)index);
            values[index] = owned_tree_make((int)index + 100);
        }
        check_equal(turbo_bplus_tree_init_with_order(
                        &tree, &owned_tree_type, &owned_tree_type, 2u, 8u),
                    TURBO_STL_OK);
        for (index = 0u; index < 8u; ++index)
            check_equal(turbo_bplus_tree_put(&tree, &keys[index],
                                             &values[index]), TURBO_STL_OK);
        check_true(tree.root != NULL && !tree.root->is_leaf);
        generation = turbo_bplus_tree_generation(&tree);

        owned_tree_fail_copy = true;
        check_equal(turbo_bplus_tree_from_arrays(
                        &tree, keys, values, 8u, &owned_tree_type,
                        &owned_tree_type, 8u), TURBO_STL_OUT_OF_MEMORY);
        check_equal(turbo_bplus_tree_generation(&tree), generation);
        check_equal(turbo_bplus_tree_size(&tree), (size_t)8u);

        owned_tree_fail_copy = false;
        check_equal(turbo_bplus_tree_remove(&tree, &keys[0], &out),
                    TURBO_STL_OK);
        check_true(out.value != NULL && *out.value == 100);
        turbo_bplus_tree_destroy(&tree);
        owned_tree_destroy(&out);
        for (index = 0u; index < 8u; ++index) {
            owned_tree_destroy(&values[index]);
            owned_tree_destroy(&keys[index]);
        }
        check_equal(owned_tree_live, (size_t)0u);
    }

    it("preserves owning entries while shrinking a BPlusTree root") {
        turbo_bplus_tree_t tree = {0};
        owned_tree_value keys[4];
        owned_tree_value values[4];
        uint64_t generation;
        cmeta_range_cursor cursor = {0};
        size_t index;
        const void *range_key = NULL;
        const void *range_value = NULL;

        check_equal(owned_tree_live, (size_t)0u);
        owned_tree_fail_copy = false;
        for (index = 0u; index < 4u; ++index) {
            keys[index] = owned_tree_make((int)index);
            values[index] = owned_tree_make((int)index + 100);
        }
        check_equal(turbo_bplus_tree_init_with_order(
                        &tree, &owned_tree_type, &owned_tree_type, 2u, 4u),
                    TURBO_STL_OK);
        for (index = 0u; index < 4u; ++index)
            check_equal(turbo_bplus_tree_put(&tree, &keys[index],
                                             &values[index]), TURBO_STL_OK);
        check_equal(bplus_tree_height(&tree), (size_t)2u);
        generation = turbo_bplus_tree_generation(&tree);
        for (index = 0u; index < 3u; ++index) {
            owned_tree_value out = {0};
            check_equal(turbo_bplus_tree_remove(&tree, &keys[index], &out),
                        TURBO_STL_OK);
            check_true(out.value != NULL &&
                       *out.value == (int)index + 100);
            check_equal(turbo_bplus_tree_generation(&tree), ++generation);
            owned_tree_destroy(&out);
        }
        check_true(tree.root != NULL && tree.root->is_leaf);
        check_true(tree.root->parent == NULL);
        check_true(bplus_metadata_is_valid(tree.root));
        check_true(turbo_bplus_tree_range_next(
            &tree, &cursor, &range_key, &range_value));
        check_true(range_key != NULL &&
                   *((const owned_tree_value *)range_key)->value == 3);
        check_true(range_value != NULL &&
                   *((const owned_tree_value *)range_value)->value == 103);
        check_false(turbo_bplus_tree_range_next(
            &tree, &cursor, &range_key, &range_value));
        turbo_bplus_tree_destroy(&tree);
        for (index = 0u; index < 4u; ++index) {
            owned_tree_destroy(&values[index]);
            owned_tree_destroy(&keys[index]);
        }
        check_equal(owned_tree_live, (size_t)0u);
    }

    it("validates semantic traits layout overflow and destroyed reuse") {
        turbo_btree_t tree = {0};
        uint64_t destroyed_generation;

        check_equal(turbo_btree_init(&tree, &missing_compare_type,
                                     &owned_tree_type, 1u),
                    TURBO_STL_TRAIT_MISSING);
        check_equal(turbo_btree_init_bytes_with_order(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), SIZE_MAX, 1u, raw_int_compare, NULL),
                    TURBO_STL_CAPACITY_EXCEEDED);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), 3u, sizeof(long), _Alignof(long),
                        1u, raw_int_compare, NULL),
                    TURBO_STL_INVALID_ARGUMENT);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_OK);
        turbo_btree_destroy(&tree);
        destroyed_generation = turbo_btree_generation(&tree);
        check_true(destroyed_generation != 0u);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    TURBO_STL_OK);
        check_true(turbo_btree_generation(&tree) > destroyed_generation);
        turbo_btree_destroy(&tree);
        turbo_btree_destroy(&tree);
    }

    bench("path-local tree replacement") {
        IntTree btree = {0};
        IntPlusTree bplus = {0};
        enum { BENCH_ENTRY_COUNT = 256 };
        int key;
        long value = 1L;

        check_equal(IntTree_init_with_order(&btree, 4u, BENCH_ENTRY_COUNT),
                    TURBO_STL_OK);
        check_equal(IntPlusTree_init_with_order(&bplus, 4u,
                                                BENCH_ENTRY_COUNT),
                    TURBO_STL_OK);
        for (key = 0; key < BENCH_ENTRY_COUNT; ++key) {
            check_equal(IntTree_put(&btree, key, (long)key), TURBO_STL_OK);
            check_equal(IntPlusTree_put(&bplus, key, (long)key),
                        TURBO_STL_OK);
        }
        key = BENCH_ENTRY_COUNT / 2;
        benchmark_ops("replace one BTree and one BPlusTree entry", 64, 2) {
            (void)IntTree_put(&btree, key, value);
            (void)IntPlusTree_put(&bplus, key, value);
            ++value;
        }
        IntPlusTree_destroy(&bplus);
        IntTree_destroy(&btree);
    }
}
