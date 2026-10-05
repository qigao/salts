#define TINYTEST_NO_MAIN
#include "tinytest.h"
#include "cmeta_fixed_array_fixture.h"

#include <stdlib.h>
#include <string.h>

const cmeta_data_desc *cmeta_fixed_array_fixture_from_peer(void);

enum { ARRAY_OWNER_ZERO_TAG = 0x51a7u, ARRAY_OWNER_MAX_BYTES = 16u };
typedef struct array_owner {
    unsigned char *data;
    size_t size;
    unsigned tag;
} array_owner;

static size_t array_owner_live;
static size_t array_owner_assign_calls;
static size_t array_owner_fail_call;

static bool array_owner_is_zero(const void *object) {
    const array_owner *value = (const array_owner *)object;
    return value != NULL && value->data == NULL && value->size == 0u &&
           value->tag == ARRAY_OWNER_ZERO_TAG;
}

static cmeta_status array_owner_init(void *object) {
    array_owner *value = (array_owner *)object;
    value->data = NULL;
    value->size = 0u;
    value->tag = ARRAY_OWNER_ZERO_TAG;
    return CMETA_OK;
}

static void array_owner_restore(void *object) {
    array_owner *value = (array_owner *)object;
    if (value->data != NULL) {
        free(value->data);
        --array_owner_live;
    }
    (void)array_owner_init(object);
}

static cmeta_status array_owner_assign(void *object, const unsigned char *data,
                                     size_t size, size_t max_bytes) {
    array_owner *value = (array_owner *)object;
    if (!array_owner_is_zero(value)) return CMETA_INVALID_ARGUMENT;
    if (size > max_bytes || size > ARRAY_OWNER_MAX_BYTES)
        return CMETA_CAPACITY_EXCEEDED;
    if (size != 0u) {
        value->data = (unsigned char *)malloc(size);
        if (value->data == NULL) return CMETA_OUT_OF_MEMORY;
        ++array_owner_live;
        memcpy(value->data, data, size);
        value->size = size;
    }
    ++array_owner_assign_calls;
    return array_owner_assign_calls == array_owner_fail_call
               ? CMETA_CALLBACK_ERROR : CMETA_OK;
}

static cmeta_status array_owner_read(const void *object,
                                     const unsigned char **data, size_t *size) {
    const array_owner *value = (const array_owner *)object;
    *data = value->data;
    *size = value->size;
    return CMETA_OK;
}

static void array_owner_move(void *destination, void *source) {
    *(array_owner *)destination = *(array_owner *)source;
    (void)array_owner_init(source);
}

static bool array_owner_copy_construct(void *destination, const void *source) {
    const array_owner *value = (const array_owner *)source;
    (void)array_owner_init(destination);
    if (array_owner_assign(destination, value->data, value->size,
                           ARRAY_OWNER_MAX_BYTES) == CMETA_OK)
        return true;
    array_owner_restore(destination);
    return false;
}

static const cmeta_type_traits array_owner_traits = {
    CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    NULL, NULL, NULL, array_owner_copy_construct, array_owner_move, array_owner_restore
};
static const cmeta_type_identity array_owner_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.ArrayOwner");
static const cmeta_type_desc array_owner_type = {
    "array_owner", sizeof(array_owner), _Alignof(array_owner), CMETA_T_OBJECT,
    NULL, &array_owner_traits, &array_owner_identity
};
static const cmeta_data_buffer_shape array_owner_shape = {CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_buffer_ops array_owner_buffer_ops = {
    sizeof(cmeta_data_buffer_ops), CMETA_DATA_BUFFER_OPS_ABI_VERSION,
    &array_owner_type, CMETA_DATA_BUFFER_OWNED, array_owner_is_zero,
    array_owner_assign, array_owner_restore, array_owner_read,
    array_owner_init, array_owner_move
};
static const cmeta_data_desc array_owner_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.ArrayOwner.data", .display_name = "ArrayOwner",
    .kind = CMETA_DATA_BYTES, .storage_type = &array_owner_type,
    .shape = &array_owner_shape, .buffer_ops = &array_owner_buffer_ops
};
typedef array_owner array_owner_triple[CMETA_FIXED_ARRAY_TEST_COUNT];
CMETA_DEFINE_FIXED_ARRAY(array_owner_triple_value, array_owner_triple,
    array_owner, CMETA_FIXED_ARRAY_TEST_COUNT, &array_owner_data,
    "test.ArrayOwnerTriple", "ArrayOwnerTriple");

typedef cmeta_fixed_array_fixture nested_array[2];
CMETA_DEFINE_FIXED_ARRAY(nested_array_value, nested_array,
    cmeta_fixed_array_fixture, 2u, &cmeta_fixed_array_value_cmeta_data,
    "test.NestedIntArrays", "NestedIntArrays");

