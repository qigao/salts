#include <salts/plugin.h>

static bool bounded_string_valid(const char *value, size_t max_bytes) {
    size_t index;
    if (value == NULL || max_bytes == 0u) return false;
    for (index = 0u; index <= max_bytes; ++index) {
        if (value[index] == '\0') return index != 0u;
    }
    return false;
}

static bool bounded_string_equal(const char *left, const char *right,
                                 size_t max_bytes) {
    size_t index;
    if (left == NULL || right == NULL || max_bytes == 0u) return false;
    for (index = 0u; index <= max_bytes; ++index) {
        if (left[index] != right[index]) return false;
        if (left[index] == '\0') return index != 0u;
    }
    return false;
}

static bool function_abi_complete(const cmeta_function_abi_desc *abi) {
    size_t index;

    if (!cmeta_function_abi_desc_valid(abi) ||
        abi->return_carrier == CMETA_ABI_UNSPECIFIED)
        return false;

    for (index = 0u; index < abi->param_count; ++index) {
        if (abi->param_carriers[index] == CMETA_ABI_UNSPECIFIED)
            return false;
    }

    return true;
}

static salts_plugin_status validate_export(const salts_plugin_export *entry) {
    if (entry == NULL || entry->struct_size != SALTS_PLUGIN_EXPORT_SIZE)
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (!bounded_string_valid(entry->export_id, SALTS_PLUGIN_EXPORT_ID_MAX) ||
        !bounded_string_valid(entry->contract_id,
                              SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        entry->contract_version == 0u)
        return SALTS_PLUGIN_INVALID_MANIFEST;

    switch (entry->kind) {
    case SALTS_PLUGIN_EXPORT_INTERFACE:
        if (entry->value.interface.value == NULL ||
            !cmeta_interface_desc_valid(entry->value.interface.desc))
            return SALTS_PLUGIN_INVALID_MANIFEST;
        break;

    case SALTS_PLUGIN_EXPORT_FUNCTION:
        if (!cmeta_function_desc_valid(entry->value.function.desc) ||
            !function_abi_complete(entry->value.function.abi) ||
            entry->value.function.abi->function != entry->value.function.desc ||
            entry->value.function.invoke == NULL)
            return SALTS_PLUGIN_INVALID_MANIFEST;
        break;

    default:
        return SALTS_PLUGIN_INVALID_MANIFEST;
    }

    return SALTS_PLUGIN_OK;
}

bool salts_plugin_export_has_capabilities(const salts_plugin_export *entry,
                                          uint64_t required) {
    return entry != NULL && (entry->capabilities & required) == required;
}

static bool contract_key_equal(const salts_plugin_export *entry,
                               const char *contract_id,
                               uint32_t contract_version) {
    return entry != NULL && contract_version != 0u &&
           bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX) &&
           entry->contract_version == contract_version &&
           bounded_string_equal(entry->contract_id, contract_id,
                                SALTS_PLUGIN_CONTRACT_ID_MAX);
}

