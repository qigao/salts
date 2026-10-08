#include <salts/component_plugin.h>
#include <salts/thread.h>

#include <string.h>

/* Keep platform resource admission separate from graph/Plugin dependencies so
 * its failure path can be exercised with a test-owned platform adapter. */
salts_component_plugin_status salts_component_plugin_runtime_init(
    salts_component_plugin_runtime *runtime) {
    if (runtime == NULL || runtime->initialized)
        return SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT;

    memset(runtime, 0, sizeof(*runtime));
    cmeta_mutex_init((cmeta_mutex_t *)&runtime->lock);
    if (runtime->lock == NULL)
        return SALTS_COMPONENT_PLUGIN_RESOURCE_ERROR;
    runtime->initialized = true;
    return SALTS_COMPONENT_PLUGIN_OK;
}

salts_component_plugin_status salts_component_plugin_runtime_destroy(
    salts_component_plugin_runtime *runtime) {
    if (runtime == NULL || !runtime->initialized)
        return SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT;

    cmeta_mutex_lock((cmeta_mutex_t *)&runtime->lock);
    if (runtime->current != NULL ||
        runtime->active_scopes != 0u ||
        runtime->attached_generations != 0u) {
        cmeta_mutex_unlock((cmeta_mutex_t *)&runtime->lock);
        return SALTS_COMPONENT_PLUGIN_BUSY;
    }
    cmeta_mutex_unlock((cmeta_mutex_t *)&runtime->lock);

    cmeta_mutex_destroy((cmeta_mutex_t *)&runtime->lock);
    memset(runtime, 0, sizeof(*runtime));
    return SALTS_COMPONENT_PLUGIN_OK;
}
