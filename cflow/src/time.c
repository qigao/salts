#include <cflow/time.h>

static const cmeta_type_traits cflow_time_value_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

const cmeta_type_desc cflow_type_duration = {
    .name = "cflow_duration",
    .size = sizeof(cflow_duration),
    .align = _Alignof(cflow_duration),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_time_value_traits,
    .identity = NULL
};

const cmeta_type_desc cflow_type_instant = {
    .name = "cflow_instant",
    .size = sizeof(cflow_instant),
    .align = _Alignof(cflow_instant),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_time_value_traits,
    .identity = NULL
};
