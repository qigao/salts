#include <cmeta/cmeta.h>
#include <cmeta/container.h>
#include "tinytest.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>

_Static_assert(CMETA_FLOAT_TRAITS_OBJECT_HASH_WIDTHS,
               "floating trait hashes require complete object-width copies");
_Static_assert(CMETA_FLOAT_TRAITS_BINARY32_BINARY64,
               "floating trait tests require the documented binary32/binary64 contract");

typedef struct owned_int {
    int *value;
} owned_int;

static size_t owned_copies;
static size_t owned_moves;
static size_t owned_destroys;

static bool owned_equal(const void *left_, const void *right_) {
    const owned_int *left = (const owned_int *)left_;
    const owned_int *right = (const owned_int *)right_;

    return left != NULL && right != NULL && left->value != NULL &&
           right->value != NULL && *left->value == *right->value;
}

static bool owned_copy(void *destination_, const void *source_) {
    owned_int *destination = (owned_int *)destination_;
    const owned_int *source = (const owned_int *)source_;

    if (destination == NULL || source == NULL || source->value == NULL)
        return false;
    destination->value = (int *)malloc(sizeof(*destination->value));
    if (destination->value == NULL)
        return false;
    *destination->value = *source->value;
    ++owned_copies;
    return true;
}

static void owned_move(void *destination_, void *source_) {
    owned_int *destination = (owned_int *)destination_;
    owned_int *source = (owned_int *)source_;

    if (destination == NULL || source == NULL)
        return;
    destination->value = source->value;
    source->value = NULL;
    ++owned_moves;
}

static void owned_destroy(void *value_) {
    owned_int *value = (owned_int *)value_;

    if (value == NULL)
        return;
    free(value->value);
    value->value = NULL;
    ++owned_destroys;
}

Traits(owned_int,
    (equal, owned_equal),
    (copy, owned_copy),
    (move, owned_move),
    (destroy, owned_destroy));

#define CMETA_TEST_OWNED_INT_ROW \
    (O, owned_int, cmeta_type_owned_int, CMETA_T_OBJECT, cmeta_traits_owned_int)

static const cmeta_type_desc cmeta_test_owned_int_type = {
    "owned_int", sizeof(owned_int), _Alignof(owned_int), CMETA_T_OBJECT, NULL,
    &CMETA_TYPE_TRAITS(CMETA_TEST_OWNED_INT_ROW)
};

const cmeta_type_desc *cmeta_traits_peer_owned_int_type(void);

Enum(cmeta_test_state,
    (CMETA_TEST_READY, 10, "ready"),
    (CMETA_TEST_DONE,  20, "done")
);

typed_any(value, int, cmeta_test_increment, (int value)) {
    return value + 1;
}

typed_any(associative, long, cmeta_test_add, (long left, long right)) {
    return left + right;
}

