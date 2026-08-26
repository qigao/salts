#ifndef CFLOW_EXECUTOR_INTERNAL_H
#define CFLOW_EXECUTOR_INTERNAL_H

#include <cflow/executor.h>
#include <turbo/thread_pool.h>

int turbo_threadpool_is_current_internal(const turbo_threadpool_t *pool);

/* True only while this thread is executing a callback owned by the same
 * repository-provided Executor. Foreign implementations return false. */
bool cflow_executor_is_current_internal(const cflow_executor *executor);

#endif /* CFLOW_EXECUTOR_INTERNAL_H */
