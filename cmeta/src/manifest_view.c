#include <cmeta/manifest_view.h>

typedef struct manifest_budget {
    const cmeta_manifest_limits *limits;
    size_t nodes;
} manifest_budget;

static bool limits_valid(const cmeta_manifest_limits *limits) {
    return limits != NULL && limits->max_items != 0u &&
        limits->max_identity_nodes != 0u && limits->max_identity_depth != 0u &&
        limits->max_identity_depth <= CMETA_MANIFEST_DEPTH_LIMIT;
}

static cmeta_status manifest_entry(const cmeta_manifest *manifest, size_t index,
    const cmeta_manifest_limits *limits, cmeta_manifest_kind kind,
    const cmeta_manifest_entry **out) {
    if (manifest == NULL || !limits_valid(limits) ||
        manifest->name == NULL || manifest->name[0] == '\0')
        return CMETA_INVALID_ARGUMENT;
    if (manifest->format_version != CMETA_MANIFEST_FORMAT_VERSION)
        return CMETA_TYPE_MISMATCH;
    if (manifest->count > limits->max_items) return CMETA_CAPACITY_EXCEEDED;
    if (manifest->entries == NULL || index >= manifest->count)
        return CMETA_INVALID_ARGUMENT;
    const cmeta_manifest_entry *entry = &manifest->entries[index];
    if (entry->kind != kind) return CMETA_TYPE_MISMATCH;
    if (entry->name == NULL || entry->name[0] == '\0' || entry->descriptor == NULL)
        return CMETA_INVALID_ARGUMENT;
    *out = entry;
    return CMETA_OK;
}

/* Check resource bounds before the existing recursive semantic validator.
 * Shared nodes are visited per edge so work, rather than allocation, is bounded. */
static cmeta_status identity_budget(const cmeta_type_identity *id, size_t depth,
    manifest_budget *budget) {
    if (id == NULL) return CMETA_OK;
    if (depth >= budget->limits->max_identity_depth || budget->nodes == 0u)
        return CMETA_CAPACITY_EXCEEDED;
    --budget->nodes;
    if (id->base != NULL) {
        cmeta_status status = identity_budget(id->base, depth + 1u, budget);
        if (status != CMETA_OK) return status;
    }
    if (id->arity > budget->limits->max_items) return CMETA_CAPACITY_EXCEEDED;
    if (id->arity != 0u && id->args == NULL) return CMETA_INVALID_ARGUMENT;
    for (size_t i = 0u; i < id->arity; ++i) {
        cmeta_status status = identity_budget(id->args[i], depth + 1u, budget);
        if (status != CMETA_OK) return status;
    }
    return CMETA_OK;
}

