#include "cmeta_scope_fixture.h"

static cmeta_status rejected_body(ScopeValue *value) {
    cmeta_status status;
    (void)value;
#ifdef CMETA_TEST_LEAVE
    cmeta_leave(outer, status, CMETA_CALLBACK_ERROR);
#else
    cmeta_scope_exit(outer, status, CMETA_CALLBACK_ERROR);
#endif
    return CMETA_OK;
}

int main(void) {
    cmeta_status status;
    cmeta_scope_checked(status, cmeta_autos((ScopeValue, value)),
        cmeta_body(rejected_body(&value)));
    return status;
}
