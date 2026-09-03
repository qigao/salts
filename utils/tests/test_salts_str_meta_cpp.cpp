#include "salts_str.h"
#include "salts_vstr.h"
#include "tinytest.h"

#include <cstddef>

struct vstr_cpp_legacy_layout {
  const char *data;
  size_t len;
};

static_assert(sizeof(vstr) == sizeof(vstr_cpp_legacy_layout),
              "vstr metadata must preserve C++ size");
static_assert(alignof(vstr) == alignof(vstr_cpp_legacy_layout),
              "vstr metadata must preserve C++ alignment");
static_assert(offsetof(vstr, data) == offsetof(vstr_cpp_legacy_layout, data),
              "vstr metadata must preserve C++ data offset");
static_assert(offsetof(vstr, len) == offsetof(vstr_cpp_legacy_layout, len),
              "vstr metadata must preserve C++ len offset");

spec("SaltsStr C++ CMeta") {
  it("exposes vstr metadata to C++17 consumers") {
    const cmeta_struct_desc *meta = vstr_meta();

    check_not_null(meta);
    check(meta->name != nullptr);
    check(meta->field_count == static_cast<size_t>(2));
    check(meta->fields[0].offset == offsetof(vstr, data));
    check(meta->fields[1].offset == offsetof(vstr, len));
  }

  it("constructs and searches through the vstr public API") {
    const vstr value = vstr_from_cstr("abc");

    check(vstr_len(value) == static_cast<size_t>(3));
    check(vstr_find(value, vstr_from_cstr("z")) == VSTR_NPOS);
  }
}
