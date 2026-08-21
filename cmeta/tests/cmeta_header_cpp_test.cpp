#include <cmeta/status.h>
#include <cmeta/type_traits.h>
#include <cmeta/cmeta.h>
#include <cmeta/meta.h>
#include "tinytest.hpp"

#include <cstddef>

Struct(cmeta_cpp_record,
    (int, value),
    (const char *, name)
);

static_assert(CMETA_ALIGNOF(cmeta_cpp_record) == alignof(cmeta_cpp_record),
              "CMETA_ALIGNOF must use the active language spelling");

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
}
