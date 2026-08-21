#ifndef CMETA_COLLECTOR_H
#define CMETA_COLLECTOR_H

#include <cmeta/cmeta.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A collector is externally serialized. The façade borrows each accepted
 * value only for the callback; adapters must copy, retain, or move it before
 * that callback returns. */
typedef enum cmeta_collector_state {
    CMETA_COLLECTOR_ZERO = 0,
    CMETA_COLLECTOR_BEGUN = 1,
    CMETA_COLLECTOR_ACCEPTING = 2,
    CMETA_COLLECTOR_COMMITTED = 3,
    CMETA_COLLECTOR_ABORTED = 4
} cmeta_collector_state;

typedef struct cmeta_collector_ops {
    cmeta_status (*begin)(void *context, const cmeta_type_desc *input,
                          size_t limit);
    cmeta_status (*accept)(void *context, const void *value);
    cmeta_status (*finish)(void *context);
    void (*abort)(void *context);
} cmeta_collector_ops;

typedef struct cmeta_collector {
    const cmeta_collector_ops *ops;
    void *context;
    void *zero_output;
    const cmeta_type_desc *input_type;
    size_t limit;
    size_t count;
    cmeta_collector_state state;
    cmeta_status status;
} cmeta_collector;

typedef cmeta_collector (*cmeta_collector_factory_fn)(void *zero_output,
                                                       size_t limit);

static inline bool cmeta_collector_callback_status_valid(cmeta_status status) {
    switch (status) {
        case CMETA_OK:
        case CMETA_INVALID_ARGUMENT:
        case CMETA_TYPE_MISMATCH:
        case CMETA_TRAIT_MISSING:
        case CMETA_CAPACITY_EXCEEDED:
        case CMETA_OUT_OF_MEMORY:
        case CMETA_CALLBACK_ERROR:
            return true;
        default:
            return false;
    }
}

static inline bool cmeta_collector_ops_valid(const cmeta_collector_ops *ops) {
    return ops != NULL && ops->begin != NULL && ops->accept != NULL &&
           ops->finish != NULL && ops->abort != NULL;
}

static inline void cmeta_collector_abort_failed_begin(cmeta_collector *collector) {
    if (collector == NULL || collector->state == CMETA_COLLECTOR_COMMITTED ||
        collector->state == CMETA_COLLECTOR_ABORTED)
        return;
    if (collector->ops != NULL && collector->ops->abort != NULL)
        collector->ops->abort(collector->context);
    collector->state = CMETA_COLLECTOR_ABORTED;
}

static inline void cmeta_collector_fail(cmeta_collector *collector,
                                        cmeta_status status) {
    if (collector == NULL)
        return;
    if (collector->status == CMETA_OK)
        collector->status = status;
    cmeta_collector_abort_failed_begin(collector);
}

static inline void cmeta_collector_terminate_pre_begin(
    cmeta_collector *collector, cmeta_status status) {
    if (collector == NULL)
        return;
    if (collector->status == CMETA_OK)
        collector->status = status;
    collector->state = CMETA_COLLECTOR_ABORTED;
}

static inline cmeta_status cmeta_collector_begin(cmeta_collector *collector) {
    cmeta_status status;

    if (collector == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (collector->state != CMETA_COLLECTOR_ZERO)
        return CMETA_INVALID_ARGUMENT;
    if (collector->zero_output == NULL || collector->input_type == NULL ||
        !cmeta_collector_ops_valid(collector->ops)) {
        cmeta_collector_terminate_pre_begin(collector, CMETA_INVALID_ARGUMENT);
        return collector->status;
    }
    status = collector->ops->begin(collector->context, collector->input_type,
                                   collector->limit);
    if (!cmeta_collector_callback_status_valid(status))
        status = CMETA_CALLBACK_ERROR;
    if (status != CMETA_OK) {
        cmeta_collector_fail(collector, status);
        return collector->status;
    }
    collector->state = CMETA_COLLECTOR_BEGUN;
    return CMETA_OK;
}

static inline cmeta_status cmeta_collector_accept(
    cmeta_collector *collector, const cmeta_type_desc *type, const void *value) {
    cmeta_status status;

    if (collector == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (collector->state != CMETA_COLLECTOR_BEGUN &&
        collector->state != CMETA_COLLECTOR_ACCEPTING)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_collector_ops_valid(collector->ops) || type == NULL ||
        value == NULL) {
        cmeta_collector_fail(collector, CMETA_INVALID_ARGUMENT);
        return collector->status;
    }
    if (!cmeta_type_equal(collector->input_type, type)) {
        cmeta_collector_fail(collector, CMETA_TYPE_MISMATCH);
        return collector->status;
    }
    if (collector->count >= collector->limit) {
        cmeta_collector_fail(collector, CMETA_CAPACITY_EXCEEDED);
        return collector->status;
    }
    status = collector->ops->accept(collector->context, value);
    if (!cmeta_collector_callback_status_valid(status))
        status = CMETA_CALLBACK_ERROR;
    if (status != CMETA_OK) {
        cmeta_collector_fail(collector, status);
        return collector->status;
    }
    ++collector->count;
    collector->state = CMETA_COLLECTOR_ACCEPTING;
    return CMETA_OK;
}

static inline cmeta_status cmeta_collector_finish(cmeta_collector *collector) {
    cmeta_status status;

    if (collector == NULL)
        return CMETA_INVALID_ARGUMENT;
    if (collector->state != CMETA_COLLECTOR_BEGUN &&
        collector->state != CMETA_COLLECTOR_ACCEPTING)
        return CMETA_INVALID_ARGUMENT;
    if (!cmeta_collector_ops_valid(collector->ops)) {
        cmeta_collector_fail(collector, CMETA_INVALID_ARGUMENT);
        return collector->status;
    }
    status = collector->ops->finish(collector->context);
    if (!cmeta_collector_callback_status_valid(status))
        status = CMETA_CALLBACK_ERROR;
    if (status != CMETA_OK) {
        cmeta_collector_fail(collector, status);
        return collector->status;
    }
    collector->state = CMETA_COLLECTOR_COMMITTED;
    return CMETA_OK;
}

static inline void cmeta_collector_abort(cmeta_collector *collector) {
    if (collector == NULL || collector->state != CMETA_COLLECTOR_BEGUN &&
        collector->state != CMETA_COLLECTOR_ACCEPTING)
        return;
    if (collector->ops != NULL && collector->ops->abort != NULL)
        collector->ops->abort(collector->context);
    collector->state = CMETA_COLLECTOR_ABORTED;
}

#ifdef __cplusplus
}
#endif
#endif /* CMETA_COLLECTOR_H */
