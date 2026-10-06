#ifndef CMETA_MANIFEST_DECLARATION_CASES_H
#define CMETA_MANIFEST_DECLARATION_CASES_H

#include <cmeta/manifest_view.h>
#include <cmeta/fingerprint.h>

#define CMETA_NAMED_REGISTRY cmeta_named_registry
#define CMETA_NAMED_SYMBOL cmeta_named_value
#define CMETA_NAMED_EMPTY_REGISTRY cmeta_named_empty_registry
#define CMETA_NAMED_EMPTY_PLUGIN cmeta_named_empty_plugin
#define CMETA_NAMED_SERVICE cmeta_named_service
#define CMETA_NAMED_PROVIDER cmeta_named_provider
#define CMETA_NAMED_METHODS(X,I) \
    X(I,FV0,void,reset,stateful,&cmeta_type_void,CMETA_ABI_VOID)
CMETA_INTERFACE(CMETA_NAMED_SERVICE, CMETA_NAMED_METHODS);
static const int cmeta_named_value = 7;
cmeta_plugin_empty(CMETA_NAMED_EMPTY_PLUGIN);
cmeta_registry_empty(CMETA_NAMED_EMPTY_REGISTRY);

/* Repeated role rows are ordered membership, not a set to deduplicate. */
#define CMETA_NAMED_ROLE_PAIR \
    cmeta_provides(CMETA_NAMED_SERVICE) cmeta_requires(CMETA_NAMED_SERVICE)
cmeta_plugin(CMETA_NAMED_PROVIDER,
    CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR
    CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR CMETA_NAMED_ROLE_PAIR);
#define CMETA_NAMED_ENTRIES \
    cmeta_entry(CMETA_NAMED_SYMBOL) \
    cmeta_manifest_type_entry("explicit.int", &cmeta_type_int) \
    cmeta_manifest_plugin_entry("empty.provider", cmeta_plugin_meta(CMETA_NAMED_EMPTY_PLUGIN)) \
    cmeta_manifest_plugin_entry("full.provider", cmeta_plugin_meta(CMETA_NAMED_PROVIDER))
cmeta_registry(CMETA_NAMED_REGISTRY, CMETA_NAMED_ENTRIES);

enum { CMETA_NAMED_COUNT = 4, CMETA_NAMED_MAX_ROLES = 16,
    CMETA_NAMED_TYPE_INDEX = 1, CMETA_NAMED_EMPTY_INDEX = 2, CMETA_NAMED_FULL_INDEX = 3 };
static const cmeta_manifest_limits cmeta_named_limits = {CMETA_MANIFEST_DEFAULT_ITEMS,
    CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};

suite("CMeta static declaration normalization") {
    it("expands symbol aliases and preserves explicit discovery names and order") {
        const cmeta_type_desc *type = NULL;
        check_equal(CMETA_NAMED_REGISTRY.name,"cmeta_named_registry");
        check_equal(CMETA_NAMED_REGISTRY.count,CMETA_NAMED_COUNT);
        check_equal(CMETA_NAMED_REGISTRY.format_version,CMETA_MANIFEST_FORMAT_VERSION);
        check_equal(CMETA_NAMED_REGISTRY.entries[0].name,"cmeta_named_value");
        check_true(CMETA_NAMED_REGISTRY.entries[0].kind == CMETA_MANIFEST_GENERIC);
        check_true(CMETA_NAMED_REGISTRY.entries[0].descriptor == &cmeta_named_value);
        check_equal(CMETA_NAMED_REGISTRY.entries[CMETA_NAMED_TYPE_INDEX].name,"explicit.int");
        check_equal(cmeta_manifest_get_type(&CMETA_NAMED_REGISTRY,CMETA_NAMED_TYPE_INDEX,
            &cmeta_named_limits,&type),CMETA_OK);
        check_true(type == &cmeta_type_int);
    }
    it("represents empty discovery without publishing an entry or changing failed outputs") {
        const cmeta_type_desc *type = &cmeta_type_int;
        check_equal(CMETA_NAMED_EMPTY_REGISTRY.name,"cmeta_named_empty_registry");
        check_equal(CMETA_NAMED_EMPTY_REGISTRY.count,0u);
        check_null(CMETA_NAMED_EMPTY_REGISTRY.entries);
        check_equal(CMETA_NAMED_EMPTY_REGISTRY.format_version,CMETA_MANIFEST_FORMAT_VERSION);
        check_equal(cmeta_manifest_get_type(&CMETA_NAMED_EMPTY_REGISTRY,0u,
            &cmeta_named_limits,&type),CMETA_INVALID_ARGUMENT);
        check_true(type == &cmeta_type_int);
    }
    it("admits empty membership and preserves the manual empty descriptor fingerprint") {
        const cmeta_plugin_desc manual = {sizeof(cmeta_plugin_desc),
            CMETA_PLUGIN_DECLARATION_VERSION,"manual.empty",NULL,0u};
        const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
            CMETA_FINGERPRINT_DEFAULT_NODES,CMETA_FINGERPRINT_DEFAULT_ROWS,
            CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
        const cmeta_plugin_desc *provider = NULL;
        uint64_t generated, expected;
        check_equal(cmeta_manifest_get_plugin(&CMETA_NAMED_REGISTRY,CMETA_NAMED_EMPTY_INDEX,
            &cmeta_named_limits,&provider),CMETA_OK);
        check_true(provider == cmeta_plugin_meta(CMETA_NAMED_EMPTY_PLUGIN));
        check_equal(provider->name,"cmeta_named_empty_plugin");
        check_equal(provider->count,0u);
        check_null(provider->capabilities);
        check_equal(cmeta_contract_fingerprint_plugin(provider,&limits,&generated),CMETA_OK);
        check_equal(cmeta_contract_fingerprint_plugin(&manual,&limits,&expected),CMETA_OK);
        check_equal(generated,expected);
        const cmeta_interface_desc *out = &cmeta_named_service_interface_meta;
        check_equal(cmeta_plugin_get_capability(provider,0u,CMETA_PLUGIN_PROVIDES,
            &cmeta_named_limits,&out),CMETA_INVALID_ARGUMENT);
        check_true(out == &cmeta_named_service_interface_meta);
    }
    it("retains all sixteen alternating role rows over canonical interface metadata") {
        const cmeta_plugin_desc *provider = NULL;
        check_equal(cmeta_manifest_get_plugin(&CMETA_NAMED_REGISTRY,CMETA_NAMED_FULL_INDEX,
            &cmeta_named_limits,&provider),CMETA_OK);
        check_equal(provider->name,"cmeta_named_provider");
        check_equal(provider->count,CMETA_NAMED_MAX_ROLES);
        check_equal(cmeta_named_service_interface_meta.name,"cmeta_named_service");
        for (size_t i = 0u; i < provider->count; ++i) {
            const cmeta_interface_desc *interface_desc = NULL;
            const cmeta_plugin_role role = i % 2u == 0u ? CMETA_PLUGIN_PROVIDES : CMETA_PLUGIN_REQUIRES;
            check_equal(cmeta_plugin_get_capability(provider,i,role,&cmeta_named_limits,
                &interface_desc),CMETA_OK);
            check_true(interface_desc == &cmeta_named_service_interface_meta);
        }
    }
}

#endif
