#include "tinymock_admission.h"

static bool tinymock_cmeta_value_kind_supported(
    const cmeta_type_desc *type,
    bool allow_void) {
  if (!type || !cmeta_type_desc_valid(type))
    return false;

  switch (type->kind) {
  case CMETA_T_VOID:
    return allow_void;
  case CMETA_T_BOOL:
  case CMETA_T_INTEGER:
  case CMETA_T_FLOAT:
  case CMETA_T_POINTER:
    return true;
  case CMETA_T_OBJECT:
  default:
    return false;
  }
}

tinymock_cmeta_admission tinymock_cmeta_function_admit(
    const cmeta_function_desc *function) {
  tinymock_cmeta_admission result = {
      TINYMOCk_CMETA_ADMISSION_OK, (size_t)-1};
  size_t index;

  if (!function || !cmeta_function_desc_valid(function)) {
    result.code = TINYMOCk_CMETA_ADMISSION_INVALID_FUNCTION;
    return result;
  }

  if (!tinymock_cmeta_value_kind_supported(function->return_type, true)) {
    result.code = TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_RETURN_BY_VALUE;
    return result;
  }

  for (index = 0u; index < function->param_count; ++index) {
    const cmeta_param_desc *param = &function->params[index];
    if (!tinymock_cmeta_value_kind_supported(param->type, false)) {
      result.code =
          TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_PARAMETER_BY_VALUE;
      result.param_index = index;
      return result;
    }
  }

  return result;
}

const char *tinymock_cmeta_admission_message(
    tinymock_cmeta_admission_code code) {
  switch (code) {
  case TINYMOCk_CMETA_ADMISSION_OK:
    return "supported";
  case TINYMOCk_CMETA_ADMISSION_INVALID_FUNCTION:
    return "invalid reflected function metadata";
  case TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_RETURN_BY_VALUE:
    return "by-value reflected return type is unsupported";
  case TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_PARAMETER_BY_VALUE:
    return "by-value reflected parameter type is unsupported";
  default:
    return "unknown TinyMock reflected ABI admission result";
  }
}
