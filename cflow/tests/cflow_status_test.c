#include <cflow/status.h>
#include "tinytest.h"

#include <string.h>

typedef struct status_case {
    cflow_status status;
    const char *message;
} status_case;

spec("CFlow status result") {
    it("provides stable canonical text for every public category") {
        static const status_case cases[] = {
            {CFLOW_STATUS_OK, "ok"},
            {CFLOW_STATUS_INVALID_ARGUMENT, "invalid argument"},
            {CFLOW_STATUS_TYPE_MISMATCH, "type mismatch"},
            {CFLOW_STATUS_UNSUPPORTED, "unsupported"},
            {CFLOW_STATUS_CAPACITY_EXCEEDED, "capacity exceeded"},
            {CFLOW_STATUS_ALLOCATION_FAILED, "allocation failed"},
            {CFLOW_STATUS_CANCELLED, "cancelled"},
            {CFLOW_STATUS_CLOSED, "closed"},
            {CFLOW_STATUS_EXECUTION_ERROR, "execution error"},
            {CFLOW_STATUS_WOULD_BLOCK, "would block"}
        };

        for (size_t i = 0u; i < sizeof(cases) / sizeof(cases[0]); ++i) {
            cflow_status_result result = {cases[i].status};
            const char *first = cflow_status_string(cases[i].status);
            const char *second = cflow_status_result_message(result);

            check_not_null(first);
            check_true(strcmp(first, cases[i].message) == 0);
            check(first == second);
            check_equal(cflow_status_result_is_ok(result),
                        cases[i].status == CFLOW_STATUS_OK);
        }
    }

    it("maps unknown numeric values to stable text") {
        const char *first = cflow_status_string((cflow_status)1000);
        const char *second = cflow_status_string((cflow_status)-1);

        check_true(strcmp(first, "unknown status") == 0);
        check(first == second);
    }
}
