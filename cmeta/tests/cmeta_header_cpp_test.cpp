#include <cmeta/struct.h>
#include "tinytest.hpp"

#include <cstddef>

Struct(cmeta_cpp_record,
    (int, value),
    (const char *, name)
);

static_assert(CMETA_ALIGNOF(cmeta_cpp_record) == alignof(cmeta_cpp_record),
              "CMETA_ALIGNOF must use the active language spelling");

spec("CMeta C++ public headers") {
  it("reflects struct fields through the C++ public surface") {
    const cmeta_struct_desc *meta = cmeta_cpp_record_meta();

    check_not_null(meta);
    check_equal(meta->name, "cmeta_cpp_record");
    check_equal(meta->field_count, static_cast<size_t>(2));
    check_equal(meta->fields[0].offset, offsetof(cmeta_cpp_record, value));
    check_equal(meta->fields[1].offset, offsetof(cmeta_cpp_record, name));
  }
}
