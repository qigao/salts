#ifndef CFLOW_MACHINE_HIERARCHY_H
#define CFLOW_MACHINE_HIERARCHY_H

#include <cflow/clock.h>
#include <cflow/machine_runtime.h>
#include <cflow/timer_event.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_machine_hierarchy_status {
    CFLOW_MACHINE_HIERARCHY_OK = 0,
    CFLOW_MACHINE_HIERARCHY_INVALID_ARGUMENT,
    CFLOW_MACHINE_HIERARCHY_EMPTY,
    CFLOW_MACHINE_HIERARCHY_LIMIT_EXCEEDED,
    CFLOW_MACHINE_HIERARCHY_ALLOCATION_FAILED,
    CFLOW_MACHINE_HIERARCHY_INVALID_ID,
    CFLOW_MACHINE_HIERARCHY_DUPLICATE_ID,
    CFLOW_MACHINE_HIERARCHY_INVALID_TYPE,
    CFLOW_MACHINE_HIERARCHY_TYPE_MISMATCH,
    CFLOW_MACHINE_HIERARCHY_INVALID_PARENT,
    CFLOW_MACHINE_HIERARCHY_INVALID_INITIAL_CHILD,
    CFLOW_MACHINE_HIERARCHY_INVALID_STATE_KIND,
    CFLOW_MACHINE_HIERARCHY_UNKNOWN_STATE,
    CFLOW_MACHINE_HIERARCHY_AMBIGUOUS_TRANSITION,
    CFLOW_MACHINE_HIERARCHY_FLAT_MACHINE_REJECTED
} cflow_machine_hierarchy_status;

typedef struct cflow_machine_hierarchy_state {
    /** Unique nonzero node ID. */
    cflow_machine_state_id id;
    /** Parent node ID, or zero for the single root. */
    cflow_machine_state_id parent;
    /** Direct child entered by default, or zero for a leaf. */
    cflow_machine_state_id initial_child;
    const cmeta_type_desc *value_type;
    cflow_machine_state_kind kind;
} cflow_machine_hierarchy_state;

typedef struct cflow_machine_hierarchy_definition {
    const cflow_machine_hierarchy_state *states;
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
} cflow_machine_hierarchy_definition;

typedef struct cflow_machine_hierarchy_route {
    /** Leaf-to-parent exit order, excluding the least common ancestor. */
    const cflow_machine_state_id *exit_states;
    size_t exit_count;
    /** Parent-to-leaf entry order, excluding the least common ancestor. */
    const cflow_machine_state_id *entry_states;
    size_t entry_count;
} cflow_machine_hierarchy_route;

typedef struct cflow_machine_hierarchy {
    void *impl;
} cflow_machine_hierarchy;

/**
 * Copy, validate, and normalize a hierarchy into the existing flat Machine IR.
 * Declaration arrays are borrowed for this call; type descriptors remain
 * borrowed until destroy. Failure leaves `out` empty.
 *
 * All nodes must use the same state type. Transitions declared at a leaf win
 * before transitions inherited from ancestors; priorities order transitions
 * declared at the same node. Composite targets descend through initial_child.
 *
 * @param out Empty owning destination.
 * @param definition Borrowed hierarchy and flat Machine declarations.
 * @return Exact hierarchy validation/allocation status. FLAT_MACHINE_REJECTED
 * means the normalized declaration violated an existing Machine invariant.
 */
cflow_machine_hierarchy_status cflow_machine_hierarchy_build(
    cflow_machine_hierarchy *out,
    const cflow_machine_hierarchy_definition *definition);

/** Destroy a quiescent hierarchy and clear its handle. */
void cflow_machine_hierarchy_destroy(cflow_machine_hierarchy *hierarchy);

/** Borrowed flat Machine view, invalid after hierarchy destruction. */
const cflow_machine *cflow_machine_hierarchy_flat_machine(
    const cflow_machine_hierarchy *hierarchy);

/** Return copied hierarchy node count, or zero for an empty handle. */
size_t cflow_machine_hierarchy_state_count(
    const cflow_machine_hierarchy *hierarchy);

/** Return a borrowed node sorted by ID, or NULL when out of range. */
const cflow_machine_hierarchy_state *cflow_machine_hierarchy_state_at(
    const cflow_machine_hierarchy *hierarchy, size_t index);

/**
 * Return borrowed exit/entry paths for a normalized flat transition row.
 * Both path pointers are invalid after hierarchy destruction.
 *
 * @return true on success; false for an empty handle, NULL output, or an
 * out-of-range flat transition index.
 */
bool cflow_machine_hierarchy_route_at(
    const cflow_machine_hierarchy *hierarchy,
    size_t flat_transition_index,
    cflow_machine_hierarchy_route *out_route);

typedef enum cflow_machine_hierarchy_instance_status {
    CFLOW_MACHINE_HIERARCHY_INSTANCE_OK = 0,
    CFLOW_MACHINE_HIERARCHY_INSTANCE_INVALID_ARGUMENT,
    CFLOW_MACHINE_HIERARCHY_INSTANCE_ALLOCATION_FAILED,
    CFLOW_MACHINE_HIERARCHY_INSTANCE_MACHINE_REJECTED,
    CFLOW_MACHINE_HIERARCHY_INSTANCE_TIMER_REJECTED
} cflow_machine_hierarchy_instance_status;

