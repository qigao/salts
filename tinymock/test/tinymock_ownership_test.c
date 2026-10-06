#ifdef __cplusplus
#include "tinytest.hpp"
#else
#include "tinytest.h"
#endif
#include "tinymock_return.h"

static size_t ownership_retains, ownership_releases, ownership_destroys;
static bool ownership_retain_fails;
static cmeta_status ownership_retain(void *context, void *object) {
    (void)context; (void)object;
    if (ownership_retain_fails) return CMETA_CALLBACK_ERROR;
    ++ownership_retains;
    return CMETA_OK;
}
static void ownership_release(void *context, void *object) {
    (void)context; (void)object; ++ownership_releases;
}
static void ownership_destroy(void *context, void *object) {
    (void)context; (void)object; ++ownership_destroys;
}
static const cmeta_object_lifecycle ownership_ops = {
    sizeof(cmeta_object_lifecycle), NULL, ownership_retain, ownership_release, ownership_destroy
};
static const cmeta_function_desc owned_result = {
    sizeof(cmeta_function_desc), "owned_result", &cmeta_type_int_ptr, NULL, 0,
    CMETA_EFFECT_STATEFUL, CMETA_PROP_NONE, CMETA_RESULT_OWNED
};
static const cmeta_function_abi_desc owned_abi = {
    sizeof(cmeta_function_abi_desc), &owned_result, CMETA_ABI_OBJECT_POINTER, NULL, 0
};

typedef int MockDataValue;
static size_t data_restores, data_live_releases;
static cmeta_status mock_data_init(MockDataValue *value) { *value = 0; return CMETA_OK; }
static void mock_data_restore(MockDataValue *value) {
    ++data_restores;
    if (*value) ++data_live_releases;
    *value = 0;
}
static void mock_data_move(MockDataValue *destination, MockDataValue *source) {
    *destination = *source; *source = 0;
}
CMETA_DEFINE_LIFECYCLE(MockDataValue, &cmeta_type_int, mock_data_init, mock_data_restore,
    mock_data_move, CMETA_LIFECYCLE_INIT_NOFAIL | CMETA_LIFECYCLE_MOVABLE)
#ifdef __cplusplus
static void destroy_return(void *authority, void *resource) {
    (void)authority;
    tinymock_cmeta_return_destroy((tinymock_cmeta_return *)resource);
}
#endif

