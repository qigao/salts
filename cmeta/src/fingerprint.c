#include <cmeta/fingerprint.h>

typedef enum fingerprint_domain {
    FINGERPRINT_TYPE = 1,
    FINGERPRINT_STRUCT = 2,
    FINGERPRINT_ENUM = 3,
    FINGERPRINT_FUNCTION = 4,
    FINGERPRINT_INTERFACE = 5
} fingerprint_domain;

typedef struct fingerprint_state {
    cmeta_abi_fingerprint_builder hash;
    const cmeta_fingerprint_limits *limits;
    size_t nodes;
    size_t rows;
    size_t strings;
    cmeta_status status;
} fingerprint_state;

static bool fail(fingerprint_state *state, cmeta_status status) {
    state->status = status;
    return false;
}

static void number(fingerprint_state *state, uint64_t value) {
    cmeta_abi_fingerprint_u64(&state->hash, value);
}

static bool node(fingerprint_state *state, size_t depth) {
    if (depth >= state->limits->max_depth || state->nodes == 0u)
        return fail(state, CMETA_CAPACITY_EXCEEDED);
    --state->nodes;
    return true;
}

static bool rows(fingerprint_state *state, size_t count, const void *array, size_t stride) {
    if (count > state->rows) return fail(state, CMETA_CAPACITY_EXCEEDED);
    if ((count != 0u && array == NULL) || count > SIZE_MAX / stride)
        return fail(state, CMETA_INVALID_ARGUMENT);
    state->rows -= count;
    number(state, (uint64_t)count);
    return true;
}

/* Bound each scan before canonical validators can compare names with strcmp.
 * Display names consume validation budget but do not enter the byte stream. */
static bool string(fingerprint_state *state, const char *text, bool semantic) {
    size_t length = 0u;
    if (text == NULL) return fail(state, CMETA_INVALID_ARGUMENT);
    for (;;) {
        if (state->strings == 0u) return fail(state, CMETA_CAPACITY_EXCEEDED);
        --state->strings;
        if (text[length] == '\0') break;
        ++length;
    }
    if (length == 0u) return fail(state, CMETA_INVALID_ARGUMENT);
    if (semantic) {
        number(state, (uint64_t)length);
        cmeta_abi_fingerprint_bytes(&state->hash, text, length);
    }
    return true;
}

static bool generic(fingerprint_state *state, const cmeta_generic_desc *desc) {
    if (desc == NULL) return fail(state, CMETA_INVALID_ARGUMENT);
    if (!string(state, desc->stable_id, true) ||
        !string(state, desc->display_name, false)) return false;
    if (!cmeta_generic_desc_valid(desc)) return fail(state, CMETA_INVALID_ARGUMENT);
    number(state, desc->min_arity);
    number(state, desc->max_arity);
    number(state, (uint64_t)desc->category);
    return true;
}

static bool identity(fingerprint_state *state, const cmeta_type_identity *id,
    size_t depth) {
    if (id == NULL) return fail(state, CMETA_TYPE_MISMATCH);
    if (!node(state, depth)) return false;
    number(state, (uint64_t)id->form);
    switch (id->form) {
        case CMETA_TYPE_ATOM:
            if (!string(state, id->stable_atom_id, true)) return false;
            break;
        case CMETA_TYPE_POINTER:
        case CMETA_TYPE_CONST:
            if (!identity(state, id->base, depth + 1u)) return false;
            break;
        case CMETA_TYPE_APPLY:
            if (!generic(state, id->constructor) || !rows(state, id->arity, id->args, sizeof(*id->args)))
                return false;
            for (size_t i = 0u; i < id->arity; ++i)
                if (!identity(state, id->args[i], depth + 1u)) return false;
            break;
        default:
            return fail(state, CMETA_INVALID_ARGUMENT);
    }
    /* Only call the recursive authority after every followed edge is bounded. */
    return cmeta_type_identity_valid(id) || fail(state, CMETA_INVALID_ARGUMENT);
}

static bool type(fingerprint_state *state, const cmeta_type_desc *desc, size_t depth) {
    if (desc == NULL) return fail(state, CMETA_INVALID_ARGUMENT);
    if (desc->kind < CMETA_T_VOID || desc->kind > CMETA_T_OBJECT ||
        (desc->traits != NULL && (desc->traits->flags & ~CMETA_TRAIT_MASK) != 0u))
        return fail(state, CMETA_INVALID_ARGUMENT);
    if (!node(state, depth) || !string(state, desc->name, false) ||
        !identity(state, desc->identity, depth + 1u)) return false;
    number(state, desc->size);
    number(state, desc->align);
    number(state, (uint64_t)desc->kind);
    number(state, desc->traits == NULL ? 0u : desc->traits->flags);
    number(state, desc->pointee != NULL);
    if (desc->pointee != NULL && !type(state, desc->pointee, depth + 1u)) return false;
    return cmeta_type_desc_valid(desc) || fail(state, CMETA_INVALID_ARGUMENT);
}

