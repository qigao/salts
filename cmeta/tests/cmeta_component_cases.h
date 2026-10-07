#ifndef CMETA_COMPONENT_CASES_H
#define CMETA_COMPONENT_CASES_H

#include <cmeta/component.h>
#include <cmeta/fingerprint.h>
#include <cmeta/manifest_view.h>

#ifdef __cplusplus
#define COMPONENT_SIZE(value_) static_cast<size_t>(value_)
#else
#define COMPONENT_SIZE(value_) ((size_t)(value_))
#endif

#define COMPONENT_STORAGE_METHODS(X, I) \
    X(I, F0, int, ready, value, &cmeta_type_int, CMETA_ABI_SCALAR)

#define COMPONENT_LOGGER_METHODS(X, I) \
    X(I, FV0, void, flush, value, &cmeta_type_void, CMETA_ABI_VOID)

CMETA_INTERFACE(component_storage, COMPONENT_STORAGE_METHODS);
CMETA_INTERFACE(component_logger, COMPONENT_LOGGER_METHODS);

cmeta_component(PatternStorage,
    cmeta_provides(component_storage)
    cmeta_requires(component_logger));

cmeta_component_configured(PatternConfiguredStorage, &cmeta_data_int,
    cmeta_provides(component_storage)
    cmeta_requires(component_logger));

cmeta_component_empty(PatternEmpty);

cmeta_registry(pattern_components,
    cmeta_manifest_component_entry(
        "storage", cmeta_component_meta(PatternStorage))
    cmeta_manifest_component_entry(
        "configured-storage", cmeta_component_meta(PatternConfiguredStorage))
    cmeta_manifest_component_entry(
        "empty", cmeta_component_meta(PatternEmpty)));

suite("CMeta component declarations") {
    it("discovers one canonical component through an explicit manifest") {
        const cmeta_manifest_limits limits = {
            CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH,
            CMETA_MANIFEST_DEFAULT_NODES
        };
        const cmeta_component_desc *component = NULL;
        const cmeta_interface_desc *provided = NULL;
        const cmeta_interface_desc *required = NULL;

        check_equal(cmeta_manifest_get_component(
            &pattern_components, 0u, &limits, &component), CMETA_OK);
        check_not_null(component);
        check_equal(component->stable_id, "PatternStorage");
        check_equal(component->capability_count, COMPONENT_SIZE(2u));

        check_equal(cmeta_component_get_capability(
            component, 0u, CMETA_COMPONENT_PROVIDES, &limits, &provided), CMETA_OK);
        check_true(cmeta_interface_desc_equal(
            provided, component_storage_interface()));

        check_equal(cmeta_component_get_capability(
            component, 1u, CMETA_COMPONENT_REQUIRES, &limits, &required), CMETA_OK);
        check_true(cmeta_interface_desc_equal(
            required, component_logger_interface()));
    }

    it("keeps role admission explicit and fail closed") {
        const cmeta_manifest_limits limits = {
            CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH,
            CMETA_MANIFEST_DEFAULT_NODES
        };
        const cmeta_component_desc *component = NULL;
        const cmeta_interface_desc *unchanged = component_logger_interface();
        const cmeta_interface_desc *out = unchanged;

        check_equal(cmeta_manifest_get_component(
            &pattern_components, 0u, &limits, &component), CMETA_OK);
        check_equal(cmeta_component_get_capability(
            component, 0u, CMETA_COMPONENT_REQUIRES, &limits, &out),
            CMETA_TYPE_MISMATCH);
        check_true(out == unchanged);
    }

    it("separates stable component identity from capability-shape fingerprint") {
        const cmeta_fingerprint_limits limits = {
            CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES,
            CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES
        };
        const cmeta_component_desc *component = cmeta_component_meta(PatternStorage);
        cmeta_component_desc alias = *component;
        uint64_t original = 0u;
        uint64_t renamed = 0u;

        alias.stable_id = "test.other.storage";
        check_equal(cmeta_contract_fingerprint_component(
            component, &limits, &original), CMETA_OK);
        check_equal(cmeta_contract_fingerprint_component(
            &alias, &limits, &renamed), CMETA_OK);
        check_equal(original, renamed);
    }

    it("binds typed configuration through canonical DataDesc identity") {
        const cmeta_manifest_limits limits = {
            CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH,
            CMETA_MANIFEST_DEFAULT_NODES
        };
        const cmeta_component_desc *component = NULL;

        check_equal(cmeta_manifest_get_component(
            &pattern_components, 1u, &limits, &component), CMETA_OK);
        check_not_null(component);
        check_not_null(component->config);
        check_true(cmeta_data_desc_equal(component->config, &cmeta_data_int));
    }

    it("includes typed config contract in the component fingerprint") {
        const cmeta_fingerprint_limits limits = {
            CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES,
            CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES
        };
        uint64_t plain = 0u;
        uint64_t configured = 0u;

        check_equal(cmeta_contract_fingerprint_component(
            cmeta_component_meta(PatternStorage), &limits, &plain), CMETA_OK);
        check_equal(cmeta_contract_fingerprint_component(
            cmeta_component_meta(PatternConfiguredStorage), &limits, &configured), CMETA_OK);
        check_not_equal(plain, configured);
    }

    it("represents an explicit empty component without sentinel capabilities") {
        const cmeta_manifest_limits limits = {
            CMETA_MANIFEST_DEFAULT_ITEMS,
            CMETA_MANIFEST_DEFAULT_DEPTH,
            CMETA_MANIFEST_DEFAULT_NODES
        };
        const cmeta_component_desc *component = NULL;

        check_equal(cmeta_manifest_get_component(
            &pattern_components, 2u, &limits, &component), CMETA_OK);
        check_not_null(component);
        check_equal(component->capability_count, COMPONENT_SIZE(0u));
        check_null(component->capabilities);
    }
}

#undef COMPONENT_SIZE

#endif /* CMETA_COMPONENT_CASES_H */
