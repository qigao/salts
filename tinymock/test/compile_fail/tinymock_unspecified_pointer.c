#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

FunctionDecl(value, int, tinymock_bad_unspecified,
    (int *, value, CMETA_PARAM_IN, &cmeta_type_int_ptr));
