#include <cstl/typed.h>
#include <cmeta/data.h>
#include "tinytest.h"

typedef struct cstl_semantic_collect_ints {
  int *values;
  size_t count;
} cstl_semantic_collect_ints;

static cmeta_status cstl_semantic_collect_int(
    void *context, const void *element) {
  cstl_semantic_collect_ints *out = (cstl_semantic_collect_ints *)context;
  if (out == NULL || element == NULL) return CMETA_INVALID_ARGUMENT;
  out->values[out->count++] = *(const int *)element;
  return CMETA_OK;
}

typedef struct cstl_semantic_map_capture {
  int keys[2];
  long values[2];
  size_t count;
} cstl_semantic_map_capture;

static cmeta_status cstl_semantic_collect_map(
    void *context, const void *key, const void *value) {
  cstl_semantic_map_capture *out = (cstl_semantic_map_capture *)context;
  if (out == NULL || key == NULL || value == NULL)
    return CMETA_INVALID_ARGUMENT;
  out->keys[out->count] = *(const int *)key;
  out->values[out->count] = *(const long *)value;
  ++out->count;
  return CMETA_OK;
}

typed(Vec, cstl_struct_vec, int);
typed(Vec, reflected_ints, int);
typed(Deque, reflected_deque, int);
typed(List, reflected_list, int);
typed(Set, reflected_set, int);
typed(HashSet, reflected_hash_set, int);
typed(Map, reflected_map, int, long);
typed(HashMap, reflected_hash_map, int, long);
typed(BTree, reflected_btree, int, long);
typed(BPlusTree, reflected_bplus, int, long);
typed(Map, capability_map, int, long);
typed(HashMap, capability_hash_map, int, long);
typed(BTree, capability_btree, int, long);
typed(BPlusTree, capability_bplus, int, long);
typed(MultiMap, capability_multimap, int, long);
typed(Vec, transactional_vec, int);
typed(Stack, reflected_stack, int);
typed(Queue, reflected_queue, int);
typed(Vec, meta_vec, int);
typed(Deque, meta_deque, int);
typed(List, meta_list, int);
typed(Stack, meta_stack, int);
typed(Queue, meta_queue, int);
typed(Heap, meta_heap, int);
typed(Set, meta_set, int);
typed(HashSet, meta_hash_set, int);
typed(HashMap, meta_hash_map, int, long);
typed(Map, meta_map, int, long);
typed(MultiMap, meta_multimap, int, long);
typed(BTree, meta_btree, int, long);
typed(BPlusTree, meta_bplus, int, long);
typed(Vec, borrow_vec, int);
typed(List, borrow_list, int);
typed(HashSet, borrow_hash_set, int);
typed(HashMap, borrow_hash_map, int, long);
typed(Map, borrow_map, int, long);
typed(MultiMap, borrow_multimap, int, long);
typed(BTree, borrow_btree, int, long);
typed(BPlusTree, borrow_bplus, int, long);

typedef struct cstl_struct_with_vec {
  cstl_struct_vec values;
  int tag;
} cstl_struct_with_vec;

static const cmeta_type_identity cstl_struct_with_vec_id =
    CMETA_TYPE_ID_ATOM_INIT("test.cstl.StructWithVec");
static const cmeta_type_desc cstl_struct_with_vec_type = {
    "cstl_struct_with_vec", sizeof(cstl_struct_with_vec),
    CMETA_ALIGNOF(cstl_struct_with_vec), CMETA_T_OBJECT,
    NULL, NULL, &cstl_struct_with_vec_id};
static const cmeta_field_desc cstl_struct_with_vec_layout_fields[] = {
    {"values", "cstl_struct_vec", offsetof(cstl_struct_with_vec, values),
     sizeof(cstl_struct_vec), CMETA_ALIGNOF(cstl_struct_vec),
     CMETA_TYPEOF(cstl_struct_vec), NULL},
    {"tag", "int", offsetof(cstl_struct_with_vec, tag),
     sizeof(int), CMETA_ALIGNOF(int), &cmeta_type_int, NULL}};
