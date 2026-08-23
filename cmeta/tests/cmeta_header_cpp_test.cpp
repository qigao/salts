#include <cmeta/status.h>
#include <cmeta/type_traits.h>
#include <cmeta/cmeta.h>
#include <cmeta/collector.h>
#include <cmeta/data.h>
#include <cmeta/range.h>
#include <cmeta/meta.h>
#include "tinytest.hpp"

#include <cstddef>
#include <type_traits>

Struct(cmeta_cpp_record,
    (int, value),
    (const char *, name)
);

Enum(cmeta_cpp_state,
    (CMETA_CPP_READY, 3, "ready"),
    (CMETA_CPP_DONE, 7, "done")
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
static_assert(CMETA_CONTAINER_CONSTRUCT_OPS_ABI_VERSION == 1u,
              "container construction ops ABI starts at version 1");
static_assert(CMETA_CONTAINER_EXT_ABI_VERSION == 1u,
              "container extension ABI starts at version 1");
static_assert(CMETA_DATA_DESC_ABI_VERSION == 1u,
              "semantic data descriptor ABI starts at version 1");
static_assert(std::is_standard_layout_v<cmeta_container_type_ops>,
              "container type ops must remain a C-compatible standard-layout type");
static_assert(std::is_standard_layout_v<cmeta_container_construct_ops>,
              "container construction ops must remain a C-compatible standard-layout type");
static_assert(std::is_standard_layout_v<cmeta_container_ext>,
              "container extensions must remain C-compatible standard-layout types");
static_assert(std::is_standard_layout_v<cmeta_data_desc>,
              "semantic data descriptors must remain C-compatible standard-layout types");
static_assert(std::is_standard_layout_v<cmeta_data_struct_shape>,
              "semantic struct shapes must remain standard layout");
static_assert(std::is_standard_layout_v<cmeta_data_variant_shape>,
              "semantic variant shapes must remain standard layout");

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
  it("expands enum reflection without C-style casts") {
    cmeta_cpp_state state = CMETA_CPP_READY;

    check_equal(EnumString(cmeta_cpp_state, state), "ready");
    check_equal(EnumSymbol(cmeta_cpp_state, CMETA_CPP_DONE), "CMETA_CPP_DONE");
    check_true(EnumParse(cmeta_cpp_state, "done", &state));
    check_equal(state, CMETA_CPP_DONE);
  }

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
        "cpp trait", sizeof(int), alignof(int), CMETA_T_OBJECT,
        nullptr, &traits, nullptr};
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
        offsetof(cmeta_container_type_ops, argument) + sizeof(type_ops.argument),
        CMETA_CONTAINER_TYPE_OPS_ABI_VERSION,
        nullptr,
        0u,
        nullptr};
    const cmeta_container_ext old_ext = {
        offsetof(cmeta_container_ext, type) + sizeof(old_ext.type),
        CMETA_CONTAINER_EXT_ABI_VERSION,
        &type_ops,
        nullptr,
        nullptr};
    const cmeta_container_ext semantic_ext = {
        offsetof(cmeta_container_ext, data) + sizeof(semantic_ext.data),
        CMETA_CONTAINER_EXT_ABI_VERSION,
        &type_ops,
        &cmeta_data_sequence,
        nullptr};
    const cmeta_container_desc old_desc = {
        "cpp old container", nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, &old_ext};
    const cmeta_container_desc semantic_desc = {
        "cpp semantic container", nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr, nullptr, nullptr, nullptr, &semantic_ext};
    const cmeta_container_header old_object = {&old_desc};
    const cmeta_container_header semantic_object = {&semantic_desc};

    check_equal(type_ops.struct_size,
                offsetof(cmeta_container_type_ops, argument) +
                    sizeof(type_ops.argument));
    check_equal(old_ext.struct_size,
                offsetof(cmeta_container_ext, type) + sizeof(old_ext.type));
    check_true(cmeta_container_extension(&old_object) == &old_ext);
    check_null(cmeta_container_data(&old_object));
    check_null(cmeta_container_construction(&old_object));
    check_true(cmeta_container_data(&semantic_object) == &cmeta_data_sequence);
    check_null(cmeta_container_construction(&semantic_object));
  }

  it("exposes semantic data descriptors through C++17") {
    cmeta_data_desc prefix = cmeta_data_int;
    prefix.struct_size = offsetof(cmeta_data_desc, shape) + sizeof(prefix.shape);

    check_true(cmeta_data_desc_valid(&prefix));
    check_equal(cmeta_data_int.kind, CMETA_DATA_SINT);
    check_equal(cmeta_data_set.kind, CMETA_DATA_SET);
    check_true(cmeta_data_kind_is_container(CMETA_DATA_SET));
  }
}
