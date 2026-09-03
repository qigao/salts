#ifndef CFLOW_MACHINE_H
#define CFLOW_MACHINE_H

#include <cflow/event.h>
#include <cflow/generated/machine_schema.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t cflow_machine_state_id;
typedef uint32_t cflow_machine_guard_id;
typedef uint32_t cflow_machine_action_id;

/* CMake exports the library's configured limits to consumers. Defaults keep
 * direct header use consistent with a default Salts build. */
#ifndef CFLOW_MACHINE_MAX_STATES
#define CFLOW_MACHINE_MAX_STATES 65536u
#endif
#ifndef CFLOW_MACHINE_MAX_EVENTS
#define CFLOW_MACHINE_MAX_EVENTS 65536u
#endif
#ifndef CFLOW_MACHINE_MAX_GUARDS
#define CFLOW_MACHINE_MAX_GUARDS 65536u
#endif
#ifndef CFLOW_MACHINE_MAX_ACTIONS
#define CFLOW_MACHINE_MAX_ACTIONS 65536u
#endif
#ifndef CFLOW_MACHINE_MAX_TRANSITIONS
#define CFLOW_MACHINE_MAX_TRANSITIONS 1048576u
#endif

typedef enum cflow_machine_state_kind {
#define CFLOW_MACHINE_STATE_KIND_ROW(name, value) \
    CFLOW_MACHINE_STATE_##name = value,
    CFLOW_MACHINE_STATE_KIND_ROWS(CFLOW_MACHINE_STATE_KIND_ROW)
#undef CFLOW_MACHINE_STATE_KIND_ROW
} cflow_machine_state_kind;

typedef enum cflow_machine_action_observation {
#define CFLOW_MACHINE_ACTION_OBSERVATION_ROW(name, value) \
    CFLOW_MACHINE_ACTION_##name = value,
    CFLOW_MACHINE_ACTION_OBSERVATION_ROWS( \
        CFLOW_MACHINE_ACTION_OBSERVATION_ROW)
#undef CFLOW_MACHINE_ACTION_OBSERVATION_ROW
} cflow_machine_action_observation;

typedef enum cflow_machine_status {
    CFLOW_MACHINE_OK = 0,
    CFLOW_MACHINE_INVALID_ARGUMENT,
    CFLOW_MACHINE_LIMIT_EXCEEDED,
    CFLOW_MACHINE_ALLOCATION_FAILED,
    CFLOW_MACHINE_EMPTY,
    CFLOW_MACHINE_INVALID_ID,
    CFLOW_MACHINE_DUPLICATE_ID,
    CFLOW_MACHINE_INVALID_TYPE,
    CFLOW_MACHINE_UNKNOWN_STATE,
    CFLOW_MACHINE_UNKNOWN_EVENT,
    CFLOW_MACHINE_UNKNOWN_GUARD,
    CFLOW_MACHINE_UNKNOWN_ACTION,
    CFLOW_MACHINE_TYPE_MISMATCH,
    CFLOW_MACHINE_INVALID_CONTRACT,
    CFLOW_MACHINE_INVALID_OBSERVATION,
    CFLOW_MACHINE_TERMINAL_TRANSITION,
    CFLOW_MACHINE_AMBIGUOUS_TRANSITION,
    CFLOW_MACHINE_UNREACHABLE_STATE,
    CFLOW_MACHINE_UNUSED_DECLARATION
} cflow_machine_status;

typedef struct cflow_machine_state {
    cflow_machine_state_id id;
    const cmeta_type_desc *value_type;
    cflow_machine_state_kind kind;
} cflow_machine_state;

typedef struct cflow_machine_guard {
    cflow_machine_guard_id id;
    const cmeta_type_desc *state_type;
    cflow_event_id event_id;
    const cmeta_type_desc *event_type;
    cmeta_effects effects;
    cmeta_properties properties;
} cflow_machine_guard;

typedef struct cflow_machine_action {
    cflow_machine_action_id id;
    const cmeta_type_desc *source_type;
    cflow_event_id event_id;
    const cmeta_type_desc *event_type;
    const cmeta_type_desc *target_type;
    cmeta_effects effects;
    cmeta_properties properties;
    cflow_machine_action_observation observation;
    const cmeta_type_desc *output_type;
    cflow_event_id output_event_id;
} cflow_machine_action;

typedef struct cflow_machine_transition {
    cflow_machine_state_id source;
    cflow_event_id event;
    cflow_machine_guard_id guard;
    cflow_machine_action_id action;
    cflow_machine_state_id target;
    uint32_t priority;
} cflow_machine_transition;

typedef struct cflow_machine_definition {
    const cflow_machine_state *states;
    size_t state_count;
    cflow_machine_state_id initial_state;
    const cflow_event_type *events;
    size_t event_count;
    const cflow_machine_guard *guards;
    size_t guard_count;
    const cflow_machine_action *actions;
    size_t action_count;
    const cflow_machine_transition *transitions;
    size_t transition_count;
} cflow_machine_definition;

typedef struct cflow_machine {
    void *impl;
} cflow_machine;

/**
 * Copy, normalize, validate, and atomically publish one immutable Machine.
 * Input arrays are borrowed for this call. CMeta descriptors are borrowed
 * until destroy. `out` must be empty; failure leaves it empty.
 *
 * @param out Zero-initialized destination that owns the published Machine.
 * @param definition Borrowed declaration arrays and their exact counts.
 * @return CFLOW_MACHINE_OK on success, otherwise a validation or allocation
 * error from cflow_machine_status. On failure, `out->impl` remains NULL.
 */
cflow_machine_status cflow_machine_build(
    cflow_machine *out, const cflow_machine_definition *definition);

/**
 * Release one Machine and clear its handle.
 *
 * @param machine Owning handle, or NULL. Control-plane destruction requires
 * all readers to be quiescent.
 */
void cflow_machine_destroy(cflow_machine *machine);

/** Return the initial state ID, or zero for an empty/NULL handle. */
cflow_machine_state_id cflow_machine_initial_state(
    const cflow_machine *machine);

/** Return normalized declaration counts, or zero for an empty/NULL handle. */
size_t cflow_machine_state_count(const cflow_machine *machine);
size_t cflow_machine_event_count(const cflow_machine *machine);
size_t cflow_machine_guard_count(const cflow_machine *machine);
size_t cflow_machine_action_count(const cflow_machine *machine);
size_t cflow_machine_transition_count(const cflow_machine *machine);

/**
 * Return a borrowed normalized declaration at `index`, or NULL when the
 * handle is empty/NULL or the index is out of range. The pointer is invalid
 * after cflow_machine_destroy().
 */
const cflow_machine_state *cflow_machine_state_at(
    const cflow_machine *machine, size_t index);
const cflow_event_type *cflow_machine_event_at(
    const cflow_machine *machine, size_t index);
const cflow_machine_guard *cflow_machine_guard_at(
    const cflow_machine *machine, size_t index);
const cflow_machine_action *cflow_machine_action_at(
    const cflow_machine *machine, size_t index);
const cflow_machine_transition *cflow_machine_transition_at(
    const cflow_machine *machine, size_t index);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_MACHINE_H */
