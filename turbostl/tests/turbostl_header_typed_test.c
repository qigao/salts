#include <turbostl/typed.h>
#include "tinytest.h"

static vec_t make_int_vec(void) {
    return VecOf(int);
}

static const cmeta_type_desc *map_key_type(map_t map) {
    return map.key_type;
}

typedef struct unregistered_element {
    int value;
} unregistered_element;

suite("TurboSTL typed public header") {
    it("exposes self-describing declarations without generated type names") {
        Vec(int, vec);
        List(int, list);
        Map(int, int, map);

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(list_init(&list, 1u), STL_OK);
        check_equal(map_init(&map, 1u), STL_OK);

        map_destroy(&map);
        list_destroy(&list);
        vec_destroy(&vec);
    }

    it("constructs unary self-describing handles from expressions") {
        vec_t vec = {0};
        deque_t deque = DequeOf(int);
        list_t list = ListOf(int);
        stack_t stack = StackOf(int);
        queue_t queue = QueueOf(int);
        heap_t heap = HeapOf(int);
        set_t set = SetOf(int);
        hash_set_t hash_set = HashSetOf(int);

        vec = VecOf(int);

#define CHECK_UNARY_EXPRESSION(handle_, generic_)                              \
        do {                                                                    \
            check_true(cmeta_container_type_application_valid(&(handle_)));    \
            check_true(cmeta_container_type_constructor(&(handle_)) ==          \
                       &(generic_));                                             \
            check_true(cmeta_container_type_argument(&(handle_), 0u) ==         \
                       &cmeta_type_int);                                         \
        } while (0)

        CHECK_UNARY_EXPRESSION(vec, stl_vec_generic_desc);
        CHECK_UNARY_EXPRESSION(deque, stl_deque_generic_desc);
        CHECK_UNARY_EXPRESSION(list, stl_list_generic_desc);
        CHECK_UNARY_EXPRESSION(stack, stl_stack_generic_desc);
        CHECK_UNARY_EXPRESSION(queue, stl_queue_generic_desc);
        CHECK_UNARY_EXPRESSION(heap, stl_heap_generic_desc);
        CHECK_UNARY_EXPRESSION(set, stl_set_generic_desc);
        CHECK_UNARY_EXPRESSION(hash_set, stl_hash_set_generic_desc);

#undef CHECK_UNARY_EXPRESSION

        check_equal(vec_init(&vec, 1u), STL_OK);
        vec_destroy(&vec);
    }

    it("constructs associative self-describing handles from expressions") {
        hash_map_t hash_map = HashMapOf(int, long);
        map_t map = {0};
        multimap_t multimap = MultiMapOf(int, long);
        btree_t btree = BTreeOf(int, long);
        bplus_tree_t bplus_tree = BPlusTreeOf(int, long);

        map = MapOf(int, long);

#define CHECK_BINARY_EXPRESSION(handle_, generic_)                             \
        do {                                                                    \
            check_true(cmeta_container_type_application_valid(&(handle_)));    \
            check_true(cmeta_container_type_constructor(&(handle_)) ==          \
                       &(generic_));                                             \
            check_true(cmeta_container_type_argument(&(handle_), 0u) ==         \
                       &cmeta_type_int);                                         \
            check_true(cmeta_container_type_argument(&(handle_), 1u) ==         \
                       &cmeta_type_long);                                        \
        } while (0)

        CHECK_BINARY_EXPRESSION(hash_map, stl_hash_map_generic_desc);
        CHECK_BINARY_EXPRESSION(map, stl_map_generic_desc);
        CHECK_BINARY_EXPRESSION(multimap, stl_multimap_generic_desc);
        CHECK_BINARY_EXPRESSION(btree, stl_btree_generic_desc);
        CHECK_BINARY_EXPRESSION(bplus_tree, stl_bplus_tree_generic_desc);

#undef CHECK_BINARY_EXPRESSION

        check_equal(map_init(&map, 1u), STL_OK);
        map_destroy(&map);
    }

    it("supports expression initializers in returns and arguments") {
        vec_t vec = make_int_vec();

        check_true(vec.element_type == &cmeta_type_int);
        check_true(map_key_type(MapOf(long, int)) == &cmeta_type_long);
    }

    it("rejects expression initializers with unregistered types") {
        vec_t vec = VecOf(unregistered_element);

        check_false(cmeta_container_type_application_valid(&vec));
        check_equal(vec_init(&vec, 1u), STL_INVALID_ARGUMENT);
    }

    it("binds unary kind descriptors and preserves them across reinitialization") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        const cmeta_container_desc *vec_kind = cmeta_container_descriptor(&vec);
        const cmeta_container_desc *deque_kind = cmeta_container_descriptor(&deque);
        const cmeta_container_desc *list_kind = cmeta_container_descriptor(&list);
        const cmeta_container_desc *stack_kind = cmeta_container_descriptor(&stack);
        const cmeta_container_desc *queue_kind = cmeta_container_descriptor(&queue);
        const cmeta_container_desc *heap_kind = cmeta_container_descriptor(&heap);
        const cmeta_container_desc *set_kind = cmeta_container_descriptor(&set);
        const cmeta_container_desc *hash_set_kind = cmeta_container_descriptor(&hash_set);

        check_not_null(vec_kind);
        check_not_null(deque_kind);
        check_not_null(list_kind);
        check_not_null(stack_kind);
        check_not_null(queue_kind);
        check_not_null(heap_kind);
        check_not_null(set_kind);
        check_not_null(hash_set_kind);

        check_equal(vec_init(&vec, 2u), STL_OK);
        check_equal(deque_init(&deque, 2u), STL_OK);
        check_equal(list_init(&list, 2u), STL_OK);
        check_equal(stack_init(&stack, 2u), STL_OK);
        check_equal(queue_init(&queue, 2u), STL_OK);
        check_equal(heap_init(&heap, 2u), STL_OK);
        check_equal(set_init(&set, 2u), STL_OK);
        check_equal(hash_set_init(&hash_set, 2u), STL_OK);

        vec_destroy(&vec);
        deque_destroy(&deque);
        list_destroy(&list);
        stack_destroy(&stack);
        queue_destroy(&queue);
        heap_destroy(&heap);
        set_destroy(&set);
        hash_set_destroy(&hash_set);

        check_true(cmeta_container_descriptor(&vec) == vec_kind);
        check_true(cmeta_container_descriptor(&deque) == deque_kind);
        check_true(cmeta_container_descriptor(&list) == list_kind);
        check_true(cmeta_container_descriptor(&stack) == stack_kind);
        check_true(cmeta_container_descriptor(&queue) == queue_kind);
        check_true(cmeta_container_descriptor(&heap) == heap_kind);
        check_true(cmeta_container_descriptor(&set) == set_kind);
        check_true(cmeta_container_descriptor(&hash_set) == hash_set_kind);

        check_equal(vec_init(&vec, 1u), STL_OK);
        check_equal(deque_init(&deque, 1u), STL_OK);
        check_equal(list_init(&list, 1u), STL_OK);
        check_equal(stack_init(&stack, 1u), STL_OK);
        check_equal(queue_init(&queue, 1u), STL_OK);
        check_equal(heap_init(&heap, 1u), STL_OK);
        check_equal(set_init(&set, 1u), STL_OK);
        check_equal(hash_set_init(&hash_set, 1u), STL_OK);

        vec_destroy(&vec);
        deque_destroy(&deque);
        list_destroy(&list);
        stack_destroy(&stack);
        queue_destroy(&queue);
        heap_destroy(&heap);
        set_destroy(&set);
        hash_set_destroy(&hash_set);
    }

    it("exposes unary default ranges and instance collectors") {
        Vec(int, vec);
        Deque(int, deque);
        List(int, list);
        Stack(int, stack);
        Queue(int, queue);
        Heap(int, heap);
        Set(int, set);
        HashSet(int, hash_set);
        Vec(int, collected);
        Vec(int, aborted);
        cmeta_range range = {0};
        cmeta_collector collector;
        const cmeta_container_desc *desc;
        int value = 7;
        int out = 0;

        check_equal(vec_init(&vec, 2u), STL_OK);
        check_equal(deque_init(&deque, 2u), STL_OK);
        check_equal(list_init(&list, 2u), STL_OK);
        check_equal(stack_init(&stack, 2u), STL_OK);
        check_equal(queue_init(&queue, 2u), STL_OK);
        check_equal(heap_init(&heap, 2u), STL_OK);
        check_equal(set_init(&set, 2u), STL_OK);
        check_equal(hash_set_init(&hash_set, 2u), STL_OK);

        check_equal(vec_push(&vec, &value), STL_OK);
        check_equal(deque_push_back(&deque, &value), STL_OK);
        check_equal(list_push_back(&list, &value, NULL), STL_OK);
        check_equal(stack_push(&stack, &value), STL_OK);
        check_equal(queue_push(&queue, &value), STL_OK);
        check_equal(heap_push(&heap, &value), STL_OK);
        check_equal(set_add(&set, &value), STL_OK);
        check_equal(hash_set_add(&hash_set, &value), STL_OK);

#define CHECK_DEFAULT_RANGE(handle, required_flags, forbidden_flags) do {       \
    cmeta_range_cursor cursor = {0};                                           \
    range = (cmeta_range){0};                                                  \
    check_true(cmeta_container_range_view(&(handle),                           \
                                           CMETA_CONTAINER_VIEW_DEFAULT,        \
                                           &range));                            \
    check_true(cmeta_type_equal(range.element_type, &cmeta_type_int));         \
    check_true((range.flags & (required_flags)) == (required_flags));          \
    check_equal(range.flags & (forbidden_flags), (cmeta_range_flags)0u);       \
    out = 0;                                                                   \
    {                                                                          \
        cmeta_gen_status status = cmeta_range_next(&range, &cursor, &out);      \
        check_true(status == CMETA_GEN_VALUE ||                                \
                   status == CMETA_GEN_VALUE_AND_DONE);                        \
    }                                                                          \
    check_equal(out, value);                                                   \
} while (0)

        CHECK_DEFAULT_RANGE(vec,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_CONTIGUOUS |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED | CMETA_RANGE_UNIQUE);
        CHECK_DEFAULT_RANGE(deque,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(list,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(stack,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(queue,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(heap,
            CMETA_RANGE_SIZED | CMETA_RANGE_RANDOM_ACCESS | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED);
        CHECK_DEFAULT_RANGE(set,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            0u);
        CHECK_DEFAULT_RANGE(hash_set,
            CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED);
#undef CHECK_DEFAULT_RANGE

        desc = cmeta_container_descriptor(&collected);
        check_not_null(desc);
        check_not_null(desc->collector);
        if (desc->collector != NULL) {
            collector = desc->collector(&collected, 2u);
            check_equal(cmeta_collector_begin(&collector), CMETA_OK);
            check_equal(cmeta_collector_accept(&collector, &cmeta_type_int,
                                               &value), CMETA_OK);
            check_equal(cmeta_collector_finish(&collector), CMETA_OK);
            check_equal(vec_size(&collected), (size_t)1u);
            check_equal(*(const int *)vec_at_const(&collected, 0u), value);
            vec_destroy(&collected);
        }

        desc = cmeta_container_descriptor(&aborted);
        check_not_null(desc);
        check_not_null(desc->collector);
        if (desc->collector != NULL) {
            const cmeta_container_desc *kind = desc;
            collector = desc->collector(&aborted, 2u);
            check_equal(cmeta_collector_begin(&collector), CMETA_OK);
            check_equal(cmeta_collector_accept(&collector, &cmeta_type_int,
                                               &value), CMETA_OK);
            cmeta_collector_abort(&collector);
            check_true(cmeta_container_descriptor(&aborted) == kind);
            check_equal(vec_init(&aborted, 1u), STL_OK);
            vec_destroy(&aborted);
        }

        vec_destroy(&vec);
        deque_destroy(&deque);
        list_destroy(&list);
        stack_destroy(&stack);
        queue_destroy(&queue);
        heap_destroy(&heap);
        set_destroy(&set);
        hash_set_destroy(&hash_set);
    }

    it("exposes associative entry views and collectors without generated entries") {
        HashMap(int, int, hash_map);
        Map(int, int, map);
        MultiMap(int, int, multimap);
        BTree(int, int, btree);
        BPlusTree(int, int, bplus_tree);
        HashMap(int, int, hash_output);
        Map(int, int, map_output);
        MultiMap(int, int, multimap_output);
        BTree(int, int, btree_output);
        BPlusTree(int, int, bplus_output);
        Map(int, int, rejected);
        const cmeta_container_desc *hash_desc = cmeta_container_descriptor(&hash_map);
        const cmeta_container_desc *map_desc = cmeta_container_descriptor(&map);
        const cmeta_container_desc *multimap_desc = cmeta_container_descriptor(&multimap);
        const cmeta_container_desc *btree_desc = cmeta_container_descriptor(&btree);
        const cmeta_container_desc *bplus_desc = cmeta_container_descriptor(&bplus_tree);
        cmeta_entry input;
        cmeta_collector collector;
        int key = 3;
        int value = 30;

        check_not_null(hash_desc);
        check_not_null(map_desc);
        check_not_null(multimap_desc);
        check_not_null(btree_desc);
        check_not_null(bplus_desc);

        check_equal(hash_map_init(&hash_map, 2u), STL_OK);
        check_equal(map_init(&map, 2u), STL_OK);
        check_equal(multimap_init(&multimap, 2u), STL_OK);
        check_equal(btree_init(&btree, 2u), STL_OK);
        check_equal(bplus_tree_init(&bplus_tree, 2u), STL_OK);

        check_equal(hash_map_put(&hash_map, &key, &value), STL_OK);
        check_equal(map_put(&map, &key, &value), STL_OK);
        check_equal(multimap_put(&multimap, &key, &value), STL_OK);
        check_equal(btree_put(&btree, &key, &value), STL_OK);
        check_equal(bplus_tree_put(&bplus_tree, &key, &value), STL_OK);

#define CHECK_ASSOC_VIEWS(handle, expected_entry_type, key_flags, value_flags, entry_flags) do { \
    cmeta_range default_range = {0};                                            \
    cmeta_range keys_range = {0};                                               \
    cmeta_range values_range = {0};                                             \
    cmeta_range entries_range = {0};                                            \
    cmeta_range_cursor key_cursor = {0};                                        \
    cmeta_range_cursor value_cursor = {0};                                      \
    cmeta_range_cursor entry_cursor = {0};                                      \
    cmeta_entry entry = {0};                                                    \
    int key_out = 0;                                                            \
    int value_out = 0;                                                          \
    cmeta_gen_status entry_status;                                              \
    check_true(cmeta_container_range_view(&(handle),                            \
                    CMETA_CONTAINER_VIEW_DEFAULT, &default_range));             \
    check_true(cmeta_container_range_view(&(handle),                            \
                    CMETA_CONTAINER_VIEW_KEYS, &keys_range));                   \
    check_true(cmeta_container_range_view(&(handle),                            \
                    CMETA_CONTAINER_VIEW_VALUES, &values_range));               \
    check_true(cmeta_container_range_view(&(handle),                            \
                    CMETA_CONTAINER_VIEW_ENTRIES, &entries_range));             \
    check_true(cmeta_type_equal(default_range.element_type,                     \
                                (expected_entry_type)));                         \
    check_true(cmeta_type_equal(entries_range.element_type,                     \
                                (expected_entry_type)));                         \
    check_true(cmeta_type_equal(keys_range.element_type, &cmeta_type_int));     \
    check_true(cmeta_type_equal(values_range.element_type, &cmeta_type_int));   \
    check_true((keys_range.flags & (key_flags)) == (key_flags));                \
    check_true((values_range.flags & (value_flags)) == (value_flags));          \
    check_true((entries_range.flags & (entry_flags)) == (entry_flags));         \
    check_equal(cmeta_range_next(&keys_range, &key_cursor, &key_out) ==         \
                    CMETA_GEN_ERROR, false);                                    \
    check_equal(key_out, key);                                                  \
    check_equal(cmeta_range_next(&values_range, &value_cursor, &value_out) ==   \
                    CMETA_GEN_ERROR, false);                                    \
    check_equal(value_out, value);                                              \
    entry_status = cmeta_range_next(&entries_range, &entry_cursor, &entry);     \
    check_true(entry_status == CMETA_GEN_VALUE ||                              \
               entry_status == CMETA_GEN_VALUE_AND_DONE);                      \
    check_true(cmeta_type_equal(entry.key_type, &cmeta_type_int));              \
    check_true(cmeta_type_equal(entry.value_type, &cmeta_type_int));            \
    check_equal(*(const int *)entry.key, key);                                  \
    check_equal(*(const int *)entry.value, value);                              \
    check_null(entry.key_storage);                                              \
    check_null(entry.value_storage);                                            \
} while (0)

        CHECK_ASSOC_VIEWS(hash_map, &cmeta_type_hash_entry,
            CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE);
        CHECK_ASSOC_VIEWS(map, &cmeta_type_ordered_entry,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE);
        CHECK_ASSOC_VIEWS(multimap, &cmeta_type_ordered_entry,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_REUSABLE);
        CHECK_ASSOC_VIEWS(btree, &cmeta_type_ordered_entry,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE);
        CHECK_ASSOC_VIEWS(bplus_tree, &cmeta_type_ordered_entry,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE,
            CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED | CMETA_RANGE_SORTED |
                CMETA_RANGE_UNIQUE | CMETA_RANGE_REUSABLE);
#undef CHECK_ASSOC_VIEWS

        input = (cmeta_entry){
            .key_type = &cmeta_type_int,
            .value_type = &cmeta_type_int,
            .key = &key,
            .value = &value
        };

#define BEGIN_ACCEPT_FINISH(output, expected_type) do {                         \
    const cmeta_container_desc *desc = cmeta_container_descriptor(&(output));   \
    check_not_null(desc);                                                       \
    check_not_null(desc->collector);                                            \
    if (desc != NULL && desc->collector != NULL) {                              \
        collector = desc->collector(&(output), 2u);                             \
        check_true(cmeta_type_equal(collector.input_type, (expected_type)));    \
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);               \
        check_equal(cmeta_collector_accept(&collector, (expected_type),         \
                                           &input), CMETA_OK);                   \
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);              \
    }                                                                           \
} while (0)

        BEGIN_ACCEPT_FINISH(hash_output, &cmeta_type_hash_entry);
        BEGIN_ACCEPT_FINISH(map_output, &cmeta_type_ordered_entry);
        BEGIN_ACCEPT_FINISH(multimap_output, &cmeta_type_ordered_entry);
        BEGIN_ACCEPT_FINISH(btree_output, &cmeta_type_ordered_entry);
        BEGIN_ACCEPT_FINISH(bplus_output, &cmeta_type_ordered_entry);
