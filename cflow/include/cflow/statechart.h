#ifndef CFLOW_STATECHART_H
#define CFLOW_STATECHART_H

#include <cflow/event.h>
#include <cflow/machine.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFLOW_STATECHART_MAX_ACTION_REFS
#define CFLOW_STATECHART_MAX_ACTION_REFS 1048576u
#endif

typedef uint32_t cflow_statechart_transition_id;
typedef uint32_t cflow_statechart_guard_id;
typedef uint32_t cflow_statechart_executable_id;

typedef enum cflow_statechart_state_kind {
    CFLOW_STATECHART_ATOMIC = 0,
    CFLOW_STATECHART_COMPOUND,
    CFLOW_STATECHART_PARALLEL,
    CFLOW_STATECHART_INITIAL,
    CFLOW_STATECHART_FINAL,
    CFLOW_STATECHART_HISTORY_SHALLOW,
    CFLOW_STATECHART_HISTORY_DEEP
} cflow_statechart_state_kind;

typedef enum cflow_statechart_trigger_kind {
    CFLOW_STATECHART_TRIGGER_EVENTLESS = 0,
    CFLOW_STATECHART_TRIGGER_EVENT,
    CFLOW_STATECHART_TRIGGER_COMPLETION
} cflow_statechart_trigger_kind;

typedef enum cflow_statechart_transition_kind {
    CFLOW_STATECHART_TRANSITION_EXTERNAL = 0,
    CFLOW_STATECHART_TRANSITION_INTERNAL
} cflow_statechart_transition_kind;

typedef enum cflow_statechart_state_action_kind {
    CFLOW_STATECHART_STATE_ACTION_ENTRY = 0,
    CFLOW_STATECHART_STATE_ACTION_EXIT
} cflow_statechart_state_action_kind;

typedef enum cflow_statechart_status {
    CFLOW_STATECHART_OK = 0,
    CFLOW_STATECHART_INVALID_ARGUMENT,
    CFLOW_STATECHART_EMPTY,
    CFLOW_STATECHART_LIMIT_EXCEEDED,
    CFLOW_STATECHART_ALLOCATION_FAILED,
    CFLOW_STATECHART_INVALID_ID,
    CFLOW_STATECHART_DUPLICATE_ID,
    CFLOW_STATECHART_DUPLICATE_ORDER,
    CFLOW_STATECHART_INVALID_TYPE,
    CFLOW_STATECHART_INVALID_CONTRACT,
    CFLOW_STATECHART_INVALID_PARENT,
    CFLOW_STATECHART_INVALID_TREE,
    CFLOW_STATECHART_INVALID_STATE_KIND,
    CFLOW_STATECHART_INVALID_INITIAL,
    CFLOW_STATECHART_INVALID_HISTORY,
    CFLOW_STATECHART_INVALID_TRIGGER,
    CFLOW_STATECHART_INVALID_COMPLETION,
    CFLOW_STATECHART_UNKNOWN_STATE,
    CFLOW_STATECHART_UNKNOWN_EVENT,
    CFLOW_STATECHART_UNKNOWN_GUARD,
    CFLOW_STATECHART_UNKNOWN_EXECUTABLE,
    CFLOW_STATECHART_UNKNOWN_TRANSITION,
    CFLOW_STATECHART_TYPE_MISMATCH,
    CFLOW_STATECHART_AMBIGUOUS_TRANSITION,
    CFLOW_STATECHART_UNUSED_DECLARATION
} cflow_statechart_status;

typedef struct cflow_statechart_state {
    cflow_machine_state_id id;
    cflow_machine_state_id parent;
    cflow_statechart_state_kind kind;
    uint32_t document_order;
} cflow_statechart_state;

typedef struct cflow_statechart_guard {
    cflow_statechart_guard_id id;
    const cmeta_type_desc *state_type;
    cmeta_effects effects;
    cmeta_properties properties;
} cflow_statechart_guard;