salts_plugin_status salts_plugin_export_require_interface(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities,
    const cmeta_interface_desc *expected_interface) {
    salts_plugin_status status = validate_export(entry);

    if (status != SALTS_PLUGIN_OK) return status;
    if (contract_version == 0u ||
        !bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX) ||
        !cmeta_interface_desc_valid(expected_interface))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (entry->kind != SALTS_PLUGIN_EXPORT_INTERFACE ||
        !contract_key_equal(entry, contract_id, contract_version) ||
        !salts_plugin_export_has_capabilities(entry, required_capabilities) ||
        !cmeta_interface_desc_equal(entry->value.interface.desc,
                                           expected_interface))
        return SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_export_require_function(
    const salts_plugin_export *entry,
    const char *contract_id,
    uint32_t contract_version,
    uint64_t required_capabilities) {
    salts_plugin_status status = validate_export(entry);

    if (status != SALTS_PLUGIN_OK) return status;
    if (contract_version == 0u ||
        !bounded_string_valid(contract_id, SALTS_PLUGIN_CONTRACT_ID_MAX))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    if (entry->kind != SALTS_PLUGIN_EXPORT_FUNCTION ||
        !contract_key_equal(entry, contract_id, contract_version) ||
        !salts_plugin_export_has_capabilities(entry, required_capabilities))
        return SALTS_PLUGIN_INCOMPATIBLE_CONTRACT;

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_manifest_validate(
    const salts_plugin_manifest *manifest) {
    size_t index;
    size_t other;
    bool has_lifecycle;

    if (manifest == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    if (manifest->struct_size != SALTS_PLUGIN_MANIFEST_SIZE)
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (manifest->abi_version != SALTS_PLUGIN_ABI_VERSION)
        return SALTS_PLUGIN_UNSUPPORTED_ABI;
    if (!bounded_string_valid(manifest->plugin_id, SALTS_PLUGIN_ID_MAX))
        return SALTS_PLUGIN_INVALID_MANIFEST;
    if (manifest->export_count > SALTS_PLUGIN_MAX_EXPORTS)
        return SALTS_PLUGIN_CAPACITY_EXCEEDED;
    if (manifest->export_count != 0u && manifest->exports == NULL)
        return SALTS_PLUGIN_INVALID_MANIFEST;

    has_lifecycle = manifest->start != NULL ||
                    manifest->request_stop != NULL ||
                    manifest->is_quiescent != NULL ||
                    manifest->destroy != NULL;
    if (has_lifecycle) {
        if (manifest->self == NULL ||
            manifest->start == NULL ||
            manifest->request_stop == NULL ||
            manifest->is_quiescent == NULL ||
            manifest->destroy == NULL)
            return SALTS_PLUGIN_INVALID_MANIFEST;
    } else if (manifest->self != NULL) {
        return SALTS_PLUGIN_INVALID_MANIFEST;
    }

    for (index = 0u; index < manifest->export_count; ++index) {
        salts_plugin_status status = validate_export(&manifest->exports[index]);
        if (status != SALTS_PLUGIN_OK) return status;

        for (other = 0u; other < index; ++other) {
            if (bounded_string_equal(manifest->exports[index].export_id,
                                     manifest->exports[other].export_id,
                                     SALTS_PLUGIN_EXPORT_ID_MAX))
                return SALTS_PLUGIN_DUPLICATE_EXPORT;
        }
    }

    return SALTS_PLUGIN_OK;
}

salts_plugin_status salts_plugin_manifest_find_export(
    const salts_plugin_manifest *manifest,
    const char *export_id,
    const salts_plugin_export **out_export) {
    size_t index;
    salts_plugin_status status;

    if (out_export == NULL)
        return SALTS_PLUGIN_INVALID_ARGUMENT;
    *out_export = NULL;

    if (!bounded_string_valid(export_id, SALTS_PLUGIN_EXPORT_ID_MAX))
        return SALTS_PLUGIN_INVALID_ARGUMENT;

    status = salts_plugin_manifest_validate(manifest);
    if (status != SALTS_PLUGIN_OK) return status;

    for (index = 0u; index < manifest->export_count; ++index) {
        if (bounded_string_equal(manifest->exports[index].export_id,
                                 export_id, SALTS_PLUGIN_EXPORT_ID_MAX)) {
            *out_export = &manifest->exports[index];
            return SALTS_PLUGIN_OK;
        }
    }

    return SALTS_PLUGIN_UNKNOWN_EXPORT;
}

const char *salts_plugin_status_string(salts_plugin_status status) {
    switch (status) {
    case SALTS_PLUGIN_OK: return "ok";
    case SALTS_PLUGIN_INVALID_ARGUMENT: return "invalid_argument";
    case SALTS_PLUGIN_INVALID_MANIFEST: return "invalid_manifest";
    case SALTS_PLUGIN_UNSUPPORTED_ABI: return "unsupported_abi";
    case SALTS_PLUGIN_DUPLICATE_PLUGIN_ID: return "duplicate_plugin_id";
    case SALTS_PLUGIN_DUPLICATE_EXPORT: return "duplicate_export";
    case SALTS_PLUGIN_UNKNOWN_EXPORT: return "unknown_export";
    case SALTS_PLUGIN_INCOMPATIBLE_CONTRACT: return "incompatible_contract";
    case SALTS_PLUGIN_CAPACITY_EXCEEDED: return "capacity_exceeded";
    case SALTS_PLUGIN_ALLOCATION_FAILED: return "allocation_failed";
    case SALTS_PLUGIN_LOAD_FAILED: return "load_failed";
    case SALTS_PLUGIN_QUERY_MISSING: return "query_missing";
    case SALTS_PLUGIN_QUERY_REJECTED: return "query_rejected";
    case SALTS_PLUGIN_UNKNOWN_PLUGIN: return "unknown_plugin";
    case SALTS_PLUGIN_STALE: return "stale";
    case SALTS_PLUGIN_UNLOAD_FAILED: return "unload_failed";
    case SALTS_PLUGIN_ALREADY: return "already";
    case SALTS_PLUGIN_BUSY: return "busy";
    case SALTS_PLUGIN_INVALID_STATE: return "invalid_state";
    default: return "unknown";
    }
}
