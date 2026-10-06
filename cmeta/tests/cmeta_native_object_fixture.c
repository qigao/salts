#include "cmeta_native_object_fixture.h"

native_object_fixture_state native_object_state;
int native_object_add(void *object, int value) {
    return ((native_object_payload *)object)->bias + value;
}
static const cmeta_receiver_operation native_object_operations[] = {
    {"add", FunctionAbi(native_object_add)}
};
static const cmeta_receiver_operation_set native_object_set = {
    sizeof(cmeta_receiver_operation_set), &native_object_type, native_object_operations, 1, NULL
};
static const cmeta_data_desc *const native_object_arguments[] = {&cmeta_data_int};
static const cmeta_function_data_desc native_object_function_data = {
    sizeof(cmeta_function_data_desc), FunctionMeta(native_object_bound), &cmeta_data_int,
    native_object_arguments, 1
};
static cmeta_status native_object_reference_bind(void *context, void *object,
    const cmeta_receiver_operation *operation, cmeta_object_operation_binding *out) {
    native_object_fixture_state *state = context;
    native_object_bound_capture capture = {object};
    cmeta_invokable reference = CMETA_INVOKABLE_INIT;
    cmeta_status status;
    ++state->reference_binds;
    if (state->reference_status != CMETA_OK) return state->reference_status;
    if (operation != &native_object_operations[0]) return CMETA_INVALID_ARGUMENT;
    status = native_object_bound_bind(&capture, &reference);
    if (status != CMETA_OK) return status;
    out->size = sizeof(*out);
    out->data = &native_object_function_data;
    out->callable = reference.callable;
    return CMETA_OK;
}
static const cmeta_object_operation_provider native_object_reference = {
    sizeof(cmeta_object_operation_provider), &native_object_set, &native_object_state,
    native_object_reference_bind
};
static cmeta_status native_object_exact_bind(void *context, void *object,
    const cmeta_receiver_operation *operation, cmeta_native_binding *out) {
    native_object_fixture_state *state = context;
    cmeta_status status;
    ++state->native_binds;
    if (operation != &native_object_operations[0]) return CMETA_INVALID_ARGUMENT;
    status = cmeta_native_context_i32_admit(operation->abi, FunctionAbi(native_object_bound),
        native_object_add, object, out);
    if (status != CMETA_OK) return status;
    /* Only the test fixture can deliberately return a malformed foreign offer. */
    if (state->override_projection != NULL) out->abi = state->override_projection;
    return state->native_status;
}
const cmeta_native_object_provider native_object_provider = {
    &native_object_reference, native_object_exact_bind
};
static cmeta_status native_object_retain(void *context, void *object) {
    (void)context;
    ++((native_object_payload *)object)->retains;
    return CMETA_OK;
}
static void native_object_release(void *context, void *object) {
    (void)context;
    ++((native_object_payload *)object)->releases;
}
static void native_object_destroy(void *context, void *object) {
    (void)context;
    ++((native_object_payload *)object)->destroys;
}
const cmeta_object_lifecycle native_object_lifecycle = {
    sizeof(cmeta_object_lifecycle), NULL, native_object_retain, native_object_release, native_object_destroy
};
