#include <cmeta/type_identity.h>

#include <assert.h>
#include <stdio.h>

static const cmeta_type_identity user_a =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity user_b =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity error_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Error");

static const cmeta_generic_desc result_ctor_a =
    CMETA_GENERIC_DESC_INIT("cmeta.Result", "Result", 2, 2, CMETA_GENERIC_VALUE);
static const cmeta_generic_desc result_ctor_b =
    CMETA_GENERIC_DESC_INIT("cmeta.Result", "Result", 2, 2, CMETA_GENERIC_VALUE);

static const cmeta_type_identity *const result_args_a[] = {
    &user_a, &error_id
};
static const cmeta_type_identity *const result_args_b[] = {
    &user_b, &error_id
};
static const cmeta_type_identity *const reversed_args[] = {
    &error_id, &user_a
};

static const cmeta_type_identity result_a =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_a, result_args_a);
static const cmeta_type_identity result_b =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_b, result_args_b);
static const cmeta_type_identity result_reversed =
    CMETA_TYPE_ID_APPLY_INIT(&result_ctor_a, reversed_args);

int main(void) {
    assert(cmeta_type_identity_valid(&user_a));
    assert(cmeta_type_identity_equal(&user_a, &user_b));
    assert(!cmeta_type_identity_equal(&user_a, &error_id));

    assert(cmeta_generic_desc_valid(&result_ctor_a));
    assert(cmeta_generic_accepts_arity(&result_ctor_a, 2u));
    assert(!cmeta_generic_accepts_arity(&result_ctor_a, 1u));
    assert(cmeta_type_identity_valid(&result_a));
    assert(cmeta_type_identity_equal(&result_a, &result_b));
    assert(!cmeta_type_identity_equal(&result_a, &result_reversed));
    assert(cmeta_type_identity_is_application(&result_a));
    assert(cmeta_type_identity_arity(&result_a) == 2u);
    assert(cmeta_type_identity_argument(&result_a, 0u) == &user_a);

    puts("namespace CMeta.TypeIdentityGeneratedC");
    puts("def atomAliasEqual : Bool := true");
    puts("def atomDifferent : Bool := false");
    puts("def resultApplicationEqual : Bool := true");
    puts("def resultArgumentOrderEqual : Bool := false");
    puts("def resultArityAccepted : Bool := true");
    puts("end CMeta.TypeIdentityGeneratedC");
    return 0;
}
