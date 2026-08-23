#include <cmeta/meta.h>
#include "tinytest.h"

#include <stddef.h>

typedef struct cmeta_fake_vec {
    void *storage;
} cmeta_fake_vec;

static const cmeta_type_desc cmeta_fake_vec_storage_type = {
    .name = "cmeta_fake_vec",
    .size = sizeof(cmeta_fake_vec),
    .align = _Alignof(cmeta_fake_vec),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_generic_desc cmeta_fake_vec_generic =
    CMETA_GENERIC_DESC_INIT("test.FakeVec", "FakeVec", 1u, 1u,
                            CMETA_GENERIC_CONTAINER);

#define CMETA_DECLARED_STORAGE_FakeVec cmeta_fake_vec
#define CMETA_DECLARED_STORAGE_DESC_FakeVec (&cmeta_fake_vec_storage_type)
#define CMETA_DECLARED_CONSTRUCTOR_FakeVec (&cmeta_fake_vec_generic)
#define CMETA_DECLARED_CONSTRUCTION_FakeVec NULL

Struct(cmeta_declared_payload,
    (int, id),
    (TYPE(FakeVec, int), values)
);

spec("CMeta declared type metadata") {
  it("lowers TYPE fields to provider storage and preserves concrete arguments") {
    const cmeta_field_desc *id =
        cmeta_struct_find_field(cmeta_declared_payload_meta(), "id");
    const cmeta_field_desc *values =
        cmeta_struct_find_field(cmeta_declared_payload_meta(), "values");
    cmeta_declared_payload payload = {0};

    check_true(id != NULL);
    check_true(id->type == &cmeta_type_int);
    check_null(id->declared_type);

    check_true(values != NULL);
    check_equal(values->offset, offsetof(cmeta_declared_payload, values));
    check_equal(values->size, sizeof(cmeta_fake_vec));
    check_true(values->type == &cmeta_fake_vec_storage_type);
    check_true(values->declared_type != NULL);
    check_true(cmeta_declared_type_valid(values->declared_type));
    check_false(cmeta_declared_type_constructible(values->declared_type));
    check_true(values->declared_type->constructor == &cmeta_fake_vec_generic);
    check_equal(values->declared_type->arity, (size_t)1u);
    check_true(cmeta_declared_type_argument(values->declared_type, 0u) ==
               &cmeta_type_int);
    check_null(cmeta_declared_type_argument(values->declared_type, 1u));
    check_equal(sizeof(payload.values), sizeof(cmeta_fake_vec));
  }

  it("rejects missing and malformed declared arguments") {
    static const cmeta_type_desc *const good_args[] = { &cmeta_type_int };
    static const cmeta_type_desc *const bad_args[] = { NULL };
    cmeta_declared_type valid = {
        &cmeta_fake_vec_storage_type, &cmeta_fake_vec_generic,
        good_args, 1u, NULL};
    cmeta_declared_type wrong_arity = {
        &cmeta_fake_vec_storage_type, &cmeta_fake_vec_generic,
        good_args, 0u, NULL};
    cmeta_declared_type missing_arg = {
        &cmeta_fake_vec_storage_type, &cmeta_fake_vec_generic,
        bad_args, 1u, NULL};

    check_true(cmeta_declared_type_valid(&valid));
    check_false(cmeta_declared_type_valid(&wrong_arity));
    check_false(cmeta_declared_type_valid(&missing_arg));
    check_false(cmeta_declared_type_valid(NULL));
  }
}
