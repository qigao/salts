#ifndef CFLOW_INSTALLED_OWNER_CONTRACT_H
#define CFLOW_INSTALLED_OWNER_CONTRACT_H

#include <cflow/executor.h>
#include <cflow/scheduler.h>

static void owner_mark(void *user) {
    int *count = (int *)user;
    ++*count;
}

/* Use installed public headers and the installed CFlow link target only.
 * No source-tree include, privately supplied symbol, or compatibility stub.
 * All callbacks run on the one explicitly driven owner Executor. */
static int owner_contract_run(void) {
    cflow_executor executor = {0};
    cflow_scheduler scheduler = {0};
    int calls = 0;
    int status = 0;

    if (!cflow_executor_owner_init_with_capacity(&executor, 8u, NULL, NULL))
        return 1;
    if (!cflow_scheduler_owner_bind(&scheduler, &executor, 4u)) {
        status = 2;
        goto finish;
    }
    if (cflow_scheduler_post(&scheduler, owner_mark, &calls) == 0u) {
        status = 3;
        goto finish;
    }
    if (!cflow_executor_run_one(&executor) || calls != 1) {
        status = 4;
        goto finish;
    }
    if (cflow_executor_run_one(&executor) || calls != 1)
        status = 5;

finish:
    if (cflow_scheduler_valid(&scheduler))
        cflow_scheduler_destroy(&scheduler);
    if (cflow_executor_valid(&executor))
        cflow_executor_destroy(&executor);
    return status;
}

#endif
