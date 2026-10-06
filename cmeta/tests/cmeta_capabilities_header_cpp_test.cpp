#include "cmeta_capabilities_fixture.h"
#include <type_traits>

static_assert(std::is_standard_layout_v<CapabilityValue>);
static_assert(std::is_standard_layout_v<CapabilityFlags>);
static_assert(cmeta_has_trait(CapabilityNested, Copyable));
static_assert(!cmeta_has_trait(CapabilityFlags, Hashable));

static bool cpp_equal(const void *left, const void *right) noexcept {
    return static_cast<const int *>(left)[0] == static_cast<const int *>(right)[0];
}
cmeta_traits(CapabilityCpp, cmeta_trait(Equal, cpp_equal));
cmeta_require_trait(CapabilityCpp, Equal);
static_assert(cmeta_traits_CapabilityCpp.hash == nullptr);

int main() {
    const int number = 7;
    if (!cmeta_traits_CapabilityCpp.equal(&number, &number)) return 1;
    CapabilityFlags flags{};
    CapabilityValue value{};
    if (CapabilityFlags_or(CapabilityFlags_Read, CapabilityFlags_High, &flags) != CMETA_OK) return 1;
    if (CapabilityValue_copy_Flags(&value, &flags) != CMETA_OK) return 1;
    const auto *read = CapabilityValue_get_Flags(&value);
    if (read == nullptr || read->bits != flags.bits) return 1;
    CapabilityValue_destroy(&value);
    return value.tag == 0 ? 0 : 1;
}
