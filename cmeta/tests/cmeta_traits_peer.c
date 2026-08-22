#include <cmeta/cmeta.h>

#include <stdlib.h>

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
