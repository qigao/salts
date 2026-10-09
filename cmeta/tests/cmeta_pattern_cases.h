#ifndef CMETA_PATTERN_CASES_H
#define CMETA_PATTERN_CASES_H

#include "cmeta_pattern_fixture.h"
#include <cmeta/object_scope.h>
#include <cmeta/ace_interceptor.h>

/* The explicit result/cleanup contract is shared by C11 and C++17.
 * This ObjectRef does NOT retain a provider DSO/Plugin lease on its own. */
static void pattern_destroy_owned_value(void *context, void *object) {
#ifdef __cplusplus
    unsigned *count = static_cast<unsigned *>(context);
#else
    unsigned *count = (unsigned *)context;
#endif
    (void)object;
    ++*count;
}


/* Exact C11/C++17 native context cast; keep strict C++17 -Werror=old-style-cast. */
#ifdef __cplusplus
#define PATTERN_INTERCEPT_CONTEXT(type_, ptr_) static_cast<type_ *>(ptr_)
#else
#define PATTERN_INTERCEPT_CONTEXT(type_, ptr_) ((type_ *)(ptr_))
#endif

/* Typed ACE Interceptor: deliberately no erased call ABI or hidden retain. */
CMETA_INTERCEPTOR_TYPE(pattern_interceptor, int, int);

typedef struct pattern_interceptor_probe {
    int trace[16];
    int count;
    int target_calls;
    bool reject;
    bool fail_target;
} pattern_interceptor_probe;

typedef struct pattern_interceptor_stage {
    pattern_interceptor_probe *probe;
    int id;
} pattern_interceptor_stage;

static cmeta_status pattern_intercept_target(
    void *user, const int *request, int *out) {
    pattern_interceptor_probe *probe = PATTERN_INTERCEPT_CONTEXT(pattern_interceptor_probe, user);
    probe->trace[probe->count++] = 9;
    ++probe->target_calls;
    if (probe->fail_target) return CMETA_CALLBACK_ERROR;
    *out = *request * 2;
    return CMETA_OK;
}
static cmeta_status pattern_intercept_before(
    void *user, const int *request, bool *proceed) {
    pattern_interceptor_stage *stage = PATTERN_INTERCEPT_CONTEXT(pattern_interceptor_stage, user);
    (void)request;
    stage->probe->trace[stage->probe->count++] = stage->id;
    *proceed = !stage->probe->reject;
    return CMETA_OK;
}
static void pattern_intercept_after(
    void *user, const int *request, const int *response) {
    pattern_interceptor_stage *stage = PATTERN_INTERCEPT_CONTEXT(pattern_interceptor_stage, user);
    (void)request; (void)response;
    stage->probe->trace[stage->probe->count++] = stage->id + 10;
}
static void pattern_intercept_error(
    void *user, const int *request, cmeta_status status) {
    pattern_interceptor_stage *stage = PATTERN_INTERCEPT_CONTEXT(pattern_interceptor_stage, user);
    (void)request; (void)status;
    stage->probe->trace[stage->probe->count++] = stage->id + 20;
}


/* Canonical FunctionDesc + FunctionAbi for this exact native target.
 * cmeta_status is an enum: never alias its reflection to an int type. */
static const cmeta_type_desc pattern_intercept_status_type = {
    "cmeta_status", sizeof(cmeta_status), CMETA_ALIGNOF(cmeta_status),
    CMETA_T_INTEGER, NULL, NULL, NULL
};
CMETA_FUNCTION_METADATA_AS_ABI_RESULT(
    pattern_intercept_target_contract, "pattern.interceptor.target",
    io, &pattern_intercept_status_type, CMETA_ABI_ENUM, CMETA_RESULT_VALUE,
    (void *, context, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &cmeta_type_void_ptr, CMETA_ABI_OBJECT_POINTER),
    (const int *, request, CMETA_PARAM_IN | CMETA_PARAM_BORROWED,
     &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER),
    (int *, response, CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,
     &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));
