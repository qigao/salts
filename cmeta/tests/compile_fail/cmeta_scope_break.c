#include "cmeta_scope_fixture.h"

int main(void) {
    cmeta_status status;
    cmeta_scope_checked(status, cmeta_autos((ScopeValue, value)),
        cmeta_body(break;));
    return status;
}
