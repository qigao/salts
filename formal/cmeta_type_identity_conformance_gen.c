#include <cmeta/type_identity.h>

#include <assert.h>
#include <stdio.h>

static const cmeta_type_identity user_a =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity user_b =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity error_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Error");

int main(void) {
    assert(cmeta_type_identity_valid(&user_a));
    assert(cmeta_type_identity_equal(&user_a, &user_b));
    assert(!cmeta_type_identity_equal(&user_a, &error_id));

    puts("namespace CMeta.TypeIdentityGeneratedC");
    puts("def atomAliasEqual : Bool := true");
    puts("def atomDifferent : Bool := false");
    puts("end CMeta.TypeIdentityGeneratedC");
    return 0;
}