static bool declared(fingerprint_state *state, const cmeta_declared_type *desc,
    size_t depth) {
    if (!node(state, depth) || !generic(state, desc->constructor) ||
        !type(state, desc->storage_type, depth + 1u) ||
        !rows(state, desc->arity, desc->arguments, sizeof(*desc->arguments))) return false;
    for (size_t i = 0u; i < desc->arity; ++i)
        if (!type(state, desc->arguments[i], depth + 1u)) return false;
    if (!cmeta_declared_type_valid(desc)) return fail(state, CMETA_INVALID_ARGUMENT);
    /* Construction is a capability fact, never a callback address. */
    if (desc->construction != NULL && !cmeta_declared_type_constructible(desc))
        return fail(state, CMETA_INVALID_ARGUMENT);
    number(state, desc->construction != NULL);
    return true;
}

static bool structure(fingerprint_state *state, const cmeta_struct_desc *desc,
    size_t depth) {
    if (desc == NULL || desc->size == 0u || desc->align == 0u)
        return fail(state, CMETA_INVALID_ARGUMENT);
    if (!node(state, depth) || !string(state, desc->name, false)) return false;
    number(state, desc->size);
    number(state, desc->align);
    if (!rows(state, desc->field_count, desc->fields, sizeof(*desc->fields))) return false;
    for (size_t i = 0u; i < desc->field_count; ++i) {
        const cmeta_field_desc *field = &desc->fields[i];
        if (field->offset == CMETA_FIELD_DYNAMIC_OFFSET)
            return fail(state, CMETA_TYPE_MISMATCH);
        if (field->align == 0u || field->offset > desc->size ||
            field->size > desc->size - field->offset)
            return fail(state, CMETA_INVALID_ARGUMENT);
        if (!string(state, field->name, true)) return false;
        number(state, field->offset);
        number(state, field->size);
        number(state, field->align);
        if (field->type == NULL) return fail(state, CMETA_TYPE_MISMATCH);
        if (!type(state, field->type, depth + 1u)) return false;
        if (field->size != field->type->size || field->align != field->type->align)
            return fail(state, CMETA_INVALID_ARGUMENT);
        number(state, field->declared_type != NULL);
        if (field->declared_type != NULL &&
            !declared(state, field->declared_type, depth + 1u)) return false;
    }
    return true;
}

static bool enumeration(fingerprint_state *state, const cmeta_enum_domain *desc,
    size_t depth) {
    if (desc == NULL || desc->struct_size < sizeof(*desc))
        return fail(state, CMETA_INVALID_ARGUMENT);
    if (desc->abi_version != CMETA_ENUM_DOMAIN_ABI_VERSION)
        return fail(state, CMETA_TYPE_MISMATCH);
    if (!node(state, depth)) return false;
    number(state, (uint64_t)desc->signedness);
    number(state, desc->bits);
    number(state, (uint64_t)desc->kind);
    number(state, desc->declared_mask);
    if (!rows(state, desc->count, desc->items, sizeof(*desc->items))) return false;
    for (size_t i = 0u; i < desc->count; ++i) {
        number(state, desc->items[i].bits);
        if (!string(state, desc->items[i].symbol, true) ||
            !string(state, desc->items[i].text, false)) return false;
    }
    return cmeta_enum_domain_valid(desc) || fail(state, CMETA_INVALID_ARGUMENT);
}

