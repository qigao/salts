#define TINYTEST_NO_MAIN
#define TINYMOCK_GENERATE_FUNCTION_OVERRIDES 1
#include <tinymock_function.h>

typedef void tinymock_void_alias;

Function0DeclAsAbi(value, tinymock_void_alias, &cmeta_type_void,
                   CMETA_ABI_VOID, tinymock_bad_typedef_void);
