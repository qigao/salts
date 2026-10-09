#include <salts/component_plugin.h>

#include <stddef.h>
#include <stdint.h>

/* The installed 2.3 SDK must expose the non-copyable Scope sentinel from
 * the breaking 2.2 -> 2.3 native ABI transition; reject older layouts. */
_Static_assert(
    offsetof(salts_component_plugin_scope, owner_address) >
        offsetof(salts_component_plugin_scope, generation_id),
    "ComponentPlugin scope must expose its post-3.0 owner-address sentinel");
_Static_assert(
    offsetof(salts_component_plugin_scope, live) >
        offsetof(salts_component_plugin_scope, owner_address),
    "scope admission status must follow the owner-address sentinel");

int main(void) {
    salts_component_plugin_scope scope = SALTS_COMPONENT_PLUGIN_SCOPE_INIT;
    salts_component_plugin_runtime runtime = SALTS_COMPONENT_PLUGIN_RUNTIME_INIT;

    if (scope.runtime != NULL || scope.generation != NULL ||
        scope.generation_id != UINT64_C(0) ||
        scope.owner_address != NULL || scope.live)
        return 1;

    if (salts_component_plugin_scope_generation_id(&scope) != UINT64_C(0))
        return 2;

    if (salts_component_plugin_runtime_init(&runtime) !=
        SALTS_COMPONENT_PLUGIN_OK)
        return 3;

    /* A second init may not overwrite the live mutex/lifecycle authority. */
    if (salts_component_plugin_runtime_init(&runtime) !=
        SALTS_COMPONENT_PLUGIN_INVALID_ARGUMENT)
        return 4;

    if (salts_component_plugin_runtime_destroy(&runtime) !=
        SALTS_COMPONENT_PLUGIN_OK)
        return 5;

    if (runtime.initialized || runtime.lock != NULL ||
        runtime.current != NULL || runtime.active_scopes != 0u ||
        runtime.attached_generations != 0u)
        return 6;

    return 0;
}
