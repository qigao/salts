#include <cmeta/meta.h>
#include <cmeta/manifest_view.h>
#include "tinytest.h"

#if defined(SALTS_FASTPATH_H) || defined(CMETA_TRACE_H) || defined(CMETA_SCOPE_H)
#error "Reflection-only package consumers must not include optional runtime facades"
#endif
cmeta_struct(InstalledRecord, cmeta_field(int, value));
FunctionDecl(value, int, installed_transform, (int, value, CMETA_PARAM_IN));
cmeta_registry(installed_manifest,
    cmeta_manifest_struct_entry("record", StructMeta(InstalledRecord))
    cmeta_manifest_function_entry("transform", FunctionAbi(installed_transform))
);
suite("Installed CMeta Reflection without runtime adapters") {
    it("discovers canonical layout and Function ABI through the installed archive") {
        const cmeta_manifest_limits limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
        const cmeta_struct_desc *record = NULL;
        const cmeta_function_abi_desc *function = NULL;
        check_equal(cmeta_manifest_get_struct(&installed_manifest, 0u, &limits, &record), CMETA_OK);
        check_true(record == StructMeta(InstalledRecord));
        check_equal(record->fields[0].offset, offsetof(InstalledRecord, value));
        check_equal(cmeta_manifest_get_function(&installed_manifest, 1u, &limits, &function), CMETA_OK);
        check_true(function == FunctionAbi(installed_transform));
        check_true(cmeta_type_equal(function->function->return_type, &cmeta_type_int));
    }
    it("computes a canonical type contract through the installed core archive") {
        const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES, CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
        uint64_t original, renamed;
        check_equal(cmeta_contract_fingerprint_type(&cmeta_type_int, &limits, &original), CMETA_OK);
        cmeta_type_desc type = cmeta_type_int;
        type.name = "InstalledDisplayName";
        check_equal(cmeta_contract_fingerprint_type(&type, &limits, &renamed), CMETA_OK);
        check_equal(original, renamed);
    }
}
