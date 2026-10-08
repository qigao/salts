#ifndef SALTS_COMPONENT_H
#define SALTS_COMPONENT_H

#include <salts/component_abi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum salts_component_phase {
    SALTS_COMPONENT_PHASE_NONE = 0,
    SALTS_COMPONENT_PHASE_INIT,
    SALTS_COMPONENT_PHASE_RESOLVE,
    SALTS_COMPONENT_PHASE_DEPENDENCY,
    SALTS_COMPONENT_PHASE_PROVIDE,
    SALTS_COMPONENT_PHASE_CREATE,
    SALTS_COMPONENT_PHASE_ACTIVATE
} salts_component_phase;

typedef enum salts_component_context_state {
    SALTS_COMPONENT_CONTEXT_ZERO = 0,
    SALTS_COMPONENT_CONTEXT_READY,
    SALTS_COMPONENT_CONTEXT_RESOLVED,
    SALTS_COMPONENT_CONTEXT_ACTIVE,
    SALTS_COMPONENT_CONTEXT_FAILED,
    SALTS_COMPONENT_CONTEXT_STOPPED
} salts_component_context_state;

typedef struct salts_component_selection {
    const char *consumer_component_id;
    const cmeta_interface_desc *requirement;
    const char *provider_component_id;
} salts_component_selection;

/*
 * Host/deployment-owned configuration. Configuration is borrowed until
 * create() returns; providers that need it afterwards must copy/retain it
 * under their own semantics.
 */
typedef struct salts_component_deployment {
    const salts_component_provider_binding *provider;
    const cmeta_data_desc *config_data;
    const void *config_value;
} salts_component_deployment;

typedef struct salts_component_instance {
    cmeta_object_ref object;
    size_t dependency_offset;
    size_t dependency_count;
    bool active;
} salts_component_instance;

typedef struct salts_component_failure {
    salts_component_phase phase;
    size_t component_index;
    size_t dependency_index;
    cmeta_status provider_status;
} salts_component_failure;

typedef struct salts_component_service {
    const cmeta_component_desc *component;
    const cmeta_object_ref *object;
    const cmeta_object_interface_provider *interfaces;
} salts_component_service;

typedef struct salts_component_context {
    const salts_component_deployment *deployments;
    size_t deployment_count;

    const salts_component_selection *selections;
    size_t selection_count;

    salts_component_instance *instances;
    size_t instance_capacity;

    salts_component_dependency *dependencies;
    size_t dependency_capacity;
    size_t dependency_count;

    size_t *activation_order;
    size_t activation_capacity;
    size_t activation_count;

    salts_component_context_state state;
    salts_component_failure failure;
} salts_component_context;

const char *salts_component_status_string(salts_component_status status);

salts_component_status salts_component_context_init(
    salts_component_context *context,
    const salts_component_deployment *deployments,
    size_t deployment_count,
    const salts_component_selection *selections,
    size_t selection_count,
    salts_component_instance *instances,
    size_t instance_capacity,
    salts_component_dependency *dependencies,
    size_t dependency_capacity,
    size_t *activation_order,
    size_t activation_capacity);

salts_component_status salts_component_context_resolve(
    salts_component_context *context);

salts_component_status salts_component_context_start(
    salts_component_context *context);

salts_component_status salts_component_context_stop(
    salts_component_context *context);

/*
 * Control-plane service lookup. The returned view borrows the ACTIVE context.
 * Bind the exact typed Interface once and retain the appropriate outer scope;
 * do not route hot calls through repeated lookup.
 */
salts_component_status salts_component_context_find_service(
    const salts_component_context *context,
    const cmeta_interface_desc *interface_desc,
    salts_component_service *out_service);

/*
 * Explicit root/service selection for contexts that intentionally contain
 * multiple providers of the same Interface. No ranking or fallback occurs.
 */
salts_component_status salts_component_context_find_service_from(
    const salts_component_context *context,
    const char *provider_component_id,
    const cmeta_interface_desc *interface_desc,
    salts_component_service *out_service);

const salts_component_failure *salts_component_context_failure(
    const salts_component_context *context);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_COMPONENT_H */
