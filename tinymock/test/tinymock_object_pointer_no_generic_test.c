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

FunctionDeclAsAbi(value, tinymock_pointer_handle,
                  &tinymock_pointer_handle_type,
                  CMETA_ABI_OBJECT_POINTER,
                  tinymock_pointer_roundtrip,
    (tinymock_pointer_handle, input, CMETA_PARAM_IN,
     &tinymock_pointer_handle_type, CMETA_ABI_OBJECT_POINTER));

#define TINYMOCK_POINTER_INTERFACE_METHODS(X, I) \
  X(I,F1,tinymock_pointer_handle,echo,value, \
    &tinymock_pointer_handle_type,CMETA_ABI_OBJECT_POINTER, \
    (tinymock_pointer_handle,input,CMETA_PARAM_IN, \
     &tinymock_pointer_handle_type,CMETA_ABI_OBJECT_POINTER))

CMETA_INTERFACE(tinymock_pointer_interface,
                TINYMOCK_POINTER_INTERFACE_METHODS);
TINYMOCk_INTERFACE(tinymock_pointer_interface,
                   TINYMOCK_POINTER_INTERFACE_METHODS);

suite("TinyMock reflected object-pointer carrier") {
  it("boxes and unboxes free-function object pointers without generic dispatch") {
    tinymock_pointer_probe input_object = {7};
    tinymock_pointer_probe returned_object = {11};
    tinymock_pointer_handle input = &input_object;
    tinymock_pointer_handle returned = &returned_object;
    const tinymock_recorded_call_t *call;

    TINYMOCk_FUNCTION_RESET(tinymock_pointer_roundtrip);
    tinymock_mock_set_default_return(
        TINYMOCk_FUNCTION(tinymock_pointer_roundtrip),
        tinymock_detail_box_ptr((const void *)returned));

    check_true(tinymock_pointer_roundtrip(input) == returned);

    call = tinymock_mock_call_at(
        TINYMOCk_FUNCTION(tinymock_pointer_roundtrip), 0u);
    check_not_null(call);
    check_equal(call->argc, (size_t)1);
    check_equal(call->args[0].kind,
                (tinymock_value_kind_t)TINYMOCk_VALUE_POINTER);
    check_true(call->args[0].as.pointer_value == (const void *)input);

    TINYMOCk_FUNCTION_DESTROY(tinymock_pointer_roundtrip);
  }

  it("boxes and unboxes reflected interface object pointers without generic dispatch") {
    tinymock_pointer_probe input_object = {13};
    tinymock_pointer_probe returned_object = {17};
    tinymock_pointer_handle input = &input_object;
    tinymock_pointer_handle returned = &returned_object;
    tinymock_tinymock_pointer_interface mock;
    tinymock_pointer_interface iface;
    const tinymock_recorded_call_t *call;

    tinymock_tinymock_pointer_interface_init(&mock);
    iface = tinymock_tinymock_pointer_interface_as_interface(&mock);

    tinymock_mock_set_default_return(
        TINYMOCk_INTERFACE_METHOD(&mock, echo),
        tinymock_detail_box_ptr((const void *)returned));

    check_true(tinymock_pointer_interface_echo(&iface, input) == returned);

    call = tinymock_mock_call_at(
        TINYMOCk_INTERFACE_METHOD(&mock, echo), 0u);
    check_not_null(call);
    check_equal(call->argc, (size_t)1);
    check_equal(call->args[0].kind,
                (tinymock_value_kind_t)TINYMOCk_VALUE_POINTER);
    check_true(call->args[0].as.pointer_value == (const void *)input);

    tinymock_tinymock_pointer_interface_destroy(&mock);
  }
}
