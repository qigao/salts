#include "cmeta_capabilities_fixture.h"
#include <tinytest.hpp>
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

suite("CMeta capabilities C++ header") {
    group("equality trait") {
        it("invokes a noexcept C++ callback") {
            const int number = 7;
            check_true(cmeta_traits_CapabilityCpp.equal(&number, &number));
        }
    }

    group("flags variant") {
        static CapabilityValue value;

        before_each() { value = CapabilityValue{}; }
        after_each() { CapabilityValue_destroy(&value); }

        it("copies combined flags and clears the tag on destruction") {
            CapabilityFlags flags{};
            check_equal(CapabilityFlags_or(CapabilityFlags_Read, CapabilityFlags_High, &flags),
                        CMETA_OK);
            check_equal(CapabilityValue_copy_Flags(&value, &flags), CMETA_OK);
            const auto *read = CapabilityValue_get_Flags(&value);
            check_not_null(read);
            check_equal(read->bits, flags.bits);
            CapabilityValue_destroy(&value);
            check_equal(value.tag, 0);
        }
    }
}
