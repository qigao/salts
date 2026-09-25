#include "tinytest.h"

#include <cmeta/abi.h>
#include <cmeta/cmeta.h>

typedef enum cmeta_abi_test_mode {
  CMETA_ABI_TEST_IDLE = 0,
  CMETA_ABI_TEST_READY = 1
} cmeta_abi_test_mode;

static const cmeta_type_traits cmeta_abi_test_enum_traits = {
  .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

static const cmeta_type_desc cmeta_abi_test_enum_type = {
  .name = "cmeta_abi_test_mode",
  .size = sizeof(cmeta_abi_test_mode),
  .align = _Alignof(cmeta_abi_test_mode),
  .kind = CMETA_T_INTEGER,
  .pointee = NULL,
  .traits = &cmeta_abi_test_enum_traits,
  .identity = NULL
};

suite("CMeta ABI carriers") {
  it("matches the linked library reflection epoch") {
    check_equal(cmeta_reflection_abi_version(), CMETA_REFLECTION_ABI_VERSION);
  }

  it("exposes explicit enum carrier semantics") {
    check_true(cmeta_abi_carrier_valid(CMETA_ABI_ENUM));
    check_equal(cmeta_abi_carrier_name(CMETA_ABI_ENUM), "enum");
    check_true(cmeta_abi_carrier_matches_type(
        CMETA_ABI_ENUM, &cmeta_abi_test_enum_type));
    check_false(cmeta_abi_carrier_matches_type(
        CMETA_ABI_ENUM, &cmeta_type_double));
  }
}
