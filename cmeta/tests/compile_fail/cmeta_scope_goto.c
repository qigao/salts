#include "cmeta_scope_fixture.h"

int main(void) {
    cmeta_status status;
    cmeta_scope(status, cmeta_autos((ScopeValue, value)),
        cmeta_body(goto outside;));
outside:
    return status;
}
