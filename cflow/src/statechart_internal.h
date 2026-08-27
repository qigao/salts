#ifndef CFLOW_STATECHART_INTERNAL_H
#define CFLOW_STATECHART_INTERNAL_H

#include <cflow/statechart.h>

#include <stddef.h>

typedef struct cflow_statechart_impl {
    const cmeta_type_desc *state_type;
    cflow_statechart_state *states;
    size_t state_count;
    size_t *parents;
    size_t *depths;
    size_t root;
    /* document rank -> dense state index */
    size_t *document_order_indices;
    /* state dense index -> document-ordered direct-child span */
    size_t *child_offsets;
    size_t *children;
    cflow_event_type *events;
    size_t event_count;
    cflow_statechart_guard *guards;
    size_t guard_count;
    cflow_statechart_executable *executables;
    size_t executable_count;
    cflow_statechart_transition *transitions;
    size_t transition_count;
    /* source state dense index -> normalized transition span */
    size_t *transition_offsets;
    size_t *transition_indices;
    size_t *transition_domains;
    size_t *default_transition_indices;
    size_t *default_target_indices;
    cflow_statechart_state_action *state_actions;
    size_t state_action_count;
    /* state dense index * 2 + action kind -> ordered action span */
    size_t *state_action_offsets;
    size_t *state_action_indices;
    cflow_statechart_transition_action *transition_actions;
    size_t transition_action_count;
    /* transition dense index -> ordered action span */
    size_t *transition_action_offsets;
    size_t *transition_action_indices;
} cflow_statechart_impl;

/*
 * Dense indices address ID-sorted public rows. Span entries are deterministic:
 * children use document order, transitions use trigger key/lower numeric
 * priority/document order, and actions use declared action order. Offsets have
 * owner_count + 1
 * entries. Missing defaults, targetless domains, and the root parent use
 * SIZE_MAX. A targetless transition uses SIZE_MAX for no exit domain. A
 * target-bearing external transition with no modeled proper common compound
 * ancestor also uses SIZE_MAX for the virtual outer domain; its nonzero target
 * distinguishes the full-configuration exit case. Internal transitions use
 * their compound source as domain only for a proper descendant target. All
 * arrays are immutable after a successful build.
 */

/** Private borrowed normalized IR; invalid after cflow_statechart_destroy(). */
const cflow_statechart_impl *cflow_statechart_internal_get(
    const cflow_statechart *statechart);

#endif /* CFLOW_STATECHART_INTERNAL_H */
