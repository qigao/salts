#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef struct tinymock_bad_payload {
  int value;
} tinymock_bad_payload;

static const cmeta_type_desc tinymock_bad_payload_type = {
  .name = "tinymock_bad_payload",
  .size = sizeof(tinymock_bad_payload),
  .align = _Alignof(tinymock_bad_payload),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

FunctionDeclAsAbi(value, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  tinymock_bad_aggregate,
    (tinymock_bad_payload, input, CMETA_PARAM_IN,
     &tinymock_bad_payload_type, CMETA_ABI_AGGREGATE));
