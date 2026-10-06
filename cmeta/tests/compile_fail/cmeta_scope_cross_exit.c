#include "cmeta_scope_fixture.h"

static cmeta_status rejected_body(ScopeValue *value) {
    cmeta_status status;
    (void)value;
    cmeta_scope_exit(outer, status, CMETA_CALLBACK_ERROR);
    return CMETA_OK;
}

int main(void) {
    cmeta_status status;
    cmeta_scope(outer, status, cmeta_resources((ScopeValue, value)),
        cmeta_body(rejected_body(&value)));
    return status;
}
