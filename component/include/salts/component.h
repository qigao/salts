#ifndef SALTS_COMPONENT_H
#define SALTS_COMPONENT_H

#include <cmeta/component.h>
#include <cmeta/object_interface.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_COMPONENT_INDEX_NONE SIZE_MAX

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
    const cmeta_component_desc *component;
    void *provider_context;

    /*
     * Deployment configuration is borrowed until create() returns. A provider
     * that needs it afterwards must copy/retain it under its own semantics.
     */
    const cmeta_data_desc *config_data;
    const void *config_value;

    /*
     * Provider-authorized Interface projection. Required when component
     * publishes at least one Interface; borrowed for the context lifetime.
     */
    const cmeta_object_interface_provider *interfaces;

    salts_component_create_fn create;
    salts_component_activate_fn activate;
    salts_component_deactivate_fn deactivate;
} salts_component_provider_binding;

struct salts_component_dependency {
    size_t consumer_index;
    size_t provider_index;
    const cmeta_interface_desc *interface_desc;
    const cmeta_component_desc *provider_component;
    const cmeta_object_ref *provider_instance;
    const cmeta_object_interface_provider *provider_interfaces;
};

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
    const salts_component_provider_binding *providers;
    size_t provider_count;

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
    const salts_component_provider_binding *providers,
    size_t provider_count,
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

/* Factory helper for one already-resolved dependency slice. */
salts_component_status salts_component_dependency_find(
    const salts_component_dependency *dependencies,
    size_t dependency_count,
    const cmeta_interface_desc *interface_desc,
    const salts_component_dependency **out_dependency);

const salts_component_failure *salts_component_context_failure(
    const salts_component_context *context);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_COMPONENT_H */
