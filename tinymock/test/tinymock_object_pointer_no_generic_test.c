#include "tinytest.h"

#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include "tinymock_function.h"
#include "tinymock_cmeta.h"

typedef struct tinymock_pointer_probe {
  int value;
} tinymock_pointer_probe;

typedef tinymock_pointer_probe *tinymock_pointer_handle;

static const cmeta_type_desc tinymock_pointer_probe_type = {
  .name = "tinymock_pointer_probe",
  .size = sizeof(tinymock_pointer_probe),
  .align = _Alignof(tinymock_pointer_probe),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

static const cmeta_type_desc tinymock_pointer_handle_type = {
  .name = "tinymock_pointer_handle",
  .size = sizeof(tinymock_pointer_handle),
  .align = _Alignof(tinymock_pointer_handle),
  .kind = CMETA_T_POINTER,
  .pointee = &tinymock_pointer_probe_type,
  .traits = NULL,
  .identity = NULL
};

/*
 * Reflected object-pointer lowering must remain valid even when the legacy
 * generic boxing/unboxing entry points are unavailable.
 */
#undef TINYMOCk_VALUE
#define TINYMOCk_VALUE(value) TINYMOCk_REFLECTED_GENERIC_BOXING_FORBIDDEN
#undef TINYMOCk_VALUE_AS
#define TINYMOCk_VALUE_AS(type, value) \
  TINYMOCk_REFLECTED_GENERIC_UNBOXING_FORBIDDEN

FunctionDeclAsAbiResult(value, tinymock_pointer_handle,
                  &tinymock_pointer_handle_type,
                  CMETA_ABI_OBJECT_POINTER, CMETA_RESULT_BORROWED,
                  tinymock_pointer_roundtrip,
    (tinymock_pointer_handle, input, CMETA_PARAM_IN,
     &tinymock_pointer_handle_type, CMETA_ABI_OBJECT_POINTER));

#define TINYMOCK_POINTER_INTERFACE_METHODS(X, I) \
  X(I,FR1,tinymock_pointer_handle,echo,value, \
    &tinymock_pointer_handle_type,CMETA_ABI_OBJECT_POINTER,CMETA_RESULT_BORROWED, \
    (tinymock_pointer_handle,input,CMETA_PARAM_IN, \
     &tinymock_pointer_handle_type,CMETA_ABI_OBJECT_POINTER))

CMETA_INTERFACE(tinymock_pointer_interface,
                TINYMOCK_POINTER_INTERFACE_METHODS);
TINYMOCk_INTERFACE(tinymock_pointer_interface,
                   TINYMOCK_POINTER_INTERFACE_METHODS);

suite("TinyMock reflected object-pointer carrier") {
  it("uses typed free-function object pointers without generic dispatch") {
    tinymock_pointer_probe input_object = {7};
    tinymock_pointer_probe returned_object = {11};
    tinymock_pointer_handle input = &input_object;
    tinymock_pointer_handle returned = &returned_object;

    TINYMOCk_FUNCTION_RESET(tinymock_pointer_roundtrip);
    check_true(TINYMOCk_FUNCTION_SET_RETURN(
        tinymock_pointer_roundtrip, returned));

    check_true(tinymock_pointer_roundtrip(input) == returned);

    check_true(TINYMOCk_FUNCTION_ARG_POINTER_EQUAL(
        tinymock_pointer_roundtrip, 0u, "input", input));

    TINYMOCk_FUNCTION_DESTROY(tinymock_pointer_roundtrip);
  }

  it("uses typed reflected interface object pointers without generic dispatch") {
    tinymock_pointer_probe input_object = {13};
    tinymock_pointer_probe returned_object = {17};
    tinymock_pointer_handle input = &input_object;
    tinymock_pointer_handle returned = &returned_object;
    tinymock_tinymock_pointer_interface mock;
    tinymock_pointer_interface iface;

    tinymock_tinymock_pointer_interface_init(&mock);
    iface = tinymock_tinymock_pointer_interface_as_interface(&mock);

    check_true(TINYMOCk_INTERFACE_SET_RETURN(
        &mock, echo, returned));

    check_true(tinymock_pointer_interface_echo(&iface, input) == returned);

    check_true(tinymock_cmeta_history_arg_pointer_equal_name(
        TINYMOCk_INTERFACE_METHOD_HISTORY(&mock, echo),
        0u, "input", (const void *)input));

    tinymock_tinymock_pointer_interface_destroy(&mock);
  }
}
