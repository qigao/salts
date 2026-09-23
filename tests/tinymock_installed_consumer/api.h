#ifndef TINYMOCK_INSTALLED_API_H
#define TINYMOCK_INSTALLED_API_H

#include <cmeta/function.h>

FunctionDecl(value, int, tinymock_installed_add,
    (int, left, CMETA_PARAM_IN),
    (int, right, CMETA_PARAM_IN));

Function0Decl(value, void, tinymock_installed_shutdown);

#endif /* TINYMOCK_INSTALLED_API_H */
