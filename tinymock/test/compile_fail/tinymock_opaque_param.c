#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef struct tinymock_bad_opaque {
  int value;
} tinymock_bad_opaque;

static const cmeta_type_desc tinymock_bad_opaque_type = {
  .name = "tinymock_bad_opaque",
  .size = sizeof(tinymock_bad_opaque),
  .align = _Alignof(tinymock_bad_opaque),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

FunctionDeclAsAbi(value, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  tinymock_bad_opaque_param,
    (tinymock_bad_opaque, input, CMETA_PARAM_IN,
     &tinymock_bad_opaque_type, CMETA_ABI_OPAQUE));
