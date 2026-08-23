#include <cmeta/type_identity.h>
#include "tinytest.h"

static const cmeta_generic_desc cmeta_test_box_generic =
    CMETA_GENERIC_DESC_INIT("test.Box", "Box", 1u, 1u, CMETA_GENERIC_VALUE);
static const cmeta_generic_desc cmeta_test_pair_generic =
    CMETA_GENERIC_DESC_INIT("test.Pair", "Pair", 2u, 2u, CMETA_GENERIC_VALUE);

static const cmeta_type_identity cmeta_test_atom_a =
    CMETA_TYPE_ID_ATOM_INIT("test.A");
static const cmeta_type_identity cmeta_test_atom_b =
    CMETA_TYPE_ID_ATOM_INIT("test.B");
static const cmeta_type_identity cmeta_test_invalid_atom = {
    CMETA_TYPE_ATOM, NULL, NULL, NULL, NULL, 0u
};

static const cmeta_type_identity *const cmeta_test_box_args[] = {
    &cmeta_test_atom_a
};
static const cmeta_type_identity cmeta_test_box_a =
    CMETA_TYPE_ID_APPLY_INIT(&cmeta_test_box_generic, cmeta_test_box_args);
static const cmeta_type_identity *const cmeta_test_nested_pair_args[] = {
    &cmeta_test_box_a, &cmeta_test_atom_b
};
static const cmeta_type_identity cmeta_test_nested_pair =
    CMETA_TYPE_ID_APPLY_INIT(&cmeta_test_pair_generic,
                             cmeta_test_nested_pair_args);

spec("CMeta generic type applications") {
  it("validates TYPE<A...> from constructor arity and recursive arguments") {
    const cmeta_type_identity *box_args[] = {&cmeta_test_atom_a};
    const cmeta_type_identity *pair_args[] = {
        &cmeta_test_atom_a, &cmeta_test_atom_b};
    const cmeta_type_identity *invalid_args[] = {&cmeta_test_invalid_atom};

    check_true(cmeta_type_application_valid(
        &cmeta_test_box_generic, box_args, 1u));
    check_true(cmeta_type_application_valid(
        &cmeta_test_pair_generic, pair_args, 2u));
    check_false(cmeta_type_application_valid(
        &cmeta_test_pair_generic, pair_args, 1u));
    check_false(cmeta_type_application_valid(
        &cmeta_test_pair_generic, NULL, 2u));
    check_false(cmeta_type_application_valid(
        &cmeta_test_box_generic, invalid_args, 1u));
  }

  it("accepts nested generic applications through the same validity rule") {
    check_true(cmeta_type_identity_valid(&cmeta_test_box_a));
    check_true(cmeta_type_identity_valid(&cmeta_test_nested_pair));
    check_true(cmeta_type_identity_constructor(&cmeta_test_nested_pair) ==
               &cmeta_test_pair_generic);
    check_equal(cmeta_type_identity_arity(&cmeta_test_nested_pair), (size_t)2u);
    check_true(cmeta_type_identity_argument(&cmeta_test_nested_pair, 0u) ==
               &cmeta_test_box_a);
    check_true(cmeta_type_identity_argument(&cmeta_test_nested_pair, 1u) ==
               &cmeta_test_atom_b);
    check_null(cmeta_type_identity_argument(&cmeta_test_nested_pair, 2u));
  }
}
