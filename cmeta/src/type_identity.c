#include <cmeta/type_identity.h>

#include <string.h>

static bool cmeta_nonempty(const char *s) {
    return s && s[0] != '\0';
}

static bool cmeta_generic_category_valid(cmeta_generic_category category) {
    switch (category) {
        case CMETA_GENERIC_VALUE:
        case CMETA_GENERIC_CONTAINER:
        case CMETA_GENERIC_HANDLE:
            return true;
    }
    return false;
}

bool cmeta_generic_desc_valid(const cmeta_generic_desc *desc) {
    return desc && cmeta_nonempty(desc->stable_id) &&
           cmeta_nonempty(desc->display_name) &&
           desc->min_arity <= desc->max_arity &&
           cmeta_generic_category_valid(desc->category);
}

bool cmeta_generic_accepts_arity(const cmeta_generic_desc *desc, size_t arity) {
    return cmeta_generic_desc_valid(desc) &&
           arity >= (size_t)desc->min_arity &&
           arity <= (size_t)desc->max_arity;
}

bool cmeta_type_application_valid(
    const cmeta_generic_desc *constructor,
    const cmeta_type_identity *const *args,
    size_t arity) {
    size_t i;
    if (!cmeta_generic_accepts_arity(constructor, arity))
        return false;
    if (arity != 0u && args == NULL)
        return false;
    for (i = 0u; i < arity; ++i) {
        if (args[i] == NULL || !cmeta_type_identity_valid(args[i]))
            return false;
    }
    return true;
}

bool cmeta_type_identity_valid(const cmeta_type_identity *identity) {
    if (!identity) return false;
    switch (identity->form) {
        case CMETA_TYPE_ATOM:
            return cmeta_nonempty(identity->stable_atom_id) &&
                   !identity->constructor && !identity->base &&
                   !identity->args && identity->arity == 0u;
        case CMETA_TYPE_POINTER:
        case CMETA_TYPE_CONST:
            return !identity->stable_atom_id && !identity->constructor &&
                   identity->base && !identity->args && identity->arity == 0u &&
                   cmeta_type_identity_valid(identity->base);
        case CMETA_TYPE_APPLY:
            return !identity->stable_atom_id && !identity->base &&
                   cmeta_type_application_valid(identity->constructor,
                                                identity->args,
                                                identity->arity);
    }
    return false;
}

static bool cmeta_generic_desc_equal(const cmeta_generic_desc *a,
                                     const cmeta_generic_desc *b) {
    if (a == b) return a != NULL;
    return a && b && cmeta_nonempty(a->stable_id) &&
           cmeta_nonempty(b->stable_id) &&
           strcmp(a->stable_id, b->stable_id) == 0;
}

bool cmeta_type_identity_equal(const cmeta_type_identity *a,
                               const cmeta_type_identity *b) {
    size_t i;
    if (a == b) return a != NULL;
    if (!a || !b || a->form != b->form) return false;

    switch (a->form) {
        case CMETA_TYPE_ATOM:
            return cmeta_nonempty(a->stable_atom_id) &&
                   cmeta_nonempty(b->stable_atom_id) &&
                   strcmp(a->stable_atom_id, b->stable_atom_id) == 0;
        case CMETA_TYPE_POINTER:
        case CMETA_TYPE_CONST:
            return cmeta_type_identity_equal(a->base, b->base);
        case CMETA_TYPE_APPLY:
            if (!cmeta_generic_desc_equal(a->constructor, b->constructor) ||
                a->arity != b->arity)
                return false;
            if (a->arity != 0u && (!a->args || !b->args))
                return false;
            for (i = 0; i < a->arity; ++i)
                if (!cmeta_type_identity_equal(a->args[i], b->args[i]))
                    return false;
            return true;
    }
    return false;
}

bool cmeta_type_identity_is_application(const cmeta_type_identity *identity) {
    return identity && identity->form == CMETA_TYPE_APPLY;
}

const cmeta_generic_desc *
cmeta_type_identity_constructor(const cmeta_type_identity *identity) {
    return cmeta_type_identity_is_application(identity) ? identity->constructor : NULL;
}

size_t cmeta_type_identity_arity(const cmeta_type_identity *identity) {
    return cmeta_type_identity_is_application(identity) ? identity->arity : 0u;
}

const cmeta_type_identity *
cmeta_type_identity_argument(const cmeta_type_identity *identity, size_t index) {
    if (!cmeta_type_identity_is_application(identity) || !identity->args ||
        index >= identity->arity)
        return NULL;
    return identity->args[index];
}
