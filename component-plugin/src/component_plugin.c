#include <salts/component_plugin.h>

#include <string.h>

static salts_component_plugin_failure component_plugin_failure_none(void) {
    salts_component_plugin_failure failure;
    failure.source_index = SALTS_COMPONENT_PLUGIN_INDEX_NONE;
    failure.plugin_status = CMETA_PLUGIN_OK;
    failure.component_status = SALTS_COMPONENT_OK;
    return failure;
}

static bool plugin_ref_equal(cmeta_plugin_ref a, cmeta_plugin_ref b) {
    return a.slot == b.slot && a.generation == b.generation;
}

static salts_component_plugin_status generation_fail_plugin(
    salts_component_plugin_generation *generation,
    size_t source_index,
    cmeta_plugin_status status) {
    generation->failure.source_index = source_index;
    generation->failure.plugin_status = status;
    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_FAILED;
    return SALTS_COMPONENT_PLUGIN_PLUGIN_ERROR;
}

static salts_component_plugin_status generation_fail_component(
    salts_component_plugin_generation *generation,
    salts_component_status status) {
    generation->failure.component_status = status;
    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_FAILED;
    return SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR;
}

static salts_component_plugin_status generation_fail_provider(
    salts_component_plugin_generation *generation,
    size_t source_index) {
    generation->failure.source_index = source_index;
    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_FAILED;
    return SALTS_COMPONENT_PLUGIN_PROVIDER_ERROR;
}

static cmeta_plugin_status release_modules(
    salts_component_plugin_generation *generation) {
    cmeta_plugin_status first = CMETA_PLUGIN_OK;
    size_t i = generation->module_count;

    while (i != 0u) {
        cmeta_plugin_status status;
        --i;
        if (!cmeta_plugin_lease_valid(generation->storage.modules[i].lease))
            continue;
        status = cmeta_plugin_registry_release(
            generation->registry,
            &generation->storage.modules[i].lease);
        if (first == CMETA_PLUGIN_OK && status != CMETA_PLUGIN_OK)
            first = status;
        if (status == CMETA_PLUGIN_OK) {
            generation->storage.modules[i].manifest = NULL;
            generation->storage.modules[i].plugin =
                (cmeta_plugin_ref){0u, 0u};
        }
    }

    if (first == CMETA_PLUGIN_OK)
        generation->module_count = 0u;
    return first;
}

static salts_component_plugin_module *find_module(
    salts_component_plugin_generation *generation,
    cmeta_plugin_ref ref) {
    size_t i;
    for (i = 0u; i < generation->module_count; ++i)
        if (plugin_ref_equal(generation->storage.modules[i].plugin, ref))
            return &generation->storage.modules[i];
    return NULL;
}

static salts_component_plugin_status acquire_module(
    salts_component_plugin_generation *generation,
    size_t source_index,
    cmeta_plugin_ref ref,
    salts_component_plugin_module **out_module) {
    salts_component_plugin_module *module;
    cmeta_plugin_status status;

    module = find_module(generation, ref);
    if (module != NULL) {
        *out_module = module;
        return SALTS_COMPONENT_PLUGIN_OK;
    }

    if (generation->module_count >= generation->storage.module_capacity)
        return SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED;

    module = &generation->storage.modules[generation->module_count];
    memset(module, 0, sizeof(*module));
    module->plugin = ref;

    status = cmeta_plugin_registry_acquire(
        generation->registry, ref, &module->lease, &module->manifest);
    if (status != CMETA_PLUGIN_OK) {
        memset(module, 0, sizeof(*module));
        return generation_fail_plugin(generation, source_index, status);
    }

    ++generation->module_count;
    *out_module = module;
    return SALTS_COMPONENT_PLUGIN_OK;
}

static salts_component_plugin_status admit_dynamic_source(
    salts_component_plugin_generation *generation,
    size_t source_index,
    const salts_component_plugin_source *source,
    salts_component_deployment *out_deployment) {
    salts_component_plugin_module *module = NULL;
    const cmeta_plugin_export *entry = NULL;
    salts_component_provider *provider;
    const salts_component_provider_binding *binding;
    cmeta_plugin_status plugin_status;
    salts_component_plugin_status status;

    if (!cmeta_plugin_ref_valid(source->plugin) ||
        source->export_id == NULL || source->export_id[0] == '\0')
        return generation_fail_provider(generation, source_index);

    status = acquire_module(
        generation, source_index, source->plugin, &module);
    if (status != SALTS_COMPONENT_PLUGIN_OK)
        return status;

    plugin_status = cmeta_plugin_manifest_find_export(
        module->manifest, source->export_id, &entry);
    if (plugin_status != CMETA_PLUGIN_OK)
        return generation_fail_plugin(
            generation, source_index, plugin_status);

    plugin_status = cmeta_plugin_export_require_interface(
        entry,
        SALTS_COMPONENT_PROVIDER_CONTRACT_ID,
        SALTS_COMPONENT_PROVIDER_CONTRACT_VERSION,
        0u,
        salts_component_provider_interface());
    if (plugin_status != CMETA_PLUGIN_OK)
        return generation_fail_plugin(
            generation, source_index, plugin_status);

    provider =
        (salts_component_provider *)entry->value.interface.value;
    if (!salts_component_provider_valid(provider))
        return generation_fail_provider(generation, source_index);

    binding = salts_component_provider_get_binding(provider);
    if (!salts_component_provider_binding_valid(binding))
        return generation_fail_provider(generation, source_index);

    out_deployment->provider = binding;
    out_deployment->config_data = source->config_data;
    out_deployment->config_value = source->config_value;
    return SALTS_COMPONENT_PLUGIN_OK;
}

