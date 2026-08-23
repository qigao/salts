#include <cmeta/cmeta.h>

#include <stdlib.h>

extern int cmeta_test_shared_canonical_target(int value);

static cmeta_fn cmeta_peer_shared_canonical_meta(void) {
    cmeta_fn meta = CMETA_WRAP_TYPED_ANY(cmeta_test_shared_canonical_target);
    meta.effects = CMETA_CONTRACT_EFFECTS(value);
    meta.properties = CMETA_CONTRACT_PROPERTIES(value);
    return meta;
}

static bool cmeta_peer_shared_canonical_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
    return self != NULL && cmeta_fn_invoke(self->meta, out, args);
}

static cmeta_gen_status cmeta_peer_shared_canonical_generate(
    const cmeta_callable *self, const void *input, void *out, size_t *cursor) {
    return self != NULL ? cmeta_fn_generate(self->meta, input, out, cursor)
                        : CMETA_GEN_ERROR;
}

cmeta_callable cmeta_traits_peer_shared_canonical(void) {
    static const cmeta_callable callable =
        CMETA_CANONICAL_RAW_CALLABLE_INIT(
            CMETA_CONTRACT_EFFECTS(value), CMETA_CONTRACT_PROPERTIES(value),
            cmeta_peer_shared_canonical_meta, cmeta_peer_shared_canonical_invoke,
            cmeta_peer_shared_canonical_generate, 0u);
    return callable;
}

typedef struct owned_int {
    int *value;
} owned_int;

static bool owned_equal(const void *left_, const void *right_) {
    const owned_int *left = (const owned_int *)left_;
    const owned_int *right = (const owned_int *)right_;

    return left != NULL && right != NULL && left->value != NULL &&
           right->value != NULL && *left->value == *right->value;
}

static bool owned_copy(void *destination_, const void *source_) {
    owned_int *destination = (owned_int *)destination_;
    const owned_int *source = (const owned_int *)source_;

    if (destination == NULL || source == NULL || source->value == NULL)
        return false;
    destination->value = (int *)malloc(sizeof(*destination->value));
    if (destination->value == NULL)
        return false;
    *destination->value = *source->value;
    return true;
}

static void owned_move(void *destination_, void *source_) {
    owned_int *destination = (owned_int *)destination_;
    owned_int *source = (owned_int *)source_;

    if (destination != NULL)
        destination->value = source != NULL ? source->value : NULL;
    if (source != NULL)
        source->value = NULL;
}

static void owned_destroy(void *value_) {
    owned_int *value = (owned_int *)value_;

    if (value == NULL)
        return;
    free(value->value);
    value->value = NULL;
}

Traits(owned_int,
    (equal, owned_equal),
    (copy, owned_copy),
    (move, owned_move),
    (destroy, owned_destroy));

static const cmeta_type_desc cmeta_peer_owned_int_type = {
    .name = "owned_int",
    .size = sizeof(owned_int),
    .align = _Alignof(owned_int),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cmeta_traits_owned_int,
    .identity = NULL
};

const cmeta_type_desc *cmeta_traits_peer_owned_int_type(void) {
    return &cmeta_peer_owned_int_type;
}
