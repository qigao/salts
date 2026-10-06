#ifndef CMETA_SCOPE_COMPILE_FAIL_FIXTURE_H
#define CMETA_SCOPE_COMPILE_FAIL_FIXTURE_H

#include <cmeta/scope.h>

typedef int ScopeValue;

static const cmeta_data_desc *ScopeValue_cmeta_data(void) {
    return &cmeta_data_int;
}

#endif
