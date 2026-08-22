#ifndef CFLOW_EXECUTOR_H
#define CFLOW_EXECUTOR_H

#include <cmeta/interface.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cflow_task_fn)(void *user);

enum {
    CMETA_EXEC_CAP_MANUAL     = 1u << 0,
    CMETA_EXEC_CAP_SERIAL     = 1u << 1,
    CMETA_EXEC_CAP_CONCURRENT = 1u << 2
};

#define CMETA_EXECUTOR_METHODS(X,I) \
    X(I,R2,bool,post,cflow_task_fn,fn,void *,user) \
    X(I,R0,bool,run_one,_) \
    X(I,R0,size_t,run_ready,_) \
    X(I,R0,bool,wait_idle,_) \
    X(I,R0,size_t,pending,_) \
    X(I,V0,void,destroy,_)
CMETA_INTERFACE(cflow_executor, CMETA_EXECUTOR_METHODS);

bool cflow_executor_manual_init(cflow_executor *executor);
bool cflow_executor_serial_init(cflow_executor *executor);
bool cflow_executor_worker_init(cflow_executor *executor, size_t workers);

#ifdef __cplusplus
}
#endif
#endif /* CFLOW_EXECUTOR_H */
