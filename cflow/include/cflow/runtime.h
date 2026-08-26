#ifndef CFLOW_RUNTIME_H
#define CFLOW_RUNTIME_H

#include <cflow/graph.h>
#include <cflow/scheduler.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

Enum(cflow_step_kind,
    (CFLOW_STEP_VALUE,          "value"),
    (CFLOW_STEP_VALUE_AND_DONE, "value_and_done"),
    (CFLOW_STEP_WAIT,           "wait"),
    (CFLOW_STEP_DONE,           "done"),
    (CFLOW_STEP_ERROR,          "error")
);

typedef struct cflow_waker {
    void (*wake)(void *user);
    void *user;
} cflow_waker;

#define CMETA_WAITABLE_METHODS(X,I) \
    X(I,R1,bool,arm,cflow_waker,waker) \
    X(I,V0,void,cancel,_)
CMETA_INTERFACE(cflow_waitable, CMETA_WAITABLE_METHODS);

typedef struct cflow_step {
    cflow_step_kind kind;
    cflow_waitable waitable;
    const char *error;
} cflow_step;

typedef struct cflow_resume_ctx {
    cflow_scheduler *scheduler;
} cflow_resume_ctx;

typedef struct cflow_resumable_ops {
    /* resume receives empty output storage and constructs a live value only
     * for VALUE or VALUE_AND_DONE. All other steps leave it empty. */
    cflow_step (*resume)(void *state, cflow_resume_ctx *ctx, void *out_value);
    void (*cancel)(void *state);
    void (*destroy)(void *state);
} cflow_resumable_ops;

typedef struct cflow_resumable {
    const char *name;
    const cmeta_type_desc *output_type;
    const cflow_resumable_ops *ops;
    void *state;
} cflow_resumable;

Enum(cflow_source_terminal,
    (CFLOW_SOURCE_OPEN,  "open"),
    (CFLOW_SOURCE_DONE,  "done"),
    (CFLOW_SOURCE_ERROR, "error")
);

#define CFLOW_SOURCE_METHODS(X,I) \
    X(I,R0,const char *,name,_) \
    X(I,R0,const cmeta_type_desc *,output_type,_) \
    X(I,R2,cflow_step,resume,cflow_resume_ctx *,ctx,void *,out_value) \
    X(I,V0,void,cancel,_) \
    X(I,V0,void,destroy,_) \
    X(I,V1,void,bind_terminal_waker,cflow_waker,waker) \
    X(I,R1,cflow_source_terminal,poll_terminal,const char **,error)
CMETA_INTERFACE(cflow_source, CFLOW_SOURCE_METHODS);

enum {
    /* resume() constructs a live value in empty out_value storage only when it
     * returns VALUE or VALUE_AND_DONE. Other results leave storage empty. */
    CFLOW_SOURCE_CAP_CONSTRUCTS_VALUES = UINT64_C(1) << 0
};

/* Move a Source interface into a generic resumable machine. */
bool cflow_resumable_from_source(cflow_resumable *out, cflow_source *source);

/* value is borrowed and remains live only until the callback returns. */
typedef bool (*cflow_value_fn)(void *user,
                               const cmeta_type_desc *type,
                               const void *value);
typedef void (*cflow_error_fn)(void *user, const char *message);
typedef void (*cflow_done_fn)(void *user);

#define CMETA_SINK_METHODS(X,I) \
    X(I,R2,bool,value,const cmeta_type_desc *,type,const void *,value) \
    X(I,V1,void,error,const char *,message) \
    X(I,V0,void,done,_)
CMETA_INTERFACE(cflow_sink, CMETA_SINK_METHODS);

typedef struct cflow_sink_callbacks {
    cflow_value_fn on_value;
    cflow_error_fn on_error;
    cflow_done_fn on_done;
    void *user;
} cflow_sink_callbacks;

cflow_sink cflow_sink_from_callbacks(cflow_sink_callbacks *callbacks);

typedef struct cflow_run {
    void *impl;
} cflow_run;

/* Move-style ownership: run must be zero-initialized. Reopening a live Run
 * fails without changing either owner. On success the run takes source and
 * clears *source.
 * Graph and scheduler are borrowed. Admission failure leaves source ownership
 * with the caller. Interpreted graphs accept managed COPY/MOVE/DESTROY values
 * from a CONSTRUCTS_VALUES Source. Compiled byte plans remain trivial-only. */
bool cflow_run_open(cflow_run *run,
                    const cflow_graph *graph,
                    cflow_source *source,
                    cflow_scheduler *scheduler,
                    const cflow_sink *sink);

/* Execute a specific immutable subgraph from the same Graph-wide IR table.
 * Primarily used by SubRun composition; public to keep Run/SubRun symmetric. */
bool cflow_run_open_subgraph(cflow_run *run,
                             const cflow_graph *graph,
                             cflow_subgraph_id subgraph,
                             cflow_source *source,
                             cflow_scheduler *scheduler,
                             const cflow_sink *sink);

/* Demand is always downstream-value demand, never source-item demand. */
bool cflow_run_request(cflow_run *run, size_t n);
void cflow_run_cancel(cflow_run *run);
/* External calls close synchronously. A call from this run's sink callback
 * requests close and defers destruction until the active pump returns. */
void cflow_run_close(cflow_run *run);

bool cflow_run_is_done(const cflow_run *run);
bool cflow_run_is_cancelled(const cflow_run *run);
const char *cflow_run_error(const cflow_run *run);
size_t cflow_run_outstanding_demand(const cflow_run *run);

/* Advanced integration hook: safe to call from arbitrary event-loop/driver callbacks. */
void cflow_run_wake(cflow_run *run);

#ifdef __cplusplus
}
#endif
#endif
