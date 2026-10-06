#include "cmeta_scope_fixture.h"

int main(void) {
    cmeta_status status;
    cmeta_scope(rejected, status, cmeta_autos(cmeta_auto(ScopeValue, value)),
        cmeta_body(goto outside;));
outside:
    return status;
}
