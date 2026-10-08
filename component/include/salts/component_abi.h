#ifndef SALTS_COMPONENT_ABI_H
#define SALTS_COMPONENT_ABI_H

#include <cmeta/component.h>
#include <cmeta/object_interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_COMPONENT_INDEX_NONE SIZE_MAX
#define SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION UINT32_C(1)

#if defined(_WIN32)
#define SALTS_COMPONENT_CALL __cdecl
#else
#define SALTS_COMPONENT_CALL
#endif

typedef enum salts_component_status {
    SALTS_COMPONENT_OK = 0,
    SALTS_COMPONENT_INVALID_ARGUMENT,
    SALTS_COMPONENT_INVALID_COMPONENT,
    SALTS_COMPONENT_DUPLICATE_COMPONENT_ID,
    SALTS_COMPONENT_INVALID_SELECTION,
    SALTS_COMPONENT_CONFIG_MISMATCH,
    SALTS_COMPONENT_CAPACITY_EXCEEDED,
    SALTS_COMPONENT_MISSING_PROVIDER,
    SALTS_COMPONENT_AMBIGUOUS_PROVIDER,
    SALTS_COMPONENT_DEPENDENCY_CYCLE,
    SALTS_COMPONENT_INTERFACE_UNAVAILABLE,
    SALTS_COMPONENT_CREATE_FAILED,
    SALTS_COMPONENT_ACTIVATE_FAILED,
    SALTS_COMPONENT_INVALID_STATE
} salts_component_status;

typedef struct salts_component_dependency salts_component_dependency;

typedef cmeta_status (SALTS_COMPONENT_CALL *salts_component_create_fn)(
    void *provider_context,
    const cmeta_data_desc *config_data,
    const void *config_value,
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    cmeta_object_ref *out_instance);

typedef cmeta_status (SALTS_COMPONENT_CALL *salts_component_activate_fn)(
    void *provider_context,
    const cmeta_object_ref *instance);

typedef void (SALTS_COMPONENT_CALL *salts_component_deactivate_fn)(
    void *provider_context,
    const cmeta_object_ref *instance);

typedef struct salts_component_provider_binding {
    size_t struct_size;
    uint32_t abi_version;

    const cmeta_component_desc *component;
    void *provider_context;

    /*
     * Provider-authorized Interface projection. Required when component
     * publishes at least one Interface. Dynamic bindings and everything
     * reachable from them remain borrowed under the enclosing module lease.
     */
    const cmeta_object_interface_provider *interfaces;

    salts_component_create_fn create;
    salts_component_activate_fn activate;
    salts_component_deactivate_fn deactivate;
} salts_component_provider_binding;

static inline bool salts_component_provider_binding_valid(
    const salts_component_provider_binding *binding) {
    size_t i;
    bool provides = false;

    if (binding == NULL ||
        binding->struct_size != sizeof(*binding) ||
        binding->abi_version != SALTS_COMPONENT_PROVIDER_BINDING_ABI_VERSION ||
        !cmeta_component_desc_valid(binding->component) ||
        binding->create == NULL ||
        ((binding->activate == NULL) != (binding->deactivate == NULL)))
        return false;

    if (binding->interfaces != NULL &&
        !cmeta_object_interface_provider_valid(binding->interfaces))
        return false;

    for (i = 0u; i < binding->component->capability_count; ++i) {
        if (binding->component->capabilities[i].role ==
            CMETA_COMPONENT_PROVIDES) {
            provides = true;
            break;
        }
    }

    return !provides ||
        cmeta_object_interface_provider_valid(binding->interfaces);
}

struct salts_component_dependency {
    size_t consumer_index;
    size_t provider_index;
    const cmeta_interface_desc *interface_desc;
    const cmeta_component_desc *provider_component;
    const cmeta_object_ref *provider_instance;
    const cmeta_object_interface_provider *provider_interfaces;
};

/*
 * ABI-only factory helper. Outputs are published only on one exact match.
 * It performs no registry lookup and owns no lifetime.
 */
static inline salts_component_status salts_component_dependency_find(
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    const cmeta_interface_desc *interface_desc,
    const salts_component_dependency **out_dependency) {
    const salts_component_dependency *candidate = NULL;
    size_t candidate_count = 0u;
    size_t i;

    if (out_dependency == NULL || !cmeta_interface_desc_valid(interface_desc) ||
        (dependency_count != 0u && dependencies == NULL))
        return SALTS_COMPONENT_INVALID_ARGUMENT;

    for (i = 0u; i < dependency_count; ++i) {
        if (cmeta_interface_desc_equal(
                dependencies[i].interface_desc, interface_desc)) {
            candidate = &dependencies[i];
            ++candidate_count;
        }
    }

    if (candidate_count == 0u)
        return SALTS_COMPONENT_MISSING_PROVIDER;
    if (candidate_count != 1u)
        return SALTS_COMPONENT_AMBIGUOUS_PROVIDER;

    *out_dependency = candidate;
    return SALTS_COMPONENT_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* SALTS_COMPONENT_ABI_H */
