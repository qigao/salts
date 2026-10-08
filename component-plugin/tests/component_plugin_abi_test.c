#include <salts/component_plugin_abi.h>
#include "tinytest.h"

suite("ComponentPlugin ABI") {
    it("describes one borrowed provider-binding Interface") {
        const cmeta_interface_desc *interface =
            salts_component_provider_interface();
        const cmeta_interface_method_desc *method;

        check_true(cmeta_interface_desc_valid(interface));
        check_equal(interface->method_count, (size_t)1u);
        method = &interface->methods[0];
        check_equal(method->name, "get_binding");
        check_not_null(method->function);
        check_not_null(method->abi);
        check_equal(method->function->result_flags & CMETA_RESULT_CLASS_MASK,
                    CMETA_RESULT_BORROWED);
        check_equal(method->abi->return_carrier, CMETA_ABI_OBJECT_POINTER);
    }

    it("keeps provider binding ABI validation header-only") {
        salts_component_provider_binding binding = {0};

        binding.struct_size = sizeof(binding);
        binding.abi_version = SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION;
        check_false(salts_component_provider_binding_valid(&binding));
    }
}