CMETA_STATIC_ASSERT(
    CMETA_TYPE_MATCHES(&pattern_intercept_target, pattern_interceptor_target_fn),
    "Interceptor target must match the exact canonical native signature");

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

    it("admits exactly one canonical owned-result cleanup obligation") {
        int value = 42;
        unsigned destroys = 0u;
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_cleanup cleanup = CMETA_CLEANUP_INIT;
        cmeta_object_lifecycle lifecycle = {
            sizeof(cmeta_object_lifecycle),
            &destroys,
            NULL,
            NULL,
            pattern_destroy_owned_value
        };

        check_equal(cmeta_object_borrow(
            &object, &value, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_take(&object, &lifecycle), CMETA_OK);

        /* A declared SHARED result cannot silently discharge OWNED storage.
         * Failed admission leaves the reference and obligation unchanged. */
        check_equal(cmeta_cleanup_object_result(
            &cleanup, CMETA_RESULT_SHARED, &object),
            CMETA_TYPE_MISMATCH);
        check_true(cleanup.release == NULL);
        check_true(cmeta_object_ref_valid(&object));
        check_equal(destroys, 0u);

        check_equal(cmeta_cleanup_object_result(
            &cleanup, CMETA_RESULT_OWNED, &object), CMETA_OK);
        check_equal(cmeta_cleanup_object_result(
            &cleanup, CMETA_RESULT_OWNED, &object), CMETA_BUSY);
        cmeta_cleanup_run(&cleanup);
        cmeta_cleanup_run(&cleanup);
        check_equal(destroys, 1u);
        check_false(cmeta_object_ref_valid(&object));
    }

    it("does not invent an owned obligation for a borrowed factory result") {
        int value = 7;
        cmeta_object_ref object = CMETA_OBJECT_REF_INIT;
        cmeta_cleanup cleanup = CMETA_CLEANUP_INIT;

        check_equal(cmeta_object_borrow(
            &object, &value, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_cleanup_object_result(
            &cleanup, CMETA_RESULT_BORROWED, &object), CMETA_OK);
        check_true(cleanup.release == NULL);
        check_true(cmeta_object_ref_valid(&object));
        check_equal(cmeta_cleanup_object_result(
            &cleanup, CMETA_RESULT_OWNED, &object), CMETA_TYPE_MISMATCH);
        cmeta_object_release(&object);
        check_false(cmeta_object_ref_valid(&object));
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


    it("admits only matching native-typed Interceptor FunctionAbi") {
        const cmeta_function_abi_desc *expected =
            &pattern_intercept_target_contract__function_abi_meta;
        cmeta_function_abi_desc provider = *expected;
        cmeta_function_desc renamed = *expected->function;
        cmeta_function_desc wrong_result = renamed;
        cmeta_abi_carrier wrong_carriers[] = {
            CMETA_ABI_OBJECT_POINTER, CMETA_ABI_OBJECT_POINTER, CMETA_ABI_UNSPECIFIED
        };
        pattern_interceptor_probe probe = {0};
        pattern_interceptor chain = {0};
        int request = 3, response = 0;

        check_true(cmeta_function_abi_desc_valid(expected));
        check_equal(expected->param_count, 3u);
        check_equal(expected->return_carrier, CMETA_ABI_ENUM);
        check_true(cmeta_function_abi_contract_compatible(expected, expected));

        renamed.name = "provider renamed this entry";
        provider.function = &renamed;
        check_equal(pattern_interceptor_admit(
            &chain, &probe, pattern_intercept_target, NULL, 0u,
            expected, &provider), CMETA_OK);
        check_equal(pattern_interceptor_invoke(&chain, &request, &response), CMETA_OK);
        check_equal(response, 6);
        check_equal(probe.target_calls, 1);

        check_equal(pattern_interceptor_admit(
            &chain, &probe, NULL, NULL, 0u, expected, &provider),
            CMETA_INVALID_ARGUMENT);
        check_true(chain.target == pattern_intercept_target);

        wrong_result.result_flags = CMETA_RESULT_OWNED;
        provider.function = &wrong_result;
        check_equal(pattern_interceptor_admit(
            &chain, &probe, pattern_intercept_target, NULL, 0u,
            expected, &provider), CMETA_TYPE_MISMATCH);
        check_true(chain.target == pattern_intercept_target);

        provider.function = &renamed;
        provider.param_carriers = wrong_carriers;
        check_equal(pattern_interceptor_admit(
            &chain, &probe, pattern_intercept_target, NULL, 0u,
            expected, &provider), CMETA_TYPE_MISMATCH);
        check_true(chain.target == pattern_intercept_target);

        check_equal(pattern_interceptor_admit(
            &chain, &probe, pattern_intercept_target, NULL, 0u,
            NULL, &provider), CMETA_TYPE_MISMATCH);
        check_true(chain.target == pattern_intercept_target);
    }

    it("invokes typed Interceptor hooks forward and after hooks in reverse") {
        pattern_interceptor_probe probe = {0};
        pattern_interceptor_stage a = {&probe, 1}, b = {&probe, 2};
        pattern_interceptor_hook hooks[2] = {
            {&a, pattern_intercept_before, pattern_intercept_after, pattern_intercept_error},
            {&b, pattern_intercept_before, pattern_intercept_after, pattern_intercept_error}
        };
        pattern_interceptor chain = {&probe, pattern_intercept_target, hooks, 2u};
        int request = 7, response = 0;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response), CMETA_OK);
        check_equal(response, 14);
        check_equal(probe.target_calls, 1);
        check_equal(probe.count, 5);
        check_equal(probe.trace[0], 1);
        check_equal(probe.trace[1], 2);
        check_equal(probe.trace[2], 9);
        check_equal(probe.trace[3], 12);
        check_equal(probe.trace[4], 11);
    }

    it("short-circuits Interceptor without invoking target and unwinds errors") {
        pattern_interceptor_probe probe = {0};
        pattern_interceptor_stage a = {&probe, 1}, b = {&probe, 2};
        pattern_interceptor_hook hooks[2] = {
            {&a, pattern_intercept_before, pattern_intercept_after, pattern_intercept_error},
            {&b, pattern_intercept_before, pattern_intercept_after, pattern_intercept_error}
        };
        pattern_interceptor chain = {&probe, pattern_intercept_target, hooks, 2u};
        int request = 5, response = 71;
        probe.reject = true;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response),
                    CMETA_CALLBACK_ERROR);
        check_equal(probe.target_calls, 0);
        check_equal(response, 71);
        check_equal(probe.count, 2);
        check_equal(probe.trace[0], 1);
        check_equal(probe.trace[1], 21);
        probe.count = 0;
        probe.reject = false;
        probe.fail_target = true;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response),
                    CMETA_CALLBACK_ERROR);
        check_equal(probe.target_calls, 1);
        check_equal(probe.count, 5);
        check_equal(probe.trace[0], 1);
        check_equal(probe.trace[1], 2);
        check_equal(probe.trace[2], 9);
        check_equal(probe.trace[3], 22);
        check_equal(probe.trace[4], 21);
    }

    it("rejects invalid typed Interceptor construction without callbacks") {
        pattern_interceptor_probe probe = {0};
        pattern_interceptor chain = {&probe, pattern_intercept_target, NULL, 1u};
        int request = 1, response = 2;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response),
                    CMETA_INVALID_ARGUMENT);
        chain.hook_count = 17u;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response),
                    CMETA_INVALID_ARGUMENT);
        chain.hook_count = 0u;
        check_equal(pattern_interceptor_invoke(&chain, &request, &response), CMETA_OK);
    }

}

#undef PATTERN_INTERCEPT_CONTEXT

#endif /* CMETA_PATTERN_CASES_H */
