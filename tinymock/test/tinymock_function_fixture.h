#ifndef TINYMOCK_FUNCTION_FIXTURE_H
#define TINYMOCK_FUNCTION_FIXTURE_H

#include <cmeta/function.h>

FunctionDecl(value, int, tinymock_fixture_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, int, tinymock_fixture_answer);

FunctionDecl(value, int, tinymock_fixture_pointer,
    (int *, value, CMETA_PARAM_IN, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_write_size,
    (int, input, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_adjust_int,
    (int *, value, CMETA_PARAM_INOUT, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_unknown_ptr,
    (int *, value, CMETA_PARAM_UNKNOWN, &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, int, tinymock_fixture_nullable_out,
    (size_t *, written, CMETA_PARAM_OUT | CMETA_PARAM_NULLABLE,
     &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(value, void, tinymock_fixture_notify,
    (int, event, CMETA_PARAM_IN),
    (size_t *, written, CMETA_PARAM_OUT, &cmeta_type_size_ptr, CMETA_ABI_OBJECT_POINTER));

Function0Decl(value, void, tinymock_fixture_shutdown);

#endif /* TINYMOCK_FUNCTION_FIXTURE_H */