static void generation_clear(
    salts_component_plugin_generation *generation) {
    if (generation == NULL)
        return;
    generation->registry = NULL;
    generation->deployment_count = 0u;
    generation->module_count = 0u;
}

const char *salts_component_plugin_status_string(
    salts_component_plugin_status status) {
    switch (status) {
    case SALTS_COMPONENT_PLUGIN_OK: return "ok";
    case SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT: return "invalid argument";
    case SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED: return "capacity exceeded";
    case SALTS_COMPONENT_PLUGIN_PLUGIN_ERROR: return "plugin error";
    case SALTS_COMPONENT_PLUGIN_PROVIDER_ERROR: return "provider error";
    case SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR: return "component error";
    case SALTS_COMPONENT_PLUGIN_INVALID_STATE: return "invalid state";
    }
    return "unknown component-plugin status";
}

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
    size_t selection_count) {
    size_t total;
    size_t i;
    salts_component_status component_status;

    if (generation == NULL || generation_id == 0u || registry == NULL ||
        storage == NULL ||
        (static_deployment_count != 0u && static_deployments == NULL) ||
        (dynamic_source_count != 0u && dynamic_sources == NULL) ||
        (selection_count != 0u && selections == NULL))
        return SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT;

    if (generation->state != SALTS_COMPONENT_PLUGIN_GENERATION_ZERO &&
        generation->state != SALTS_COMPONENT_PLUGIN_GENERATION_DRAINED)
        return SALTS_COMPONENT_PLUGIN_INVALID_STATE;

    if (static_deployment_count > SIZE_MAX - dynamic_source_count)
        return SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED;
    total = static_deployment_count + dynamic_source_count;

    if (total > storage->deployment_capacity ||
        total > storage->instance_capacity ||
        total > storage->activation_capacity ||
        (total != 0u &&
         (storage->deployments == NULL || storage->instances == NULL ||
          storage->activation_order == NULL)) ||
        (storage->dependency_capacity != 0u && storage->dependencies == NULL) ||
        (storage->module_capacity != 0u && storage->modules == NULL))
        return SALTS_COMPONENT_PLUGIN_CAPACITY_EXCEEDED;

    memset(&generation->components, 0, sizeof(generation->components));
    generation->id = generation_id;
    generation->registry = registry;
    generation->storage = *storage;
    generation->deployment_count = total;
    generation->module_count = 0u;
    generation->failure = component_plugin_failure_none();
    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_ZERO;

    for (i = 0u; i < static_deployment_count; ++i)
        storage->deployments[i] = static_deployments[i];

    for (i = 0u; i < dynamic_source_count; ++i) {
        salts_component_plugin_status status = admit_dynamic_source(
            generation, i, &dynamic_sources[i],
            &storage->deployments[static_deployment_count + i]);
        if (status != SALTS_COMPONENT_PLUGIN_OK) {
            cmeta_plugin_status release_status = release_modules(generation);
            if (generation->failure.plugin_status == CMETA_PLUGIN_OK &&
                release_status != CMETA_PLUGIN_OK)
                generation->failure.plugin_status = release_status;
            return status;
        }
    }

    component_status = salts_component_context_init(
        &generation->components,
        storage->deployments, total,
        selections, selection_count,
        storage->instances, storage->instance_capacity,
        storage->dependencies, storage->dependency_capacity,
        storage->activation_order, storage->activation_capacity);
    if (component_status == SALTS_COMPONENT_OK)
        component_status =
            salts_component_context_resolve(&generation->components);
    if (component_status == SALTS_COMPONENT_OK)
        component_status =
            salts_component_context_start(&generation->components);

    if (component_status != SALTS_COMPONENT_OK) {
        cmeta_plugin_status release_status;
        generation_fail_component(generation, component_status);
        release_status = release_modules(generation);
        if (release_status != CMETA_PLUGIN_OK)
            generation->failure.plugin_status = release_status;
        return SALTS_COMPONENT_PLUGIN_COMPONENT_ERROR;
    }

    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_BUILT;
    return SALTS_COMPONENT_PLUGIN_OK;
}

salts_component_plugin_status salts_component_plugin_generation_discard(
    salts_component_plugin_generation *generation) {
    cmeta_plugin_status plugin_status;

    if (generation == NULL)
        return SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT;
    if (generation->state != SALTS_COMPONENT_PLUGIN_GENERATION_BUILT)
        return SALTS_COMPONENT_PLUGIN_INVALID_STATE;

    if (salts_component_context_stop(&generation->components) !=
        SALTS_COMPONENT_OK)
        return generation_fail_component(
            generation, SALTS_COMPONENT_INVALID_STATE);

    plugin_status = release_modules(generation);
    if (plugin_status != CMETA_PLUGIN_OK)
        return generation_fail_plugin(
            generation, SALTS_COMPONENT_PLUGIN_INDEX_NONE, plugin_status);

    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_DRAINED;
    generation_clear(generation);
    generation->state = SALTS_COMPONENT_PLUGIN_GENERATION_DRAINED;
    return SALTS_COMPONENT_PLUGIN_OK;
}

const salts_component_plugin_failure *salts_component_plugin_generation_failure(
    const salts_component_plugin_generation *generation) {
    return generation == NULL ? NULL : &generation->failure;
}
