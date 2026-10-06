#ifndef CFLOW_EXECUTOR_DSO_FIXTURE_H
#define CFLOW_EXECUTOR_DSO_FIXTURE_H

#include <cflow/executor.h>
#include <stdatomic.h>

#ifdef _WIN32
#  ifdef CFLOW_EXECUTOR_DSO_FIXTURE_BUILD
#    define CFLOW_EXECUTOR_DSO_API __declspec(dllexport)
#  else
#    define CFLOW_EXECUTOR_DSO_API __declspec(dllimport)
#  endif
#else
#  define CFLOW_EXECUTOR_DSO_API
#endif

typedef struct cflow_executor_dso_probe {
    cflow_executor *executor;
    atomic_int run_count;
    atomic_int cancel_count;
    atomic_int finalize_count;
    atomic_int current_count;
} cflow_executor_dso_probe;

CFLOW_EXECUTOR_DSO_API int cflow_executor_dso_submit(
    cflow_executor *executor, cflow_executor_dso_probe *probe);

#endif /* CFLOW_EXECUTOR_DSO_FIXTURE_H */
