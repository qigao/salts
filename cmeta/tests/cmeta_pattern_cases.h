#ifndef CMETA_PATTERN_CASES_H
#define CMETA_PATTERN_CASES_H

#include "cmeta_pattern_fixture.h"

suite("CMeta pattern composition") {
    it("uses Interface directly as the Strategy dispatch primitive") {
        int value = 7;
        pattern_counter counter = pattern_counter_impl_as_pattern_counter(&value);

        check_true(pattern_counter_valid(&counter));
        check_equal(pattern_counter_get(&counter), 7);
        check_equal(pattern_counter_implementation(&counter), "pattern_counter_impl");
    }

    it("uses canonical Function metadata for Factory result ownership") {
        const cmeta_function_desc *function = FunctionMeta(pattern_owned_factory);
        const cmeta_function_abi_desc *abi = FunctionAbi(pattern_owned_factory);

        check_true(cmeta_function_desc_valid(function));
        check_true(cmeta_function_abi_desc_valid(abi));
        check_true((function->result_flags & CMETA_RESULT_OWNED) != 0u);
        check_equal(function->result_flags & CMETA_RESULT_CLASS_MASK,
                    CMETA_RESULT_OWNED);
        check_equal(abi->return_carrier, CMETA_ABI_OBJECT_POINTER);
    }

    it("projects multiple Extension Interfaces over one native identity") {
        int value = 9;
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        pattern_counter counter;
        pattern_reset reset;

        check_equal(cmeta_object_borrow(
            &object, &value, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(pattern_counter_borrow_from_object(
            &object, &pattern_extension_provider, &counter), CMETA_OK);
        check_equal(pattern_reset_borrow_from_object(
            &object, &pattern_extension_provider, &reset), CMETA_OK);

        check_true(counter.self == object.object);
        check_true(reset.self == object.object);
        check_true(counter.self == reset.self);
        check_equal(pattern_counter_get(&counter), 9);

        pattern_reset_reset(&reset);
        check_equal(value, 0);
        check_equal(pattern_counter_get(&counter), 0);

        cmeta_object_release(&object);
    }

    it("rejects a borrowed Extension Interface with owning self authority") {
        int value = 1;
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        pattern_owning owning = pattern_owning_bind(NULL, NULL);

        check_equal(cmeta_object_borrow(
            &object, &value, &cmeta_data_int, NULL), CMETA_OK);
        check_true(cmeta_interface_desc_has_owning_method(
            pattern_owning_interface()));
        check_equal(pattern_owning_borrow_from_object(
            &object, &pattern_extension_provider, &owning),
            CMETA_TRAIT_MISSING);
        check_false(pattern_owning_valid(&owning));

        cmeta_object_release(&object);
    }
}

#endif /* CMETA_PATTERN_CASES_H */