#undef BEGIN_ACCEPT_FINISH

        check_equal(*(const int *)hash_map_get_const(&hash_output, &key), value);
        check_equal(*(const int *)map_get_const(&map_output, &key), value);
        check_equal(multimap_count(&multimap_output, &key), (size_t)1u);
        check_equal(*(const int *)btree_get_const(&btree_output, &key), value);
        check_equal(*(const int *)bplus_tree_get_const(&bplus_output, &key), value);

        {
            const cmeta_container_desc *desc = cmeta_container_descriptor(&rejected);
            cmeta_type_desc missing_traits = cmeta_type_ordered_entry;
            check_not_null(desc);
            check_not_null(desc->collector);
            if (desc != NULL && desc->collector != NULL) {
                missing_traits.traits = NULL;
                collector = desc->collector(&rejected, 1u);
                collector.input_type = &missing_traits;
                check_equal(cmeta_collector_begin(&collector), CMETA_TRAIT_MISSING);
                check_equal(map_init(&rejected, 1u), STL_OK);
                map_destroy(&rejected);
            }
        }

        {
            long wrong_value = 30L;
            cmeta_entry mismatch = {
                .key_type = &cmeta_type_int,
                .value_type = &cmeta_type_long,
                .key = &key,
                .value = &wrong_value
            };
            const cmeta_container_desc *desc = cmeta_container_descriptor(&rejected);
            collector = desc->collector(&rejected, 1u);
            check_equal(cmeta_collector_begin(&collector), CMETA_OK);
            check_equal(cmeta_collector_accept(&collector,
                                               &cmeta_type_ordered_entry,
                                               &mismatch),
                        CMETA_TYPE_MISMATCH);
            check_equal(map_init(&rejected, 1u), STL_OK);
            map_destroy(&rejected);
        }

        hash_map_destroy(&hash_output);
        map_destroy(&map_output);
        multimap_destroy(&multimap_output);
        btree_destroy(&btree_output);
        bplus_tree_destroy(&bplus_output);
        hash_map_destroy(&hash_map);
        map_destroy(&map);
        multimap_destroy(&multimap);
        btree_destroy(&btree);
        bplus_tree_destroy(&bplus_tree);
    }
}
