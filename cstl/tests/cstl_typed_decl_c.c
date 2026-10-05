#include <cstl/typed.h>
#include <stddef.h>

cmeta_type(Vec, DeclIntVec, int);
cmeta_type(List, DeclIntList, int);
cmeta_type(Set, DeclIntSet, int);
cmeta_type(Map, DeclIntLongMap, int, long);
cmeta_type(HashMap, DeclIntLongHashMap, int, long);

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
DECL_BINARY_ABI(DeclIntLongMap)
DECL_BINARY_ABI(DeclIntLongHashMap)

const cmeta_data_desc *DeclIntVec_c_data(void) {
  return DeclIntVec_cmeta_data();
}

const cmeta_data_desc *DeclIntLongMap_c_data(void) {
  return DeclIntLongMap_cmeta_data();
}