static cmeta_status type_valid(const cmeta_type_desc *type, manifest_budget *budget) {
    if (type == NULL) return CMETA_INVALID_ARGUMENT;
    cmeta_status status = identity_budget(type->identity, 0u, budget);
    if (status != CMETA_OK) return status;
    if (type->pointee != NULL) {
        status = identity_budget(type->pointee->identity, 0u, budget);
        if (status != CMETA_OK) return status;
    }
    return cmeta_type_desc_valid(type) ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

static cmeta_status function_valid(const cmeta_function_abi_desc *abi,
    manifest_budget *budget) {
    if (abi == NULL || abi->size < sizeof(*abi) || abi->function == NULL ||
        abi->function->size < sizeof(*abi->function)) return CMETA_INVALID_ARGUMENT;
    const cmeta_function_desc *function = abi->function;
    if (abi->param_count > budget->limits->max_items ||
        function->param_count > budget->limits->max_items)
        return CMETA_CAPACITY_EXCEEDED;
    if (function->param_count != 0u && function->params == NULL)
        return CMETA_INVALID_ARGUMENT;
    cmeta_status status = type_valid(function->return_type, budget);
    if (status != CMETA_OK) return status;
    for (size_t i = 0u; i < function->param_count; ++i) {
        status = type_valid(function->params[i].type, budget);
        if (status != CMETA_OK) return status;
    }
    return cmeta_function_abi_desc_valid(abi) ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

static cmeta_status struct_valid(const cmeta_struct_desc *desc,
    manifest_budget *budget) {
    if (desc->name == NULL || desc->name[0] == '\0' || desc->size == 0u ||
        desc->align == 0u || (desc->field_count != 0u && desc->fields == NULL))
        return CMETA_INVALID_ARGUMENT;
    if (desc->field_count > budget->limits->max_items) return CMETA_CAPACITY_EXCEEDED;
    for (size_t i = 0u; i < desc->field_count; ++i) {
        const cmeta_field_desc *field = &desc->fields[i];
        if (field->name == NULL || field->name[0] == '\0' || field->align == 0u ||
            (field->offset != CMETA_FIELD_DYNAMIC_OFFSET &&
             (field->offset > desc->size || field->size > desc->size - field->offset)))
            return CMETA_INVALID_ARGUMENT;
        if (field->type != NULL) {
            cmeta_status status = type_valid(field->type, budget);
            if (status != CMETA_OK) return status;
        }
        if (field->declared_type != NULL) {
            /* Declared generic metadata has its own validation authority. */
            const cmeta_declared_type *declared = field->declared_type;
            if (declared->arity > budget->limits->max_items) return CMETA_CAPACITY_EXCEEDED;
            if (declared->arity != 0u && declared->arguments == NULL) return CMETA_INVALID_ARGUMENT;
            for (size_t j = 0u; j < declared->arity; ++j) {
                cmeta_status status = type_valid(declared->arguments[j], budget);
                if (status != CMETA_OK) return status;
            }
            cmeta_status status = type_valid(declared->storage_type, budget);
            if (status != CMETA_OK) return status;
            if (!cmeta_declared_type_valid(declared)) return CMETA_INVALID_ARGUMENT;
        }
    }
    return CMETA_OK;
}

static cmeta_status interface_valid(const cmeta_interface_desc *desc,
    manifest_budget *budget) {
    if (desc->size < sizeof(*desc) ||
        (desc->method_count != 0u && desc->methods == NULL)) return CMETA_INVALID_ARGUMENT;
    if (desc->method_count > budget->limits->max_items) return CMETA_CAPACITY_EXCEEDED;
    for (size_t i = 0u; i < desc->method_count; ++i) {
        if (desc->methods[i].size < sizeof(desc->methods[i])) return CMETA_INVALID_ARGUMENT;
        if (desc->methods[i].abi != NULL) {
            cmeta_status status = function_valid(desc->methods[i].abi, budget);
            if (status != CMETA_OK) return status;
            if (desc->methods[i].function != desc->methods[i].abi->function)
                return CMETA_INVALID_ARGUMENT;
        }
    }
    return cmeta_interface_desc_valid(desc) ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

static cmeta_status enum_valid(const cmeta_enum_domain *desc, manifest_budget *budget) {
    if (desc->struct_size < sizeof(*desc)) return CMETA_INVALID_ARGUMENT;
    if (desc->abi_version != CMETA_ENUM_DOMAIN_ABI_VERSION) return CMETA_TYPE_MISMATCH;
    if (desc->count > budget->limits->max_items) return CMETA_CAPACITY_EXCEEDED;
    return cmeta_enum_domain_valid(desc) ? CMETA_OK : CMETA_INVALID_ARGUMENT;
}

static cmeta_status component_valid(const cmeta_component_desc *desc,
    manifest_budget *budget) {
    if (desc == NULL || desc->size != sizeof(*desc))
        return CMETA_INVALID_ARGUMENT;
    if (desc->format_version != CMETA_COMPONENT_DECLARATION_VERSION)
        return CMETA_TYPE_MISMATCH;
    if (desc->capability_count > budget->limits->max_items)
        return CMETA_CAPACITY_EXCEEDED;
    if ((desc->capability_count != 0u && desc->capabilities == NULL) ||
        desc->capability_count > SIZE_MAX / sizeof(*desc->capabilities))
        return CMETA_INVALID_ARGUMENT;
    if (desc->config != NULL) {
        cmeta_status status;
        if (!cmeta_data_desc_valid(desc->config))
            return CMETA_INVALID_ARGUMENT;
        if (desc->config->storage_type != NULL) {
            status = type_valid(desc->config->storage_type, budget);
            if (status != CMETA_OK) return status;
        }
    }
    for (size_t i = 0u; i < desc->capability_count; ++i) {
        cmeta_status status =
            interface_valid(desc->capabilities[i].interface_desc, budget);
        if (status != CMETA_OK) return status;
    }
    return cmeta_component_desc_valid(desc)
        ? CMETA_OK
        : CMETA_INVALID_ARGUMENT;
}

cmeta_status cmeta_component_get_capability(const cmeta_component_desc *desc, size_t index,
    cmeta_component_role role, const cmeta_manifest_limits *limits,
    const cmeta_interface_desc **out) {
    if (out == NULL || !limits_valid(limits) ||
        (role != CMETA_COMPONENT_PROVIDES && role != CMETA_COMPONENT_REQUIRES))
        return CMETA_INVALID_ARGUMENT;
    manifest_budget budget = {limits, limits->max_identity_nodes};
    cmeta_status status = component_valid(desc, &budget);
    if (status != CMETA_OK) return status;
    if (index >= desc->capability_count) return CMETA_INVALID_ARGUMENT;
    if (desc->capabilities[index].role != role) return CMETA_TYPE_MISMATCH;
    *out = desc->capabilities[index].interface_desc;
    return CMETA_OK;
}

/* Each getter validates the one canonical descriptor and publishes only on
 * success. No descriptor copies or mutable discovery state are maintained. */
#define MANIFEST_GETTER(name_, type_, kind_, validate_) \
    cmeta_status cmeta_manifest_get_##name_(const cmeta_manifest *manifest, size_t index, \
        const cmeta_manifest_limits *limits, const type_ **out) { \
        const cmeta_manifest_entry *entry; \
        if (out == NULL) return CMETA_INVALID_ARGUMENT; \
        cmeta_status status = manifest_entry(manifest, index, limits, kind_, &entry); \
        if (status != CMETA_OK) return status; \
        const type_ *desc = (const type_ *)entry->descriptor; \
        manifest_budget budget = {limits, limits->max_identity_nodes}; \
        status = validate_(desc, &budget); \
        if (status != CMETA_OK) return status; \
        *out = desc; \
        return CMETA_OK; \
    }

MANIFEST_GETTER(type, cmeta_type_desc, CMETA_MANIFEST_TYPE, type_valid)
MANIFEST_GETTER(struct, cmeta_struct_desc, CMETA_MANIFEST_STRUCT, struct_valid)
MANIFEST_GETTER(function, cmeta_function_abi_desc, CMETA_MANIFEST_FUNCTION, function_valid)
MANIFEST_GETTER(interface, cmeta_interface_desc, CMETA_MANIFEST_INTERFACE, interface_valid)
MANIFEST_GETTER(trace, cmeta_struct_desc, CMETA_MANIFEST_TRACEPOINT, struct_valid)
MANIFEST_GETTER(capability, cmeta_interface_desc, CMETA_MANIFEST_CAPABILITY, interface_valid)
MANIFEST_GETTER(enum, cmeta_enum_domain, CMETA_MANIFEST_ENUM_DOMAIN, enum_valid)
MANIFEST_GETTER(component, cmeta_component_desc, CMETA_MANIFEST_COMPONENT, component_valid)
