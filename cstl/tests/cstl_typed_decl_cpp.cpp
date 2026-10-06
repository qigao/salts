#include <cstl/typed.h>
#include <tinytest.hpp>

#include <cstddef>
#include <type_traits>

cstl_typed_decl(Vec, DeclIntVec, int);
cstl_typed_decl(List, DeclIntList, int);
cstl_typed_decl(Set, DeclIntSet, int);
cstl_typed_decl(Map, DeclIntLongMap, int, long);
cstl_typed_decl(HashMap, DeclIntLongHashMap, int, long);
cstl_typed_decl(Vec, DeclInnerVec, int);
cstl_typed_decl(Vec, DeclNestedVec, DeclInnerVec);

static_assert(std::is_standard_layout_v<DeclIntVec>);
static_assert(std::is_standard_layout_v<DeclIntList>);
static_assert(std::is_standard_layout_v<DeclIntSet>);
static_assert(std::is_standard_layout_v<DeclIntLongMap>);
static_assert(std::is_standard_layout_v<DeclIntLongHashMap>);
static_assert(std::is_standard_layout_v<DeclIntLongMap_entry>);
static_assert(std::is_standard_layout_v<DeclIntLongHashMap_entry>);
static_assert(std::is_standard_layout_v<DeclInnerVec>);
static_assert(std::is_standard_layout_v<DeclNestedVec>);

extern "C" {
#define DECL_UNARY_ABI(NAME) \
  std::size_t NAME##_c_size(void); \
  std::size_t NAME##_c_align(void); \
  std::size_t NAME##_c_raw_offset(void);

#define DECL_BINARY_ABI(NAME) \
  DECL_UNARY_ABI(NAME) \
  std::size_t NAME##_c_entry_size(void); \
  std::size_t NAME##_c_entry_key_offset(void); \
  std::size_t NAME##_c_entry_value_offset(void);

DECL_UNARY_ABI(DeclIntVec)
DECL_UNARY_ABI(DeclIntList)
DECL_UNARY_ABI(DeclIntSet)
DECL_UNARY_ABI(DeclInnerVec)
DECL_UNARY_ABI(DeclNestedVec)
DECL_BINARY_ABI(DeclIntLongMap)
DECL_BINARY_ABI(DeclIntLongHashMap)
const cmeta_data_desc *DeclIntVec_c_data(void);
const cmeta_data_desc *DeclIntLongMap_c_data(void);
const cmeta_data_desc *DeclNestedVec_c_data(void);
}

#define CHECK_UNARY(NAME) \
  do { \
    check_equal(NAME##_c_size(), sizeof(NAME)); \
    check_equal(NAME##_c_align(), alignof(NAME)); \
    check_equal(NAME##_c_raw_offset(), offsetof(NAME, raw)); \
  } while (false)

#define CHECK_BINARY(NAME) \
  do { \
    CHECK_UNARY(NAME); \
    check_equal(NAME##_c_entry_size(), sizeof(NAME##_entry)); \
    check_equal(NAME##_c_entry_key_offset(), offsetof(NAME##_entry, key)); \
    check_equal(NAME##_c_entry_value_offset(), offsetof(NAME##_entry, value)); \
  } while (false)

suite("CSTL typed declarations in C++") {
  group("C and C++ ABI agreement") {
    it("matches vector layout") { CHECK_UNARY(DeclIntVec); }
    it("matches list layout") { CHECK_UNARY(DeclIntList); }
    it("matches set layout") { CHECK_UNARY(DeclIntSet); }
    it("matches map and entry layout") { CHECK_BINARY(DeclIntLongMap); }
    it("matches hash map and entry layout") { CHECK_BINARY(DeclIntLongHashMap); }
    it("matches inner vector layout") { CHECK_UNARY(DeclInnerVec); }
    it("matches nested vector layout") { CHECK_UNARY(DeclNestedVec); }
  }

  group("C-defined metadata") {
    it("exposes valid construction operations and stable identifiers") {
      const cmeta_data_desc *vec_data = DeclIntVec_c_data();
      const cmeta_data_desc *map_data = DeclIntLongMap_c_data();
      check_not_null(vec_data);
      check_not_null(map_data);
      check_true(cmeta_data_desc_valid(vec_data));
      check_true(cmeta_data_desc_valid(map_data));
      check_not_null(vec_data->construct_ops);
      check_not_null(map_data->construct_ops);
      check_equal(vec_data->stable_id, "DeclIntVec.data");
      check_equal(map_data->stable_id, "DeclIntLongMap.data");
    }

    it("preserves nested vector capabilities and generic identity") {
      const cmeta_data_desc *nested = DeclNestedVec_c_data();
      check_true(cmeta_data_desc_valid(nested));
      check_true(cmeta_data_value_copy_supported(nested));
      check_true(cmeta_data_value_move_supported(nested));
      const cmeta_type_identity *identity = cmeta_type_identity_of(nested->storage_type);
      check_true(cmeta_type_identity_valid(identity));
      const cmeta_type_identity *inner = cmeta_type_identity_argument(identity, 0u);
      check_true(cmeta_generic_desc_equal(cmeta_type_identity_constructor(identity),
                                         &stl_vec_generic_desc));
      check_true(cmeta_generic_desc_equal(cmeta_type_identity_constructor(inner),
                                         &stl_vec_generic_desc));
      const cmeta_data_desc *element = cmeta_data_collection_element_data(nested);
      check_not_null(element);
      check_equal(cmeta_type_identity_of(element->storage_type), inner);
      check_true(cmeta_type_identity_equal(cmeta_type_identity_argument(inner, 0u),
                                          cmeta_type_identity_of(&cmeta_type_int)));
      const cmeta_type_identity *arguments[] = {cmeta_type_identity_of(&cmeta_type_int)};
      const cmeta_type_identity expected_inner =
          CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, arguments);
      const cmeta_type_identity *outer_arguments[] = {&expected_inner};
      const cmeta_type_identity expected_outer =
          CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, outer_arguments);
      check_true(cmeta_type_identity_equal(identity, &expected_outer));
    }
  }
}
