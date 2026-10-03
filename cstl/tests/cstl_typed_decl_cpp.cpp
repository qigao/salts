#include <cstl/typed.h>

#include <cstddef>
#include <type_traits>

cstl_typed_decl(Vec, DeclIntVec, int);
cstl_typed_decl(List, DeclIntList, int);
cstl_typed_decl(Set, DeclIntSet, int);
cstl_typed_decl(Map, DeclIntLongMap, int, long);
cstl_typed_decl(HashMap, DeclIntLongHashMap, int, long);

static_assert(std::is_standard_layout_v<DeclIntVec>);
static_assert(std::is_standard_layout_v<DeclIntList>);
static_assert(std::is_standard_layout_v<DeclIntSet>);
static_assert(std::is_standard_layout_v<DeclIntLongMap>);
static_assert(std::is_standard_layout_v<DeclIntLongHashMap>);
static_assert(std::is_standard_layout_v<DeclIntLongMap_entry>);
static_assert(std::is_standard_layout_v<DeclIntLongHashMap_entry>);

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
DECL_BINARY_ABI(DeclIntLongMap)
DECL_BINARY_ABI(DeclIntLongHashMap)
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
  CHECK_UNARY(DeclIntVec, 10);
  CHECK_UNARY(DeclIntList, 20);
  CHECK_UNARY(DeclIntSet, 30);
  CHECK_BINARY(DeclIntLongMap, 40);
  CHECK_BINARY(DeclIntLongHashMap, 50);
  return 0;
}