typedef struct cflow_statechart_executable {
    cflow_statechart_executable_id id;
    const cmeta_type_desc *state_type;
    cmeta_effects effects;
    cmeta_properties properties;
} cflow_statechart_executable;

typedef struct cflow_statechart_transition {
    cflow_statechart_transition_id id;
    cflow_machine_state_id source;
    cflow_statechart_trigger_kind trigger;
    cflow_event_id event;
    cflow_machine_state_id completion;
    cflow_statechart_guard_id guard;
    /** Target state/history ID, or zero for a targetless transition. */
    cflow_machine_state_id target;
    cflow_statechart_transition_kind kind;
    uint32_t priority;
    uint32_t document_order;
} cflow_statechart_transition;

typedef struct cflow_statechart_state_action {
    cflow_machine_state_id state;
    cflow_statechart_state_action_kind kind;
    cflow_statechart_executable_id executable;
    uint32_t order;
} cflow_statechart_state_action;

typedef struct cflow_statechart_transition_action {
    cflow_statechart_transition_id transition;
    cflow_statechart_executable_id executable;
    uint32_t order;
} cflow_statechart_transition_action;

typedef struct cflow_statechart_definition {
    const cmeta_type_desc *state_type;
    const cflow_statechart_state *states;
    size_t state_count;
    const cflow_event_type *events;
    size_t event_count;
    const cflow_statechart_guard *guards;
    size_t guard_count;
    const cflow_statechart_executable *executables;
    size_t executable_count;
    const cflow_statechart_transition *transitions;
    size_t transition_count;
    const cflow_statechart_state_action *state_actions;
    size_t state_action_count;
    const cflow_statechart_transition_action *transition_actions;
    size_t transition_action_count;
} cflow_statechart_definition;

typedef struct cflow_statechart {
    void *impl;
} cflow_statechart;

/**
 * Copy, normalize, validate, and atomically publish one immutable Statechart.
 * `out` must point to a zero-initialized empty handle. Input rows are borrowed
 * only for this call. CMeta descriptors remain borrowed until destroy. A
 * failed build with an empty output leaves it empty; passing a nonempty output
 * returns INVALID_ARGUMENT and preserves its existing handle.
 */
cflow_statechart_status cflow_statechart_build(
    cflow_statechart *out, const cflow_statechart_definition *definition);

/** Destroy a quiescent Statechart and clear its handle. */
void cflow_statechart_destroy(cflow_statechart *statechart);

const cmeta_type_desc *cflow_statechart_state_type(
    const cflow_statechart *statechart);
size_t cflow_statechart_state_count(const cflow_statechart *statechart);
size_t cflow_statechart_event_count(const cflow_statechart *statechart);
size_t cflow_statechart_guard_count(const cflow_statechart *statechart);
size_t cflow_statechart_executable_count(const cflow_statechart *statechart);
size_t cflow_statechart_transition_count(const cflow_statechart *statechart);
size_t cflow_statechart_state_action_count(
    const cflow_statechart *statechart);
size_t cflow_statechart_transition_action_count(
    const cflow_statechart *statechart);

/** Borrowed normalized rows; each pointer is invalid after destroy. */
const cflow_statechart_state *cflow_statechart_state_at(
    const cflow_statechart *statechart, size_t index);
const cflow_event_type *cflow_statechart_event_at(
    const cflow_statechart *statechart, size_t index);
const cflow_statechart_guard *cflow_statechart_guard_at(
    const cflow_statechart *statechart, size_t index);
const cflow_statechart_executable *cflow_statechart_executable_at(
    const cflow_statechart *statechart, size_t index);
const cflow_statechart_transition *cflow_statechart_transition_at(
    const cflow_statechart *statechart, size_t index);
const cflow_statechart_state_action *cflow_statechart_state_action_at(
    const cflow_statechart *statechart, size_t index);
const cflow_statechart_transition_action *
cflow_statechart_transition_action_at(
    const cflow_statechart *statechart, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_STATECHART_H */
