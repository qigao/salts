#include <cflow/admission.h>

static const cmeta_type_traits cflow_admission_trivial_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY |
             CMETA_TRAIT_TRIVIAL_DESTROY
};

const cmeta_type_desc cflow_type_admission_status = {
    .name = "cflow_admission_status",
    .size = sizeof(cflow_admission_status),
    .align = _Alignof(cflow_admission_status),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = &cflow_admission_trivial_traits,
    .identity = NULL
};

const cmeta_type_desc cflow_type_task_id = {
    .name = "cflow_task_id",
    .size = sizeof(cflow_task_id),
    .align = _Alignof(cflow_task_id),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .traits = &cflow_admission_trivial_traits,
    .identity = NULL
};

const cmeta_type_desc cflow_type_schedule_result = {
    .name = "cflow_schedule_result",
    .size = sizeof(cflow_schedule_result),
    .align = _Alignof(cflow_schedule_result),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_admission_trivial_traits,
    .identity = NULL
};
