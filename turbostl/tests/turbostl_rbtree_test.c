#include "../src/rbtree_internal.h"
#include "tinytest.h"

static int compare_int(const void *left, const void *right, void *context) {
    int lhs = *(const int *)left;
    int rhs = *(const int *)right;
    (void)context;
    return (lhs > rhs) - (lhs < rhs);
}

static bool validate_subtree(const rbtree_node_t *node,
                             const rbtree_node_t *parent,
                             size_t *out_black_height,
                             size_t *out_count) {
    size_t left_height;
    size_t right_height;
    size_t left_count;
    size_t right_count;

    if (node == NULL) {
        *out_black_height = 1u;
        *out_count = 0u;
        return true;
    }
    if (node->parent != parent)
        return false;
    if (node->red && ((node->left != NULL && node->left->red) ||
                      (node->right != NULL && node->right->red)))
        return false;
    if (!validate_subtree(node->left, node, &left_height, &left_count) ||
        !validate_subtree(node->right, node, &right_height, &right_count) ||
        left_height != right_height)
        return false;
    *out_black_height = left_height + (node->red ? 0u : 1u);
    *out_count = left_count + right_count + 1u;
    return true;
}

static bool validate_tree(const rbtree_t *tree) {
    const rbtree_node_t *node;
    const rbtree_node_t *previous = NULL;
    size_t black_height;
    size_t node_count;
    size_t linked_count = 0u;

    if (tree->root != NULL && tree->root->red)
        return false;
    if (!validate_subtree(tree->root, NULL, &black_height, &node_count) ||
        node_count != tree->size)
        return false;
    node = tree->head;
    while (node != NULL) {
        if (node->previous != previous)
            return false;
        if (previous != NULL &&
            *(const int *)previous->key >= *(const int *)node->key)
            return false;
        previous = node;
        node = node->next;
        ++linked_count;
    }
    return linked_count == tree->size && previous == tree->tail;
}

spec("Internal red-black tree invariants") {
    it("preserves coloring black height and sorted links across churn") {
        rbtree_t *tree = NULL;
        enum { item_count = 257 };
        int keys[item_count];
        int value;
        int index;

        check_equal(rbtree_create(
                        &tree, NULL, NULL, sizeof(int), _Alignof(int),
                        sizeof(int), _Alignof(int), item_count, compare_int,
                        NULL, false), TURBO_STL_OK);
        for (index = 0; index < item_count; ++index)
            keys[index] = (index * 73) % item_count;
        for (index = 0; index < item_count; ++index) {
            rbtree_put_result result;
            value = keys[index] * 10;
            check_equal(rbtree_put(tree, &keys[index], &value, &result),
                        TURBO_STL_OK);
            check_equal(result, TURBO_RBTREE_INSERTED);
            check_true(validate_tree(tree));
        }
        for (index = 0; index < item_count; ++index) {
            rbtree_node_t *node = rbtree_find(tree, &keys[index]);
            check_not_null(node);
            check_equal(rbtree_remove_node(tree, node, NULL),
                        TURBO_STL_OK);
            check_true(validate_tree(tree));
        }
        check_true(tree->root == NULL && tree->head == NULL &&
                   tree->tail == NULL);
        rbtree_destroy(tree);
    }
}
