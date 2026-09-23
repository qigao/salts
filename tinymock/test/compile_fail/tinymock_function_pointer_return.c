#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef int (*tinymock_bad_callback_return)(int);

static const cmeta_type_desc tinymock_bad_callback_return_type = {
  .name = "tinymock_bad_callback_return",
  .size = sizeof(tinymock_bad_callback_return),
  .align = _Alignof(tinymock_bad_callback_return),
  .kind = CMETA_T_OBJECT,
  .pointee = NULL,
  .traits = NULL,
  .identity = NULL
};

Function0DeclAsAbi(value, tinymock_bad_callback_return,
                   &tinymock_bad_callback_return_type,
                   CMETA_ABI_FUNCTION_POINTER,
                   tinymock_bad_function_pointer_return);
