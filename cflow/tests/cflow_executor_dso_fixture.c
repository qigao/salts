#include "cflow_executor_dso_fixture.h"
#include "executor_internal.h"

static void dso_run(void *user) {
    cflow_executor_dso_probe *probe = (cflow_executor_dso_probe *)user;
    if (probe && probe->executor &&
        cflow_executor_is_current_internal(probe->executor))
        atomic_fetch_add(&probe->current_count, 1);
    if (probe) atomic_fetch_add(&probe->run_count, 1);
}

static void dso_cancel(void *user) {
    cflow_executor_dso_probe *probe = (cflow_executor_dso_probe *)user;
    if (probe) atomic_fetch_add(&probe->cancel_count, 1);
}

static void dso_finalize(void *user) {
    cflow_executor_dso_probe *probe = (cflow_executor_dso_probe *)user;
    if (probe) atomic_fetch_add(&probe->finalize_count, 1);
}

int cflow_executor_dso_submit(
    cflow_executor *executor, cflow_executor_dso_probe *probe) {
    cflow_executor_control control = {0};
    const cflow_executor_task task = {
        .run = dso_run,
        .cancel = dso_cancel,
        .finalize = dso_finalize,
        .user = probe
    };

    if (!executor || !probe) return 1;
    if (!cflow_executor_as_control(executor, &control)) return 2;
    if (cflow_executor_try_post_task(executor, &task) !=
        CFLOW_ADMISSION_ACCEPTED)
        return 3;
    if (cflow_executor_control_post_task(&control, &task) !=
        CFLOW_EXECUTOR_POST_ACCEPTED)
        return 4;
    if (cflow_executor_control_wait_idle(&control) !=
        CFLOW_EXECUTOR_WAIT_IDLE)
        return 5;
    return 0;
}
