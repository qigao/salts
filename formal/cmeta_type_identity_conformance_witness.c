#include <cmeta/type_identity.h>

#include <assert.h>
#include <stdbool.h>
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

static const char *lean_bool(bool value) {
    return value ? "true" : "false";
}

int main(void) {
    const bool atom_alias_equal = cmeta_type_identity_equal(&user_a, &user_b);
    const bool atom_different = cmeta_type_identity_equal(&user_a, &error_id);
    const bool result_application_equal =
        cmeta_type_identity_equal(&result_a, &result_b);
    const bool result_argument_order_equal =
        cmeta_type_identity_equal(&result_a, &result_reversed);
    const bool result_arity_accepted =
        cmeta_generic_accepts_arity(&result_ctor_a, 2u);

    assert(cmeta_type_identity_valid(&user_a));
    assert(atom_alias_equal);
    assert(!atom_different);

    assert(cmeta_generic_desc_valid(&result_ctor_a));
    assert(result_arity_accepted);
    assert(!cmeta_generic_accepts_arity(&result_ctor_a, 1u));
    assert(cmeta_type_identity_valid(&result_a));
    assert(result_application_equal);
    assert(!result_argument_order_equal);
    assert(cmeta_type_identity_is_application(&result_a));
    assert(cmeta_type_identity_arity(&result_a) == 2u);
    assert(cmeta_type_identity_argument(&result_a, 0u) == &user_a);

    puts("namespace CMeta.TypeIdentityGeneratedC");
    printf("def atomAliasEqual : Bool := %s\n", lean_bool(atom_alias_equal));
    printf("def atomDifferent : Bool := %s\n", lean_bool(atom_different));
    printf("def resultApplicationEqual : Bool := %s\n",
           lean_bool(result_application_equal));
    printf("def resultArgumentOrderEqual : Bool := %s\n",
           lean_bool(result_argument_order_equal));
    printf("def resultArityAccepted : Bool := %s\n",
           lean_bool(result_arity_accepted));
    puts("end CMeta.TypeIdentityGeneratedC");
    return 0;
}
