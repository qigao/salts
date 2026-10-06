#include "cmeta_scope_fixture.h"

static cmeta_status rejected_body(ScopeValue *value) {
    (void)value;
    goto outer__cmeta_cleanup;
    return CMETA_OK;
}

int main(void) {
    cmeta_status status;
    cmeta_scope(outer, status, cmeta_resources((ScopeValue, value)),
        cmeta_body(rejected_body(&value)));
    return status;
}
