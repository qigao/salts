#include <cflow/status.h>

bool cflow_status_result_is_ok(cflow_status_result result) {
    return result.status == CFLOW_STATUS_OK;
}

const char *cflow_status_string(cflow_status status) {
    switch (status) {
        case CFLOW_STATUS_OK: return "ok";
        case CFLOW_STATUS_INVALID_ARGUMENT: return "invalid argument";
        case CFLOW_STATUS_TYPE_MISMATCH: return "type mismatch";
        case CFLOW_STATUS_UNSUPPORTED: return "unsupported";
        case CFLOW_STATUS_CAPACITY_EXCEEDED: return "capacity exceeded";
        case CFLOW_STATUS_ALLOCATION_FAILED: return "allocation failed";
        case CFLOW_STATUS_CANCELLED: return "cancelled";
        case CFLOW_STATUS_CLOSED: return "closed";
        case CFLOW_STATUS_EXECUTION_ERROR: return "execution error";
        case CFLOW_STATUS_WOULD_BLOCK: return "would block";
    }
    return "unknown status";
}

const char *cflow_status_result_message(cflow_status_result result) {
    return cflow_status_string(result.status);
}