typedef struct cflow_machine_hierarchy_instance_config {
    /** Borrowed immutable hierarchy; must outlive the instance. */
    const cflow_machine_hierarchy *hierarchy;
    const void *initial_state;
    const cmeta_type_desc *output_type;
    const cflow_machine_guard_binding *guards;
    size_t guard_count;
    const cflow_machine_action_binding *actions;
    size_t action_count;
    size_t mailbox_capacity;
    cflow_executor *executor;
    /** Borrowed monotonic Clock; must outlive the instance. */
    cflow_clock *clock;
    size_t timer_capacity;
} cflow_machine_hierarchy_instance_config;

typedef struct cflow_machine_hierarchy_instance_init_result {
    cflow_machine_hierarchy_instance_status status;
    cflow_machine_runtime_status machine_status;
    cflow_timer_event_status timer_status;
} cflow_machine_hierarchy_instance_init_result;

typedef struct cflow_machine_hierarchy_instance_stats {
    cflow_machine_instance_stats machine;
    cflow_timer_event_stats timers;
} cflow_machine_hierarchy_instance_stats;

typedef struct cflow_machine_hierarchy_instance {
    void *impl;
} cflow_machine_hierarchy_instance;

/**
 * Initialize one hierarchy runtime transactionally.
 *
 * The instance owns one flat Machine instance, bounded Mailbox, and bounded
 * Timer Event queue. It borrows every object referenced by `config` until
 * destroy. `machine_status` or `timer_status` preserves the nested rejection
 * when the top-level status names that subsystem.
 */
cflow_machine_hierarchy_instance_init_result
cflow_machine_hierarchy_instance_init(
    cflow_machine_hierarchy_instance *instance,
    const cflow_machine_hierarchy_instance_config *config);

/** Copy one typed Event into the inner bounded Machine Mailbox. */
cflow_mailbox_status cflow_machine_hierarchy_instance_try_send(
    cflow_machine_hierarchy_instance *instance,
    const cflow_event_view *event);

/** Attach the single allowed Resumable adapter to the inner Machine. */
bool cflow_machine_hierarchy_instance_as_resumable(
    cflow_machine_hierarchy_instance *instance,
    cflow_resumable *out);

/** Attach the single allowed Source adapter to the inner Machine. */
bool cflow_machine_hierarchy_instance_as_source(
    cflow_machine_hierarchy_instance *instance,
    cflow_source *out);

/**
 * Schedule one copied Event in an active state scope at an absolute deadline.
 * An unknown or currently inactive scope returns INVALID_ARGUMENT. Pending
 * timers are canceled when their scope appears in a committed exit path.
 */
cflow_timer_event_schedule_result
cflow_machine_hierarchy_instance_try_schedule_at(
    cflow_machine_hierarchy_instance *instance,
    cflow_machine_state_id scope,
    cflow_deadline deadline,
    const cflow_event_view *event);

/** Schedule one copied Event after a monotonic duration in an active scope. */
cflow_timer_event_schedule_result
cflow_machine_hierarchy_instance_try_schedule_after(
    cflow_machine_hierarchy_instance *instance,
    cflow_machine_state_id scope,
    cflow_duration delay,
    const cflow_event_view *event);

/** Cancel one pending scoped timer; FIRE_WON preserves an existing claim. */
cflow_timer_event_status cflow_machine_hierarchy_instance_cancel_timer(
    cflow_machine_hierarchy_instance *instance,
    cflow_timer_event_id timer_id);

/** Hand off at most one ready scoped Event to the inner Mailbox. */
cflow_timer_event_fire_result
cflow_machine_hierarchy_instance_run_one_ready(
    cflow_machine_hierarchy_instance *instance);

/** Gracefully stop timer/Event admission and preserve an in-flight commit. */
void cflow_machine_hierarchy_instance_close(
    cflow_machine_hierarchy_instance *instance);

/** Cancel timer/Event admission and discard an uncommitted transition. */
void cflow_machine_hierarchy_instance_cancel(
    cflow_machine_hierarchy_instance *instance);

/** Copy the inner Machine's single mutex-consistent state value. */
bool cflow_machine_hierarchy_instance_copy_state(
    const cflow_machine_hierarchy_instance *instance,
    const cmeta_type_desc **out_type,
    void *out_value,
    size_t out_value_capacity);

/** Return the inner Machine's current leaf state ID, or zero when empty. */
cflow_machine_state_id cflow_machine_hierarchy_instance_current_state(
    const cflow_machine_hierarchy_instance *instance);

/** Read bounded Timer Event and Machine statistics under the wrapper gate. */
bool cflow_machine_hierarchy_instance_get_stats(
    const cflow_machine_hierarchy_instance *instance,
    cflow_machine_hierarchy_instance_stats *out);

/** Return the inner Machine's first owned error, or NULL. */
const char *cflow_machine_hierarchy_instance_error(
    const cflow_machine_hierarchy_instance *instance);

/**
 * Close and destroy a quiescent instance. Attached Source/Resumable adapters
 * and all concurrent callers must already be released/quiescent. The borrowed
 * executor remains operational while destroy waits for any committed hook.
 */
void cflow_machine_hierarchy_instance_destroy(
    cflow_machine_hierarchy_instance *instance);

#ifdef __cplusplus
}
#endif

#endif /* CFLOW_MACHINE_HIERARCHY_H */
