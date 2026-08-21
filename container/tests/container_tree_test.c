#include <turbo/container/typed.h>
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

spec("Container trees") {
    it("splits BTree nodes and iterates entries in key order") {
        IntTree tree = {0};
        cmeta_range range;
        size_t cursor = 0u;
        IntTree_entry entry = {0};
        int key;

        check_equal(IntTree_init(&tree, 32u), CONTAINER_OK);
        for (key = 31; key >= 0; --key)
            check_equal(IntTree_put(&tree, key, (long)key * 10L), CONTAINER_OK);
        check_equal(IntTree_size(&tree), (size_t)32u);
        check_true(cmeta_container_range_view(&tree,
                                              CMETA_CONTAINER_VIEW_DEFAULT,
                                              &range));
        for (key = 0; key < 32; ++key) {
            check_true(cmeta_range_next(&range, &cursor, &entry) == CMETA_GEN_VALUE ||
                       cursor == 32u);
            check_equal(entry.key, key);
            check_equal(entry.value, (long)key * 10L);
        }
        IntTree_destroy(&tree);
    }

    it("replaces before destroy removes by transfer and enforces tree limits") {
        IntPlusTree tree = {0};
        long out = 91L;

        check_equal(IntPlusTree_init(&tree, 1u), CONTAINER_OK);
        check_equal(IntPlusTree_put(&tree, 1, 10L), CONTAINER_OK);
        check_equal(IntPlusTree_put(&tree, 1, 20L), CONTAINER_OK);
        check_equal(*IntPlusTree_get_const(&tree, 1), 20L);
        check_equal(IntPlusTree_put(&tree, 2, 30L),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(IntPlusTree_remove(&tree, 1, &out), CONTAINER_OK);
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
                    CONTAINER_INVALID_ARGUMENT);
        check_equal(turbo_btree_init_bytes(&tree, sizeof(int), _Alignof(int),
                                           sizeof(long), _Alignof(long), 2u,
                                           raw_int_compare, NULL), CONTAINER_OK);
        before = tree;
        check_equal(turbo_btree_init_bytes(&tree, sizeof(int), _Alignof(int),
                                           sizeof(long), _Alignof(long), 2u,
                                           raw_int_compare, NULL),
                    CONTAINER_INVALID_ARGUMENT);
        check_equal(memcmp(&tree, &before, sizeof(tree)), 0);
        turbo_btree_destroy(&tree);
    }

    it("builds a real split BPlusTree and invalidates its ordered Range") {
        IntPlusTree tree = {0};
        cmeta_range range;
        IntPlusTree_entry entry = {0};
        size_t cursor = 0u;
        uint64_t generation;
        int key;

        check_equal(IntPlusTree_init_with_order(&tree, 2u, 48u), CONTAINER_OK);
        for (key = 47; key >= 0; --key)
            check_equal(IntPlusTree_put(&tree, key, (long)key + 100L),
                        CONTAINER_OK);
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
        check_equal(IntPlusTree_put(&tree, 12, 999L), CONTAINER_OK);
        check_equal(turbo_bplus_tree_generation(&tree.raw), generation + 1u);
        cursor = 0u;
        entry.key = -1;
        check_equal(cmeta_range_next(&range, &cursor, &entry),
                    CMETA_GEN_MUTATED);
        check_equal(cursor, (size_t)0u);
        check_equal(entry.key, -1);
        IntPlusTree_destroy(&tree);
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
                                     &owned_tree_type, 2u), CONTAINER_OK);
        check_equal(turbo_btree_put(&tree, &key, &value), CONTAINER_OK);
        check_equal(turbo_btree_put(&tree, &key, &replacement), CONTAINER_OK);
        stored = (const owned_tree_value *)turbo_btree_get_const(&tree, &key);
        check_true(stored != NULL && *stored->value == 20);

        generation = turbo_btree_generation(&tree);
        owned_tree_fail_copy = true;
        check_equal(turbo_btree_put(&tree, &failed_key, &failed_value),
                    CONTAINER_OUT_OF_MEMORY);
        check_equal(turbo_btree_generation(&tree), generation);
        check_equal(turbo_btree_size(&tree), (size_t)1u);
        stored = (const owned_tree_value *)turbo_btree_get_const(&tree, &key);
        check_true(stored != NULL && *stored->value == 20);

        owned_tree_fail_copy = false;
        check_equal(turbo_btree_put(&tree, turbo_btree_key_at_const(&tree, 0u),
                                    turbo_btree_value_at_const(&tree, 0u)),
                    CONTAINER_OK);
        check_equal(turbo_btree_remove(&tree, &key, &out), CONTAINER_OK);
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
                    CONTAINER_OK);
        for (index = 0u; index < 8u; ++index)
            check_equal(turbo_bplus_tree_put(&tree, &keys[index],
                                             &values[index]), CONTAINER_OK);
        check_true(tree.root != NULL && !tree.root->is_leaf);
        generation = turbo_bplus_tree_generation(&tree);

        owned_tree_fail_copy = true;
        check_equal(turbo_bplus_tree_from_arrays(
                        &tree, keys, values, 8u, &owned_tree_type,
                        &owned_tree_type, 8u), CONTAINER_OUT_OF_MEMORY);
        check_equal(turbo_bplus_tree_generation(&tree), generation);
        check_equal(turbo_bplus_tree_size(&tree), (size_t)8u);

        owned_tree_fail_copy = false;
        check_equal(turbo_bplus_tree_remove(&tree, &keys[0], &out),
                    CONTAINER_OK);
        check_true(out.value != NULL && *out.value == 100);
        turbo_bplus_tree_destroy(&tree);
        owned_tree_destroy(&out);
        for (index = 0u; index < 8u; ++index) {
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
                    CONTAINER_TRAIT_MISSING);
        check_equal(turbo_btree_init_bytes_with_order(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), SIZE_MAX, 1u, raw_int_compare, NULL),
                    CONTAINER_CAPACITY_EXCEEDED);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), 3u, sizeof(long), _Alignof(long),
                        1u, raw_int_compare, NULL),
                    CONTAINER_INVALID_ARGUMENT);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    CONTAINER_OK);
        turbo_btree_destroy(&tree);
        destroyed_generation = turbo_btree_generation(&tree);
        check_true(destroyed_generation != 0u);
        check_equal(turbo_btree_init_bytes(
                        &tree, sizeof(int), _Alignof(int), sizeof(long),
                        _Alignof(long), 1u, raw_int_compare, NULL),
                    CONTAINER_OK);
        check_true(turbo_btree_generation(&tree) > destroyed_generation);
        turbo_btree_destroy(&tree);
        turbo_btree_destroy(&tree);
    }
}
