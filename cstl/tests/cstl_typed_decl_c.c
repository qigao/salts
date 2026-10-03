#include <cstl/typed.h>
#include <stddef.h>

typed(Vec, DeclIntVec, int);
typed(List, DeclIntList, int);
typed(Set, DeclIntSet, int);
typed(Map, DeclIntLongMap, int, long);
typed(HashMap, DeclIntLongHashMap, int, long);

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
