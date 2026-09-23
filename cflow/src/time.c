#include <cflow/time.h>

static bool cflow_duration_equal(const void *left_, const void *right_) {
    const cflow_duration *left = (const cflow_duration *)left_;
    const cflow_duration *right = (const cflow_duration *)right_;

    return left != NULL && right != NULL && left->ns == right->ns;
}

static bool cflow_instant_equal(const void *left_, const void *right_) {
    const cflow_instant *left = (const cflow_instant *)left_;
    const cflow_instant *right = (const cflow_instant *)right_;

    return left != NULL && right != NULL && left->ns == right->ns;
}

static const cmeta_type_traits cflow_duration_traits = {
    .flags = CMETA_TRAIT_EQUAL |
             CMETA_TRAIT_TRIVIAL_COPY |
             CMETA_TRAIT_TRIVIAL_DESTROY,
    .equal = cflow_duration_equal
};

static const cmeta_type_traits cflow_instant_traits = {
    .flags = CMETA_TRAIT_EQUAL |
             CMETA_TRAIT_TRIVIAL_COPY |
             CMETA_TRAIT_TRIVIAL_DESTROY,
    .equal = cflow_instant_equal
};

const cmeta_type_desc cflow_type_duration = {
    .name = "cflow_duration",
    .size = sizeof(cflow_duration),
    .align = _Alignof(cflow_duration),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_duration_traits,
    .identity = NULL
};

const cmeta_type_desc cflow_type_instant = {
    .name = "cflow_instant",
    .size = sizeof(cflow_instant),
    .align = _Alignof(cflow_instant),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_instant_traits,
    .identity = NULL
};
