#include "cmeta_manifest_view_fixture.h"
#include "tinytest.hpp"
#include "cmeta_manifest_declaration_cases.h"

cmeta_registry(cpp_view_manifest, cmeta_manifest_type_entry("int", &cmeta_type_int));
suite("CMeta C++ validated manifest borrowing") {
    it("constructs typed static entries and consumes C metadata") {
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_type_desc *type = nullptr;
        check_equal(cmeta_manifest_get_type(&cpp_view_manifest, 0u, &limits, &type), CMETA_OK);
        check_true(type == &cmeta_type_int);
        const cmeta_function_abi_desc *function = nullptr;
        check_equal(cmeta_manifest_get_function(cmeta_view_fixture_manifest(), 2u, &limits, &function), CMETA_OK);
        check_equal(function->param_count, static_cast<size_t>(1u));
    }
}
