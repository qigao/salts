#include <cmeta/cmeta.h>
#include "tinytest.h"

#include <stddef.h>

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
}
