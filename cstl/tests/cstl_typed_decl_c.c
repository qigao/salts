#include <cstl/typed.h>
#include <stddef.h>

cmeta_type(Vec, DeclIntVec, int);
cmeta_type(List, DeclIntList, int);
cmeta_type(Set, DeclIntSet, int);
cmeta_type(Map, DeclIntLongMap, int, long);
cmeta_type(HashMap, DeclIntLongHashMap, int, long);

/* Primitive identities are descriptor-owned, so publish their references
 * during this fixture's single-threaded metadata construction. */
static const cmeta_type_identity *decl_inner_arguments[1];
static const cmeta_type_identity decl_inner_identity =
    CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, decl_inner_arguments);
cmeta_type(Vec, DeclInnerVec, int, &cmeta_type_int, &cmeta_data_int,
           &decl_inner_identity);
static const cmeta_type_identity *const decl_nested_arguments[] = {
    &decl_inner_identity};
static const cmeta_type_identity decl_nested_identity =
    CMETA_TYPE_ID_APPLY_INIT(&stl_vec_generic_desc, decl_nested_arguments);
cmeta_type(Vec, DeclNestedVec, DeclInnerVec, &DeclInnerVec_cmeta_type,
           &DeclInnerVec_collection_data, &decl_nested_identity);

#define DECL_UNARY_ABI(NAME) \
  size_t NAME##_c_size(void) { return sizeof(NAME); } \
  size_t NAME##_c_align(void) { return _Alignof(NAME); } \
  size_t NAME##_c_raw_offset(void) { return offsetof(NAME, raw); }

#define DECL_BINARY_ABI(NAME) \
  DECL_UNARY_ABI(NAME) \
  size_t NAME##_c_entry_size(void) { return sizeof(NAME##_entry); } \
  size_t NAME##_c_entry_key_offset(void) { return offsetof(NAME##_entry, key); } \
  size_t NAME##_c_entry_value_offset(void) { return offsetof(NAME##_entry, value); }

DECL_UNARY_ABI(DeclIntVec)
DECL_UNARY_ABI(DeclIntList)
DECL_UNARY_ABI(DeclIntSet)
DECL_UNARY_ABI(DeclInnerVec)
DECL_UNARY_ABI(DeclNestedVec)
DECL_BINARY_ABI(DeclIntLongMap)
DECL_BINARY_ABI(DeclIntLongHashMap)

const cmeta_data_desc *DeclIntVec_c_data(void) {
  return DeclIntVec_cmeta_data();
}

const cmeta_data_desc *DeclIntLongMap_c_data(void) {
  return DeclIntLongMap_cmeta_data();
}

const cmeta_data_desc *DeclNestedVec_c_data(void) {
  decl_inner_arguments[0] = cmeta_type_identity_of(&cmeta_type_int);
  return DeclNestedVec_cmeta_data();
}