static bool function(fingerprint_state *state, const cmeta_function_abi_desc *abi,
    size_t depth) {
    if (abi == NULL || abi->size < sizeof(*abi) || abi->function == NULL ||
        abi->function->size < sizeof(*abi->function))
        return fail(state, CMETA_INVALID_ARGUMENT);
    const cmeta_function_desc *desc = abi->function;
    if (abi->param_count != desc->param_count)
        return fail(state, CMETA_INVALID_ARGUMENT);
    if (abi->return_carrier == CMETA_ABI_UNSPECIFIED)
        return fail(state, CMETA_TYPE_MISMATCH);
    if (!node(state, depth) || !string(state, desc->name, false)) return false;
    number(state, (uint64_t)abi->return_carrier);
    if (!type(state, desc->return_type, depth + 1u)) return false;
    number(state, desc->effects);
    number(state, desc->properties);
    number(state, desc->result_flags);
    if (!rows(state, desc->param_count, desc->params, sizeof(*desc->params))) return false;
    if (desc->param_count != 0u && abi->param_carriers == NULL)
        return fail(state, CMETA_INVALID_ARGUMENT);
    for (size_t i = 0u; i < desc->param_count; ++i) {
        const cmeta_param_desc *param = &desc->params[i];
        if (param->size < sizeof(*param)) return fail(state, CMETA_INVALID_ARGUMENT);
        if (abi->param_carriers[i] == CMETA_ABI_UNSPECIFIED)
            return fail(state, CMETA_TYPE_MISMATCH);
        if (!string(state, param->name, false)) return false;
        number(state, param->flags);
        number(state, (uint64_t)abi->param_carriers[i]);
        if (!type(state, param->type, depth + 1u)) return false;
    }
    return cmeta_function_abi_desc_valid(abi) || fail(state, CMETA_INVALID_ARGUMENT);
}

static bool interface_contract(fingerprint_state *state, const cmeta_interface_desc *desc,
    size_t depth) {
    if (desc == NULL || desc->size < sizeof(*desc))
        return fail(state, CMETA_INVALID_ARGUMENT);
    if (!node(state, depth) || !string(state, desc->name, false) ||
        !rows(state, desc->method_count, desc->methods, sizeof(*desc->methods))) return false;
    for (size_t i = 0u; i < desc->method_count; ++i) {
        const cmeta_interface_method_desc *method = &desc->methods[i];
        if (method->size < sizeof(*method)) return fail(state, CMETA_INVALID_ARGUMENT);
        if (method->function == NULL || method->abi == NULL)
            return fail(state, CMETA_TYPE_MISMATCH);
        if (!string(state, method->name, true)) return false;
        number(state, method->dispatch_arity);
        number(state, method->flags);
        if (!function(state, method->abi, depth + 1u)) return false;
        /* The method must borrow exactly its own canonical FunctionAbi row. */
        if (method->function != method->abi->function)
            return fail(state, CMETA_INVALID_ARGUMENT);
    }
    return cmeta_interface_desc_valid(desc) || fail(state, CMETA_INVALID_ARGUMENT);
}

static bool begin(fingerprint_state *state, const cmeta_fingerprint_limits *limits,
    uint64_t *out, fingerprint_domain domain) {
    state->status = CMETA_INVALID_ARGUMENT;
    if (limits == NULL || out == NULL || limits->max_depth == 0u ||
        limits->max_depth > CMETA_FINGERPRINT_DEPTH_LIMIT || limits->max_nodes == 0u ||
        limits->max_rows == 0u || limits->max_string_bytes == 0u) return false;
    state->hash = cmeta_abi_fingerprint_begin();
    state->limits = limits;
    state->nodes = limits->max_nodes;
    state->rows = limits->max_rows;
    state->strings = limits->max_string_bytes;
    state->status = CMETA_OK;
    cmeta_abi_fingerprint_string(&state->hash, "cmeta.contract");
    number(state, CMETA_ABI_FINGERPRINT_VERSION);
    number(state, CMETA_CONTRACT_FINGERPRINT_VERSION);
    number(state, (uint64_t)domain);
    return true;
}

/* Publish only a complete, validated projection. No partial digest escapes. */
#define FINGERPRINT_QUERY(name_, type_, domain_, project_) \
    cmeta_status cmeta_contract_fingerprint_##name_(const type_ *desc, \
        const cmeta_fingerprint_limits *limits, uint64_t *out) { \
        fingerprint_state state; \
        if (!begin(&state, limits, out, domain_) || !project_(&state, desc, 0u)) \
            return state.status; \
        *out = cmeta_abi_fingerprint_finish(&state.hash); \
        return CMETA_OK; \
    }

FINGERPRINT_QUERY(type, cmeta_type_desc, FINGERPRINT_TYPE, type)
FINGERPRINT_QUERY(struct, cmeta_struct_desc, FINGERPRINT_STRUCT, structure)
FINGERPRINT_QUERY(enum, cmeta_enum_domain, FINGERPRINT_ENUM, enumeration)
FINGERPRINT_QUERY(function, cmeta_function_abi_desc, FINGERPRINT_FUNCTION, function)
FINGERPRINT_QUERY(interface, cmeta_interface_desc, FINGERPRINT_INTERFACE, interface_contract)