static const cmeta_struct_desc cstl_struct_with_vec_layout = {
    "cstl_struct_with_vec", sizeof(cstl_struct_with_vec),
    CMETA_ALIGNOF(cstl_struct_with_vec), cstl_struct_with_vec_layout_fields, 2u};
static const cmeta_data_field_desc cstl_struct_with_vec_fields[] = {
    {"test.cstl.StructWithVec.values", "values",
     offsetof(cstl_struct_with_vec, values), &cstl_struct_vec_collection_data},
    {"test.cstl.StructWithVec.tag", "tag",
     offsetof(cstl_struct_with_vec, tag), &cmeta_data_int}};
static const cmeta_data_struct_shape cstl_struct_with_vec_shape = {
    &cstl_struct_with_vec_layout, cstl_struct_with_vec_fields, 2u};
static const cmeta_data_desc cstl_struct_with_vec_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.cstl.StructWithVec.data",
    .display_name = "StructWithVec",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cstl_struct_with_vec_type,
    .shape = &cstl_struct_with_vec_shape};


spec("CSTL semantic projection") {
  it("projects sequence-like containers without duplicating element type") {
    Vec(int, vec);
    Deque(int, deque);
    List(int, list);
    Stack(int, stack);
    Queue(int, queue);

    check_true(cmeta_container_data(&vec) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&deque) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&list) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&stack) == &cmeta_data_sequence);
    check_true(cmeta_container_data(&queue) == &cmeta_data_sequence);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&vec, 0u),
                                &cmeta_type_int));
  }

  it("projects set containers") {
    Set(int, set);
    HashSet(int, hash_set);

    check_true(cmeta_container_data(&set) == &cmeta_data_set);
    check_true(cmeta_container_data(&hash_set) == &cmeta_data_set);
  }

  it("projects map containers while type arguments stay in generic metadata") {
    HashMap(int, long, hash_map);
    Map(int, long, map);
    BTree(int, long, btree);
    BPlusTree(int, long, bplus_tree);

    check_true(cmeta_container_data(&hash_map) == &cmeta_data_map);
    check_true(cmeta_container_data(&map) == &cmeta_data_map);
    check_true(cmeta_container_data(&btree) == &cmeta_data_map);
    check_true(cmeta_container_data(&bplus_tree) == &cmeta_data_map);
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 0u),
                                &cmeta_type_int));
    check_true(cmeta_type_equal(cmeta_container_type_argument(&map, 1u),
                                &cmeta_type_long));
  }

  it("keeps Heap type-reflectable while MultiMap has map semantics") {
    Heap(int, heap);
    MultiMap(int, long, multimap);

    check_null(cmeta_container_data(&heap));
    check_true(cmeta_container_data(&multimap) == &cmeta_data_map);
    check_true(cmeta_container_type_application_valid(&heap));
    check_true(cmeta_container_type_application_valid(&multimap));
  }

  it("projects typed Vec through canonical CMeta collection reflection") {
    reflected_ints values = {0};
    cmeta_data_collection_view view = {0};
    check_equal(reflected_ints_init(&values, 8u), STL_OK);
    check_equal(reflected_ints_push(&values, 3), STL_OK);
    check_equal(reflected_ints_push(&values, 5), STL_OK);
    check_true(reflected_ints_collection_data.collection_ops ==
               &reflected_ints_collection_ops);
    check_equal(cmeta_data_collection_read(
                    &reflected_ints_collection_data, &values, &view),
                CMETA_OK);
    check_true(view.element == &cmeta_data_int);
    check_equal(view.count, 2u);
    check_equal(view.stride, sizeof(int));
    check_equal(*(const int *)view.data, 3);
    reflected_ints_destroy(&values);
  }


  it("projects typed Deque without claiming contiguous storage") {
    reflected_deque values = {0};
    int seen[2] = {0};
    cstl_semantic_collect_ints collected = {seen, 0u};
    check_equal(reflected_deque_init(&values, 8u), STL_OK);
    check_equal(reflected_deque_push_back(&values, 3), STL_OK);
    check_equal(reflected_deque_push_back(&values, 5), STL_OK);
    check_null(reflected_deque_collection_ops.read);
    check_true(reflected_deque_collection_ops.foreach != NULL);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_deque_collection_data, &values,
                    cstl_semantic_collect_int, &collected, 2u),
                CMETA_OK);
    check_equal(collected.count, 2u);
    check_equal(seen[0], 3);
    check_equal(seen[1], 5);
    reflected_deque_destroy(&values);
  }


  it("projects typed List through linked iteration only") {
    reflected_list values = {0};
    int seen[2] = {0};
    cstl_semantic_collect_ints collected = {seen, 0u};
    check_equal(reflected_list_init(&values, 8u), STL_OK);
    check_equal(reflected_list_push_back(&values, 7), STL_OK);
    check_equal(reflected_list_push_back(&values, 11), STL_OK);
    check_null(reflected_list_collection_ops.read);
    check_true(reflected_list_collection_ops.foreach != NULL);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_list_collection_data, &values,
                    cstl_semantic_collect_int, &collected, 2u),
                CMETA_OK);
    check_equal(collected.count, 2u);
    check_equal(seen[0], 7);
    check_equal(seen[1], 11);
    reflected_list_destroy(&values);
  }


  it("projects typed Set and HashSet with SET semantics") {
    reflected_set ordered = {0};
    reflected_hash_set hashed = {0};
    int ordered_seen[2] = {0};
    int hashed_seen[2] = {0};
    cstl_semantic_collect_ints ordered_out = {ordered_seen, 0u};
    cstl_semantic_collect_ints hashed_out = {hashed_seen, 0u};

    check_equal(reflected_set_init(&ordered, 8u), STL_OK);
    check_equal(reflected_set_add(&ordered, 5), STL_OK);
    check_equal(reflected_set_add(&ordered, 3), STL_OK);
    check_equal(reflected_hash_set_init(&hashed, 8u), STL_OK);
    check_equal(reflected_hash_set_add(&hashed, 5), STL_OK);
    check_equal(reflected_hash_set_add(&hashed, 3), STL_OK);

    check_equal(reflected_set_collection_data.kind, CMETA_DATA_SET);
    check_equal(reflected_hash_set_collection_data.kind, CMETA_DATA_SET);
    check_true((reflected_set_collection_ops.flags &
                (CMETA_DATA_COLLECTION_ORDERED |
                 CMETA_DATA_COLLECTION_SORTED |
                 CMETA_DATA_COLLECTION_UNIQUE)) ==
               (CMETA_DATA_COLLECTION_ORDERED |
                CMETA_DATA_COLLECTION_SORTED |
                CMETA_DATA_COLLECTION_UNIQUE));
    check_equal(reflected_hash_set_collection_ops.flags,
                CMETA_DATA_COLLECTION_UNIQUE);
    check_true(reflected_set_collection_element(&ordered) == &cmeta_data_int);
    check_true(reflected_hash_set_collection_element(&hashed) == &cmeta_data_int);

    check_equal(cmeta_data_collection_foreach(
                    &reflected_set_collection_data, &ordered,
                    cstl_semantic_collect_int, &ordered_out, 2u),
                CMETA_OK);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_hash_set_collection_data, &hashed,
                    cstl_semantic_collect_int, &hashed_out, 2u),
                CMETA_OK);
    check_equal(ordered_out.count, 2u);
    check_equal(hashed_out.count, 2u);

    reflected_set_destroy(&ordered);
    reflected_hash_set_destroy(&hashed);
  }


  it("projects typed Map as key-value reflection, never pair sequence") {
    reflected_map values = {0};
    cstl_semantic_map_capture captured = {{0}, {0}, 0u};

    check_equal(reflected_map_init(&values, 8u), STL_OK);
    check_equal(reflected_map_put(&values, 5, 50L), STL_OK);
    check_equal(reflected_map_put(&values, 3, 30L), STL_OK);

    check_equal(reflected_map_map_data.kind, CMETA_DATA_MAP);
    check_true(reflected_map_map_data.collection_ops == NULL);
    check_true(reflected_map_map_data.map_ops == &reflected_map_map_ops);
    check_true(reflected_map_map_key(&values) == &cmeta_data_int);
    check_true(reflected_map_map_value(&values) == &cmeta_data_long);

    check_equal(cmeta_data_map_foreach(
                    &reflected_map_map_data, &values,
                    cstl_semantic_collect_map, &captured, 2u),
                CMETA_OK);
    check_equal(captured.count, 2u);
    check_equal(captured.keys[0], 3);
    check_equal(captured.values[0], 30L);
    check_equal(captured.keys[1], 5);
    check_equal(captured.values[1], 50L);

    reflected_map_destroy(&values);
  }


  it("projects HashMap and tree maps through one CMeta map contract") {
    reflected_hash_map hashed = {0};
    reflected_btree btree = {0};
    reflected_bplus bplus = {0};
    cstl_semantic_map_capture hc = {{0}, {0}, 0u};
    cstl_semantic_map_capture tc = {{0}, {0}, 0u};
    cstl_semantic_map_capture pc = {{0}, {0}, 0u};

    check_equal(reflected_hash_map_init(&hashed, 8u), STL_OK);
    check_equal(reflected_btree_init(&btree, 8u), STL_OK);
    check_equal(reflected_bplus_init(&bplus, 8u), STL_OK);
    check_equal(reflected_hash_map_put(&hashed, 5, 50L), STL_OK);
    check_equal(reflected_hash_map_put(&hashed, 3, 30L), STL_OK);
    check_equal(reflected_btree_put(&btree, 5, 50L), STL_OK);
    check_equal(reflected_btree_put(&btree, 3, 30L), STL_OK);
    check_equal(reflected_bplus_put(&bplus, 5, 50L), STL_OK);
    check_equal(reflected_bplus_put(&bplus, 3, 30L), STL_OK);

    check_equal(cmeta_data_map_foreach(&reflected_hash_map_map_data, &hashed,
                    cstl_semantic_collect_map, &hc, 2u), CMETA_OK);
    check_equal(cmeta_data_map_foreach(&reflected_btree_map_data, &btree,
                    cstl_semantic_collect_map, &tc, 2u), CMETA_OK);
    check_equal(cmeta_data_map_foreach(&reflected_bplus_map_data, &bplus,
                    cstl_semantic_collect_map, &pc, 2u), CMETA_OK);
    check_equal(hc.count, 2u);
    check_equal(tc.count, 2u);
    check_equal(pc.count, 2u);
    check_equal(tc.keys[0], 3);
    check_equal(tc.keys[1], 5);
    check_equal(pc.keys[0], 3);
    check_equal(pc.keys[1], 5);

    reflected_hash_map_destroy(&hashed);
    reflected_btree_destroy(&btree);
    reflected_bplus_destroy(&bplus);
  }


  it("preserves map ordering and repeated-key capabilities") {
    capability_multimap multi = {0};
    cstl_semantic_map_capture captured = {{0}, {0}, 0u};

    check_true((capability_map_map_ops.flags & CMETA_DATA_MAP_UNIQUE_KEYS) != 0u);
    check_true((capability_map_map_ops.flags & CMETA_DATA_MAP_ORDERED) != 0u);
    check_true((capability_map_map_ops.flags & CMETA_DATA_MAP_SORTED) != 0u);
    check_equal(capability_hash_map_map_ops.flags, CMETA_DATA_MAP_UNIQUE_KEYS);
    check_true((capability_btree_map_ops.flags & CMETA_DATA_MAP_SORTED) != 0u);
    check_true((capability_bplus_map_ops.flags & CMETA_DATA_MAP_SORTED) != 0u);
    check_true((capability_multimap_map_ops.flags &
                CMETA_DATA_MAP_REPEATED_KEYS) != 0u);
    check_true((capability_multimap_map_ops.flags &
                CMETA_DATA_MAP_UNIQUE_KEYS) == 0u);

    check_equal(capability_multimap_init(&multi, 8u), STL_OK);
    check_equal(capability_multimap_put(&multi, 3, 30L), STL_OK);
    check_equal(capability_multimap_put(&multi, 3, 31L), STL_OK);
    check_equal(cmeta_data_map_foreach(
                    &capability_multimap_map_data, &multi,
                    cstl_semantic_collect_map, &captured, 2u),
                CMETA_OK);
    check_equal(captured.count, 2u);
    check_equal(captured.keys[0], 3);
    check_equal(captured.keys[1], 3);
    check_equal(captured.values[0], 30L);
    check_equal(captured.values[1], 31L);
    capability_multimap_destroy(&multi);
  }


  it("gives typed Vec an explicit transactional construction lifecycle") {
    transactional_vec source = {0};
    transactional_vec destination = {0};

    check_true(transactional_vec_collection_data.construct_ops ==
               &transactional_vec_construct_ops);
    check_equal(cmeta_data_construct_init_zero(
                    &transactional_vec_collection_data, &source),
                CMETA_OK);
    check_equal(transactional_vec_push(&source, 17), STL_OK);
    check_equal(cmeta_data_construct_init_zero(
                    &transactional_vec_collection_data, &destination),
                CMETA_OK);
    check_equal(cmeta_data_construct_move(
                    &transactional_vec_collection_data,
                    &destination, &source),
                CMETA_OK);
    check_equal(transactional_vec_size(&destination), 1u);
    check_equal(*transactional_vec_at_const(&destination, 0u), 17);
    check_equal(transactional_vec_size(&source), 0u);
    check_equal(cmeta_data_construct_restore_zero(
                    &transactional_vec_collection_data, &destination),
                CMETA_OK);
  }


  it("derives struct lifecycle through a typed CSTL collection field") {
    cstl_struct_with_vec source = {0};
    cstl_struct_with_vec destination = {0};

    check_true(cmeta_data_struct_constructible(&cstl_struct_with_vec_data));
    check_equal(cmeta_data_value_init_zero(&cstl_struct_with_vec_data, &source),
                CMETA_OK);
    check_equal(cmeta_data_value_init_zero(
                    &cstl_struct_with_vec_data, &destination),
                CMETA_OK);
    check_equal(cstl_struct_vec_push(&source.values, 23), STL_OK);
    source.tag = 4;
    check_equal(cmeta_data_value_move(
                    &cstl_struct_with_vec_data, &destination, &source),
                CMETA_OK);
    check_equal(cstl_struct_vec_size(&destination.values), 1u);
    check_equal(*cstl_struct_vec_at_const(&destination.values, 0u), 23);
    check_equal(destination.tag, 4);
    check_equal(cstl_struct_vec_size(&source.values), 0u);
    check_equal(source.tag, 0);
    check_equal(cmeta_data_value_restore_zero(
                    &cstl_struct_with_vec_data, &destination),
                CMETA_OK);
  }


  it("projects Stack and Queue as ordered sequences") {
    reflected_stack stack = {0};
    reflected_queue queue = {0};
    int stack_seen[2] = {0};
    int queue_seen[2] = {0};
    cstl_semantic_collect_ints stack_out = {stack_seen, 0u};
    cstl_semantic_collect_ints queue_out = {queue_seen, 0u};

    check_equal(reflected_stack_init(&stack, 8u), STL_OK);
    check_equal(reflected_queue_init(&queue, 8u), STL_OK);
    check_equal(reflected_stack_push(&stack, 3), STL_OK);
    check_equal(reflected_stack_push(&stack, 5), STL_OK);
    check_equal(reflected_queue_push(&queue, 3), STL_OK);
    check_equal(reflected_queue_push(&queue, 5), STL_OK);

    check_equal(reflected_stack_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(reflected_queue_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_stack_collection_data, &stack,
                    cstl_semantic_collect_int, &stack_out, 2u),
                CMETA_OK);
    check_equal(cmeta_data_collection_foreach(
                    &reflected_queue_collection_data, &queue,
                    cstl_semantic_collect_int, &queue_out, 2u),
                CMETA_OK);
    /* Stack sequence order is declaration/storage order: bottom -> top.
     * Queue sequence order is front -> back. */
    check_equal(stack_seen[0], 3);
    check_equal(stack_seen[1], 5);
    check_equal(queue_seen[0], 3);
    check_equal(queue_seen[1], 5);

    reflected_stack_destroy(&stack);
    reflected_queue_destroy(&queue);
  }


  it("gates CMeta type coverage across every typed CSTL family") {

    check_not_null(CMETA_TYPEOF_OR(meta_vec, &meta_vec_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_deque, &meta_deque_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_list, &meta_list_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_stack, &meta_stack_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_queue, &meta_queue_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_heap, &meta_heap_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_set, &meta_set_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_hash_set, &meta_hash_set_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_hash_map, &meta_hash_map_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_map, &meta_map_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_multimap, &meta_multimap_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_btree, &meta_btree_cmeta_type));
    check_not_null(CMETA_TYPEOF_OR(meta_bplus, &meta_bplus_cmeta_type));

    check_equal(meta_vec_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(meta_deque_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(meta_list_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(meta_stack_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(meta_queue_collection_data.kind, CMETA_DATA_SEQUENCE);
    check_equal(meta_set_collection_data.kind, CMETA_DATA_SET);
    check_equal(meta_hash_set_collection_data.kind, CMETA_DATA_SET);
    check_equal(meta_hash_map_map_data.kind, CMETA_DATA_MAP);
    check_equal(meta_map_map_data.kind, CMETA_DATA_MAP);
    check_equal(meta_multimap_map_data.kind, CMETA_DATA_MAP);
    check_equal(meta_btree_map_data.kind, CMETA_DATA_MAP);
    check_equal(meta_bplus_map_data.kind, CMETA_DATA_MAP);

    /* Heap is intentionally type-only until CMeta has a semantic kind that
     * preserves priority-queue / heap invariants without exposing storage. */
    Heap(int, raw_heap);
    check_null(cmeta_container_data(&raw_heap));
  }


  it("borrows collection elements without copying and detects mutation") {
    borrow_vec vec = {0};
    borrow_list list = {0};
    borrow_hash_set set = {0};
    cmeta_data_collection_borrow_cursor cursor = {0};
    const void *element = NULL;
    size_t size = 0u;

    check_equal(borrow_vec_init(&vec, 8u), STL_OK);
    check_equal(borrow_vec_push(&vec, 3), STL_OK);
    check_equal(borrow_vec_push(&vec, 5), STL_OK);
    check_equal(cmeta_data_collection_borrow_begin(
                    &borrow_vec_collection_data, &vec, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_collection_borrow_size(&cursor, &size), CMETA_OK);
    check_equal(size, 2u);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)element, 3);
    check_equal(borrow_vec_push(&vec, 7), STL_OK);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_MUTATED);
    borrow_vec_destroy(&vec);

    check_equal(borrow_list_init(&list, 8u), STL_OK);
    check_equal(borrow_list_push_back(&list, 7), STL_OK);
    check_equal(borrow_list_push_back(&list, 11), STL_OK);
    cursor = (cmeta_data_collection_borrow_cursor){0};
    check_equal(cmeta_data_collection_borrow_begin(
                    &borrow_list_collection_data, &list, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)element, 7);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)element, 11);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_DONE);
    borrow_list_destroy(&list);

    check_equal(borrow_hash_set_init(&set, 8u), STL_OK);
    check_equal(borrow_hash_set_add(&set, 13), STL_OK);
    cursor = (cmeta_data_collection_borrow_cursor){0};
    check_equal(cmeta_data_collection_borrow_begin(
                    &borrow_hash_set_collection_data, &set, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_collection_borrow_next(&cursor, &element),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)element, 13);
    borrow_hash_set_destroy(&set);
  }

  it("borrows map entries across every typed topology and detects mutation") {
    borrow_hash_map hash_map = {0};
    borrow_map map = {0};
    borrow_multimap multimap = {0};
    borrow_btree btree = {0};
    borrow_bplus bplus = {0};
    cmeta_data_map_borrow_cursor cursor = {0};
    const void *key = NULL;
    const void *value = NULL;
    size_t size = 0u;

    check_equal(borrow_hash_map_init(&hash_map, 8u), STL_OK);
    check_equal(borrow_map_init(&map, 8u), STL_OK);
    check_equal(borrow_multimap_init(&multimap, 8u), STL_OK);
    check_equal(borrow_btree_init(&btree, 8u), STL_OK);
    check_equal(borrow_bplus_init(&bplus, 8u), STL_OK);
    check_equal(borrow_hash_map_put(&hash_map, 1, 10L), STL_OK);
    check_equal(borrow_map_put(&map, 2, 20L), STL_OK);
    check_equal(borrow_multimap_put(&multimap, 3, 30L), STL_OK);
    check_equal(borrow_multimap_put(&multimap, 3, 31L), STL_OK);
    check_equal(borrow_btree_put(&btree, 4, 40L), STL_OK);
    check_equal(borrow_bplus_put(&bplus, 5, 50L), STL_OK);

    check_equal(cmeta_data_map_borrow_begin(
                    &borrow_hash_map_map_data, &hash_map, &cursor),
                CMETA_OK);
    check_true(cursor.key == &cmeta_data_int);
    check_true(cursor.value == &cmeta_data_long);
    check_equal(cmeta_data_map_borrow_size(&cursor, &size), CMETA_OK);
    check_equal(size, 1u);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 1);
    check_equal(*(const long *)value, 10L);

    cursor = (cmeta_data_map_borrow_cursor){0};
    check_equal(cmeta_data_map_borrow_begin(
                    &borrow_map_map_data, &map, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 2);
    check_equal(*(const long *)value, 20L);
    check_equal(borrow_map_put(&map, 6, 60L), STL_OK);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_MUTATED);

    cursor = (cmeta_data_map_borrow_cursor){0};
    check_equal(cmeta_data_map_borrow_begin(
                    &borrow_multimap_map_data, &multimap, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_map_borrow_size(&cursor, &size), CMETA_OK);
    check_equal(size, 2u);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 3);
    check_equal(*(const long *)value, 30L);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 3);
    check_equal(*(const long *)value, 31L);

    cursor = (cmeta_data_map_borrow_cursor){0};
    check_equal(cmeta_data_map_borrow_begin(
                    &borrow_btree_map_data, &btree, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 4);
    check_equal(*(const long *)value, 40L);

    cursor = (cmeta_data_map_borrow_cursor){0};
    check_equal(cmeta_data_map_borrow_begin(
                    &borrow_bplus_map_data, &bplus, &cursor),
                CMETA_OK);
    check_equal(cmeta_data_map_borrow_next(&cursor, &key, &value),
                CMETA_GEN_VALUE);
    check_equal(*(const int *)key, 5);
    check_equal(*(const long *)value, 50L);

    borrow_hash_map_destroy(&hash_map);
    borrow_map_destroy(&map);
    borrow_multimap_destroy(&multimap);
    borrow_btree_destroy(&btree);
    borrow_bplus_destroy(&bplus);
  }

}
