#ifndef SALTS_COMPONENT_PLUGIN_H
#define SALTS_COMPONENT_PLUGIN_H

#include <salts/component_plugin_abi.h>
#include <salts/plugin.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SALTS_COMPONENT_PLUGIN_INDEX_NONE SIZE_MAX

typedef enum salts_component_plugin_status {
    SALTS_COMPONENT_PLUGIN_OK = 0,
    SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT,
    SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED,
    SALTS_COMPONENT_PLUGIN_PLUGIN_ERROR,
    SALTS_COMPONENT_PLUGIN_PROVIDER_ERROR,
    SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR,
    SALTS_COMPONENT_PLUGIN_BUSY,
    SALTS_COMPONENT_PLUGIN_INVALID_STATE
} salts_component_plugin_status;

typedef enum salts_component_plugin_generation_state {
    SALTS_COMPONENT_PLUGIN_GENERATION_ZERO = 0,
    SALTS_COMPONENT_PLUGIN_GENERATION_BUILT,
    SALTS_COMPONENT_PLUGIN_GENERATION_PUBLISHED,
    SALTS_COMPONENT_PLUGIN_GENERATION_DRAINING,
    SALTS_COMPONENT_PLUGIN_GENERATION_STOPPING,
    SALTS_COMPONENT_PLUGIN_GENERATION_FAILED,
    SALTS_COMPONENT_PLUGIN_GENERATION_DRAINED
} salts_component_plugin_generation_state;

typedef struct salts_component_plugin_source {
    cmeta_plugin_ref plugin;
    const char *export_id;
    const cmeta_data_desc *config_data;
    const void *config_value;
} salts_component_plugin_source;

typedef struct salts_component_plugin_module {
    cmeta_plugin_ref plugin;
    cmeta_plugin_lease lease;
    const cmeta_plugin_manifest *manifest;
} salts_component_plugin_module;

typedef struct salts_component_plugin_generation_storage {
    salts_component_deployment *deployments;
    size_t deployment_capacity;

    salts_component_instance *instances;
    size_t instance_capacity;

    salts_component_dependency *dependencies;
    size_t dependency_capacity;

    size_t *activation_order;
    size_t activation_capacity;

    salts_component_plugin_module *modules;
    size_t module_capacity;
} salts_component_plugin_generation_storage;

typedef struct salts_component_plugin_failure {
    size_t source_index;
    cmeta_plugin_status plugin_status;
    salts_component_status component_status;
} salts_component_plugin_failure;

typedef struct salts_component_plugin_runtime salts_component_plugin_runtime;

typedef struct salts_component_plugin_generation {
    uint64_t id;
    cmeta_plugin_registry *registry;
    salts_component_context components;

    salts_component_plugin_generation_storage storage;
    size_t deployment_count;
    size_t module_count;

    salts_component_plugin_runtime *runtime_owner;
    size_t active_scopes;

    salts_component_plugin_generation_state state;
    salts_component_plugin_failure failure;
} salts_component_plugin_generation;

typedef struct salts_component_plugin_scope {
    salts_component_plugin_runtime *runtime;
    salts_component_plugin_generation *generation;
    uint64_t generation_id;
    bool live;
} salts_component_plugin_scope;

struct salts_component_plugin_runtime {
    void *lock;
    salts_component_plugin_generation *current;
    size_t active_scopes;
    size_t attached_generations;
    bool initialized;
};

const char *salts_component_plugin_status_string(
    salts_component_plugin_status status);

salts_component_plugin_status salts_component_plugin_generation_build(
    salts_component_plugin_generation *generation,
    uint64_t generation_id,
    cmeta_plugin_registry *registry,
    const salts_component_plugin_generation_storage *storage,
    const salts_component_deployment *static_deployments,
    size_t static_deployment_count,
    const salts_component_plugin_source *dynamic_sources,
    size_t dynamic_source_count,
    const salts_component_selection *selections,
    size_t selection_count);

salts_component_plugin_status salts_component_plugin_generation_discard(
    salts_component_plugin_generation *generation);

salts_component_plugin_status salts_component_plugin_runtime_init(
    salts_component_plugin_runtime *runtime);

salts_component_plugin_status salts_component_plugin_runtime_destroy(
    salts_component_plugin_runtime *runtime);

salts_component_plugin_status salts_component_plugin_runtime_publish(
    salts_component_plugin_runtime *runtime,
    salts_component_plugin_generation *generation,
    salts_component_plugin_generation **out_previous);

salts_component_plugin_status salts_component_plugin_runtime_close(
    salts_component_plugin_runtime *runtime,
    salts_component_plugin_generation **out_previous);

salts_component_plugin_status salts_component_plugin_scope_acquire(
    salts_component_plugin_runtime *runtime,
    salts_component_plugin_scope *scope);

salts_component_plugin_status salts_component_plugin_scope_release(
    salts_component_plugin_scope *scope);

uint64_t salts_component_plugin_scope_generation_id(
    const salts_component_plugin_scope *scope);

salts_component_plugin_status salts_component_plugin_scope_find_service(
    const salts_component_plugin_scope *scope,
    const cmeta_interface_desc *interface_desc,
    salts_component_service *out_service);

salts_component_plugin_status salts_component_plugin_scope_find_service_from(
    const salts_component_plugin_scope *scope,
    const char *provider_component_id,
    const cmeta_interface_desc *interface_desc,
    salts_component_service *out_service);

salts_component_plugin_status salts_component_plugin_generation_drain(
    salts_component_plugin_runtime *runtime,
    salts_component_plugin_generation *generation);

const salts_component_plugin_failure *salts_component_plugin_generation_failure(
    const salts_component_plugin_generation *generation);

#ifdef __cplusplus
}
#endif

#endif /* SALTS_COMPONENT_PLUGIN_H */
