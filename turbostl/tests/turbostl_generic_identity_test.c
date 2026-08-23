#include <turbostl/typed.h>
#include "tinytest.h"

#include <string.h>

typedef struct generic_only_key {
    int value;
} generic_only_key;

static const cmeta_type_identity generic_only_key_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.generic_only_key");

static const cmeta_type_desc generic_only_key_type = {
    .name = "generic_only_key",
    .size = sizeof(generic_only_key),
    .align = _Alignof(generic_only_key),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = &generic_only_key_identity
};

/* Canonical constructors are part of the generic metadata contract. Keeping
 * these references direct makes the public-symbol requirement compile-time
 * visible instead of relying only on runtime descriptor traversal. */
static const cmeta_generic_desc *const canonical_constructors[] = {
    &stl_vec_generic_desc,
    &stl_deque_generic_desc,
    &stl_list_generic_desc,
    &stl_stack_generic_desc,
    &stl_queue_generic_desc,
    &stl_heap_generic_desc,
    &stl_set_generic_desc,
    &stl_hash_set_generic_desc,
    &stl_hash_map_generic_desc,
    &stl_map_generic_desc,
    &stl_multimap_generic_desc,
    &stl_btree_generic_desc,
    &stl_bplus_tree_generic_desc
};

#define CHECK_UNARY_APPLICATION(handle, stable_id_) do {                      \
    const cmeta_generic_desc *constructor_ =                                  \
        cmeta_container_type_constructor(&(handle));                           \
    check_true(cmeta_container_type_application_valid(&(handle)));             \
    check_not_null(constructor_);                                              \
    if (constructor_ != NULL)                                                  \
        check_equal(strcmp(constructor_->stable_id, (stable_id_)), 0);         \
    check_equal(cmeta_container_type_arity(&(handle)), (size_t)1u);            \
    check_true(cmeta_type_equal(                                               \
        cmeta_container_type_argument(&(handle), 0u), &cmeta_type_int));       \
    check_null(cmeta_container_type_argument(&(handle), 1u));                  \
} while (0)

#define CHECK_BINARY_APPLICATION(handle, stable_id_) do {                     \
    const cmeta_generic_desc *constructor_ =                                  \
        cmeta_container_type_constructor(&(handle));                           \
    check_true(cmeta_container_type_application_valid(&(handle)));             \
    check_not_null(constructor_);                                              \
    if (constructor_ != NULL)                                                  \
        check_equal(strcmp(constructor_->stable_id, (stable_id_)), 0);         \
    check_equal(cmeta_container_type_arity(&(handle)), (size_t)2u);            \
    check_true(cmeta_type_equal(                                               \
        cmeta_container_type_argument(&(handle), 0u), &cmeta_type_int));       \
    check_true(cmeta_type_equal(                                               \
        cmeta_container_type_argument(&(handle), 1u), &cmeta_type_long));      \
    check_null(cmeta_container_type_argument(&(handle), 2u));                  \
} while (0)

suite("TurboSTL generic type identities") {
    it("exposes canonical generic applications for every typed container kind") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        HashMap(int, long, hash_map);
        Map(int, long, map);
        MultiMap(int, long, multimap);
        BTree(int, long, btree);
        BPlusTree(int, long, bplus_tree);

        check_equal(sizeof(canonical_constructors) /
                        sizeof(canonical_constructors[0]),
                    (size_t)13u);
        CHECK_UNARY_APPLICATION(vec, "turbostl.Vec");
        CHECK_UNARY_APPLICATION(deque, "turbostl.Deque");
        CHECK_UNARY_APPLICATION(list, "turbostl.List");
        CHECK_UNARY_APPLICATION(stack, "turbostl.Stack");
        CHECK_UNARY_APPLICATION(queue, "turbostl.Queue");
        CHECK_UNARY_APPLICATION(heap, "turbostl.Heap");
        CHECK_UNARY_APPLICATION(set, "turbostl.Set");
        CHECK_UNARY_APPLICATION(hash_set, "turbostl.HashSet");
        CHECK_BINARY_APPLICATION(hash_map, "turbostl.HashMap");
        CHECK_BINARY_APPLICATION(map, "turbostl.Map");
        CHECK_BINARY_APPLICATION(multimap, "turbostl.MultiMap");
        CHECK_BINARY_APPLICATION(btree, "turbostl.BTree");
        CHECK_BINARY_APPLICATION(bplus_tree, "turbostl.BPlusTree");
    }

    it("preserves typed generic metadata across init and destroy") {
        Vec(int, values);

        check_true(cmeta_container_type_application_valid(&values));
        check_equal(vec_init(&values, 2u), STL_OK);
        check_true(cmeta_container_type_application_valid(&values));
        vec_destroy(&values);
        check_true(cmeta_container_type_application_valid(&values));
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&values, 0u), &cmeta_type_int));
    }

    it("does not invent a generic application for a raw byte container") {
        vec_t raw = {0};

        check_equal(vec_init_bytes(&raw, sizeof(int), _Alignof(int), 2u),
                    STL_OK);
        check_false(cmeta_container_type_application_valid(&raw));
        check_null(cmeta_container_type_constructor(&raw));
        vec_raw_destroy_storage(&raw);
    }

    it("keeps type well formedness separate from operation traits") {
        set_t set = {
            .cmeta = {&stl_set_container_desc},
            .element_type = &generic_only_key_type
        };

        check_true(cmeta_type_desc_valid(&generic_only_key_type));
        check_true(cmeta_container_type_application_valid(&set));
        check_true(cmeta_type_equal(
            cmeta_container_type_argument(&set, 0u), &generic_only_key_type));
        check_equal(set_init(&set, 2u), STL_TRAIT_MISSING);
    }
}

#undef CHECK_UNARY_APPLICATION
#undef CHECK_BINARY_APPLICATION
