#include <salts/component_plugin_abi.h>
#include "tinytest.hpp"

suite("ComponentPlugin ABI C++17") {
    it("exposes canonical provider Interface metadata") {
        check_true(cmeta_interface_desc_valid(
            salts_component_provider_interface()));
        check_equal(SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION,
                    static_cast<uint32_t>(1));
    }
}
