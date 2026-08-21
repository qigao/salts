#include <cmeta/cmeta.h>

#include <stdio.h>
#include <string.h>

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "descriptor bridge check failed: %s\n", #expr); \
            return 1; \
        } \
    } while (0)

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
    const cmeta_type_identity *int_id = cmeta_type_identity_of(&cmeta_type_int);
    const cmeta_type_identity *int_ptr_id = cmeta_type_identity_of(&cmeta_type_int_ptr);
    const cmeta_type_identity *long_ptr_id = cmeta_type_identity_of(&cmeta_type_long_ptr);
    const bool legacy_equal = cmeta_type_equal(&legacy_a, &legacy_b);
    const bool structural_equal =
        cmeta_type_equal(&structural_user_a, &structural_user_b);
    const bool different_identity =
        cmeta_type_equal(&structural_user_a, &structural_order_same_layout);
    const bool mixed_equal = cmeta_type_equal(&structural_user_a, &legacy_a);
    const bool pointer_different =
        cmeta_type_equal(&cmeta_type_int_ptr, &cmeta_type_long_ptr);

    CHECK(legacy_equal);
    CHECK(structural_equal);
    CHECK(!different_identity);
    CHECK(!mixed_equal);
    CHECK(cmeta_type_identity_of(&structural_user_a) == &user_id_a);
    CHECK(cmeta_type_identity_of(NULL) == NULL);
    CHECK(cmeta_type_desc_valid(&legacy_a));
    CHECK(cmeta_type_desc_valid(&structural_user_a));

    CHECK(int_id != NULL);
    CHECK(int_id->form == CMETA_TYPE_ATOM);
    CHECK(strcmp(int_id->stable_atom_id, "cmeta.int") == 0);
    CHECK(int_ptr_id != NULL);
    CHECK(int_ptr_id->form == CMETA_TYPE_POINTER);
    CHECK(cmeta_type_identity_equal(int_ptr_id->base, int_id));
    CHECK(cmeta_type_desc_valid(&cmeta_type_int));
    CHECK(cmeta_type_desc_valid(&cmeta_type_int_ptr));
    CHECK(long_ptr_id != NULL);
    CHECK(!pointer_different);

    puts("namespace CMeta.DescriptorBridgeGeneratedC");
    printf("def legacyEqual : Bool := %s\n", lean_bool(legacy_equal));
    printf("def structuralEqual : Bool := %s\n", lean_bool(structural_equal));
    printf("def differentIdentityEqual : Bool := %s\n", lean_bool(different_identity));
    printf("def mixedEqual : Bool := %s\n", lean_bool(mixed_equal));
    printf("def builtinIntStructural : Bool := %s\n", lean_bool(int_id != NULL));
    printf("def pointerDifferentEqual : Bool := %s\n", lean_bool(pointer_different));
    puts("end CMeta.DescriptorBridgeGeneratedC");
    return 0;
}
