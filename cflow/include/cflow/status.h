#ifndef CFLOW_STATUS_H
#define CFLOW_STATUS_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Cross-layer outcome categories for new CFlow adapter APIs.
 *
 * Domain-specific enums remain authoritative for their own protocols. These
 * values are stable adapter categories and must not replace exact Executor,
 * Mailbox, Machine, Statechart, or Actor state.
 */
typedef enum cflow_status {
    CFLOW_STATUS_OK = 0,
    CFLOW_STATUS_INVALID_ARGUMENT = 1,
    CFLOW_STATUS_TYPE_MISMATCH = 2,
    CFLOW_STATUS_UNSUPPORTED = 3,
    CFLOW_STATUS_CAPACITY_EXCEEDED = 4,
    CFLOW_STATUS_ALLOCATION_FAILED = 5,
    CFLOW_STATUS_CANCELLED = 6,
    CFLOW_STATUS_CLOSED = 7,
    CFLOW_STATUS_EXECUTION_ERROR = 8,
    CFLOW_STATUS_WOULD_BLOCK = 9
} cflow_status;

/** Allocation-free structured result. It owns no resources. */
typedef struct cflow_status_result {
    cflow_status status;
} cflow_status_result;

bool cflow_status_result_is_ok(cflow_status_result result);

/**
 * Return a canonical library-owned diagnostic with static storage duration.
 * Unknown numeric values return "unknown status". The caller must not free or
 * modify the returned string.
 */
const char *cflow_status_string(cflow_status status);
const char *cflow_status_result_message(cflow_status_result result);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STATUS_H */
