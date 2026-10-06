#ifndef CMETA_NATIVE_TEST_TARGETS_H
#define CMETA_NATIVE_TEST_TARGETS_H
#include <cmeta/bind.h>
#include <cmeta/native/thunk.h>

enum { NATIVE_TEST_PAGE_BUDGET = 65536, NATIVE_TEST_REDIRECTS = 32 };
FunctionDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_test_identity, (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
FunctionDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_test_increment, (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
static const cmeta_type_desc native_context_type = {
    "native context", sizeof(void *), CMETA_ALIGNOF(void *), CMETA_T_POINTER, &cmeta_type_void, NULL, NULL
};
#define NATIVE_CONTEXT_FLAGS (CMETA_PARAM_IN | CMETA_PARAM_BORROWED | CMETA_PARAM_RECEIVER)
FunctionDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_test_context, (void *, context, NATIVE_CONTEXT_FLAGS, &native_context_type, CMETA_ABI_OBJECT_POINTER),
    (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR));
FunctionBindDeclAsAbiResult(value, int, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,
    native_test_bound, native_test_context, CMETA_SIG_U_I_I,
    (borrow, (void *, context, NATIVE_CONTEXT_FLAGS, &native_context_type, CMETA_ABI_OBJECT_POINTER)),
    (arg, (int, value, CMETA_PARAM_IN, &cmeta_type_int, CMETA_ABI_SCALAR)));
#endif
