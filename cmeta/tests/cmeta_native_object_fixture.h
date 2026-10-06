#ifndef CMETA_NATIVE_OBJECT_FIXTURE_H
#define CMETA_NATIVE_OBJECT_FIXTURE_H
#include <cmeta/bind.h>
#include <cmeta/native/object.h>

typedef struct native_object_payload {
    int bias;
    unsigned retains, releases, destroys;
} native_object_payload;
typedef struct native_object_fixture_state {
    unsigned reference_binds, native_binds;
    cmeta_status reference_status, native_status;
    const cmeta_function_abi_desc *override_projection;
} native_object_fixture_state;
extern native_object_fixture_state native_object_state;
extern const cmeta_native_object_provider native_object_provider;
extern const cmeta_object_lifecycle native_object_lifecycle;

static const cmeta_type_identity native_object_identity = CMETA_TYPE_ID_ATOM_INIT("test.native.object");
static const cmeta_type_desc native_object_type = {
    "native_object_payload", sizeof(native_object_payload), CMETA_ALIGNOF(native_object_payload),
    CMETA_T_OBJECT, NULL, NULL, &native_object_identity
};
static const unsigned char native_object_shape = 0;
static const cmeta_data_desc native_object_data = {
    sizeof(cmeta_data_desc), CMETA_DATA_DESC_ABI_VERSION, "test.native.object.data",
    "native_object_payload", CMETA_DATA_CUSTOM, &native_object_type, &native_object_shape,
    NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL
};
static const cmeta_type_desc native_object_pointer = {
    "native object receiver", sizeof(void *), CMETA_ALIGNOF(void *), CMETA_T_POINTER,
    &native_object_type, NULL, NULL
};
#define NATIVE_OBJECT_RECEIVER (CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER)
FunctionDeclAsAbiResult(stateful, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_object_add,
    (void *, object, NATIVE_OBJECT_RECEIVER, &native_object_pointer, CMETA_ABI_OBJECT_POINTER),
    (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
FunctionBindDeclAsAbiResult(stateful, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_object_bound, native_object_add, CMETA_SIG_U_I_I,
    (borrow, (void *, object, NATIVE_OBJECT_RECEIVER, &native_object_pointer, CMETA_ABI_OBJECT_POINTER)),
    (arg, (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)));
#endif
