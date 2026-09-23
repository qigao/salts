#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef int (*tinymock_bad_callback)(int);

static const cmeta_type_desc tinymock_bad_callback_type = {
  .name = "tinymock_bad_callback",
  .size = sizeof(tinymock_bad_callback),
  .align = _Alignof(tinymock_bad_callback),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

FunctionDeclAsAbi(value, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  tinymock_bad_function_pointer,
    (tinymock_bad_callback, callback, CMETA_PARAM_IN,
     &tinymock_bad_callback_type, CMETA_ABI_FUNCTION_POINTER));