suite("TinyMock canonical result ownership") {
    before_each() {
        ownership_retains = ownership_releases = ownership_destroys = 0;
        ownership_retain_fails = false;
        data_restores = data_live_releases = 0;
    }
    it("rejects an oversized reflected value before reading or allocating its payload") {
        cmeta_type_desc type = cmeta_type_int;
        tinymock_cmeta_value slot;
        int source = 17;
        type.size = TINYMOCk_MAX_VALUE_BYTES + sizeof(int);
        tinymock_cmeta_value_init(&slot);
        check_false(tinymock_cmeta_value_copy(&slot, &type, &source));
        check_null(slot.allocation);
        check_false(slot.constructed);
        tinymock_cmeta_value_reset(&slot);
    }
    it("transfers one OWNED result and rejects a second transfer") {
        tinymock_cmeta_return state;
        int value = 17, *pointer = &value, *output = NULL;
        tinymock_cmeta_arg_view view = {&pointer, true, pointer};
        cmeta_object_ref owner = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&owner, pointer, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_take(&owner, &ownership_ops), CMETA_OK);
        tinymock_cmeta_return_init(&state, &owned_result);
        check_false(tinymock_cmeta_return_set(&state, &owned_result, &pointer));
        check_true(tinymock_cmeta_return_set_object(&state, &owned_abi, &view, &owner));
        check_equal(owner.lifetime, CMETA_OBJECT_LIFETIME_NONE);
        check_true(tinymock_cmeta_return_write_admitted(&state, &output));
        check_true(output == pointer);
        check_false(tinymock_cmeta_return_write_admitted(&state, &output));
        tinymock_cmeta_return_destroy(&state);
        check_equal(ownership_destroys, (size_t)0);
        check_equal(cmeta_object_borrow(&owner, output, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_take(&owner, &ownership_ops), CMETA_OK);
        cmeta_object_release(&owner);
        check_equal(ownership_destroys, (size_t)1);
    }
    it("destroys an unconsumed owned script once and preserves source on rejection") {
        tinymock_cmeta_return state;
        int value = 17, other = 3, *pointer = &value;
        tinymock_cmeta_arg_view view = {&pointer, true, &other};
        cmeta_object_ref owner = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&owner, pointer, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_take(&owner, &ownership_ops), CMETA_OK);
        tinymock_cmeta_return_init(&state, &owned_result);
        check_false(tinymock_cmeta_return_set_object(&state, &owned_abi, &view, &owner));
        check_equal(owner.lifetime, CMETA_OBJECT_LIFETIME_OWNED);
        view.object_pointer_identity = pointer;
        check_true(tinymock_cmeta_return_set_object(&state, &owned_abi, &view, &owner));
        tinymock_cmeta_return_clear(&state);
        tinymock_cmeta_return_destroy(&state);
        check_equal(ownership_destroys, (size_t)1);
    }
    it("retains SHARED results only through canonical authority and preserves output on failure") {
        cmeta_function_desc function = owned_result;
        cmeta_function_abi_desc abi = owned_abi;
        tinymock_cmeta_return state;
        int value = 17, *pointer = &value, *output = NULL;
        tinymock_cmeta_arg_view view = {&pointer, true, pointer};
        cmeta_object_ref owner = CMETA_OBJECT_REF_INIT;
        function.result_flags = CMETA_RESULT_SHARED; abi.function = &function;
        check_equal(cmeta_object_borrow(&owner, pointer, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_share(&owner, &ownership_ops), CMETA_OK);
        tinymock_cmeta_return_init(&state, &function);
        ownership_retain_fails = true;
        check_false(tinymock_cmeta_return_set_object(&state, &abi, &view, &owner));
        check_equal(owner.lifetime, CMETA_OBJECT_LIFETIME_SHARED);
        ownership_retain_fails = false;
        check_true(tinymock_cmeta_return_set_object(&state, &abi, &view, &owner));
        check_equal(ownership_retains, (size_t)2);
        ownership_retain_fails = true;
        check_false(tinymock_cmeta_return_write_admitted(&state, &output));
        check_null(output);
        ownership_retain_fails = false;
        check_true(tinymock_cmeta_return_write_admitted(&state, &output));
        check_true(output == pointer);
        check_equal(ownership_retains, (size_t)3);
        /* write transferred precisely one retained reference to the caller. */
        cmeta_object_ref returned = owner;
        cmeta_object_release(&returned);
        tinymock_cmeta_return_destroy(&state);
        cmeta_object_release(&owner);
        check_equal(ownership_releases, ownership_retains);
    }
    it("fails closed for UNKNOWN and borrows without extending pointee lifetime") {
        cmeta_function_desc function = owned_result;
        tinymock_cmeta_return state;
        int value = 17, *pointer = &value, *output = NULL;
        function.result_flags = CMETA_RESULT_UNKNOWN;
        tinymock_cmeta_return_init(&state, &function);
        check_false(tinymock_cmeta_return_set(&state, &function, &pointer));
        tinymock_cmeta_return_destroy(&state);
        function.result_flags = CMETA_RESULT_BORROWED;
        tinymock_cmeta_return_init(&state, &function);
        check_true(tinymock_cmeta_return_set(&state, &function, &pointer));
        check_true(tinymock_cmeta_return_write_admitted(&state, &output));
        check_true(output == pointer);
        tinymock_cmeta_return_destroy(&state);
        check_equal(ownership_retains + ownership_releases + ownership_destroys, (size_t)0);
    }
    it("moves an admitted Data value through a one-shot script and restores each zero once") {
        cmeta_data_desc data = cmeta_data_int;
        cmeta_function_desc function = owned_result;
        cmeta_lifecycle_binding binding = CMETA_LIFECYCLE_BINDING_INIT;
        tinymock_cmeta_return state;
        MockDataValue source = 17, output = 0;
        data.struct_size = sizeof(data); data.construct_ops = &MockDataValue_construct_ops;
        function.return_type = &cmeta_type_int; function.result_flags = CMETA_RESULT_VALUE;
        check_equal(cmeta_lifecycle_admit(&data, sizeof(source), CMETA_ALIGNOF(int), &binding), CMETA_OK);
        tinymock_cmeta_return_init(&state, &function);
        check_true(tinymock_cmeta_return_take_data(&state, &function, &binding, &source));
        check_equal(source, 0);
        check_true(tinymock_cmeta_return_write_admitted(&state, &output));
        check_equal(output, 17);
        check_false(tinymock_cmeta_return_write_admitted(&state, &output));
        check_equal(data_restores, (size_t)1);
        check_equal(data_live_releases, (size_t)0);
        tinymock_cmeta_return_destroy(&state);
        check_equal(cmeta_lifecycle_restore(&binding, &source), CMETA_OK);
        check_equal(cmeta_lifecycle_restore(&binding, &output), CMETA_OK);
        check_equal(data_restores, (size_t)3);
        check_equal(data_live_releases, (size_t)1);
    }
#ifdef __cplusplus
    it("discharges an unconsumed owned result exactly once during a C++ exception") {
        tinymock_cmeta_return state;
        cmeta_cleanup obligation = CMETA_CLEANUP_INIT;
        int value = 17, *pointer = &value;
        tinymock_cmeta_arg_view view = {&pointer, true, pointer};
        cmeta_object_ref owner = CMETA_OBJECT_REF_INIT;
        check_equal(cmeta_object_borrow(&owner, pointer, &cmeta_data_int, NULL), CMETA_OK);
        check_equal(cmeta_object_take(&owner, &ownership_ops), CMETA_OK);
        tinymock_cmeta_return_init(&state, &owned_result);
        check_true(tinymock_cmeta_return_set_object(&state, &owned_abi, &view, &owner));
        check_equal(cmeta_cleanup_arm(&obligation, destroy_return, NULL, &state), CMETA_OK);
        try { cmeta::cleanup_scope guard(obligation); throw 1; } catch (int) {}
        cmeta_cleanup_run(&obligation);
        check_equal(ownership_destroys, (size_t)1);
    }
#endif
}