spec("CMeta fixed array lifecycle") {
  static array_owner_triple source;
  static array_owner_triple destination;
  static array_owner_triple moved;
  static const cmeta_data_desc *data = &array_owner_triple_value_cmeta_data;

  before_each() {
    array_owner_fail_call = 0u;
    array_owner_assign_calls = 0u;
    memset(source, 0xa5, sizeof(source));
    memset(destination, 0xa5, sizeof(destination));
    memset(moved, 0xa5, sizeof(moved));
    check_equal(cmeta_data_value_init_zero(data, source), CMETA_OK);
    check_equal(cmeta_data_value_init_zero(data, destination), CMETA_OK);
    check_equal(cmeta_data_value_init_zero(data, moved), CMETA_OK);
  }

  after_each() {
    array_owner_fail_call = 0u;
    cmeta_data_value_destroy(data, source);
    cmeta_data_value_destroy(data, destination);
    cmeta_data_value_destroy(data, moved);
    check_equal(array_owner_live, 0u);
  }

  it("preserves inline ABI and semantic identity across translation units") {
    const cmeta_data_desc *peer = cmeta_fixed_array_fixture_from_peer();
    const cmeta_fixed_array_fixture original = {2, 4, 8};
    cmeta_fixed_array_fixture copy;
    bool zero = false;
    check_true(cmeta_data_desc_equal(peer, &cmeta_fixed_array_value_cmeta_data));
    check_true(peer != &cmeta_fixed_array_value_cmeta_data);
    check_equal(peer->storage_type->size, sizeof(original));
    check_equal(cmeta_data_value_init_zero(peer, copy), CMETA_OK);
    check_equal(cmeta_data_value_copy(peer, copy, original), CMETA_OK);
    check_equal(copy, original, sizeof(copy));
    check_equal(cmeta_data_value_restore_zero(peer, copy), CMETA_OK);
    check_equal(cmeta_data_value_is_zero(peer, copy, &zero), CMETA_OK);
    check_true(zero);
  }

  it("has live fixed slots even when every element is semantic zero") {
    cmeta_data_collection_view view = {0};
    bool zero = false;
    check_equal(cmeta_data_value_is_zero(data, destination, &zero), CMETA_OK);
    check_true(zero);
    check_equal(cmeta_data_collection_read(data, destination, &view), CMETA_OK);
    check_equal(view.count, CMETA_FIXED_ARRAY_TEST_COUNT);
    check_equal(view.stride, sizeof(array_owner));
    check_equal(destination[0].tag, ARRAY_OWNER_ZERO_TAG);
  }

  it("deep copies owners and moves without retaining source payloads") {
    size_t i;
    for (i = 0u; i < CMETA_FIXED_ARRAY_TEST_COUNT; ++i)
        check_equal(cmeta_data_buffer_assign(&array_owner_data, &source[i],
            (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    check_equal(cmeta_data_value_copy(data, destination, source), CMETA_OK);
    check_equal(array_owner_live, 2u * CMETA_FIXED_ARRAY_TEST_COUNT);
    for (i = 0u; i < CMETA_FIXED_ARRAY_TEST_COUNT; ++i) {
        check_true(destination[i].data != source[i].data);
        check_equal(destination[i].data, source[i].data, source[i].size);
    }
    check_equal(cmeta_data_value_move(data, moved, destination), CMETA_OK);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(cmeta_data_value_restore_zero(data, source), CMETA_OK);
    check_equal(moved[1].data, "abc", 3u);
    check_equal(cmeta_data_value_restore_zero(data, moved), CMETA_OK);
    check_equal(cmeta_data_value_restore_zero(data, moved), CMETA_OK);
  }

  it("rolls back a copied prefix and the partially failed element") {
    size_t i;
    for (i = 0u; i < CMETA_FIXED_ARRAY_TEST_COUNT; ++i)
        check_equal(cmeta_data_buffer_assign(&array_owner_data, &source[i],
            (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    array_owner_fail_call = array_owner_assign_calls + 2u;
    check_equal(cmeta_data_value_copy(data, destination, source), CMETA_CALLBACK_ERROR);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(array_owner_live, CMETA_FIXED_ARRAY_TEST_COUNT);
    array_owner_fail_call = 0u;
    check_equal(cmeta_data_value_copy(data, destination, source), CMETA_OK);
  }

  it("rejects short and excessive collections and releases accepted owners") {
    cmeta_collector collector = {0};
    size_t i;
    check_equal(cmeta_data_buffer_assign(&array_owner_data, &source[0],
        (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    check_equal(cmeta_data_collection_collector(data, destination, SIZE_MAX,
                                               &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_data_collection_accept(data, &collector, &array_owner_data,
                                            &source[0]), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_TYPE_MISMATCH);
    check_equal(collector.state, CMETA_COLLECTOR_ABORTED);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(array_owner_live, 1u);
    check_equal(cmeta_data_collection_collector(data, destination, SIZE_MAX,
                                               &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    for (i = 0u; i < CMETA_FIXED_ARRAY_TEST_COUNT; ++i)
        check_equal(cmeta_data_collection_accept(data, &collector, &array_owner_data,
                                                &source[0]), CMETA_OK);
    check_equal(cmeta_data_collection_accept(data, &collector, &array_owner_data,
                                            &source[0]), CMETA_CAPACITY_EXCEEDED);
    cmeta_collector_abort(&collector);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(array_owner_live, 1u);
  }

  it("preserves an output that failed begin and allocates nothing before begin") {
    cmeta_collector collector = {0};
    check_equal(cmeta_data_collection_collector(data, destination, 0u, &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_CAPACITY_EXCEEDED);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(cmeta_data_buffer_assign(&array_owner_data, &destination[0],
        (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    check_equal(cmeta_data_collection_collector(data, destination, SIZE_MAX,
                                               &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_INVALID_ARGUMENT);
    check_equal(destination[0].data, "abc", 3u);
    check_equal(array_owner_live, 1u);
  }

  it("commits exactly the fixed element count and repeated abort is harmless") {
    cmeta_collector collector = {0};
    size_t i;
    check_equal(cmeta_data_buffer_assign(&array_owner_data, &source[0],
        (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    check_equal(cmeta_data_collection_collector(data, destination,
        CMETA_FIXED_ARRAY_TEST_COUNT, &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    for (i = 0u; i < CMETA_FIXED_ARRAY_TEST_COUNT; ++i)
        check_equal(cmeta_data_collection_accept(data, &collector, &array_owner_data,
                                                &source[0]), CMETA_OK);
    check_equal(cmeta_collector_finish(&collector), CMETA_OK);
    cmeta_collector_abort(&collector);
    check_equal(collector.state, CMETA_COLLECTOR_COMMITTED);
    check_equal(array_owner_live, 1u + CMETA_FIXED_ARRAY_TEST_COUNT);
    check_equal(destination[1].data, "abc", 3u);
  }

  it("aborts explicit cancellation once and keeps type mismatch distinct") {
    cmeta_collector collector = {0};
    int wrong = 1;
    check_equal(cmeta_data_collection_collector(data, destination, SIZE_MAX,
                                               &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &wrong),
                CMETA_TYPE_MISMATCH);
    check_equal(collector.state, CMETA_COLLECTOR_ABORTED);
    cmeta_collector_abort(&collector);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(array_owner_live, 0u);
    check_equal(cmeta_data_buffer_assign(&array_owner_data, &source[0],
        (const unsigned char *)"abc", 3u, ARRAY_OWNER_MAX_BYTES), CMETA_OK);
    check_equal(cmeta_data_collection_collector(data, destination, SIZE_MAX,
                                               &collector), CMETA_OK);
    check_equal(cmeta_collector_begin(&collector), CMETA_OK);
    check_equal(cmeta_data_collection_accept(data, &collector, &array_owner_data,
                                            &source[0]), CMETA_OK);
    cmeta_collector_abort(&collector);
    cmeta_collector_abort(&collector);
    check_equal(collector.state, CMETA_COLLECTOR_ABORTED);
    check_true(array_owner_triple_value_is_zero(destination));
    check_equal(array_owner_live, 1u);
  }

  it("does not read the new optional callbacks beyond an old collection prefix") {
    enum { PREFIX_SIZE = offsetof(cmeta_data_collection_ops, collector_init) };
    cmeta_data_collection_ops *prefix = (cmeta_data_collection_ops *)malloc(PREFIX_SIZE);
    cmeta_data_desc legacy = *data;
    cmeta_collector collector = {0};
    bool zero = true;
    cmeta_status construct_status;
    cmeta_status zero_status;
    check_not_null(prefix);
    memcpy(prefix, data->collection_ops, PREFIX_SIZE);
    prefix->struct_size = PREFIX_SIZE;
    legacy.collection_ops = prefix;
    construct_status = cmeta_data_collection_collector(&legacy, destination,
                                                      SIZE_MAX, &collector);
    zero_status = cmeta_data_value_is_zero(&legacy, destination, &zero);
    free(prefix);
    check_equal(construct_status, CMETA_TRAIT_MISSING);
    check_equal(zero_status, CMETA_OK);
    check_false(zero);
  }

  it("composes fixed arrays without interpreting native offsets as wire data") {
    nested_array original = {{1, 2, 3}, {4, 5, 6}};
    nested_array copy;
    check_equal(cmeta_data_value_init_zero(&nested_array_value_cmeta_data, copy), CMETA_OK);
    check_equal(cmeta_data_value_copy(&nested_array_value_cmeta_data, copy, original), CMETA_OK);
    check_equal(copy, original, sizeof(copy));
    check_equal(cmeta_data_value_restore_zero(&nested_array_value_cmeta_data, copy), CMETA_OK);
    check_true(nested_array_value_is_zero(copy));
  }

  it("fails closed on malformed native array extents") {
    cmeta_data_fixed_array_spec invalid = array_owner_triple_value_cmeta_spec;
    invalid.count = SIZE_MAX;
    check_false(cmeta_data_fixed_array_spec_valid(&invalid));
    check_equal(cmeta_data_fixed_array_init_zero(&invalid, destination), CMETA_INVALID_ARGUMENT);
    check_true(array_owner_triple_value_is_zero(destination));
  }
}
