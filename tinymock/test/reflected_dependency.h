#ifndef TINYMOCK_REFLECTED_DEPENDENCY_H
#define TINYMOCK_REFLECTED_DEPENDENCY_H

#include <cmeta/function.h>

FunctionDecl(value, int, tinymock_reflected_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, int, tinymock_reflected_status);

#endif /* TINYMOCK_REFLECTED_DEPENDENCY_H */