static cmeta_gen_status cmeta_test_expand(int input, long *out, size_t *cursor) {
    if (*cursor >= 2u) {
        return CMETA_GEN_DONE;
    }
    *out = (long)input + (long)*cursor;
    ++*cursor;
    return *cursor == 2u ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

typedef struct cmeta_test_range_owner {
    uint64_t generation;
    int values[2];
    size_t count;
    size_t next_calls;
    size_t version_reads;
} cmeta_test_range_owner;

static uint64_t cmeta_test_range_version(const void *object) {
    cmeta_test_range_owner *owner = (cmeta_test_range_owner *)object;

    ++owner->version_reads;
    return owner->generation;
}

static size_t cmeta_test_range_size(const void *object) {
    const cmeta_test_range_owner *owner = (const cmeta_test_range_owner *)object;

    return owner->count;
}

static cmeta_gen_status cmeta_test_range_next(const void *object,
                                               cmeta_range_cursor *cursor,
                                               void *out_value) {
    cmeta_test_range_owner *owner = (cmeta_test_range_owner *)object;

    if (cursor->index >= owner->count)
        return CMETA_GEN_DONE;
    *(int *)out_value = owner->values[cursor->index];
    ++owner->next_calls;
    ++cursor->index;
    return cursor->index == owner->count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

typedef struct cmeta_test_link_node {
    int value;
    struct cmeta_test_link_node *next;
} cmeta_test_link_node;

typedef struct cmeta_test_link_owner {
    cmeta_test_link_node *head;
} cmeta_test_link_owner;

static cmeta_gen_status cmeta_test_link_next(const void *object,
                                             cmeta_range_cursor *cursor,
                                             void *out_value) {
    const cmeta_test_link_owner *owner =
        (const cmeta_test_link_owner *)object;
    cmeta_test_link_node *node = cursor->state[0] == NULL
                                     ? owner->head
                                     : (cmeta_test_link_node *)cursor->state[0];

    if (node == NULL)
        return CMETA_GEN_DONE;
    *(int *)out_value = node->value;
    cursor->state[0] = node->next;
    return node->next == NULL ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range cmeta_test_range(const cmeta_test_range_owner *owner) {
    return (cmeta_range){
        owner, &cmeta_type_int, CMETA_RANGE_SIZED,
        cmeta_test_range_size, cmeta_test_range_next,
        owner->generation, cmeta_test_range_version
    };
}

typedef struct cmeta_test_generated_range_raw {
    int values[2];
    size_t count;
} cmeta_test_generated_range_raw;

typedef struct cmeta_test_generated_range_container {
    cmeta_container_header cmeta;
    cmeta_test_generated_range_raw raw;
    uint64_t generation;
} cmeta_test_generated_range_container;

typedef struct cmeta_test_required_range_container {
    cmeta_container_header cmeta;
    cmeta_test_generated_range_raw raw;
    uint64_t generation;
} cmeta_test_required_range_container;

static size_t cmeta_test_generated_range_raw_size(
    const cmeta_test_generated_range_raw *raw) {
    return raw->count;
}

static const void *cmeta_test_generated_range_raw_at_const(
    const cmeta_test_generated_range_raw *raw, size_t index) {
    return index < raw->count ? &raw->values[index] : NULL;
}

static uint64_t cmeta_test_generated_range_version(const void *object) {
    const cmeta_test_generated_range_container *container =
        (const cmeta_test_generated_range_container *)object;

    return container->generation;
}

static size_t cmeta_test_required_range_version_reads;
static size_t cmeta_test_null_owner_next_calls;

static uint64_t cmeta_test_required_range_version(const void *object) {
    const cmeta_test_required_range_container *container =
        (const cmeta_test_required_range_container *)object;

    ++cmeta_test_required_range_version_reads;
    if (container == NULL)
        return UINT64_C(0);
    return container->generation;
}

static cmeta_gen_status cmeta_test_null_owner_next(const void *object,
                                                    cmeta_range_cursor *cursor,
                                                    void *out_value) {
    (void)object;
    (void)cursor;
    (void)out_value;
    ++cmeta_test_null_owner_next_calls;
    return CMETA_GEN_VALUE;
}

typedef struct cmeta_test_slot_range_raw {
    int values[2];
    bool occupied[2];
} cmeta_test_slot_range_raw;

typedef struct cmeta_test_slot_range_container {
    cmeta_container_header cmeta;
    cmeta_test_slot_range_raw raw;
    uint64_t generation;
} cmeta_test_slot_range_container;

static size_t cmeta_test_slot_range_raw_size(
    const cmeta_test_slot_range_raw *raw) {
    size_t count = 0u;
    size_t index;

    for (index = 0u; index < 2u; ++index)
        count += raw->occupied[index] ? 1u : 0u;
    return count;
}

static size_t cmeta_test_slot_range_raw_capacity(
    const cmeta_test_slot_range_raw *raw) {
    (void)raw;
    return 2u;
}

static const void *cmeta_test_slot_range_raw_key_at(
    const cmeta_test_slot_range_raw *raw, size_t index) {
    return index < 2u && raw->occupied[index] ? &raw->values[index] : NULL;
}

static uint64_t cmeta_test_slot_range_version(const void *object) {
    const cmeta_test_slot_range_container *container =
        (const cmeta_test_slot_range_container *)object;

    return container->generation;
}

CMETA_CONTAINER1_SLOT_RANGE_DEFINE(
    cmeta_test_slot_range_container,
    int,
    cmeta_test_slot_range_raw,
    CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE,
    cmeta_test_slot_range_version,
    NULL)

typedef struct cmeta_test_assoc_range_raw {
    int keys[2];
    long values[2];
    bool occupied[2];
} cmeta_test_assoc_range_raw;

typedef struct cmeta_test_assoc_range_container_entry {
    int key;
    long value;
} cmeta_test_assoc_range_container_entry;

typedef struct cmeta_test_assoc_range_container {
    cmeta_container_header cmeta;
    cmeta_test_assoc_range_raw raw;
    uint64_t generation;
} cmeta_test_assoc_range_container;

static size_t cmeta_test_assoc_range_raw_size(
    const cmeta_test_assoc_range_raw *raw) {
    size_t count = 0u;
    size_t index;

    for (index = 0u; index < 2u; ++index)
        count += raw->occupied[index] ? 1u : 0u;
    return count;
}

static size_t cmeta_test_assoc_range_raw_capacity(
    const cmeta_test_assoc_range_raw *raw) {
    (void)raw;
    return 2u;
}

static const void *cmeta_test_assoc_range_raw_key_at(
    const cmeta_test_assoc_range_raw *raw, size_t index) {
    return index < 2u && raw->occupied[index] ? &raw->keys[index] : NULL;
}

static const void *cmeta_test_assoc_range_raw_value_at(
    const cmeta_test_assoc_range_raw *raw, size_t index) {
    return index < 2u && raw->occupied[index] ? &raw->values[index] : NULL;
}

static uint64_t cmeta_test_assoc_range_version(const void *object) {
    const cmeta_test_assoc_range_container *container =
        (const cmeta_test_assoc_range_container *)object;

    return container->generation;
}

CMETA_CONTAINER2_RANGES_DEFINE(
    cmeta_test_assoc_range_container,
    int,
    long,
    cmeta_test_assoc_range_raw,
    key_at,
    value_at,
    CMETA_RANGE_SIZED | CMETA_RANGE_UNIQUE,
    CMETA_RANGE_SIZED,
    CMETA_RANGE_SIZED,
    cmeta_test_assoc_range_version,
    NULL)

CMETA_CONTAINER1_INDEX_RANGE_DEFINE(
    cmeta_test_generated_range_container,
    int,
    cmeta_test_generated_range_raw,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED,
    cmeta_test_generated_range_version,
    NULL)

CMETA_CONTAINER1_INDEX_RANGE_DEFINE(
    cmeta_test_required_range_container,
    int,
    cmeta_test_generated_range_raw,
    CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED,
    cmeta_test_required_range_version,
    NULL)

enum {
    CMETA_TEST_COUNTER_RESET = 1u << 0
};

#define CMETA_TEST_COUNTER_METHODS(X,I) \
    X(I,R1,int,add,int,delta) \
    X(I,R0,int,value,_) \
    X(I,V0,void,reset,_)

CMETA_INTERFACE(cmeta_test_counter, CMETA_TEST_COUNTER_METHODS);

typedef struct cmeta_test_counter_state {
    int value;
} cmeta_test_counter_state;

static int cmeta_test_basic_add(void *self, int delta) {
    cmeta_test_counter_state *state = (cmeta_test_counter_state *)self;
    state->value += delta;
    return state->value;
}

static int cmeta_test_basic_value(void *self) {
    return ((cmeta_test_counter_state *)self)->value;
}

static void cmeta_test_basic_reset(void *self) {
    ((cmeta_test_counter_state *)self)->value = 0;
}

CMETA_IMPLEMENTS(cmeta_test_counter, cmeta_test_basic_counter,
                 CMETA_TEST_COUNTER_RESET,
    .add = cmeta_test_basic_add,
    .value = cmeta_test_basic_value,
    .reset = cmeta_test_basic_reset
);

suite("CMeta core") {
    it("defines a complete interface without a separate implementation replay") {
        cmeta_test_counter_state state = {4};
        cmeta_test_counter counter =
            cmeta_test_basic_counter_as_cmeta_test_counter(&state);
        const cmeta_interface_desc *meta = cmeta_test_counter_interface();

        check_true(cmeta_test_counter_valid(&counter));
        check_equal(cmeta_test_counter_implementation(&counter),
                    "cmeta_test_basic_counter");
        check_true(cmeta_test_counter_has(&counter,
                                          CMETA_TEST_COUNTER_RESET));
        check_equal(cmeta_test_counter_add(&counter, 3), 7);
        check_equal(cmeta_test_counter_value(&counter), 7);
        cmeta_test_counter_reset(&counter);
        check_equal(cmeta_test_counter_value(&counter), 0);

        check_not_null(meta);
        check_equal(meta->name, "cmeta_test_counter");
        check_equal(meta->method_count, (size_t)3);
        check_equal(meta->methods[0].name, "add");
        check_equal(meta->methods[0].arity, 1u);
        check_equal(meta->methods[1].name, "value");
        check_equal(meta->methods[1].arity, 0u);
        check_equal(meta->methods[2].name, "reset");
        check_equal(meta->methods[2].arity, 0u);
    }

    it("rejects an interface whose required vtable method is missing") {
        const cmeta_test_counter_vtable incomplete_vtable = {
            .implementation = "incomplete_counter",
            .capabilities = 0u,
            .add = cmeta_test_basic_add,
            .value = cmeta_test_basic_value,
            .reset = NULL
        };
        cmeta_test_counter_state state = {4};
        cmeta_test_counter counter =
            cmeta_test_counter_bind(&state, &incomplete_vtable);

        check_false(cmeta_test_counter_valid(&counter));
    }

    it("exposes builtin type metadata through a bounded registry") {
        const cmeta_type_desc *int_type = cmeta_type_find("int");
        cmeta_type_desc equivalent;

        check_not_null(int_type);
        check_equal(int_type->name, "int");
        check_equal(int_type->size, sizeof(int));
        check_equal(int_type->align, _Alignof(int));
        check_true(int_type->kind == CMETA_T_INTEGER);

        equivalent = *int_type;
        check_true(cmeta_type_equal(int_type, &equivalent));
        check_null(cmeta_type_find("not-a-cmeta-type"));
        check_null(cmeta_type_find(NULL));
        check_null(cmeta_type_registry_at(cmeta_type_registry_count()));
    }

    it("rejects malformed unnamed type descriptors") {
        cmeta_type_desc unnamed = {
            NULL, sizeof(int), _Alignof(int), CMETA_T_OBJECT, NULL, NULL
        };

        check_false(cmeta_type_equal(&unnamed, &unnamed));
        check_false(cmeta_type_equal(&unnamed, &cmeta_type_int));
        check_false(cmeta_type_equal(&cmeta_type_int, &unnamed));
    }

    it("attaches named traits to every builtin scalar descriptor") {
        const cmeta_trait_flags required =
            CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
            CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY;

        check_true(cmeta_type_bool.traits == &cmeta_traits_bool);
        check_true(cmeta_type_int.traits == &cmeta_traits_int);
        check_true(cmeta_type_long.traits == &cmeta_traits_long);
        check_true(cmeta_type_float.traits == &cmeta_traits_float);
        check_true(cmeta_type_double.traits == &cmeta_traits_double);
        check_null(cmeta_type_int_ptr.traits);
        check_true((cmeta_type_bool.traits->flags & required) == required);
        check_true((cmeta_type_long.traits->flags & required) == required);
    }

    it("normalizes floating keys for zero and NaN equivalence") {
        const cmeta_type_traits *float_traits = cmeta_type_float.traits;
        const cmeta_type_traits *double_traits = cmeta_type_double.traits;
        float float_zero = 0.0f;
        float float_negative_zero = -0.0f;
        float float_nan_one = NAN;
        float float_nan_two = nanf("2");
        float float_one = 1.0f;
        double double_zero = 0.0;
        double double_negative_zero = -0.0;
        double double_nan_one = NAN;
        double double_nan_two = nan("2");
        double double_one = 1.0;

        check_true(CMETA_FLOAT_TRAITS_OBJECT_HASH_WIDTHS);
        check_true(CMETA_FLOAT_TRAITS_BINARY32_BINARY64);
        check_true(float_traits->equal(&float_zero, &float_negative_zero));
        check_true(float_traits->hash(&float_zero) ==
                   float_traits->hash(&float_negative_zero));
        check_true(float_traits->equal(&float_nan_one, &float_nan_two));
        check_true(float_traits->hash(&float_nan_one) ==
                   float_traits->hash(&float_nan_two));
        check_equal(float_traits->compare(&float_nan_one, &float_nan_two), 0);
        check_true(float_traits->compare(&float_nan_one, &float_one) > 0);
        check_true(float_traits->compare(&float_one, &float_nan_one) < 0);

        check_true(double_traits->equal(&double_zero, &double_negative_zero));
        check_true(double_traits->hash(&double_zero) ==
                   double_traits->hash(&double_negative_zero));
        check_true(double_traits->equal(&double_nan_one, &double_nan_two));
        check_true(double_traits->hash(&double_nan_one) ==
                   double_traits->hash(&double_nan_two));
        check_equal(double_traits->compare(&double_nan_one, &double_nan_two), 0);
        check_true(double_traits->compare(&double_nan_one, &double_one) > 0);
        check_true(double_traits->compare(&double_one, &double_nan_one) < 0);
    }

    it("keeps custom trait descriptors semantically equal across translation units") {
        const cmeta_type_desc *peer = cmeta_traits_peer_owned_int_type();
        owned_int source = {(int *)malloc(sizeof(*source.value))};
        owned_int copied = {NULL};
        owned_int moved = {NULL};
        const cmeta_trait_flags required =
            CMETA_TRAIT_EQUAL | CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
            CMETA_TRAIT_DESTROY;

        check_not_null(source.value);
        *source.value = 17;
        owned_copies = 0u;
        owned_moves = 0u;
        owned_destroys = 0u;

        check_true(cmeta_test_owned_int_type.traits->copy_construct(&copied, &source));
        check_true(cmeta_test_owned_int_type.traits->equal(&source, &copied));
        check_equal(owned_copies, (size_t)1u);
        cmeta_test_owned_int_type.traits->move_construct(&moved, &copied);
        check_null(copied.value);
        check_not_null(moved.value);
        check_equal(owned_moves, (size_t)1u);
        cmeta_test_owned_int_type.traits->destroy(&moved);
        cmeta_test_owned_int_type.traits->destroy(&source);
        check_equal(owned_destroys, (size_t)2u);

        check_not_null(peer);
        check_true(&cmeta_test_owned_int_type != peer);
        check_true(cmeta_test_owned_int_type.traits != peer->traits);
        check_true(cmeta_type_equal(&cmeta_test_owned_int_type, peer));
        check_true((cmeta_test_owned_int_type.traits->flags & required) == required);
        check_true((peer->traits->flags & required) == required);
    }

    it("exposes explicit built-in scalar traits") {
        int a = 7;
        int b = 7;
        int copied = 0;
        int moved = 0;
        const cmeta_type_traits *traits = cmeta_type_int.traits;

        check_not_null(traits);
        check_true(traits->equal(&a, &b));
        check_true(traits->hash(&a) == traits->hash(&b));
        check_equal(traits->compare(&a, &b), 0);
        b = 8;
        check_true(traits->compare(&a, &b) < 0);
        check_true(traits->copy_construct(&copied, &a));
        check_equal(copied, 7);
        traits->move_construct(&moved, &copied);
        check_equal(moved, 7);
        traits->destroy(&moved);
        check_equal(cmeta_type_require_traits(
                        &cmeta_type_int,
                        CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH |
                            CMETA_TRAIT_COMPARE | CMETA_TRAIT_COPY |
                            CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY),
                    CMETA_OK);
    }

    it("rejects missing or invalid required traits") {
        cmeta_type_desc opaque = {
            "opaque", sizeof(int), _Alignof(int), CMETA_T_OBJECT, NULL, NULL
        };
        cmeta_type_traits trivial_copy_without_operation = {
            CMETA_TRAIT_COPY | CMETA_TRAIT_TRIVIAL_COPY,
            NULL, NULL, NULL, NULL, NULL, NULL
        };
        cmeta_type_desc advertised_copy = {
            "advertised_copy", sizeof(int), _Alignof(int), CMETA_T_OBJECT,
            NULL, &trivial_copy_without_operation
        };

        check_equal(cmeta_type_require_traits(NULL, CMETA_TRAIT_HASH),
                    CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_type_require_traits(&opaque, CMETA_TRAIT_HASH),
                    CMETA_TRAIT_MISSING);
        check_equal(cmeta_type_require_traits(&advertised_copy, CMETA_TRAIT_COPY),
                    CMETA_TRAIT_MISSING);
        check_equal(cmeta_type_require_traits(&cmeta_type_int, 1u << 31),
                    CMETA_INVALID_ARGUMENT);
    }

    it("reflects enum text, symbols, values, and invalid input") {
        cmeta_test_state parsed = CMETA_TEST_READY;
        const cmeta_enum_desc *meta = cmeta_test_state_meta();

        check_not_null(meta);
        check_equal(meta->name, "cmeta_test_state");
        check_equal(meta->count, (size_t)2);
        check_equal(cmeta_test_state_to_string(CMETA_TEST_DONE), "done");
        check_equal(cmeta_test_state_to_symbol(CMETA_TEST_READY), "CMETA_TEST_READY");
        check_true(cmeta_test_state_from_string("done", &parsed));
        check_true(parsed == CMETA_TEST_DONE);
        check_true(cmeta_test_state_from_string("CMETA_TEST_READY", &parsed));
        check_true(parsed == CMETA_TEST_READY);
        check_false(cmeta_test_state_from_string("missing", &parsed));
        check_false(cmeta_test_state_from_string("ready", NULL));
        check_null(cmeta_test_state_to_string((cmeta_test_state)999));
    }

    it("binds typed callables and preserves their contracts") {
        cmeta_callable bound;
        const cmeta_sig_desc *signature;
        int input = 41;
        int output = 0;
        const void *args[] = {&input};

        check_true(cmeta_callable_contract_valid(cmeta_test_increment));
        check_true(cmeta_callable_bind(cmeta_test_increment, &bound));
        check_true(bound.meta.sig != CMETA_SIG_INVALID);
        check_null(bound.resolve);
        check_true(cmeta_callable_same(cmeta_test_increment, bound));
        check_true(cmeta_callable_invoke(&bound, &output, args));
        check_equal(output, 42);

        signature = cmeta_callable_signature(bound);
        check_not_null(signature);
        check_equal(signature->param_count, (size_t)1);
        check_true(signature->protocol == CMETA_FN_PROTOCOL_VALUE);
        check_true(cmeta_effects_are_pure(bound.meta.effects));
        check_true(cmeta_properties_include(bound.meta.properties,
                                            CMETA_PROP_DETERMINISTIC |
                                                CMETA_PROP_TOTAL));

        check_true(cmeta_callable_contract_valid(cmeta_test_add));
        check_true(cmeta_properties_include(
            cmeta_test_add.meta.properties, CMETA_PROP_ASSOCIATIVE));
    }

    it("rejects a raw callable whose active target is missing") {
        cmeta_callable bound;
        cmeta_fn missing_target;

        check_true(cmeta_callable_bind(cmeta_test_increment, &bound));
        missing_target = bound.meta;
        memset(&missing_target.call, 0, sizeof(missing_target.call));

        check_false(cmeta_fn_contract_valid(missing_target));
    }

    it("rejects null argument slots without invoking the target") {
        cmeta_callable unary;
        cmeta_callable binary;
        int left = 7;
        int output = 0;
        const void *unary_args[] = {NULL};
        const void *binary_args[] = {&left, NULL};

        check_true(cmeta_callable_bind(cmeta_test_increment, &unary));
        check_true(cmeta_callable_bind(cmeta_test_add, &binary));
        check_false(cmeta_fn_invoke(unary.meta, &output, unary_args));
        check_false(cmeta_fn_invoke(binary.meta, &output, binary_args));
    }

    it("dispatches generators and rejects invalid invocation shapes") {
        cmeta_fn generator = CMETA_WRAP_TYPED_ANY(cmeta_test_expand);
        int input = 7;
        long output = 0;
        size_t cursor = 0;

        check_true(cmeta_fn_contract_valid(generator));
        check_true(cmeta_fn_signature(generator)->protocol ==
                   CMETA_FN_PROTOCOL_GENERATOR);
        check_true(cmeta_fn_generate(generator, &input, &output, &cursor) ==
                   CMETA_GEN_VALUE);
        check_equal(output, 7L);
        check_equal(cursor, (size_t)1);
        check_true(cmeta_fn_generate(generator, &input, &output, &cursor) ==
                   CMETA_GEN_VALUE_AND_DONE);
        check_equal(output, 8L);
        check_equal(cursor, (size_t)2);
        check_true(cmeta_fn_generate(generator, &input, &output, &cursor) ==
                   CMETA_GEN_DONE);
        check_false(cmeta_fn_invoke(generator, &output, NULL));
        check_true(cmeta_fn_generate(generator, NULL, &output, &cursor) ==
                   CMETA_GEN_ERROR);
    }

    it("fails when a borrowed range owner mutates") {
        cmeta_test_range_owner owner = {
            .generation = 4u, .values = {1, 2}, .count = 2u
        };
        cmeta_range range = cmeta_test_range(&owner);
        cmeta_range_cursor cursor = {0};
        int out = 0;

        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(cursor.index, (size_t)1u);
        check_equal(out, 1);
        check_equal(owner.next_calls, (size_t)1u);
        ++owner.generation;
        out = 99;
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_MUTATED);
        check_equal(cursor.index, (size_t)1u);
        check_equal(out, 99);
        check_equal(owner.next_calls, (size_t)1u);
    }

    it("stores linked traversal state without integer pointer encoding") {
        cmeta_test_link_node second = {2, NULL};
        cmeta_test_link_node first = {1, &second};
        cmeta_test_link_owner owner = {&first};
        cmeta_range range = {
            &owner, &cmeta_type_int, CMETA_RANGE_ORDERED, NULL,
            cmeta_test_link_next, UINT64_C(0), NULL
        };
        cmeta_range_cursor cursor = {0};
        int out = 0;

        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(out, 1);
        check_true(cursor.state[0] == &second);
        check_equal(cmeta_range_next(&range, &cursor, &out),
                    CMETA_GEN_VALUE_AND_DONE);
        check_equal(out, 2);
        check_null(cursor.state[0]);
    }

    it("checks range arguments before reading owner generation") {
        cmeta_test_range_owner owner = {
            .generation = 4u, .values = {1, 2}, .count = 2u
        };
        cmeta_range range = cmeta_test_range(&owner);
        int out = 0;

        check_equal(cmeta_range_next(&range, NULL, &out), CMETA_GEN_ERROR);
        check_equal(owner.version_reads, (size_t)0u);
        check_equal(owner.next_calls, (size_t)0u);
    }

    it("generated ranges capture their version accessor") {
        cmeta_test_generated_range_container container = {
            .raw = {.values = {3, 5}, .count = 2u}, .generation = 9u
        };
        cmeta_range range = cmeta_test_generated_range_container_range(&container);
        cmeta_range_cursor cursor = {0};
        int out = 0;

        check_equal(range.version, UINT64_C(9));
        check_not_null(range.current_version);
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(out, 3);
        ++container.generation;
        out = 77;
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_MUTATED);
        check_equal(cursor.index, (size_t)1u);
        check_equal(out, 77);
    }

    it("generated ranges require explicit version accessors") {
        cmeta_test_required_range_container container = {
            .raw = {.values = {3, 5}, .count = 2u}, .generation = 11u
        };
        cmeta_range range = cmeta_test_required_range_container_range(&container);
        cmeta_range_cursor cursor = {0};
        int out = 0;

        check_equal(range.version, UINT64_C(11));
        check_not_null(range.current_version);
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(out, 3);
        ++container.generation;
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_MUTATED);
    }

    it("rejects a null owner before version or next callbacks") {
        cmeta_range range = {
            NULL, &cmeta_type_int, CMETA_RANGE_NONE, NULL,
            cmeta_test_null_owner_next, UINT64_C(0),
            cmeta_test_required_range_version
        };
        cmeta_range_cursor cursor = {0};
        int out = 0;

        cmeta_test_required_range_version_reads = 0u;
        cmeta_test_null_owner_next_calls = 0u;
        check_equal(cmeta_range_capture_version(
                        cmeta_test_required_range_version, NULL),
                    UINT64_C(0));
        check_equal(cmeta_test_required_range_version_reads, (size_t)0u);
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_ERROR);
        check_equal(cmeta_test_required_range_version_reads, (size_t)0u);
        check_equal(cmeta_test_null_owner_next_calls, (size_t)0u);
    }

    it("preserves stateless ranges that accept a null object") {
        cmeta_range range = {
            NULL, &cmeta_type_int, CMETA_RANGE_NONE, NULL,
            cmeta_test_null_owner_next, UINT64_C(0), NULL
        };
        cmeta_range_cursor cursor = {0};
        int out = 0;

        cmeta_test_null_owner_next_calls = 0u;
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(cmeta_test_null_owner_next_calls, (size_t)1u);
    }

    it("generated versioned ranges do not access a null owner") {
        cmeta_range range;
        cmeta_range_cursor cursor = {0};
        int out = 0;

        cmeta_test_required_range_version_reads = 0u;
        range = cmeta_test_required_range_container_range(NULL);
        check_equal(range.version, UINT64_C(0));
        check_not_null(range.current_version);
        check_equal(cmeta_test_required_range_version_reads, (size_t)0u);
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_ERROR);
        check_equal(cmeta_test_required_range_version_reads, (size_t)0u);
    }

    it("detects mutation through generated slot ranges") {
        cmeta_test_slot_range_container container = {
            .raw = { .values = {17, 0}, .occupied = {true, false} },
            .generation = UINT64_C(21)
        };
        cmeta_range range = cmeta_test_slot_range_container_range(&container);
        cmeta_range_cursor cursor = {0};
        int out = 0;

        check_equal(range.version, UINT64_C(21));
        check_not_null(range.current_version);
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_VALUE);
        check_equal(out, 17);
        ++container.generation;
        check_equal(cmeta_range_next(&range, &cursor, &out), CMETA_GEN_MUTATED);
    }

    it("detects mutation through generated associative ranges") {
        cmeta_test_assoc_range_container container = {
            .raw = { .keys = {7, 0}, .values = {70, 0},
                     .occupied = {true, false} },
            .generation = UINT64_C(31)
        };
        cmeta_range range;
        cmeta_test_assoc_range_container_entry entry = {0};
        cmeta_range_cursor cursor;
        int key = 0;
        long value = 0;

        range = cmeta_test_assoc_range_container_keys_range(&container);
        cursor = (cmeta_range_cursor){0};
        check_equal(cmeta_range_next(&range, &cursor, &key), CMETA_GEN_VALUE);
        check_equal(key, 7);
        ++container.generation;
        check_equal(cmeta_range_next(&range, &cursor, &key), CMETA_GEN_MUTATED);

        range = cmeta_test_assoc_range_container_values_range(&container);
        cursor = (cmeta_range_cursor){0};
        check_equal(cmeta_range_next(&range, &cursor, &value), CMETA_GEN_VALUE);
        check_equal(value, 70L);
        ++container.generation;
        check_equal(cmeta_range_next(&range, &cursor, &value), CMETA_GEN_MUTATED);

        range = cmeta_test_assoc_range_container_entries_range(&container);
        cursor = (cmeta_range_cursor){0};
        check_equal(cmeta_range_next(&range, &cursor, &entry), CMETA_GEN_VALUE);
        check_equal(entry.key, 7);
        check_equal(entry.value, 70L);
        ++container.generation;
        check_equal(cmeta_range_next(&range, &cursor, &entry), CMETA_GEN_MUTATED);
    }
}
