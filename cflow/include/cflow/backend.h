#ifndef CFLOW_BACKEND_H
#define CFLOW_BACKEND_H

#include <cflow/status.h>
#include <cmeta/range.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Backend implementations own every state returned by open() until close().
 * Values passed to insert_if_absent()/append() are borrowed for that call.
 * The interface tables and type descriptors are borrowed and must outlive the
 * Run. Every successful open has exactly one close, including cancellation and
 * execution failure. Implementations are single-Run and need not synchronize. */
typedef struct cflow_set_state_ops {
    cflow_status (*open)(void **state,
                         const cmeta_type_desc *type,
                         size_t limit);
    cflow_status (*insert_if_absent)(void *state,
                                     const void *value,
                                     bool *inserted);
    void (*close)(void *state);
} cflow_set_state_ops;

typedef struct cflow_sequence_state_ops {
    cflow_status (*open)(void **state,
                         const cmeta_type_desc *type,
                         size_t limit);
    cflow_status (*append)(void *state, const void *value);
    cflow_status (*stable_sort)(void *state);
    cmeta_range (*range)(const void *state);
    void (*close)(void *state);
} cflow_sequence_state_ops;

/* The Run copies this pair of borrowed, immutable interface pointers. */
typedef struct cflow_eval_options {
    const cflow_set_state_ops *set_state;
    const cflow_sequence_state_ops *sequence_state;
} cflow_eval_options;

bool cflow_set_state_ops_valid(const cflow_set_state_ops *ops);
bool cflow_sequence_state_ops_valid(const cflow_sequence_state_ops *ops);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_BACKEND_H */
