#include "tinytest.h"
#include "tinymock_admission.h"

#include <cmeta/cmeta.h>

typedef struct tinymock_admission_object {
  int value;
} tinymock_admission_object;

static const cmeta_type_desc admission_object_type = {
  .name = "tinymock_admission_object",
  .size = sizeof(tinymock_admission_object),
  .align = _Alignof(tinymock_admission_object),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

static const cmeta_type_desc admission_object_ptr_type = {
  .name = "tinymock_admission_object *",
  .size = sizeof(tinymock_admission_object *),
  .align = _Alignof(tinymock_admission_object *),
  .kind = CMETA_T_POINTER,
  .pointee = &admission_object_type,
  .traits = NULL,
  .identity = NULL
};

static const cmeta_param_desc supported_params[] = {
  {
    sizeof(cmeta_param_desc),
    "value",
    &cmeta_type_int,
    CMETA_PARAM_IN
  },
  {
    sizeof(cmeta_param_desc),
    "object",
    &admission_object_ptr_type,
    CMETA_PARAM_IN
  }
};

static const cmeta_function_desc supported_function = {
  sizeof(cmeta_function_desc),
  "tinymock_admission_supported",
  &cmeta_type_int,
  supported_params,
  2u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

static const cmeta_param_desc object_param[] = {
  {
    sizeof(cmeta_param_desc),
    "object",
    &admission_object_type,
    CMETA_PARAM_IN
  }
};

static const cmeta_function_desc unsupported_param_function = {
  sizeof(cmeta_function_desc),
  "tinymock_admission_bad_param",
  &cmeta_type_int,
  object_param,
  1u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

static const cmeta_function_desc unsupported_return_function = {
  sizeof(cmeta_function_desc),
  "tinymock_admission_bad_return",
  &admission_object_type,
  NULL,
  0u,
  CMETA_EFFECT_PURE,
  CMETA_PROP_NONE
};

suite("TinyMock reflected ABI admission") {
  it("admits scalar and object-pointer carrier shapes") {
    tinymock_cmeta_admission admission =
        tinymock_cmeta_function_admit(&supported_function);

    check_equal(admission.code, TINYMOCk_CMETA_ADMISSION_OK);
  }

  it("classifies a by-value reflected parameter") {
    tinymock_cmeta_admission admission =
        tinymock_cmeta_function_admit(&unsupported_param_function);

    check_equal(
        admission.code,
        TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_PARAMETER_BY_VALUE);
    check_equal(admission.param_index, (size_t)0);
    check_equal(
        tinymock_cmeta_admission_message(admission.code),
        "by-value reflected parameter type is unsupported");
  }

  it("classifies a by-value reflected return") {
    tinymock_cmeta_admission admission =
        tinymock_cmeta_function_admit(&unsupported_return_function);

    check_equal(
        admission.code,
        TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_RETURN_BY_VALUE);
    check_equal(
        tinymock_cmeta_admission_message(admission.code),
        "by-value reflected return type is unsupported");
  }
}
