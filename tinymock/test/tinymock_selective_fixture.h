#ifndef TINYMOCK_SELECTIVE_FIXTURE_H
#define TINYMOCK_SELECTIVE_FIXTURE_H

#include <cmeta/function.h>

FunctionDeclResult(value, int, CMETA_RESULT_VALUE, tinymock_selective_mocked,
    (int, value, CMETA_PARAM_IN));

FunctionDeclResult(value, int, CMETA_RESULT_VALUE, tinymock_selective_real,
    (int, value, CMETA_PARAM_IN));

Function0Decl(value, void, tinymock_selective_void);

#endif /* TINYMOCK_SELECTIVE_FIXTURE_H */
