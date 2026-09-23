#ifndef TINYMOCK_FUNCTION_FIXTURE_H
#define TINYMOCK_FUNCTION_FIXTURE_H

#include <cmeta/function.h>

FunctionDecl(value, int, tinymock_fixture_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, int, tinymock_fixture_answer);

#endif /* TINYMOCK_FUNCTION_FIXTURE_H */
