#include <cmeta/type_identity.h>

#include <string.h>

static bool cmeta_nonempty(const char *s) {
    return s && s[0] != '\0';
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
            return false;
    }
    return false;
}

bool cmeta_type_identity_equal(const cmeta_type_identity *a,
                               const cmeta_type_identity *b) {
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
            return false;
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
