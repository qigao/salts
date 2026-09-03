#ifndef CFLOW_EXECUTOR_INTERNAL_H
#define CFLOW_EXECUTOR_INTERNAL_H

#include <cflow/executor.h>
#include <salts/thread_pool.h>

int salts_threadpool_is_current_internal(const salts_threadpool_t *pool);

/* True only while this thread is executing a callback owned by the same
 * repository-provided Executor. Foreign implementations return false. */
bool cflow_executor_is_current_internal(const cflow_executor *executor);

#endif /* CFLOW_EXECUTOR_INTERNAL_H */
