#include <salts/plugin.h>
#include <tinytest.h>

#ifndef PLUGIN_PRIVATE_DEPENDENCY_PATH
#error "PLUGIN_PRIVATE_DEPENDENCY_PATH is required"
#endif

spec("Salts Plugin Windows private dependency loading") {
    it("loads dependent DLLs from the explicit plugin module directory") {
        salts_plugin_registry registry = {0};
        salts_plugin_registry_config config = {1u};
        salts_plugin_ref ref = {0};
        salts_plugin_ref found = {0};

        check_equal(salts_plugin_registry_init(&registry, &config),
                    SALTS_PLUGIN_OK);
        check_equal(salts_plugin_registry_load(
                        &registry, PLUGIN_PRIVATE_DEPENDENCY_PATH, &ref),
                    SALTS_PLUGIN_OK);
        check_true(salts_plugin_ref_valid(ref));
        check_equal(salts_plugin_registry_find(
                        &registry, "test.loader.private_dependency", &found),
                    SALTS_PLUGIN_OK);
        check_equal(found.slot, ref.slot);
        check_equal(found.generation, ref.generation);
        check_equal(salts_plugin_registry_destroy(&registry),
                    SALTS_PLUGIN_OK);
    }
}
