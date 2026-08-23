#ifndef CFLOW_ADMISSION_H
#define CFLOW_ADMISSION_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_admission_status {
    CFLOW_ADMISSION_ACCEPTED = 0,
    CFLOW_ADMISSION_INVALID_ARGUMENT,
    CFLOW_ADMISSION_FULL,
    CFLOW_ADMISSION_CLOSED,
    CFLOW_ADMISSION_ALLOCATION_FAILED
} cflow_admission_status;

typedef uint64_t cflow_task_id;

typedef struct cflow_schedule_result {
    cflow_admission_status status;
    cflow_task_id task_id;
} cflow_schedule_result;

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_ADMISSION_H */
