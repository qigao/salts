#include <cmeta/data.h>
#include <type_traits>

static_assert(std::is_standard_layout<cmeta_enum_domain>::value, "C domain ABI");
static_assert(std::is_same<decltype(cmeta_enum_bits_item::bits), uint64_t>::value,
              "enum values never narrow through int64_t");
static_assert(std::is_same<decltype(&cmeta_data_enum_read_bits),
              cmeta_status (*)(const cmeta_data_desc *, const void *, uint64_t *)>::value,
              "read canonical unsigned bits");
int main() {
    cmeta_data_desc desc{};
    cmeta_data_enum_bits_ops ops{};
    cmeta_enum_domain domain{};
    uint64_t value = UINT64_MAX;
    ops.domain = &domain;
    desc.enum_bits_ops = &ops;
    return cmeta_data_enum_assign_bits(&desc, &value, UINT64_MAX) == CMETA_INVALID_ARGUMENT
        && cmeta_data_enum_read_bits(&desc, &value, &value) == CMETA_INVALID_ARGUMENT ? 0 : 1;
}
