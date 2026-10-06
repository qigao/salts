#include <salts/plugin.h>

#include <type_traits>

static_assert(std::is_standard_layout<cmeta_plugin_version>::value,
              "plugin version must be a C-compatible value");
static_assert(std::is_standard_layout<cmeta_plugin_export>::value,
              "plugin export must be a C-compatible ABI row");
static_assert(std::is_standard_layout<cmeta_plugin_interface_export>::value,
              "plugin interface payload must be C-compatible");
static_assert(std::is_standard_layout<cmeta_plugin_function_export>::value,
              "plugin function payload must be C-compatible");
static_assert(std::is_standard_layout<cmeta_plugin_export_value>::value,
              "plugin capability union must be C-compatible");
static_assert(std::is_same<
                  decltype(cmeta_plugin_export{}.value.function.desc),
                  const cmeta_function_desc *>::value,
              "Function export must publish canonical FunctionMeta");
static_assert(std::is_same<
                  decltype(cmeta_plugin_export{}.value.function.abi),
                  const cmeta_function_abi_desc *>::value,
              "Function export must publish canonical FunctionAbi");
static_assert(std::is_standard_layout<cmeta_plugin_manifest>::value,
              "plugin manifest must be a C-compatible ABI row");
static_assert(std::is_same<
                  decltype(cmeta_plugin_export{}.value.interface.value),
                  void *>::value,
              "interface export must expose a mutable borrowed handle");
static_assert(std::is_standard_layout<cmeta_plugin_ref>::value,
              "plugin ref must remain a C-compatible value");
static_assert(std::is_standard_layout<cmeta_plugin_registry_config>::value,
              "registry config must remain a C-compatible value");
static_assert(std::is_standard_layout<cmeta_plugin_registry>::value,
              "registry handle must remain a C-compatible value");
static_assert(std::is_standard_layout<cmeta_plugin_lease>::value,
              "plugin lease must remain a C-compatible value");
static_assert(std::is_standard_layout<cmeta_plugin_lifecycle_info>::value,
              "lifecycle info must remain a C-compatible value");

SALTS_PLUGIN_QUERY_EXPORT
const cmeta_plugin_manifest *SALTS_PLUGIN_CALL
cmeta_plugin_query(uint32_t host_abi) {
    (void)host_abi;
    return nullptr;
}

int main() {
    cmeta_plugin_manifest manifest{};
    cmeta_plugin_registry registry{};
    cmeta_plugin_registry_config config{1u};
    cmeta_plugin_ref ref{};
    cmeta_plugin_lease lease{};
    cmeta_plugin_lifecycle_info lifecycle{};
    cmeta_plugin_query_fn query = &cmeta_plugin_query;
    const cmeta_plugin_export *entry = nullptr;

    (void)query;
    (void)registry;
    (void)config;
    (void)ref;
    (void)lease;
    (void)lifecycle;
    return cmeta_plugin_manifest_find_export(
               &manifest, "missing", &entry) == SALTS_PLUGIN_INVALID_MANIFEST
               ? 0
               : 1;
}
