#include <cmeta/cmeta.h>

#include <assert.h>
#include <stdio.h>

static const cmeta_type_identity user_id_a =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity user_id_b =
    CMETA_TYPE_ID_ATOM_INIT("app.User");
static const cmeta_type_identity order_id =
    CMETA_TYPE_ID_ATOM_INIT("app.Order");

static const cmeta_type_desc legacy_a = {
    .name = "Legacy",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = NULL
};
static const cmeta_type_desc legacy_b = {
    .name = "Legacy",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = NULL
};
static const cmeta_type_desc structural_user_a = {
    .name = "UserA",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = &user_id_a
};
static const cmeta_type_desc structural_user_b = {
    .name = "DifferentDisplayName",
    .size = 32u,
    .align = 16u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = &user_id_b
};
static const cmeta_type_desc structural_order_same_layout = {
    .name = "UserA",
    .size = 16u,
    .align = 8u,
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .identity = &order_id
};

static const char *lean_bool(bool value) {
    return value ? "true" : "false";
}

int main(void) {
    const bool legacy_equal = cmeta_type_equal(&legacy_a, &legacy_b);
    const bool structural_equal =
        cmeta_type_equal(&structural_user_a, &structural_user_b);
    const bool different_identity =
        cmeta_type_equal(&structural_user_a, &structural_order_same_layout);
    const bool mixed_equal = cmeta_type_equal(&structural_user_a, &legacy_a);

    assert(legacy_equal);
    assert(structural_equal);
    assert(!different_identity);
    assert(!mixed_equal);
    assert(cmeta_type_identity_of(&structural_user_a) == &user_id_a);
    assert(cmeta_type_identity_of(NULL) == NULL);
    assert(cmeta_type_desc_valid(&legacy_a));
    assert(cmeta_type_desc_valid(&structural_user_a));

    puts("namespace CMeta.DescriptorBridgeGeneratedC");
    printf("def legacyEqual : Bool := %s\n", lean_bool(legacy_equal));
    printf("def structuralEqual : Bool := %s\n", lean_bool(structural_equal));
    printf("def differentIdentityEqual : Bool := %s\n", lean_bool(different_identity));
    printf("def mixedEqual : Bool := %s\n", lean_bool(mixed_equal));
    puts("end CMeta.DescriptorBridgeGeneratedC");
    return 0;
}
