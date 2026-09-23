#ifndef TINYMOCK_ADMISSION_H
#define TINYMOCK_ADMISSION_H

#include <cmeta/function.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum tinymock_cmeta_admission_code {
  TINYMOCk_CMETA_ADMISSION_OK = 0,
  TINYMOCk_CMETA_ADMISSION_INVALID_FUNCTION,
  TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_RETURN_BY_VALUE,
  TINYMOCk_CMETA_ADMISSION_UNSUPPORTED_PARAMETER_BY_VALUE
} tinymock_cmeta_admission_code;

typedef struct tinymock_cmeta_admission {
  tinymock_cmeta_admission_code code;
  size_t param_index;
} tinymock_cmeta_admission;

tinymock_cmeta_admission tinymock_cmeta_function_admit(
    const cmeta_function_desc *function);

const char *tinymock_cmeta_admission_message(
    tinymock_cmeta_admission_code code);

#ifdef __cplusplus
}
#endif

#endif /* TINYMOCK_ADMISSION_H */
