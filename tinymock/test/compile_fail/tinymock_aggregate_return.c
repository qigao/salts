#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef struct tinymock_bad_result {
  int value;
} tinymock_bad_result;

static const cmeta_type_desc tinymock_bad_result_type = {
  .name = "tinymock_bad_result",
  .size = sizeof(tinymock_bad_result),
  .align = _Alignof(tinymock_bad_result),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

FunctionDeclAsAbi(value, tinymock_bad_result, &tinymock_bad_result_type,
                  CMETA_ABI_AGGREGATE, tinymock_bad_return,
    (int, value, CMETA_PARAM_IN));
