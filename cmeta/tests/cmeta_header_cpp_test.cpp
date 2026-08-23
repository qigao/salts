#include <cmeta/status.h>
#include <cmeta/type_traits.h>
#include <cmeta/cmeta.h>
#include <cmeta/collector.h>
#include <cmeta/meta.h>
#include "tinytest.hpp"

#include <cstddef>
#include <type_traits>

Struct(cmeta_cpp_record,
    (int, value),
    (const char *, name)
);

static_assert(CMETA_ALIGNOF(cmeta_cpp_record) == alignof(cmeta_cpp_record),
              "CMETA_ALIGNOF must use the active language spelling");
static_assert(CMETA_FLOAT_TRAITS_BINARY32_BINARY64,
              "floating traits require the documented binary32/binary64 contract");
static_assert(CMETA_GEN_MUTATED == 5,
              "the C++ public enum surface must expose mutated ranges");
static_assert(CMETA_COLLECTOR_ABORTED == 4,
              "the C++ public enum surface must expose collector states");
static_assert(CMETA_CONTAINER_TYPE_OPS_ABI_VERSION == 1u,
              "container type ops ABI starts at version 1");
static_assert(CMETA_CONTAINER_EXT_ABI_VERSION == 1u,
              "container extension ABI starts at version 1");
static_assert(std::is_standard_layout_v<cmeta_container_type_ops>,
              "container type ops must remain a C-compatible standard-layout type");
static_assert(std::is_standard_layout_v<cmeta_container_ext>,
              "container extensions must remain C-compatible standard-layout types");

static const cmeta_generic_desc cmeta_cpp_pair_generic =
    CMETA_GENERIC_DESC_INIT("cpp.Pair", "Pair", 2u, 2u, CMETA_GENERIC_VALUE);
static const cmeta_type_identity cmeta_cpp_atom_a =
    CMETA_TYPE_ID_ATOM_INIT("cpp.A");
static const cmeta_type_identity cmeta_cpp_atom_b =
    CMETA_TYPE_ID_ATOM_INIT("cpp.B");
static const cmeta_type_identity *const cmeta_cpp_pair_args[] = {
    &cmeta_cpp_atom_a, &cmeta_cpp_atom_b
};
static const cmeta_type_identity cmeta_cpp_pair_identity =
    CMETA_TYPE_ID_APPLY_INIT(&cmeta_cpp_pair_generic, cmeta_cpp_pair_args);

static bool cmeta_cpp_copy_construct(void *destination, const void *source) {
  if (destination == nullptr || source == nullptr) return false;
  *static_cast<int *>(destination) = *static_cast<const int *>(source);
  return true;
}

spec("CMeta C++ public headers") {
  it("reflects struct fields through the C++ public surface") {
    const cmeta_struct_desc *meta = cmeta_cpp_record_meta();

    check_not_null(meta);
    check_equal(meta->name, "cmeta_cpp_record");
    check_equal(meta->field_count, static_cast<size_t>(2));
    check_equal(meta->fields[0].offset, offsetof(cmeta_cpp_record, value));
    check_equal(meta->fields[1].offset, offsetof(cmeta_cpp_record, name));
  }

  it("validates a trait descriptor through the C++ public surface") {
    const cmeta_type_traits traits = {
        CMETA_TRAIT_COPY, nullptr, nullptr, nullptr,
        cmeta_cpp_copy_construct, nullptr, nullptr};
    const cmeta_type_desc type = {
        "cpp trait", sizeof(int), alignof(int), CMETA_T_OBJECT, nullptr, &traits};
    int source = 7;
    int destination = 0;

    check_true(traits.copy_construct(&destination, &source));
    check_equal(destination, 7);
    check_equal(cmeta_type_require_traits(&type, CMETA_TRAIT_COPY), CMETA_OK);
  }

  it("validates generic type applications through the C++ public surface") {
    check_true(cmeta_type_application_valid(
        &cmeta_cpp_pair_generic, cmeta_cpp_pair_args, 2u));
    check_true(cmeta_type_identity_valid(&cmeta_cpp_pair_identity));
    check_equal(cmeta_type_identity_arity(&cmeta_cpp_pair_identity),
                static_cast<size_t>(2));
    check_true(cmeta_type_identity_argument(&cmeta_cpp_pair_identity, 0u) ==
               &cmeta_cpp_atom_a);
  }

  it("exposes versioned container extension structs through C++") {
    const cmeta_container_type_ops type_ops = {
        sizeof(cmeta_container_type_ops),
        CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,
        nullptr,
        0u,
        nullptr};
    const cmeta_container_ext ext = {
        sizeof(cmeta_container_ext),
        CMETA_CONTAINER_EXT_ABI_VERSION,
        &type_ops};

    check_equal(type_ops.struct_size, sizeof(cmeta_container_type_ops));
    check_equal(ext.struct_size, sizeof(cmeta_container_ext));
    check_true(ext.type == &type_ops);
  }
}
