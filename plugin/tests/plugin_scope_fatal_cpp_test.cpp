#include <salts/plugin_scope.h>
#include <cstdlib>
#include <exception>

int main() {
    cmeta_plugin_registry registry{};
    cmeta_plugin_ref ref{};
    const cmeta_plugin_registry_config config{1u};
    if (cmeta_plugin_registry_init(&registry, &config) != CMETA_PLUGIN_OK ||
        cmeta_plugin_registry_load(&registry, PLUGIN_SCOPE_PATH, &ref) != CMETA_PLUGIN_OK ||
        cmeta_plugin_registry_start(&registry, ref) != CMETA_PLUGIN_OK)
        return EXIT_FAILURE;
    {
        salts::plugin_lease_scope owner;
        if (owner.acquire(registry, ref) != CMETA_PLUGIN_OK) return EXIT_FAILURE;
        std::set_terminate([] { std::_Exit(EXIT_SUCCESS); });
        /* Keep the owned lease, but violate the stable registry contract. The
         * destructor must terminate instead of losing an unreported error. */
        registry.impl = nullptr;
    }
    return EXIT_FAILURE;
}
