#include <cstl/typed.h>

#include <cstddef>
#include <cstring>
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

#define CHECK_UNARY(NAME, CODE) \
  do { \
    if (NAME##_c_size() != sizeof(NAME)) return (CODE); \
    if (NAME##_c_align() != alignof(NAME)) return (CODE) + 1; \
    if (NAME##_c_raw_offset() != offsetof(NAME, raw)) return (CODE) + 2; \
  } while (false)

#define CHECK_BINARY(NAME, CODE) \
  do { \
    CHECK_UNARY(NAME, CODE); \
    if (NAME##_c_entry_size() != sizeof(NAME##_entry)) return (CODE) + 3; \
    if (NAME##_c_entry_key_offset() != offsetof(NAME##_entry, key)) return (CODE) + 4; \
    if (NAME##_c_entry_value_offset() != offsetof(NAME##_entry, value)) return (CODE) + 5; \
  } while (false)

int main() {
  const cmeta_data_desc *vec_data = DeclIntVec_c_data();
  const cmeta_data_desc *map_data = DeclIntLongMap_c_data();

  if (vec_data == nullptr || map_data == nullptr) return 1;
  if (!cmeta_data_desc_valid(vec_data) ||
      !cmeta_data_desc_valid(map_data)) return 2;
  if (vec_data->construct_ops == nullptr ||
      map_data->construct_ops == nullptr) return 3;
  if (std::strcmp(vec_data->stable_id, "DeclIntVec.data") != 0) return 4;
  if (std::strcmp(map_data->stable_id, "DeclIntLongMap.data") != 0) return 5;

  CHECK_UNARY(DeclIntVec, 10);
  CHECK_UNARY(DeclIntList, 20);
  CHECK_UNARY(DeclIntSet, 30);
  CHECK_BINARY(DeclIntLongMap, 40);
  CHECK_BINARY(DeclIntLongHashMap, 50);
  enum { INNER_ABI_ERROR = 60, NESTED_ABI_ERROR = 70,
         NESTED_METADATA_ERROR = 80, NESTED_IDENTITY_ERROR,
         NESTED_ARGUMENT_ERROR, NESTED_SEMANTIC_ERROR };
  CHECK_UNARY(DeclInnerVec, INNER_ABI_ERROR);
  CHECK_UNARY(DeclNestedVec, NESTED_ABI_ERROR);
  const cmeta_data_desc *nested = DeclNestedVec_c_data();
  if (!cmeta_data_desc_valid(nested) ||
      !cmeta_data_value_copy_supported(nested) ||
      !cmeta_data_value_move_supported(nested)) return NESTED_METADATA_ERROR;
  const cmeta_type_identity *identity =
      cmeta_type_identity_of(nested->storage_type);
  const cmeta_type_identity *inner = cmeta_type_identity_argument(identity, 0u);
  if (!cmeta_type_identity_valid(identity) ||
      !cmeta_generic_desc_equal(cmeta_type_identity_constructor(identity),
                                &stl_vec_generic_desc) ||
      !cmeta_generic_desc_equal(cmeta_type_identity_constructor(inner),
                                &stl_vec_generic_desc)) return NESTED_IDENTITY_ERROR;
  const cmeta_data_desc *element = cmeta_data_collection_element_data(nested);
  if (element == nullptr ||
      cmeta_type_identity_of(element->storage_type) != inner ||
      !cmeta_type_identity_equal(cmeta_type_identity_argument(inner, 0u),
                                 cmeta_type_identity_of(&cmeta_type_int)))
    return NESTED_ARGUMENT_ERROR;
  const cmeta_type_identity *arguments[] = {
      cmeta_type_identity_of(&cmeta_type_int)};
  const cmeta_type_identity expected_inner =
      CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, arguments);
  const cmeta_type_identity *outer_arguments[] = {&expected_inner};
  const cmeta_type_identity expected_outer =
      CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, outer_arguments);
  if (!cmeta_type_identity_equal(identity, &expected_outer))
    return NESTED_SEMANTIC_ERROR;
  return 0;
}
