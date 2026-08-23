#include <turbostl/typed.h>
#include "tinytest.h"

#include <string.h>

Struct(construction_payload,
    (TYPE(Vec, int), values),
    (TYPE(Map, int, long), index)
);

Struct(construction_conflict_payload,
    (TYPE(Vec, long), values)
);

Struct(construction_matrix,
    (TYPE(Vec, int), vec),
    (TYPE(Deque, int), deque),
    (TYPE(List, int), list),
    (TYPE(Stack, int), stack),
    (TYPE(Queue, int), queue),
    (TYPE(Heap, int), heap),
    (TYPE(Set, int), set),
    (TYPE(HashSet, int), hash_set),
    (TYPE(HashMap, int, long), hash_map),
    (TYPE(Map, int, long), map),
    (TYPE(MultiMap, int, long), multimap),
    (TYPE(BTree, int, long), btree),
    (TYPE(BPlusTree, int, long), bplus_tree)
);

#define CHECK_UNARY_BIND(matrix_, member_, generic_)                           \
    do {                                                                        \
        const cmeta_field_desc *field_ = cmeta_struct_find_field(               \
            construction_matrix_meta(), #member_);                              \
        check_true(field_ != NULL);                                              \
        check_true(field_->declared_type != NULL);                              \
        check_equal(cmeta_container_bind_types(                                 \
                        &(matrix_).member_, field_->declared_type),              \
                    CMETA_OK);                                                   \
        check_true(cmeta_container_type_constructor(&(matrix_).member_) ==       \
                   &(generic_));                                                 \
        check_equal(cmeta_container_type_arity(&(matrix_).member_),              \
                    (size_t)1u);                                                 \
        check_true(cmeta_container_type_argument(&(matrix_).member_, 0u) ==      \
                   &cmeta_type_int);                                             \
        check_true(cmeta_container_construction(&(matrix_).member_) != NULL);    \
    } while (0)

#define CHECK_BINARY_BIND(matrix_, member_, generic_)                          \
    do {                                                                        \
        const cmeta_field_desc *field_ = cmeta_struct_find_field(               \
            construction_matrix_meta(), #member_);                              \
        check_true(field_ != NULL);                                              \
        check_true(field_->declared_type != NULL);                              \
        check_equal(cmeta_container_bind_types(                                 \
                        &(matrix_).member_, field_->declared_type),              \
                    CMETA_OK);                                                   \
        check_true(cmeta_container_type_constructor(&(matrix_).member_) ==       \
                   &(generic_));                                                 \
        check_equal(cmeta_container_type_arity(&(matrix_).member_),              \
                    (size_t)2u);                                                 \
        check_true(cmeta_container_type_argument(&(matrix_).member_, 0u) ==      \
                   &cmeta_type_int);                                             \
        check_true(cmeta_container_type_argument(&(matrix_).member_, 1u) ==      \
                   &cmeta_type_long);                                            \
        check_true(cmeta_container_construction(&(matrix_).member_) != NULL);    \
    } while (0)

spec("TurboSTL construction binding") {
  it("binds a zero Vec field from static TYPE metadata before Collector") {
    const cmeta_field_desc *values =
        cmeta_struct_find_field(construction_payload_meta(), "values");
    construction_payload payload = {0};
    cmeta_collector collector;
    int first = 17;
    int second = 29;

    check_true(values != NULL);
    check_true(values->type == &stl_vec_storage_type);
    check_true(values->declared_type != NULL);
    check_true(values->declared_type->constructor == &stl_vec_generic_desc);
    check_equal(values->declared_type->arity, (size_t)1u);
    check_true(cmeta_declared_type_argument(values->declared_type, 0u) ==
               &cmeta_type_int);

    check_equal(cmeta_container_bind_types(
                    &payload.values, values->declared_type),
                CMETA_OK);
    check_true(payload.values.cmeta.descriptor == &stl_vec_container_desc);
    check_true(payload.values.element_type == &cmeta_type_int);
    check_true(cmeta_container_type_application_valid(&payload.values));
    check_true(cmeta_container_construction(&payload.values) ==
               &stl_vec_construct_ops);

    collector = stl_vec_container_desc.collector(&payload.values, 4u);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_collector_accept(
                    &collector, &cmeta_type_int, &first), CMETA_OK);
    check_equal(cmeta_collector_accept(
                    &collector, &cmeta_type_int, &second), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);
    check_equal(vec_size(&payload.values), (size_t)2u);
    check_equal(*(const int *)vec_at_const(&payload.values, 0u), first);
    check_equal(*(const int *)vec_at_const(&payload.values, 1u), second);
    vec_destroy(&payload.values);
  }

  it("binds K and V before the existing ordered Map Collector") {
    const cmeta_field_desc *index =
        cmeta_struct_find_field(construction_payload_meta(), "index");
    construction_payload payload = {0};
    cmeta_collector collector;
    int key = 7;
    long value = 9001L;
    cmeta_entry entry = {
        .key_type = &cmeta_type_int,
        .value_type = &cmeta_type_long,
        .key = &key,
        .value = &value,
        .key_storage = NULL,
        .value_storage = NULL
    };
    const long *stored;

    check_true(index != NULL);
    check_true(index->type == &stl_map_storage_type);
    check_true(index->declared_type != NULL);
    check_true(index->declared_type->constructor == &stl_map_generic_desc);
    check_equal(index->declared_type->arity, (size_t)2u);
    check_true(cmeta_declared_type_argument(index->declared_type, 0u) ==
               &cmeta_type_int);
    check_true(cmeta_declared_type_argument(index->declared_type, 1u) ==
               &cmeta_type_long);

    check_equal(cmeta_container_bind_types(
                    &payload.index, index->declared_type),
                CMETA_OK);
    check_true(payload.index.cmeta.descriptor == &stl_map_container_desc);
    check_true(payload.index.key_type == &cmeta_type_int);
    check_true(payload.index.value_type == &cmeta_type_long);

    collector = stl_map_container_desc.collector(&payload.index, 4u);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_collector_accept(
                    &collector, &cmeta_type_ordered_entry, &entry), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);
    stored = (const long *)map_get_const(&payload.index, &key);
    check_true(stored != NULL);
    check_equal(*stored, value);
    map_destroy(&payload.index);
  }

  it("binds every canonical TurboSTL generic kind without semantic duplication") {
    construction_matrix matrix = {0};

    CHECK_UNARY_BIND(matrix, vec, stl_vec_generic_desc);
    CHECK_UNARY_BIND(matrix, deque, stl_deque_generic_desc);
    CHECK_UNARY_BIND(matrix, list, stl_list_generic_desc);
    CHECK_UNARY_BIND(matrix, stack, stl_stack_generic_desc);
    CHECK_UNARY_BIND(matrix, queue, stl_queue_generic_desc);
    CHECK_UNARY_BIND(matrix, heap, stl_heap_generic_desc);
    CHECK_UNARY_BIND(matrix, set, stl_set_generic_desc);
    CHECK_UNARY_BIND(matrix, hash_set, stl_hash_set_generic_desc);

    CHECK_BINARY_BIND(matrix, hash_map, stl_hash_map_generic_desc);
    CHECK_BINARY_BIND(matrix, map, stl_map_generic_desc);
    CHECK_BINARY_BIND(matrix, multimap, stl_multimap_generic_desc);
    CHECK_BINARY_BIND(matrix, btree, stl_btree_generic_desc);
    CHECK_BINARY_BIND(matrix, bplus_tree, stl_bplus_tree_generic_desc);

    check_null(cmeta_container_data(&matrix.heap));
    check_true(cmeta_container_construction(&matrix.heap) ==
               &stl_heap_construct_ops);
    check_null(cmeta_container_data(&matrix.multimap));
    check_true(cmeta_container_construction(&matrix.multimap) ==
               &stl_multimap_construct_ops);
  }

  it("is idempotent before init and rejects conflicting or partial binding") {
    const cmeta_field_desc *ints =
        cmeta_struct_find_field(construction_payload_meta(), "values");
    const cmeta_field_desc *longs =
        cmeta_struct_find_field(construction_conflict_payload_meta(), "values");
    vec_t vec = {0};
    vec_t partial = {0};
    vec_t before;

    check_equal(cmeta_container_bind_types(&vec, ints->declared_type),
                CMETA_OK);
    check_equal(cmeta_container_bind_types(&vec, ints->declared_type),
                CMETA_OK);
    check_equal(cmeta_container_bind_types(&vec, longs->declared_type),
                CMETA_TYPE_MISMATCH);

    partial.element_type = &cmeta_type_int;
    before = partial;
    check_equal(cmeta_container_bind_types(&partial, ints->declared_type),
                CMETA_INVALID_ARGUMENT);
    check_equal(memcmp(&partial, &before, sizeof(partial)), 0);
  }

  it("rejects binding a live container without changing it") {
    const cmeta_field_desc *values =
        cmeta_struct_find_field(construction_payload_meta(), "values");
    vec_t vec = {0};
    const cmeta_container_desc *descriptor;
    const cmeta_type_desc *element_type;
    size_t capacity;

    check_equal(cmeta_container_bind_types(&vec, values->declared_type),
                CMETA_OK);
    check_equal(vec_init(&vec, 8u), STL_OK);
    descriptor = vec.cmeta.descriptor;
    element_type = vec.element_type;
    capacity = vec.capacity;

    check_equal(cmeta_container_bind_types(&vec, values->declared_type),
                CMETA_INVALID_ARGUMENT);
    check_true(vec.cmeta.descriptor == descriptor);
    check_true(vec.element_type == element_type);
    check_equal(vec.capacity, capacity);
    vec_destroy(&vec);
  }
}

#undef CHECK_UNARY_BIND
#undef CHECK_BINARY_BIND
