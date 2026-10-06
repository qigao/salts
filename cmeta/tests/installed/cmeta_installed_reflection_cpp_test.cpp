#include <cmeta/meta.h>
#include <cmeta/manifest_view.h>
#include "tinytest.hpp"

#if defined(SALTS_FASTPATH_H) || defined(CMETA_TRACE_H) || defined(CMETA_SCOPE_H)
#error "Reflection-only package consumers must not include optional runtime facades"
#endif
cmeta_registry(installed_cpp_manifest, cmeta_manifest_type_entry("int", &cmeta_type_int));
suite("Installed C++ CMeta Reflection without runtime adapters") {
    it("borrows semantic type metadata from the installed C ABI") {
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_type_desc *type = nullptr;
        check_equal(cmeta_manifest_get_type(&installed_cpp_manifest, 0u, &limits, &type), CMETA_OK);
        check_true(cmeta_type_equal(type, &cmeta_type_int));
        check_equal(type->size, sizeof(int));
    }
}
