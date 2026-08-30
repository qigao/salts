#include <cflow/scxml.h>
#include <tlog.h>
#include <turbo/thread.h>
#include <turbo_uuid.h>

#include "cmeta_expr.h"
#include "cmeta_assign.h"
#include "cmeta_foreach.h"

#include <ctype.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CFLOW_SCXML_NAMESPACE "http://www.w3.org/2005/07/scxml"
#define CFLOW_SCXML_DEFAULT_MAX_STATES 65536u
#define CFLOW_SCXML_DEFAULT_MAX_EVENTS 65536u
#define CFLOW_SCXML_DEFAULT_MAX_TRANSITIONS 1048576u
#define CFLOW_SCXML_DEFAULT_MAX_NAME_BYTES (16u * 1024u * 1024u)

static const char SCXML_ERROR_EXECUTION_EVENT[] = "error.execution";
static const char SCXML_ERROR_COMMUNICATION_EVENT[] = "error.communication";

typedef enum scxml_data_model {
    SCXML_DATA_MODEL_NULL = 0,
    SCXML_DATA_MODEL_CMETA
} scxml_data_model;

typedef struct scxml_name_ref {
    turbo_xml_string_view name;
    turbo_xml_location location;
    uint64_t id;
    size_t order;
} scxml_name_ref;

typedef struct scxml_program_name {
    const char *name;
    size_t size;
    uint64_t id;
} scxml_program_name;

typedef struct scxml_node_ref {
    const void *node;
    cflow_machine_state_id id;
} scxml_node_ref;

typedef struct scxml_synthetic_initial {
    cflow_machine_state_id parent;
    cflow_machine_state_id state;
    turbo_xml_string_view target;
    turbo_xml_location location;
} scxml_synthetic_initial;

typedef enum scxml_step_kind {
    SCXML_STEP_RAISE = 1,
    SCXML_STEP_IF,
    SCXML_STEP_LOG,
    SCXML_STEP_ASSIGN,
    SCXML_STEP_FOREACH,
    SCXML_STEP_SEND,
    SCXML_STEP_CANCEL,
    SCXML_STEP_INVOKE_ENTER,
    SCXML_STEP_INVOKE_EXIT
} scxml_step_kind;

typedef enum scxml_effect_kind {
    SCXML_EFFECT_SEND = 1,
    SCXML_EFFECT_CANCEL
} scxml_effect_kind;

typedef struct scxml_effect_descriptor {
    scxml_effect_kind kind;
    cflow_event_id event_id;
    bool internal_target;
    union {
        cflow_scxml_send_request send;
        cflow_scxml_cancel_request cancel;
    } request;
} scxml_effect_descriptor;

typedef struct scxml_step {
    scxml_step_kind kind;
    size_t next;
    cflow_event_id event;
    size_t branch_first;
    size_t branch_count;
    const char *label;
    size_t effect;
    size_t invocation;
    size_t assignment;
    size_t foreach_descriptor;
} scxml_step;

typedef struct scxml_foreach_descriptor {
    cflow_scxml_cmeta_foreach_program program;
    size_t step_begin;
    size_t step_end;
} scxml_foreach_descriptor;

typedef struct scxml_branch {
    cflow_machine_state_id state;
    cflow_scxml_cmeta_expr_program condition;
    size_t step_begin;
    size_t step_end;
    bool unconditional;
} scxml_branch;

typedef struct scxml_block {
    const cmeta_type_desc *state_type;
    const scxml_step *steps;
    const scxml_branch *branches;
    const scxml_effect_descriptor *effects;
    const cflow_scxml_cmeta_assign_program *assignments;
    const scxml_foreach_descriptor *foreach_descriptors;
    const struct scxml_invocation_descriptor *invocations;
    size_t step_begin;
    size_t step_end;
    size_t step_storage_count;
    size_t branch_storage_count;
    size_t effect_storage_count;
    size_t assignment_storage_count;
    size_t foreach_storage_count;
    size_t invocation_storage_count;
    size_t max_conditional_depth;
    cflow_event_id execution_error_event;
    const scxml_program_name *const *event_names_by_id;
    size_t event_name_count;
    cflow_scxml_cmeta_expr_system_values system_values;
} scxml_block;

typedef struct scxml_invocation_descriptor {
    cflow_machine_state_id owner;
    const char *id;
    size_t id_size;
    const char *type;
    size_t type_size;
    const char *src;
    size_t src_size;
    const char *done_name;
    size_t done_name_size;
    cflow_event_id done_event;
    const scxml_block *finalize;
    const void *source_node;
    bool autoforward;
} scxml_invocation_descriptor;

typedef struct scxml_guard_user {
    scxml_data_model data_model;
    union {
        cflow_machine_state_id state;
        cflow_scxml_cmeta_expr_program expression;
    } value;
    const scxml_program_name *const *event_names_by_id;
    size_t event_name_count;
    cflow_scxml_cmeta_expr_system_values system_values;
} scxml_guard_user;

typedef struct scxml_counts {
    size_t state_rows;
    size_t node_refs;
    size_t state_names;
    size_t synthetic_initials;
    size_t transition_rows;
    size_t guard_rows;
    size_t event_occurrences;
    size_t executable_blocks;
    size_t block_rows;
    size_t executable_steps;
    size_t log_label_bytes;
    size_t effect_rows;
    size_t assignment_rows;
    size_t foreach_rows;
    size_t effect_string_bytes;
    size_t conditional_branches;
    size_t max_conditional_depth;
    size_t state_action_rows;
    size_t transition_action_rows;
    size_t invocation_rows;
    size_t invocation_string_bytes;
    uint32_t requirements;
} scxml_counts;

typedef struct scxml_build {
    cflow_scxml_limits limits;
    cflow_scxml_diagnostic *diagnostic;
    scxml_data_model data_model;
    const cmeta_data_desc *cmeta_root;
    cflow_scxml_cmeta_expr_limits cmeta_expression_limits;
    cflow_statechart_state *states;
    cflow_statechart_transition *transitions;
    cflow_statechart_guard *guards;
    cflow_event_type *events;
    cflow_statechart_executable *executables;
    cflow_statechart_state_action *state_actions;
    cflow_statechart_transition_action *transition_actions;
    cflow_statechart_executable_binding *bindings;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_guard_user *guard_users;
    scxml_block *blocks;
    scxml_step *steps;
    scxml_branch *branches;
    scxml_effect_descriptor *effects;
    cflow_scxml_cmeta_assign_program *assignments;
    scxml_foreach_descriptor *foreach_descriptors;
    scxml_invocation_descriptor *invocations;
    scxml_name_ref *invocation_names;
    char *log_storage;
    char *effect_storage;
    char *invocation_storage;
    scxml_name_ref *state_names;
    scxml_name_ref *event_names;
    scxml_name_ref *event_occurrences;
    scxml_node_ref *node_refs;
    scxml_synthetic_initial *synthetic_initials;
    size_t state_index;
    size_t node_ref_index;
    size_t state_name_index;
    size_t synthetic_index;
    size_t transition_index;
    size_t guard_index;
    size_t event_occurrence_index;
    size_t event_name_count;
    size_t executable_index;
    size_t block_index;
    size_t step_index;
    size_t branch_index;
    size_t effect_index;
    size_t assignment_index;
    size_t foreach_index;
    size_t log_storage_index;
    size_t effect_storage_index;
    size_t step_capacity;
    size_t branch_capacity;
    size_t effect_capacity;
    size_t assignment_capacity;
    size_t foreach_capacity;
    size_t max_iterations;
    size_t log_storage_capacity;
    size_t effect_storage_capacity;
    size_t guard_capacity;
    size_t max_conditional_depth;
    size_t state_action_index;
    size_t transition_action_index;
    size_t invocation_index;
    size_t invocation_emit_index;
    size_t invocation_storage_index;
    size_t invocation_storage_capacity;
    size_t invocation_capacity;
    uint32_t requirements;
    cflow_event_id execution_error_event;
} scxml_build;

typedef struct cflow_scxml_program_impl {
    cflow_statechart statechart;
    scxml_data_model data_model;
    const cmeta_data_desc *cmeta_root;
    scxml_program_name *state_names;
    size_t state_name_count;
    scxml_program_name *event_names;
    const scxml_program_name **event_names_by_id;
    size_t event_name_count;
    cflow_statechart_executable_binding *bindings;
    size_t binding_count;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_guard_user *guard_users;
    size_t guard_binding_count;
    scxml_block *blocks;
    scxml_step *steps;
    scxml_branch *branches;
    size_t branch_count;
    scxml_effect_descriptor *effects;
    cflow_scxml_cmeta_assign_program *assignments;
    size_t assignment_count;
    scxml_foreach_descriptor *foreach_descriptors;
    size_t foreach_count;
    scxml_invocation_descriptor *invocations;
    size_t invocation_count;
    const char *document_name;
    size_t document_name_size;
    char *name_storage;
    char *log_storage;
    char *effect_storage;
    char *invocation_storage;
    cflow_event_id execution_error_event;
    cflow_event_id communication_error_event;
    uint32_t requirements;
    bool null_value;
} cflow_scxml_program_impl;

typedef struct cflow_scxml_session_impl cflow_scxml_session_impl;

typedef struct scxml_session_binding_user {
    const scxml_block *block;
    cflow_scxml_session_impl *session;
} scxml_session_binding_user;

typedef struct scxml_session_guard_user {
    const scxml_guard_user *guard;
    cflow_scxml_session_impl *session;
} scxml_session_guard_user;

typedef enum scxml_delayed_state {
    SCXML_DELAYED_FREE = 0,
    SCXML_DELAYED_RESERVED,
    SCXML_DELAYED_ACTIVE,
    SCXML_DELAYED_CANCEL_RESERVED
} scxml_delayed_state;

typedef struct scxml_delayed_send {
    const char *id;
    size_t id_size;
    scxml_delayed_state state;
    scxml_delayed_state previous_state;
} scxml_delayed_send;

typedef enum scxml_prepared_kind {
    SCXML_PREPARED_SEND = 1,
    SCXML_PREPARED_DELAYED_SEND,
    SCXML_PREPARED_CANCEL
} scxml_prepared_kind;

typedef struct scxml_prepared_effect {
    cflow_scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    size_t registry_index;
    scxml_prepared_kind kind;
    bool in_use;
} scxml_prepared_effect;

typedef enum scxml_invocation_state {
    SCXML_INVOCATION_INACTIVE = 0,
    SCXML_INVOCATION_PENDING,
    SCXML_INVOCATION_STARTING,
    SCXML_INVOCATION_ACTIVE,
    SCXML_INVOCATION_FAILED
} scxml_invocation_state;

typedef struct scxml_invocation_row {
    uint64_t token;
    scxml_invocation_state state;
} scxml_invocation_row;

typedef struct scxml_invocation_lifecycle_effect {
    cflow_scxml_session_impl *session;
    size_t invocation;
    bool enter;
    bool in_use;
} scxml_invocation_lifecycle_effect;

struct cflow_scxml_session_impl {
    const cflow_scxml_program_impl *program;
    cflow_statechart_instance instance;
    cflow_statechart_executable_binding *bindings;
    scxml_session_binding_user *binding_users;
    size_t binding_count;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_session_guard_user *guard_users;
    size_t guard_binding_count;
    char *system_name;
    char session_id[TURBO_UUID_STRING_SIZE];
    cflow_scxml_cmeta_expr_system_values system_values;
    cflow_scxml_event_io_adapter_v1 event_io;
    void *adapter_user;
    turbo_mutex_t registry_lock;
    scxml_delayed_send *delayed_sends;
    size_t delayed_send_capacity;
    scxml_prepared_effect *prepared_effects;
    size_t prepared_effect_capacity;
    cflow_scxml_invoke_adapter_v1 invoke;
    void *invoke_user;
    scxml_invocation_row *invocation_rows;
    size_t invocation_capacity;
    scxml_invocation_lifecycle_effect *invocation_effects;
    size_t invocation_effect_capacity;
    cflow_scxml_invoke_stats invoke_stats;
    uint64_t next_invocation_token;
    bool has_event_io;
    bool has_invoke;
    atomic_bool adapter_close_called;
    atomic_bool invoke_close_called;
};

typedef enum scxml_element_kind {
    SCXML_ELEMENT_UNKNOWN = 0,
    SCXML_ELEMENT_SCXML,
    SCXML_ELEMENT_STATE,
    SCXML_ELEMENT_PARALLEL,
    SCXML_ELEMENT_TRANSITION,
    SCXML_ELEMENT_INITIAL,
    SCXML_ELEMENT_FINAL,
    SCXML_ELEMENT_HISTORY,
    SCXML_ELEMENT_ONENTRY,
    SCXML_ELEMENT_ONEXIT,
    SCXML_ELEMENT_RAISE,
    SCXML_ELEMENT_SEND,
    SCXML_ELEMENT_CANCEL,
    SCXML_ELEMENT_LOG,
    SCXML_ELEMENT_ASSIGN,
    SCXML_ELEMENT_FOREACH,
    SCXML_ELEMENT_IF,
    SCXML_ELEMENT_ELSEIF,
    SCXML_ELEMENT_ELSE,
    SCXML_ELEMENT_INVOKE,
    SCXML_ELEMENT_FINALIZE
} scxml_element_kind;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool checked_multiply(size_t left, size_t right, size_t *out) {
    if (out == NULL || (left != 0u && right > SIZE_MAX / left)) return false;
    *out = left * right;
    return true;
}

static bool view_equal(turbo_xml_string_view left,
                       turbo_xml_string_view right) {
    return left.size == right.size &&
           (left.size == 0u || memcmp(left.data, right.data, left.size) == 0);
}

static bool view_equal_raw(turbo_xml_string_view view, const char *raw) {
    const size_t size = strlen(raw);
    return view.size == size &&
           (size == 0u || memcmp(view.data, raw, size) == 0);
}

static int compare_view(turbo_xml_string_view left,
                        turbo_xml_string_view right) {
    const size_t common = left.size < right.size ? left.size : right.size;
    const int compared = common != 0u ? memcmp(left.data, right.data, common) : 0;
    if (compared != 0) return compared;
    if (left.size < right.size) return -1;
    if (left.size > right.size) return 1;
    return 0;
}

static int compare_name_ref(const void *left, const void *right) {
    return compare_view(((const scxml_name_ref *)left)->name,
                        ((const scxml_name_ref *)right)->name);
}

static int compare_name_order(const void *left, const void *right) {
    const size_t left_order = ((const scxml_name_ref *)left)->order;
    const size_t right_order = ((const scxml_name_ref *)right)->order;
    return left_order < right_order ? -1 : left_order > right_order ? 1 : 0;
}

static const scxml_name_ref *find_earliest_duplicate(
        scxml_name_ref *names, size_t count) {
    const scxml_name_ref *earliest = NULL;
    size_t group_begin = 0u;

    while (group_begin < count) {
        const scxml_name_ref *first = NULL;
        const scxml_name_ref *second = NULL;
        size_t group_end = group_begin + 1u;
        size_t index;

        while (group_end < count &&
               view_equal(names[group_begin].name, names[group_end].name)) {
            ++group_end;
        }
        for (index = group_begin; index < group_end; ++index) {
            const scxml_name_ref *candidate = &names[index];
            if (first == NULL || candidate->order < first->order) {
                second = first;
                first = candidate;
            } else if (second == NULL || candidate->order < second->order) {
                second = candidate;
            }
        }
        if (second != NULL &&
            (earliest == NULL || second->order < earliest->order)) {
            earliest = second;
        }
        group_begin = group_end;
    }
    return earliest;
}

static int compare_node_ref(const void *left, const void *right) {
    const uintptr_t left_node =
        (uintptr_t)((const scxml_node_ref *)left)->node;
    const uintptr_t right_node =
        (uintptr_t)((const scxml_node_ref *)right)->node;
    return left_node < right_node ? -1 : left_node > right_node ? 1 : 0;
}

static int compare_program_name(const void *left, const void *right) {
    const scxml_program_name *left_name = (const scxml_program_name *)left;
    const scxml_program_name *right_name = (const scxml_program_name *)right;
    const turbo_xml_string_view left_view = {left_name->name, left_name->size};
    const turbo_xml_string_view right_view = {right_name->name, right_name->size};
    return compare_view(left_view, right_view);
}

static bool bind_current_event_system_values(
    const cflow_scxml_cmeta_expr_system_values *base,
    const scxml_program_name *const *event_names_by_id,
    size_t event_name_count,
    const cflow_event_view *event,
    cflow_scxml_cmeta_expr_system_values *out) {
    const scxml_program_name *name;
    if (base == NULL || out == NULL) return false;
    *out = *base;
    out->event_name = (cflow_scxml_cmeta_expr_string_view){NULL, 0u};
    if (event == NULL) return true;
    if (event_names_by_id == NULL || event->id == 0u ||
        event->id > event_name_count)
        return false;
    name = event_names_by_id[event->id - 1u];
    if (name == NULL || name->id != event->id) return false;
    out->event_name = (cflow_scxml_cmeta_expr_string_view){
        name->name, name->size};
    return true;
}

static cflow_scxml_status scxml_fail(scxml_build *build,
                                     cflow_scxml_status status,
                                     turbo_xml_location location,
                                     const char *message) {
    if (build != NULL && build->diagnostic != NULL) {
        build->diagnostic->status = status;
        build->diagnostic->location = location;
        (void)snprintf(build->diagnostic->message,
                       sizeof(build->diagnostic->message), "%s", message);
    }
    return status;
}

static bool is_empty_view(turbo_xml_string_view view) {
    return view.data == NULL || view.size == 0u;
}

static bool decode_utf8(const char *data, size_t size, size_t *cursor,
                        uint32_t *codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;

    if (lead <= 0x7fu) {
        *codepoint = lead;
        *cursor = start + 1u;
        return true;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2u;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3u;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4u;
        value = lead & 0x07u;
    } else {
        return false;
    }
    if (width > size - start) return false;
    for (index = 1u; index < width; ++index) {
        const unsigned char continuation = (unsigned char)data[start + index];
        if ((continuation & 0xc0u) != 0x80u) return false;
        value = (value << 6) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu) {
        return false;
    }
    *codepoint = value;
    *cursor = start + width;
    return true;
}

static bool is_ncname_start(uint32_t codepoint) {
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
           (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
           (codepoint >= 0xf8u && codepoint <= 0x2ffu) ||
           (codepoint >= 0x370u && codepoint <= 0x37du) ||
           (codepoint >= 0x37fu && codepoint <= 0x1fffu) ||
           (codepoint >= 0x200cu && codepoint <= 0x200du) ||
           (codepoint >= 0x2070u && codepoint <= 0x218fu) ||
           (codepoint >= 0x2c00u && codepoint <= 0x2fefu) ||
           (codepoint >= 0x3001u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xf900u && codepoint <= 0xfdcfu) ||
           (codepoint >= 0xfdf0u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0xeffffu);
}

static bool is_ncname_char(uint32_t codepoint) {
    return is_ncname_start(codepoint) || codepoint == '-' || codepoint == '.' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool is_xml_ncname(turbo_xml_string_view name) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (is_empty_view(name) ||
        !decode_utf8(name.data, name.size, &cursor, &codepoint) ||
        !is_ncname_start(codepoint)) {
        return false;
    }
    while (cursor < name.size) {
        if (!decode_utf8(name.data, name.size, &cursor, &codepoint) ||
            !is_ncname_char(codepoint)) {
            return false;
        }
    }
    return true;
}

static bool is_xml_nmtoken(turbo_xml_string_view token) {
    size_t cursor = 0u;
    uint32_t codepoint;
    if (is_empty_view(token)) return false;
    while (cursor < token.size) {
        if (!decode_utf8(token.data, token.size, &cursor, &codepoint) ||
            !(is_ncname_char(codepoint) || codepoint == ':')) {
            return false;
        }
    }
    return true;
}

static bool xml_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

static bool is_xml_whitespace(turbo_xml_string_view value) {
    size_t index;
    for (index = 0u; index < value.size; ++index) {
        if (!xml_space(value.data[index])) return false;
    }
    return true;
}

static void skip_xml_space(turbo_xml_string_view value, size_t *cursor) {
    while (*cursor < value.size && xml_space(value.data[*cursor]))
        ++*cursor;
}

static bool parse_null_in_condition(
    turbo_xml_string_view value, turbo_xml_string_view *out_state) {
    size_t cursor = 0u;
    size_t begin;
    if (out_state == NULL) return false;
    *out_state = (turbo_xml_string_view){NULL, 0u};
    skip_xml_space(value, &cursor);
    if (cursor > value.size || value.size - cursor < 2u ||
        memcmp(value.data + cursor, "In", 2u) != 0)
        return false;
    cursor += 2u;
    skip_xml_space(value, &cursor);
    if (cursor == value.size || value.data[cursor++] != '(') return false;
    skip_xml_space(value, &cursor);
    begin = cursor;
    while (cursor < value.size && value.data[cursor] != ')' &&
           !xml_space(value.data[cursor]))
        ++cursor;
    out_state->data = value.data + begin;
    out_state->size = cursor - begin;
    skip_xml_space(value, &cursor);
    if (cursor == value.size || value.data[cursor++] != ')') return false;
    skip_xml_space(value, &cursor);
    return cursor == value.size && is_xml_ncname(*out_state);
}

static scxml_element_kind element_kind(turbo_xml_node node) {
    const turbo_xml_string_view name = turbo_xml_node_local_name(node);
    if (view_equal_raw(name, "scxml")) return SCXML_ELEMENT_SCXML;
    if (view_equal_raw(name, "state")) return SCXML_ELEMENT_STATE;
    if (view_equal_raw(name, "parallel")) return SCXML_ELEMENT_PARALLEL;
    if (view_equal_raw(name, "transition")) return SCXML_ELEMENT_TRANSITION;
    if (view_equal_raw(name, "initial")) return SCXML_ELEMENT_INITIAL;
    if (view_equal_raw(name, "final")) return SCXML_ELEMENT_FINAL;
    if (view_equal_raw(name, "history")) return SCXML_ELEMENT_HISTORY;
    if (view_equal_raw(name, "onentry")) return SCXML_ELEMENT_ONENTRY;
    if (view_equal_raw(name, "onexit")) return SCXML_ELEMENT_ONEXIT;
    if (view_equal_raw(name, "raise")) return SCXML_ELEMENT_RAISE;
    if (view_equal_raw(name, "send")) return SCXML_ELEMENT_SEND;
    if (view_equal_raw(name, "cancel")) return SCXML_ELEMENT_CANCEL;
    if (view_equal_raw(name, "log")) return SCXML_ELEMENT_LOG;
    if (view_equal_raw(name, "assign")) return SCXML_ELEMENT_ASSIGN;
    if (view_equal_raw(name, "foreach")) return SCXML_ELEMENT_FOREACH;
    if (view_equal_raw(name, "if")) return SCXML_ELEMENT_IF;
    if (view_equal_raw(name, "elseif")) return SCXML_ELEMENT_ELSEIF;
    if (view_equal_raw(name, "else")) return SCXML_ELEMENT_ELSE;
    if (view_equal_raw(name, "invoke")) return SCXML_ELEMENT_INVOKE;
    if (view_equal_raw(name, "finalize")) return SCXML_ELEMENT_FINALIZE;
    return SCXML_ELEMENT_UNKNOWN;
}

static bool is_state_element(scxml_element_kind kind) {
    return kind == SCXML_ELEMENT_STATE || kind == SCXML_ELEMENT_PARALLEL ||
           kind == SCXML_ELEMENT_FINAL;
}

static turbo_xml_attribute find_attribute(turbo_xml_node node,
                                          const char *local_name) {
    turbo_xml_attribute result = {NULL};
    size_t index;
    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(node, index);
        const turbo_xml_string_view namespace_uri =
            turbo_xml_attribute_namespace_uri(attribute);
        if (!is_empty_view(namespace_uri)) continue;
        if (view_equal_raw(turbo_xml_attribute_local_name(attribute),
                           local_name)) {
            return attribute;
        }
    }
    return result;
}

static bool attribute_allowed(scxml_element_kind kind,
                              turbo_xml_string_view name) {
    switch (kind) {
        case SCXML_ELEMENT_SCXML:
            return view_equal_raw(name, "version") ||
                   view_equal_raw(name, "datamodel") ||
                   view_equal_raw(name, "initial") ||
                   view_equal_raw(name, "name") ||
                   view_equal_raw(name, "id");
        case SCXML_ELEMENT_STATE:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "initial");
        case SCXML_ELEMENT_PARALLEL:
        case SCXML_ELEMENT_FINAL:
            return view_equal_raw(name, "id");
        case SCXML_ELEMENT_HISTORY:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "type");
        case SCXML_ELEMENT_TRANSITION:
            return view_equal_raw(name, "event") ||
                   view_equal_raw(name, "target") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "cond");
        case SCXML_ELEMENT_RAISE:
            return view_equal_raw(name, "event");
        case SCXML_ELEMENT_SEND:
            return view_equal_raw(name, "event") ||
                   view_equal_raw(name, "target") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "id") ||
                   view_equal_raw(name, "delay");
        case SCXML_ELEMENT_CANCEL:
            return view_equal_raw(name, "sendid");
        case SCXML_ELEMENT_LOG:
            return view_equal_raw(name, "label") ||
                   view_equal_raw(name, "expr");
        case SCXML_ELEMENT_ASSIGN:
            return view_equal_raw(name, "location") ||
                   view_equal_raw(name, "expr");
        case SCXML_ELEMENT_FOREACH:
            return view_equal_raw(name, "array") ||
                   view_equal_raw(name, "item") ||
                   view_equal_raw(name, "index");
        case SCXML_ELEMENT_IF:
        case SCXML_ELEMENT_ELSEIF:
            return view_equal_raw(name, "cond");
        case SCXML_ELEMENT_INVOKE:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "src") ||
                   view_equal_raw(name, "autoforward");
        case SCXML_ELEMENT_ELSE:
        case SCXML_ELEMENT_FINALIZE:
        case SCXML_ELEMENT_INITIAL:
        case SCXML_ELEMENT_ONENTRY:
        case SCXML_ELEMENT_ONEXIT:
        case SCXML_ELEMENT_UNKNOWN: return false;
    }
    return false;
}

static cflow_scxml_status validate_element_attributes(
    scxml_build *build, turbo_xml_node node, scxml_element_kind kind) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_attribute_count(node); ++index) {
        const turbo_xml_attribute attribute =
            turbo_xml_node_attribute_at(node, index);
        const turbo_xml_string_view namespace_uri =
            turbo_xml_attribute_namespace_uri(attribute);
        const turbo_xml_string_view name =
            turbo_xml_attribute_local_name(attribute);
        if (!is_empty_view(namespace_uri)) continue;
        if (kind == SCXML_ELEMENT_LOG && view_equal_raw(name, "expr")) {
            return scxml_fail(
                build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                turbo_xml_attribute_location(attribute),
                "SCXML null data model has no log value expressions");
        }
        if (!attribute_allowed(kind, name)) {
            return scxml_fail(
                build,
                kind == SCXML_ELEMENT_LOG || kind == SCXML_ELEMENT_ASSIGN
                    ? CFLOW_SCXML_INVALID_STRUCTURE
                    : CFLOW_SCXML_UNSUPPORTED_FEATURE,
                turbo_xml_attribute_location(attribute),
                kind == SCXML_ELEMENT_LOG || kind == SCXML_ELEMENT_ASSIGN
                    ? "executable element has an invalid unqualified attribute"
                    : "unsupported unqualified SCXML attribute");
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status require_scxml_element(scxml_build *build,
                                                turbo_xml_node node,
                                                scxml_element_kind *out_kind) {
    const turbo_xml_string_view namespace_uri =
        turbo_xml_node_namespace_uri(node);
    const scxml_element_kind kind = element_kind(node);
    if (!view_equal_raw(namespace_uri, CFLOW_SCXML_NAMESPACE)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_NAMESPACE,
                          turbo_xml_node_location(node),
                          "SCXML elements must use the W3C SCXML namespace");
    }
    if (kind == SCXML_ELEMENT_UNKNOWN) {
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "unsupported SCXML element");
    }
    *out_kind = kind;
    return validate_element_attributes(build, node, kind);
}

static size_t element_child_count(turbo_xml_node node,
                                  scxml_element_kind wanted) {
    size_t count = 0u;
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            element_kind(child) == wanted) {
            ++count;
        }
    }
    return count;
}

static turbo_xml_node first_real_child(turbo_xml_node node) {
    size_t index;
    turbo_xml_node empty = {NULL};
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            is_state_element(element_kind(child))) {
            return child;
        }
    }
    return empty;
}

static bool token_next(turbo_xml_string_view value, size_t *cursor,
                       turbo_xml_string_view *token) {
    size_t begin;
    if (cursor == NULL || token == NULL) return false;
    begin = *cursor;
    while (begin < value.size && isspace((unsigned char)value.data[begin]))
        ++begin;
    if (begin == value.size) {
        *cursor = begin;
        return false;
    }
    *cursor = begin;
    while (*cursor < value.size &&
           !isspace((unsigned char)value.data[*cursor])) {
        ++*cursor;
    }
    token->data = value.data + begin;
    token->size = *cursor - begin;
    return true;
}

static bool token_has_wildcard(turbo_xml_string_view token) {
    return token.size != 0u && memchr(token.data, '*', token.size) != NULL;
}

static bool completion_token(turbo_xml_string_view token,
                             turbo_xml_string_view *state_name) {
    static const char prefix[] = "done.state.";
    if (token.size <= sizeof(prefix) - 1u ||
        memcmp(token.data, prefix, sizeof(prefix) - 1u) != 0) {
        return false;
    }
    state_name->data = token.data + sizeof(prefix) - 1u;
    state_name->size = token.size - (sizeof(prefix) - 1u);
    return true;
}

static cflow_scxml_status analyze_raise(scxml_build *build,
                                        turbo_xml_node node,
                                        scxml_counts *counts) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_RAISE);
    size_t index;
    if (status != CFLOW_SCXML_OK) return status;
    if (event_attribute.impl == NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "raise requires one event NMTOKEN");
    }
    if (!is_xml_nmtoken(turbo_xml_attribute_value(event_attribute))) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(event_attribute),
                          "raise event must be one XML NMTOKEN");
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) != TURBO_XML_COMMENT) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "raise cannot contain executable or text content");
        }
    }
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->event_occurrences, 1u,
                     &counts->event_occurrences)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(event_attribute),
                          "raise step count overflow");
    }
    return CFLOW_SCXML_OK;
}

static bool parse_delay_ms(turbo_xml_string_view value, uint64_t *out_ms) {
    size_t number_size;
    size_t index;
    size_t dot = SIZE_MAX;
    size_t fraction_digits = 0u;
    uint64_t whole = 0u;
    uint64_t fraction = 0u;
    bool seconds;
    if (out_ms == NULL || value.data == NULL || value.size < 2u)
        return false;
    if (value.size >= 3u && value.data[value.size - 2u] == 'm' &&
        value.data[value.size - 1u] == 's') {
        seconds = false;
        number_size = value.size - 2u;
    } else if (value.data[value.size - 1u] == 's') {
        seconds = true;
        number_size = value.size - 1u;
    } else {
        return false;
    }
    if (number_size == 0u) return false;
    for (index = 0u; index < number_size; ++index) {
        const unsigned char digit = (unsigned char)value.data[index];
        if (digit == '.') {
            if (!seconds || dot != SIZE_MAX || index == 0u ||
                index + 1u == number_size) {
                return false;
            }
            dot = index;
            continue;
        }
        if (!isdigit(digit)) return false;
        if (dot == SIZE_MAX) {
            if (whole > (UINT64_MAX - (uint64_t)(digit - '0')) / 10u)
                return false;
            whole = whole * 10u + (uint64_t)(digit - '0');
        } else {
            ++fraction_digits;
            if (fraction_digits > 3u) return false;
            fraction = fraction * 10u + (uint64_t)(digit - '0');
        }
    }
    if (!seconds) {
        *out_ms = whole;
        return true;
    }
    if (whole > UINT64_MAX / 1000u) return false;
    while (fraction_digits < 3u) {
        fraction *= 10u;
        ++fraction_digits;
    }
    if (whole * 1000u > UINT64_MAX - fraction) return false;
    *out_ms = whole * 1000u + fraction;
    return true;
}

static bool count_retained_view(turbo_xml_string_view value,
                                size_t *total) {
    size_t retained;
    return checked_add(value.size, 1u, &retained) &&
           checked_add(*total, retained, total);
}

static cflow_scxml_status validate_effect_children(
    scxml_build *build, turbo_xml_node node, bool send) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child)))) {
            continue;
        }
        return scxml_fail(
            build,
            send ? CFLOW_SCXML_UNSUPPORTED_FEATURE
                 : CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(child),
            send ? "send content and parameters are not supported"
                 : "cancel must be empty");
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_send(scxml_build *build,
                                       turbo_xml_node node,
                                       scxml_counts *counts) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    const turbo_xml_attribute target_attribute = find_attribute(node, "target");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute delay_attribute = find_attribute(node, "delay");
    const turbo_xml_string_view event =
        event_attribute.impl != NULL
            ? turbo_xml_attribute_value(event_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    const turbo_xml_string_view target =
        target_attribute.impl != NULL
            ? turbo_xml_attribute_value(target_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    const turbo_xml_string_view type =
        type_attribute.impl != NULL
            ? turbo_xml_attribute_value(type_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    const turbo_xml_string_view id =
        id_attribute.impl != NULL
            ? turbo_xml_attribute_value(id_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    uint64_t delay_ms = 0u;
    bool internal_target;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_SEND);
    if (status != CFLOW_SCXML_OK) return status;
    if (event_attribute.impl == NULL || !is_xml_nmtoken(event)) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            event_attribute.impl != NULL
                ? turbo_xml_attribute_location(event_attribute)
                : turbo_xml_node_location(node),
            "send requires one literal event NMTOKEN");
    }
    if ((target_attribute.impl != NULL && target.size == 0u) ||
        (type_attribute.impl != NULL && type.size == 0u) ||
        (id_attribute.impl != NULL && !is_xml_ncname(id))) {
        turbo_xml_attribute owner = target_attribute.impl != NULL &&
                                            target.size == 0u
                                        ? target_attribute
                                    : type_attribute.impl != NULL &&
                                              type.size == 0u
                                        ? type_attribute
                                        : id_attribute;
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(owner),
                          "send literal attributes must be non-empty and id must be an XML NCName");
    }
    if (delay_attribute.impl != NULL &&
        !parse_delay_ms(turbo_xml_attribute_value(delay_attribute),
                        &delay_ms)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(delay_attribute),
                          "send delay must be an unsigned ms or s literal with millisecond precision");
    }
    if (delay_ms != 0u && id_attribute.impl == NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(delay_attribute),
                          "delayed send requires one literal id");
    }
    status = validate_effect_children(build, node, true);
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        !checked_add(counts->event_occurrences, 1u,
                     &counts->event_occurrences) ||
        !count_retained_view(event, &counts->effect_string_bytes) ||
        (target_attribute.impl != NULL &&
         !count_retained_view(target, &counts->effect_string_bytes)) ||
        (type_attribute.impl != NULL &&
         !count_retained_view(type, &counts->effect_string_bytes)) ||
        (id_attribute.impl != NULL &&
         !count_retained_view(id, &counts->effect_string_bytes))) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "send descriptor storage size overflow");
    }
    internal_target = view_equal_raw(target, "#_internal") ||
                      view_equal_raw(target, "_internal");
    if (!internal_target || delay_ms != 0u)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_EVENT_IO;
    if (delay_ms != 0u)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_DELAYED_SEND;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_cancel(scxml_build *build,
                                         turbo_xml_node node,
                                         scxml_counts *counts) {
    const turbo_xml_attribute sendid_attribute = find_attribute(node, "sendid");
    const turbo_xml_string_view sendid =
        sendid_attribute.impl != NULL
            ? turbo_xml_attribute_value(sendid_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_CANCEL);
    if (status != CFLOW_SCXML_OK) return status;
    if (sendid_attribute.impl == NULL || !is_xml_ncname(sendid)) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            sendid_attribute.impl != NULL
                ? turbo_xml_attribute_location(sendid_attribute)
                : turbo_xml_node_location(node),
            "cancel requires one literal sendid XML NCName");
    }
    status = validate_effect_children(build, node, false);
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        !count_retained_view(sendid, &counts->effect_string_bytes)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "cancel descriptor storage size overflow");
    }
    counts->requirements |= CFLOW_SCXML_REQUIREMENT_EVENT_IO |
                            CFLOW_SCXML_REQUIREMENT_DELAYED_SEND |
                            CFLOW_SCXML_REQUIREMENT_CANCEL;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_executable_content(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    size_t conditional_depth, bool finalize_safe);

static cflow_scxml_status analyze_log(scxml_build *build,
                                      turbo_xml_node node,
                                      scxml_counts *counts) {
    const turbo_xml_attribute label_attribute = find_attribute(node, "label");
    const turbo_xml_string_view label =
        label_attribute.impl != NULL
            ? turbo_xml_attribute_value(label_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    size_t retained_bytes;
    size_t index;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_LOG);
    if (status != CFLOW_SCXML_OK) return status;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) != TURBO_XML_COMMENT) {
            return scxml_fail(
                build, CFLOW_SCXML_INVALID_STRUCTURE,
                turbo_xml_node_location(child),
                "log cannot contain executable or text content");
        }
    }
    if (!checked_add(label.size, 1u, &retained_bytes) ||
        !checked_add(counts->log_label_bytes, retained_bytes,
                     &counts->log_label_bytes) ||
        !checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "log label storage size overflow");
    }
    if (counts->log_label_bytes > build->limits.max_name_bytes) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "log labels exceed max_name_bytes");
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_assign(scxml_build *build,
                                         turbo_xml_node node,
                                         scxml_counts *counts) {
    const turbo_xml_attribute location = find_attribute(node, "location");
    const turbo_xml_attribute expression = find_attribute(node, "expr");
    size_t index;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_ASSIGN);
    if (status != CFLOW_SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "assign requires the CMeta data model");
    if (location.impl == NULL ||
        is_empty_view(turbo_xml_attribute_value(location)) ||
        expression.impl == NULL ||
        is_empty_view(turbo_xml_attribute_value(expression))) {
        const turbo_xml_location failure_location =
            location.impl == NULL || expression.impl == NULL
                ? turbo_xml_node_location(node)
                : location.impl != NULL &&
                          is_empty_view(turbo_xml_attribute_value(location))
                      ? turbo_xml_attribute_location(location)
                      : turbo_xml_attribute_location(expression);
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          failure_location,
                          "assign requires non-empty location and expr attributes");
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) != TURBO_XML_COMMENT)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "assign cannot contain child content");
    }
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->assignment_rows, 1u,
                     &counts->assignment_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "assignment descriptor count overflow");
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_foreach(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    size_t executable_depth, bool finalize_safe) {
    const turbo_xml_attribute array = find_attribute(node, "array");
    const turbo_xml_attribute item = find_attribute(node, "item");
    const turbo_xml_attribute index = find_attribute(node, "index");
    const size_t first_child_step = counts->executable_steps;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_FOREACH);
    if (status != CFLOW_SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "foreach requires the CMeta data model");
    if (finalize_safe)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "CMeta finalize foreach is not supported yet");
    if (array.impl == NULL ||
        is_empty_view(turbo_xml_attribute_value(array)) ||
        item.impl == NULL ||
        is_empty_view(turbo_xml_attribute_value(item)) ||
        (index.impl != NULL &&
         is_empty_view(turbo_xml_attribute_value(index))))
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            array.impl == NULL || item.impl == NULL
                ? turbo_xml_node_location(node)
                : array.impl != NULL &&
                          is_empty_view(turbo_xml_attribute_value(array))
                      ? turbo_xml_attribute_location(array)
                      : item.impl != NULL &&
                                is_empty_view(turbo_xml_attribute_value(item))
                            ? turbo_xml_attribute_location(item)
                            : turbo_xml_attribute_location(index),
            "foreach requires non-empty array and item, and a non-empty index when present");
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->foreach_rows, 1u, &counts->foreach_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "foreach descriptor count overflow");
    if (executable_depth > counts->max_conditional_depth)
        counts->max_conditional_depth = executable_depth;
    status = analyze_executable_content(
        build, node, counts, executable_depth, false);
    if (status != CFLOW_SCXML_OK) return status;
    if (counts->executable_steps == first_child_step + 1u)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "foreach requires executable child content");
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status validate_condition_attribute(
    scxml_build *build, turbo_xml_node node) {
    const turbo_xml_attribute condition = find_attribute(node, "cond");
    turbo_xml_string_view state;
    if (build->data_model == SCXML_DATA_MODEL_CMETA) {
        if (condition.impl == NULL ||
            is_empty_view(turbo_xml_attribute_value(condition))) {
            return scxml_fail(
                build, CFLOW_SCXML_INVALID_STRUCTURE,
                condition.impl != NULL
                    ? turbo_xml_attribute_location(condition)
                    : turbo_xml_node_location(node),
                "if and elseif require one non-empty CMeta condition");
        }
        return CFLOW_SCXML_OK;
    }
    if (condition.impl == NULL) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(node),
            "if and elseif require one null-model In(id) condition");
    }
    if (!parse_null_in_condition(
            turbo_xml_attribute_value(condition), &state)) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(condition),
            "null-model condition must be In(id)");
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status validate_empty_marker(
    scxml_build *build, turbo_xml_node node) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(child),
            "elseif and else markers must be empty");
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_conditional(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    size_t conditional_depth, bool finalize_safe) {
    size_t branch_count = 1u;
    size_t index;
    bool saw_else = false;
    cflow_scxml_status status;
    if (build->data_model == SCXML_DATA_MODEL_CMETA && finalize_safe) {
        const turbo_xml_attribute condition = find_attribute(node, "cond");
        return scxml_fail(
            build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
            condition.impl != NULL
                ? turbo_xml_attribute_location(condition)
                : turbo_xml_node_location(node),
            "CMeta finalize conditions are not supported yet");
    }
    status = validate_condition_attribute(build, node);
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(
            counts->executable_steps, 1u, &counts->executable_steps)) {
        return scxml_fail(
            build, CFLOW_SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(node), "conditional step count overflow");
    }
    if (conditional_depth > counts->max_conditional_depth)
        counts->max_conditional_depth = conditional_depth;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) {
            return scxml_fail(
                build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(child),
                "conditional text content is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != CFLOW_SCXML_OK) return status;
        if (child_kind == SCXML_ELEMENT_ELSEIF ||
            child_kind == SCXML_ELEMENT_ELSE) {
            if (saw_else) {
                return scxml_fail(
                    build, CFLOW_SCXML_INVALID_STRUCTURE,
                    turbo_xml_node_location(child),
                    "elseif and else cannot follow else");
            }
            if (child_kind == SCXML_ELEMENT_ELSEIF) {
                status = validate_condition_attribute(build, child);
                if (status != CFLOW_SCXML_OK) return status;
            } else {
                saw_else = true;
            }
            status = validate_empty_marker(build, child);
            if (status != CFLOW_SCXML_OK) return status;
            if (!checked_add(branch_count, 1u, &branch_count)) {
                return scxml_fail(
                    build, CFLOW_SCXML_LIMIT_EXCEEDED,
                    turbo_xml_node_location(child),
                    "conditional branch count overflow");
            }
        } else if (child_kind == SCXML_ELEMENT_RAISE) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_raise(build, child, counts);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_SEND) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_send(build, child, counts);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_CANCEL) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_cancel(build, child, counts);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_LOG) {
            status = analyze_log(build, child, counts);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_ASSIGN) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot mutate CMeta state");
            status = analyze_assign(build, child, counts);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_IF) {
            size_t next_depth;
            if (!checked_add(conditional_depth, 1u, &next_depth)) {
                return scxml_fail(
                    build, CFLOW_SCXML_LIMIT_EXCEEDED,
                    turbo_xml_node_location(child),
                    "conditional nesting depth overflow");
            }
            status = analyze_conditional(
                build, child, counts, next_depth, finalize_safe);
            if (status != CFLOW_SCXML_OK) return status;
        } else if (child_kind == SCXML_ELEMENT_FOREACH) {
            size_t next_depth;
            if (!checked_add(conditional_depth, 1u, &next_depth))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(child),
                                  "foreach nesting depth overflow");
            status = analyze_foreach(
                build, child, counts, next_depth, finalize_safe);
            if (status != CFLOW_SCXML_OK) return status;
        } else {
            return scxml_fail(
                build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                turbo_xml_node_location(child),
                "unsupported conditional executable element");
        }
    }
    if (!checked_add(counts->conditional_branches, branch_count,
                     &counts->conditional_branches)) {
        return scxml_fail(
            build, CFLOW_SCXML_LIMIT_EXCEEDED,
            turbo_xml_node_location(node),
            "conditional branch count overflow");
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_executable_content(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    size_t conditional_depth, bool finalize_safe) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        cflow_scxml_status status;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "executable block text content is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != CFLOW_SCXML_OK) return status;
        if (child_kind == SCXML_ELEMENT_RAISE) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_raise(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_SEND) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_send(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_CANCEL) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot raise or invoke external effects");
            status = analyze_cancel(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_LOG) {
            status = analyze_log(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_ASSIGN) {
            if (finalize_safe)
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "finalize cannot mutate CMeta state");
            status = analyze_assign(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_IF) {
            size_t next_depth;
            if (!checked_add(conditional_depth, 1u, &next_depth)) {
                return scxml_fail(
                    build, CFLOW_SCXML_LIMIT_EXCEEDED,
                    turbo_xml_node_location(child),
                    "conditional nesting depth overflow");
            }
            status = analyze_conditional(
                build, child, counts, next_depth, finalize_safe);
        } else if (child_kind == SCXML_ELEMENT_FOREACH) {
            size_t next_depth;
            if (!checked_add(conditional_depth, 1u, &next_depth))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(child),
                                  "foreach nesting depth overflow");
            status = analyze_foreach(
                build, child, counts, next_depth, finalize_safe);
        } else if (child_kind == SCXML_ELEMENT_ELSEIF ||
                   child_kind == SCXML_ELEMENT_ELSE) {
            return scxml_fail(
                build, CFLOW_SCXML_INVALID_STRUCTURE,
                turbo_xml_node_location(child),
                "elseif and else are legal only inside if");
        } else {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "unsupported executable element");
        }
        if (status != CFLOW_SCXML_OK) return status;
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_executable_block(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    bool finalize_safe, bool *out_nonempty) {
    const size_t first_step = counts->executable_steps;
    cflow_scxml_status status;
    *out_nonempty = false;
    status = analyze_executable_content(
        build, node, counts, 0u, finalize_safe);
    if (status != CFLOW_SCXML_OK) return status;
    if (counts->executable_steps != first_step &&
        (!checked_add(counts->block_rows, 1u, &counts->block_rows) ||
         (!finalize_safe &&
          !checked_add(counts->executable_blocks, 1u,
                       &counts->executable_blocks)))) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "executable block count overflow");
    }
    *out_nonempty = counts->executable_steps != first_step;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_transition(scxml_build *build,
                                             turbo_xml_node node,
                                             bool require_default,
                                             scxml_counts *counts) {
    turbo_xml_attribute event_attribute;
    turbo_xml_attribute target_attribute;
    turbo_xml_attribute condition_attribute;
    turbo_xml_string_view value;
    turbo_xml_string_view token;
    size_t cursor = 0u;
    size_t token_count = 0u;
    bool nonempty = false;
    cflow_scxml_status status;

    status = validate_element_attributes(build, node, SCXML_ELEMENT_TRANSITION);
    if (status != CFLOW_SCXML_OK) return status;
    event_attribute = find_attribute(node, "event");
    target_attribute = find_attribute(node, "target");
    condition_attribute = find_attribute(node, "cond");
    if (require_default && event_attribute.impl != NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(event_attribute),
                          "initial and history defaults must be eventless");
    }
    if (require_default && target_attribute.impl == NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "initial and history defaults require one target");
    }
    if (require_default && condition_attribute.impl != NULL) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(condition_attribute),
            "initial and history default transitions cannot have cond");
    }
    if (condition_attribute.impl != NULL &&
        build->data_model == SCXML_DATA_MODEL_NULL) {
        turbo_xml_string_view state_name;
        if (!parse_null_in_condition(
                turbo_xml_attribute_value(condition_attribute), &state_name)) {
            return scxml_fail(
                build, CFLOW_SCXML_INVALID_STRUCTURE,
                turbo_xml_attribute_location(condition_attribute),
                "null-model transition condition must be In(id)");
        }
    }
    if (target_attribute.impl != NULL) {
        value = turbo_xml_attribute_value(target_attribute);
        cursor = 0u;
        while (token_next(value, &cursor, &token)) ++token_count;
        if (token_count != 1u) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_attribute_location(target_attribute),
                              "exactly one transition target is supported");
        }
    }
    token_count = 0u;
    if (event_attribute.impl != NULL) {
        value = turbo_xml_attribute_value(event_attribute);
        cursor = 0u;
        while (token_next(value, &cursor, &token)) {
            turbo_xml_string_view completed = {NULL, 0u};
            if (token_has_wildcard(token)) {
                return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                                  turbo_xml_attribute_location(event_attribute),
                                  "wildcard event descriptors are not supported");
            }
            ++token_count;
            if (!completion_token(token, &completed) &&
                !checked_add(counts->event_occurrences, 1u,
                             &counts->event_occurrences)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(event_attribute),
                                  "event occurrence count overflow");
            }
        }
        if (token_count == 0u) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(event_attribute),
                              "event attribute must contain a descriptor");
        }
    } else {
        token_count = 1u;
    }
    if (!checked_add(counts->transition_rows, token_count,
                     &counts->transition_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "transition count overflow");
    }
    if (condition_attribute.impl != NULL &&
        !checked_add(counts->guard_rows, token_count,
                     &counts->guard_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(condition_attribute),
                          "transition guard count overflow");
    }
    status = analyze_executable_block(
        build, node, counts, false, &nonempty);
    if (status != CFLOW_SCXML_OK) return status;
    if (nonempty &&
        !checked_add(counts->transition_action_rows, token_count,
                     &counts->transition_action_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "transition action count overflow");
    }
    return CFLOW_SCXML_OK;
}

static size_t decimal_digits(size_t value) {
    size_t digits = 1u;
    while (value >= 10u) {
        value /= 10u;
        ++digits;
    }
    return digits;
}

static cflow_scxml_status analyze_invoke(
    scxml_build *build, turbo_xml_node node,
    turbo_xml_string_view owner_id, size_t ordinal,
    scxml_counts *counts) {
    static const size_t generated_separator_size =
        sizeof(".invoke.") - 1u;
    static const size_t done_prefix_size =
        sizeof("done.invoke.") - 1u;
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute src_attribute = find_attribute(node, "src");
    const turbo_xml_attribute autoforward_attribute =
        find_attribute(node, "autoforward");
    const turbo_xml_string_view id = id_attribute.impl != NULL
        ? turbo_xml_attribute_value(id_attribute)
        : (turbo_xml_string_view){NULL, 0u};
    const turbo_xml_string_view type = type_attribute.impl != NULL
        ? turbo_xml_attribute_value(type_attribute)
        : (turbo_xml_string_view){NULL, 0u};
    const turbo_xml_string_view src = src_attribute.impl != NULL
        ? turbo_xml_attribute_value(src_attribute)
        : (turbo_xml_string_view){NULL, 0u};
    size_t id_size = id.size;
    size_t done_size;
    size_t index;
    size_t finalize_count = 0u;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_INVOKE);
    if (status != CFLOW_SCXML_OK) return status;
    if (id_attribute.impl != NULL && !is_xml_ncname(id)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(id_attribute),
                          "invoke id must be an XML NCName");
    }
    if (id_attribute.impl == NULL &&
        (!checked_add(owner_id.size, generated_separator_size, &id_size) ||
         !checked_add(id_size, decimal_digits(ordinal), &id_size))) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "generated invoke id size overflow");
    }
    if ((type_attribute.impl != NULL && type.size == 0u) ||
        (src_attribute.impl != NULL && src.size == 0u)) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(
                type_attribute.impl != NULL && type.size == 0u
                    ? type_attribute : src_attribute),
            "invoke literal type and src must be nonempty");
    }
    if (autoforward_attribute.impl != NULL) {
        const turbo_xml_string_view value =
            turbo_xml_attribute_value(autoforward_attribute);
        if (!view_equal_raw(value, "true") &&
            !view_equal_raw(value, "false")) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(
                                  autoforward_attribute),
                              "invoke autoforward must be true or false");
        }
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        bool nonempty = false;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "invoke text content is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != CFLOW_SCXML_OK) return status;
        if (child_kind != SCXML_ELEMENT_FINALIZE) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "invoke parameters and content are not supported");
        }
        if (++finalize_count > 1u) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "invoke may contain at most one finalize");
        }
        status = analyze_executable_block(
            build, child, counts, true, &nonempty);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (!checked_add(done_prefix_size, id_size, &done_size) ||
        !checked_add(counts->invocation_rows, 1u,
                     &counts->invocation_rows) ||
        !checked_add(counts->event_occurrences, 1u,
                     &counts->event_occurrences) ||
        !checked_add(counts->executable_steps, 2u,
                     &counts->executable_steps) ||
        !checked_add(counts->executable_blocks, 2u,
                     &counts->executable_blocks) ||
        !checked_add(counts->block_rows, 2u,
                     &counts->block_rows) ||
        !checked_add(counts->state_action_rows, 2u,
                     &counts->state_action_rows) ||
        !checked_add(id_size, 1u, &id_size) ||
        !checked_add(counts->invocation_string_bytes, id_size,
                     &counts->invocation_string_bytes) ||
        !checked_add(done_size, 1u, &done_size) ||
        !checked_add(counts->invocation_string_bytes, done_size,
                     &counts->invocation_string_bytes) ||
        (type_attribute.impl != NULL &&
         !count_retained_view(type, &counts->invocation_string_bytes)) ||
        (src_attribute.impl != NULL &&
         !count_retained_view(src, &counts->invocation_string_bytes))) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "invoke descriptor storage size overflow");
    }
    counts->requirements |= CFLOW_SCXML_REQUIREMENT_INVOKE;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_state(scxml_build *build,
                                        turbo_xml_node node,
                                        scxml_element_kind kind,
                                        bool is_root,
                                        scxml_counts *counts) {
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute initial_attribute = find_attribute(node, "initial");
    const size_t real_children =
        element_child_count(node, SCXML_ELEMENT_STATE) +
        element_child_count(node, SCXML_ELEMENT_PARALLEL) +
        element_child_count(node, SCXML_ELEMENT_FINAL);
    const size_t explicit_initials =
        element_child_count(node, SCXML_ELEMENT_INITIAL);
    const bool compound = is_root ||
        (kind == SCXML_ELEMENT_STATE && real_children != 0u);
    size_t index;
    size_t invoke_ordinal = 0u;
    cflow_scxml_status status;

    if (!checked_add(counts->state_rows, 1u, &counts->state_rows) ||
        !checked_add(counts->node_refs, 1u, &counts->node_refs)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "state count overflow");
    }
    if (id_attribute.impl != NULL &&
        !is_xml_ncname(turbo_xml_attribute_value(id_attribute))) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(id_attribute),
                          "SCXML state id must be an XML NCName");
    }
    if (is_root) {
        if (id_attribute.impl != NULL) {
            if (!checked_add(counts->state_names, 1u,
                             &counts->state_names)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(id_attribute),
                                  "state name count overflow");
            }
        }
    } else if (kind != SCXML_ELEMENT_INITIAL) {
        if (id_attribute.impl == NULL ||
            is_empty_view(turbo_xml_attribute_value(id_attribute))) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(node),
                              "supported SCXML states require a nonempty id");
        }
        if (!checked_add(counts->state_names, 1u, &counts->state_names)) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(id_attribute),
                              "state name count overflow");
        }
    }
    if (kind == SCXML_ELEMENT_HISTORY) {
        const turbo_xml_attribute type_attribute = find_attribute(node, "type");
        if (type_attribute.impl != NULL) {
            const turbo_xml_string_view type =
                turbo_xml_attribute_value(type_attribute);
            if (!view_equal_raw(type, "shallow") &&
                !view_equal_raw(type, "deep")) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_attribute_location(type_attribute),
                                  "history type must be shallow or deep");
            }
        }
    }
    if ((kind == SCXML_ELEMENT_PARALLEL && real_children == 0u) ||
        (is_root && real_children == 0u)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "compound and parallel states require real children");
    }
    if (kind == SCXML_ELEMENT_STATE && !compound &&
        (explicit_initials != 0u || initial_attribute.impl != NULL ||
         element_child_count(node, SCXML_ELEMENT_HISTORY) != 0u)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "atomic states cannot declare initial or history children");
    }
    if (kind == SCXML_ELEMENT_PARALLEL &&
        (explicit_initials != 0u || initial_attribute.impl != NULL)) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "parallel states do not declare one initial child");
    }
    if (compound) {
        if (explicit_initials > 1u ||
            (explicit_initials != 0u && initial_attribute.impl != NULL)) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(node),
                              "compound states require one initial declaration");
        }
        if (explicit_initials == 0u) {
            if (!checked_add(counts->state_rows, 1u, &counts->state_rows) ||
                !checked_add(counts->synthetic_initials, 1u,
                             &counts->synthetic_initials) ||
                !checked_add(counts->transition_rows, 1u,
                             &counts->transition_rows)) {
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(node),
                                  "synthetic initial count overflow");
            }
        }
    }

    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT) continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "non-whitespace SCXML text is not supported");
        }
        status = require_scxml_element(build, child, &child_kind);
        if (status != CFLOW_SCXML_OK) return status;
        if (is_state_element(child_kind)) {
            if (kind == SCXML_ELEMENT_FINAL || kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "this SCXML element cannot contain states");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_INITIAL) {
            if (!compound) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "initial must be a child of a compound state");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_HISTORY) {
            if (!(compound || kind == SCXML_ELEMENT_PARALLEL)) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "history must be a child of compound or parallel");
            }
            status = analyze_state(build, child, child_kind, false, counts);
        } else if (child_kind == SCXML_ELEMENT_TRANSITION) {
            if (is_root || kind == SCXML_ELEMENT_FINAL) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "this SCXML element cannot contain transitions");
            }
            status = analyze_transition(
                build, child,
                kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY,
                counts);
        } else if (child_kind == SCXML_ELEMENT_ONENTRY ||
                   child_kind == SCXML_ELEMENT_ONEXIT) {
            bool nonempty = false;
            if (kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY ||
                is_root) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "onentry/onexit is not allowed at this location");
            }
            status = analyze_executable_block(
                build, child, counts, false, &nonempty);
            if (status == CFLOW_SCXML_OK && nonempty &&
                !checked_add(counts->state_action_rows, 1u,
                             &counts->state_action_rows)) {
                status = scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                    turbo_xml_node_location(child),
                                    "state action count overflow");
            }
        } else if (child_kind == SCXML_ELEMENT_INVOKE) {
            if (is_root || kind == SCXML_ELEMENT_FINAL ||
                kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY) {
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "invoke is allowed only in state or parallel");
            }
            ++invoke_ordinal;
            status = analyze_invoke(
                build, child, turbo_xml_attribute_value(id_attribute),
                invoke_ordinal, counts);
        } else if (child_kind == SCXML_ELEMENT_FINALIZE) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "finalize is allowed only inside invoke");
        } else {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "unsupported SCXML child element");
        }
        if (status != CFLOW_SCXML_OK) return status;
    }

    if ((kind == SCXML_ELEMENT_INITIAL || kind == SCXML_ELEMENT_HISTORY) &&
        element_child_count(node, SCXML_ELEMENT_TRANSITION) != 1u) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "initial and history require exactly one transition");
    }
    return CFLOW_SCXML_OK;
}

static cflow_statechart_state_kind native_state_kind(turbo_xml_node node,
                                                      bool is_root) {
    const scxml_element_kind kind = element_kind(node);
    if (is_root) return CFLOW_STATECHART_COMPOUND;
    if (kind == SCXML_ELEMENT_PARALLEL) return CFLOW_STATECHART_PARALLEL;
    if (kind == SCXML_ELEMENT_FINAL) return CFLOW_STATECHART_FINAL;
    if (kind == SCXML_ELEMENT_INITIAL) return CFLOW_STATECHART_INITIAL;
    if (kind == SCXML_ELEMENT_HISTORY) {
        const turbo_xml_attribute type = find_attribute(node, "type");
        return type.impl != NULL &&
                       view_equal_raw(turbo_xml_attribute_value(type), "deep")
                   ? CFLOW_STATECHART_HISTORY_DEEP
                   : CFLOW_STATECHART_HISTORY_SHALLOW;
    }
    return first_real_child(node).impl != NULL ? CFLOW_STATECHART_COMPOUND
                                                : CFLOW_STATECHART_ATOMIC;
}

static cflow_scxml_status emit_state(scxml_build *build,
                                     turbo_xml_node node,
                                     cflow_machine_state_id parent,
                                     bool is_root) {
    const scxml_element_kind kind = element_kind(node);
    const cflow_machine_state_id id =
        (cflow_machine_state_id)(build->state_index + 1u);
    const cflow_statechart_state_kind native_kind =
        native_state_kind(node, is_root);
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute initial_attribute = find_attribute(node, "initial");
    const bool compound = native_kind == CFLOW_STATECHART_COMPOUND;
    const bool has_explicit_initial =
        element_child_count(node, SCXML_ELEMENT_INITIAL) != 0u;
    size_t index;

    build->states[build->state_index] = (cflow_statechart_state){
        id, parent, native_kind, (uint32_t)build->state_index};
    ++build->state_index;
    build->node_refs[build->node_ref_index++] =
        (scxml_node_ref){node.impl, id};
    if (id_attribute.impl != NULL) {
        build->state_names[build->state_name_index] = (scxml_name_ref){
            turbo_xml_attribute_value(id_attribute),
            turbo_xml_attribute_location(id_attribute), id,
            build->state_name_index};
        ++build->state_name_index;
    }

    if (compound && !has_explicit_initial) {
        turbo_xml_node target_node = first_real_child(node);
        turbo_xml_attribute target_id;
        turbo_xml_string_view target;
        turbo_xml_location location;
        const cflow_machine_state_id initial_id =
            (cflow_machine_state_id)(build->state_index + 1u);
        if (initial_attribute.impl != NULL) {
            target = turbo_xml_attribute_value(initial_attribute);
            location = turbo_xml_attribute_location(initial_attribute);
        } else {
            target_id = find_attribute(target_node, "id");
            target = turbo_xml_attribute_value(target_id);
            location = turbo_xml_node_location(target_node);
        }
        build->states[build->state_index] = (cflow_statechart_state){
            initial_id, id, CFLOW_STATECHART_INITIAL,
            (uint32_t)build->state_index};
        ++build->state_index;
        build->synthetic_initials[build->synthetic_index++] =
            (scxml_synthetic_initial){id, initial_id, target, location};
    }

    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            (is_state_element(child_kind) ||
             child_kind == SCXML_ELEMENT_INITIAL ||
             child_kind == SCXML_ELEMENT_HISTORY)) {
            cflow_scxml_status status = emit_state(build, child, id, false);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    (void)kind;
    return CFLOW_SCXML_OK;
}

static const scxml_name_ref *find_name_ref(const scxml_name_ref *names,
                                           size_t count,
                                           turbo_xml_string_view name) {
    size_t low = 0u;
    size_t high = count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const int compared = compare_view(names[middle].name, name);
        if (compared < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && view_equal(names[low].name, name) ? &names[low]
                                                             : NULL;
}

static cflow_machine_state_id node_id(const scxml_build *build,
                                      turbo_xml_node node,
                                      size_t node_count) {
    size_t low = 0u;
    size_t high = node_count;
    const uintptr_t wanted = (uintptr_t)node.impl;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const uintptr_t value = (uintptr_t)build->node_refs[middle].node;
        if (value < wanted) low = middle + 1u;
        else high = middle;
    }
    return low < node_count && build->node_refs[low].node == node.impl
               ? build->node_refs[low].id
               : 0u;
}

static char *reserve_invocation_storage(scxml_build *build, size_t size) {
    char *result;
    if (size == 0u ||
        build->invocation_storage_index >
            build->invocation_storage_capacity ||
        size > build->invocation_storage_capacity -
            build->invocation_storage_index)
        return NULL;
    result = build->invocation_storage + build->invocation_storage_index;
    build->invocation_storage_index += size;
    return result;
}

static bool retain_invocation_view(
    scxml_build *build, turbo_xml_string_view value,
    const char **out_data, size_t *out_size) {
    char *stored;
    size_t retained;
    *out_data = NULL;
    *out_size = 0u;
    if (value.data == NULL) return true;
    if (!checked_add(value.size, 1u, &retained) ||
        (stored = reserve_invocation_storage(build, retained)) == NULL)
        return false;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static cflow_scxml_status emit_invocation_declarations(
    scxml_build *build, turbo_xml_node node, size_t node_count) {
    static const char generated_separator[] = ".invoke.";
    static const char done_prefix[] = "done.invoke.";
    const cflow_machine_state_id owner = node_id(build, node, node_count);
    const turbo_xml_attribute owner_id_attribute = find_attribute(node, "id");
    const turbo_xml_string_view owner_id =
        owner_id_attribute.impl != NULL
            ? turbo_xml_attribute_value(owner_id_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    size_t ordinal = 0u;
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind kind = element_kind(child);
        scxml_invocation_descriptor *descriptor;
        turbo_xml_attribute id_attribute;
        turbo_xml_attribute type_attribute;
        turbo_xml_attribute src_attribute;
        turbo_xml_attribute autoforward_attribute;
        turbo_xml_location id_location;
        char *generated;
        size_t generated_size;
        size_t retained_size;
        char ordinal_buffer[3u * sizeof(size_t) + 1u];
        int ordinal_size;
        char *done_name;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            kind != SCXML_ELEMENT_INVOKE)
            continue;
        ++ordinal;
        if (build->invocation_index >= build->invocation_capacity) {
            return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                              turbo_xml_node_location(child),
                              "invoke exceeded admitted descriptor storage");
        }
        descriptor = &build->invocations[build->invocation_index];
        id_attribute = find_attribute(child, "id");
        type_attribute = find_attribute(child, "type");
        src_attribute = find_attribute(child, "src");
        autoforward_attribute = find_attribute(child, "autoforward");
        if (id_attribute.impl != NULL) {
            if (!retain_invocation_view(
                    build, turbo_xml_attribute_value(id_attribute),
                    &descriptor->id, &descriptor->id_size)) {
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(child),
                                  "invoke id storage mismatched admission");
            }
            id_location = turbo_xml_attribute_location(id_attribute);
        } else {
            ordinal_size = snprintf(
                ordinal_buffer, sizeof(ordinal_buffer), "%zu", ordinal);
            if (ordinal_size <= 0 ||
                !checked_add(owner_id.size,
                             sizeof(generated_separator) - 1u,
                             &generated_size) ||
                !checked_add(generated_size, (size_t)ordinal_size,
                             &generated_size) ||
                !checked_add(generated_size, 1u, &retained_size) ||
                (generated = reserve_invocation_storage(
                     build, retained_size)) == NULL) {
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(child),
                                  "generated invoke id storage mismatched admission");
            }
            memcpy(generated, owner_id.data, owner_id.size);
            memcpy(generated + owner_id.size, generated_separator,
                   sizeof(generated_separator) - 1u);
            memcpy(generated + owner_id.size +
                       sizeof(generated_separator) - 1u,
                   ordinal_buffer, (size_t)ordinal_size);
            generated[generated_size] = '\0';
            descriptor->id = generated;
            descriptor->id_size = generated_size;
            id_location = turbo_xml_node_location(child);
        }
        if (!retain_invocation_view(
                build,
                type_attribute.impl != NULL
                    ? turbo_xml_attribute_value(type_attribute)
                    : (turbo_xml_string_view){NULL, 0u},
                &descriptor->type, &descriptor->type_size) ||
            !retain_invocation_view(
                build,
                src_attribute.impl != NULL
                    ? turbo_xml_attribute_value(src_attribute)
                    : (turbo_xml_string_view){NULL, 0u},
                &descriptor->src, &descriptor->src_size) ||
            !checked_add(sizeof(done_prefix) - 1u, descriptor->id_size,
                         &generated_size) ||
            !checked_add(generated_size, 1u, &retained_size) ||
            (done_name = reserve_invocation_storage(
                 build, retained_size)) == NULL) {
            return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                              turbo_xml_node_location(child),
                              "invoke literal storage mismatched admission");
        }
        memcpy(done_name, done_prefix, sizeof(done_prefix) - 1u);
        memcpy(done_name + sizeof(done_prefix) - 1u,
               descriptor->id, descriptor->id_size);
        done_name[generated_size] = '\0';
        descriptor->owner = owner;
        descriptor->done_name = done_name;
        descriptor->done_name_size = generated_size;
        descriptor->source_node = child.impl;
        descriptor->autoforward =
            autoforward_attribute.impl != NULL &&
            view_equal_raw(
                turbo_xml_attribute_value(autoforward_attribute), "true");
        build->invocation_names[build->invocation_index] =
            (scxml_name_ref){
                {descriptor->id, descriptor->id_size}, id_location,
                build->invocation_index + 1u, build->invocation_index};
        build->event_occurrences[build->event_occurrence_index] =
            (scxml_name_ref){
                {descriptor->done_name, descriptor->done_name_size},
                id_location, 0u, build->event_occurrence_index};
        ++build->event_occurrence_index;
        ++build->invocation_index;
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind kind = element_kind(child);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            (is_state_element(kind) || kind == SCXML_ELEMENT_INITIAL ||
             kind == SCXML_ELEMENT_HISTORY)) {
            cflow_scxml_status status = emit_invocation_declarations(
                build, child, node_count);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status resolve_invocation_events(scxml_build *build) {
    size_t index;
    for (index = 0u; index < build->invocation_index; ++index) {
        scxml_invocation_descriptor *descriptor =
            &build->invocations[index];
        const scxml_name_ref *event = find_name_ref(
            build->event_names, build->event_name_count,
            (turbo_xml_string_view){
                descriptor->done_name, descriptor->done_name_size});
        if (event == NULL) {
            return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                              (turbo_xml_location){0u, 0u, 0u},
                              "invoke done Event was not retained");
        }
        descriptor->done_event = (cflow_event_id)event->id;
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status collect_transition_events(
    scxml_build *build, turbo_xml_node node) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const turbo_xml_attribute event_attribute =
                find_attribute(child, "event");
            if (event_attribute.impl != NULL) {
                const turbo_xml_string_view value =
                    turbo_xml_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (token_next(value, &cursor, &token)) {
                    turbo_xml_string_view completed = {NULL, 0u};
                    if (!completion_token(token, &completed)) {
                        build->event_occurrences[build->event_occurrence_index] =
                            (scxml_name_ref){
                                token,
                                turbo_xml_attribute_location(event_attribute),
                                0u, build->event_occurrence_index};
                        ++build->event_occurrence_index;
                    }
                }
            }
            {
                cflow_scxml_status status =
                    collect_transition_events(build, child);
                if (status != CFLOW_SCXML_OK) return status;
            }
        } else if (child_kind == SCXML_ELEMENT_RAISE ||
                   child_kind == SCXML_ELEMENT_SEND) {
            const turbo_xml_attribute event_attribute =
                find_attribute(child, "event");
            build->event_occurrences[build->event_occurrence_index] =
                (scxml_name_ref){
                    turbo_xml_attribute_value(event_attribute),
                    turbo_xml_attribute_location(event_attribute), 0u,
                    build->event_occurrence_index};
            ++build->event_occurrence_index;
        } else if (is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY ||
                   child_kind == SCXML_ELEMENT_ONENTRY ||
                   child_kind == SCXML_ELEMENT_ONEXIT ||
                   child_kind == SCXML_ELEMENT_IF ||
                   child_kind == SCXML_ELEMENT_FOREACH) {
            cflow_scxml_status status =
                collect_transition_events(build, child);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static void collect_reserved_error_events(scxml_build *build,
                                          turbo_xml_location location) {
    size_t order = build->event_occurrence_index;
    build->event_occurrences[order] =
        (scxml_name_ref){
            {SCXML_ERROR_EXECUTION_EVENT,
             sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u},
            location, 0u, order};
    ++build->event_occurrence_index;
    order = build->event_occurrence_index;
    build->event_occurrences[order] =
        (scxml_name_ref){
            {SCXML_ERROR_COMMUNICATION_EVENT,
             sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u},
            location, 0u, order};
    ++build->event_occurrence_index;
}

static cflow_scxml_status build_event_names(scxml_build *build,
                                            size_t occurrence_count) {
    size_t index = 0u;
    size_t unique = 0u;
    if (occurrence_count == 0u) {
        build->event_name_count = 0u;
        return CFLOW_SCXML_OK;
    }
    qsort(build->event_occurrences, occurrence_count,
          sizeof(*build->event_occurrences), compare_name_ref);
    while (index < occurrence_count) {
        size_t next = index + 1u;
        scxml_name_ref selected = build->event_occurrences[index];
        while (next < occurrence_count &&
               view_equal(build->event_occurrences[index].name,
                          build->event_occurrences[next].name)) {
            if (build->event_occurrences[next].order < selected.order)
                selected = build->event_occurrences[next];
            ++next;
        }
        build->event_names[unique++] = selected;
        index = next;
    }
    if (unique > build->limits.max_events) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          build->event_names[build->limits.max_events].location,
                          "SCXML event count exceeds max_events");
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          compare_name_order);
    for (index = 0u; index < unique; ++index) {
        build->event_names[index].id = (cflow_event_id)(index + 1u);
        build->events[index] = (cflow_event_type){
            (cflow_event_id)(index + 1u), &cmeta_type_bool};
    }
    qsort(build->event_names, unique, sizeof(*build->event_names),
          compare_name_ref);
    build->event_name_count = unique;
    return CFLOW_SCXML_OK;
}

typedef enum scxml_execute_outcome {
    SCXML_EXECUTE_CONTINUE = 0,
    SCXML_EXECUTE_BLOCK_ABORTED,
    SCXML_EXECUTE_FATAL
} scxml_execute_outcome;

static bool same_send_id(const scxml_delayed_send *row,
                         const char *id, size_t id_size) {
    return row->state != SCXML_DELAYED_FREE && row->id_size == id_size &&
           id != NULL &&
           memcmp(row->id, id, id_size) == 0;
}

static scxml_prepared_effect *acquire_prepared_effect_locked(
    cflow_scxml_session_impl *session) {
    size_t index;
    for (index = 0u; index < session->prepared_effect_capacity; ++index) {
        if (!session->prepared_effects[index].in_use) {
            scxml_prepared_effect *effect = &session->prepared_effects[index];
            memset(effect, 0, sizeof(*effect));
            effect->session = session;
            effect->registry_index = SIZE_MAX;
            effect->in_use = true;
            return effect;
        }
    }
    return NULL;
}

static scxml_delayed_send *find_delayed_send_locked(
    cflow_scxml_session_impl *session, const char *id, size_t id_size,
    size_t *out_index) {
    size_t index;
    for (index = 0u; index < session->delayed_send_capacity; ++index) {
        if (same_send_id(&session->delayed_sends[index], id, id_size)) {
            if (out_index != NULL) *out_index = index;
            return &session->delayed_sends[index];
        }
    }
    return NULL;
}

static scxml_delayed_send *reserve_delayed_send_locked(
    cflow_scxml_session_impl *session, const char *id, size_t id_size,
    size_t *out_index, bool *out_duplicate) {
    size_t index;
    scxml_delayed_send *free_row = NULL;
    size_t free_index = SIZE_MAX;
    *out_duplicate = false;
    for (index = 0u; index < session->delayed_send_capacity; ++index) {
        scxml_delayed_send *row = &session->delayed_sends[index];
        if (same_send_id(row, id, id_size)) {
            *out_duplicate = true;
            return NULL;
        }
        if (free_row == NULL && row->state == SCXML_DELAYED_FREE) {
            free_row = row;
            free_index = index;
        }
    }
    if (free_row == NULL) return NULL;
    *free_row = (scxml_delayed_send){
        .id = id, .id_size = id_size, .state = SCXML_DELAYED_RESERVED};
    *out_index = free_index;
    return free_row;
}

static void rollback_prepared_effect_locked(scxml_prepared_effect *effect) {
    cflow_scxml_session_impl *session = effect->session;
    if (effect->registry_index != SIZE_MAX &&
        effect->registry_index < session->delayed_send_capacity) {
        scxml_delayed_send *row =
            &session->delayed_sends[effect->registry_index];
        if (effect->kind == SCXML_PREPARED_DELAYED_SEND &&
            row->state == SCXML_DELAYED_RESERVED) {
            *row = (scxml_delayed_send){0};
        } else if (effect->kind == SCXML_PREPARED_CANCEL &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED) {
            row->state = row->previous_state;
            row->previous_state = SCXML_DELAYED_FREE;
        }
    }
    effect->in_use = false;
}

static void commit_prepared_effect(void *user) {
    scxml_prepared_effect *effect = (scxml_prepared_effect *)user;
    cflow_scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    adapter_ticket = effect->adapter_ticket;
    if (effect->registry_index != SIZE_MAX &&
        effect->registry_index < session->delayed_send_capacity) {
        scxml_delayed_send *row =
            &session->delayed_sends[effect->registry_index];
        if (effect->kind == SCXML_PREPARED_DELAYED_SEND &&
            row->state == SCXML_DELAYED_RESERVED) {
            row->state = SCXML_DELAYED_ACTIVE;
        } else if (effect->kind == SCXML_PREPARED_CANCEL &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED) {
            *row = (scxml_delayed_send){0};
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.commit(adapter_ticket.user);
}

static void discard_prepared_effect(void *user) {
    scxml_prepared_effect *effect = (scxml_prepared_effect *)user;
    cflow_scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    adapter_ticket = effect->adapter_ticket;
    rollback_prepared_effect_locked(effect);
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.discard(adapter_ticket.user);
}

static void increment_u64(uint64_t *value) {
    if (*value != UINT64_MAX) ++*value;
}

static scxml_invocation_lifecycle_effect *
acquire_invocation_effect_locked(cflow_scxml_session_impl *session) {
    size_t index;
    for (index = 0u; index < session->invocation_effect_capacity; ++index) {
        scxml_invocation_lifecycle_effect *effect =
            &session->invocation_effects[index];
        if (!effect->in_use) {
            *effect = (scxml_invocation_lifecycle_effect){
                .session = session, .in_use = true};
            return effect;
        }
    }
    return NULL;
}

static cflow_mailbox_status report_invocation_adapter_error(
    cflow_scxml_session_impl *session, cflow_scxml_adapter_status status) {
    const bool null_value = false;
    const cflow_event_id id =
        status == CFLOW_SCXML_ADAPTER_ERROR_EXECUTION ||
        status == CFLOW_SCXML_ADAPTER_INVALID_CONTRACT
            ? session->program->execution_error_event
            : session->program->communication_error_event;
    const cflow_event_view event = {id, &cmeta_type_bool, &null_value};
    cflow_mailbox_status admission = CFLOW_MAILBOX_INVALID_ARGUMENT;
    if (id != 0u)
        admission = cflow_statechart_instance_try_send_internal(
            &session->instance, &event);
    if (admission != CFLOW_MAILBOX_OK) {
        turbo_mutex_lock(&session->registry_lock);
        increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
    }
    return admission;
}

static void commit_invocation_lifecycle(void *user) {
    scxml_invocation_lifecycle_effect *effect =
        (scxml_invocation_lifecycle_effect *)user;
    cflow_scxml_session_impl *session;
    const scxml_invocation_descriptor *descriptor;
    cflow_scxml_invoke_cancel_request request;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_scxml_adapter_status status;
    const char *adapter_error = NULL;
    uint64_t token = 0u;
    bool cancel = false;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    if (effect->invocation >= session->program->invocation_count) return;
    descriptor = &session->program->invocations[effect->invocation];
    turbo_mutex_lock(&session->registry_lock);
    if (effect->invocation < session->invocation_capacity) {
        scxml_invocation_row *row =
            &session->invocation_rows[effect->invocation];
        if (effect->enter) {
            row->token = 0u;
            row->state = SCXML_INVOCATION_PENDING;
        } else {
            if (row->state == SCXML_INVOCATION_ACTIVE) {
                token = row->token;
                cancel = token != 0u;
                if (session->invoke_stats.active != 0u)
                    --session->invoke_stats.active;
            }
            row->token = 0u;
            row->state = SCXML_INVOCATION_INACTIVE;
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    if (!cancel) return;
    request = (cflow_scxml_invoke_cancel_request){
        .token = token, .id = descriptor->id,
        .id_size = descriptor->id_size};
    status = session->invoke.prepare_cancel(
        session->invoke_user, &request, &adapter_ticket, &adapter_error);
    (void)adapter_error;
    if (status != CFLOW_SCXML_ADAPTER_ACCEPTED ||
        adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        increment_u64(&session->invoke_stats.cancel_failed);
        turbo_mutex_unlock(&session->registry_lock);
        (void)report_invocation_adapter_error(
            session, status == CFLOW_SCXML_ADAPTER_ACCEPTED
                ? CFLOW_SCXML_ADAPTER_INVALID_CONTRACT : status);
        return;
    }
    turbo_mutex_lock(&session->registry_lock);
    increment_u64(&session->invoke_stats.cancelled);
    turbo_mutex_unlock(&session->registry_lock);
    adapter_ticket.commit(adapter_ticket.user);
}

static void discard_invocation_lifecycle(void *user) {
    scxml_invocation_lifecycle_effect *effect =
        (scxml_invocation_lifecycle_effect *)user;
    cflow_scxml_session_impl *session;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
}

static scxml_execute_outcome execute_invocation_lifecycle(
    const scxml_block *block, cflow_scxml_session_impl *session,
    const scxml_step *step,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    if (session == NULL || !session->has_invoke ||
        block->invocations == NULL ||
        step->invocation >= block->invocation_storage_count ||
        step->invocation >= session->invocation_capacity ||
        context->stage_effect == NULL) {
        *out_error = "SCXML invoke requires an owning invocation session";
        return SCXML_EXECUTE_FATAL;
    }
    turbo_mutex_lock(&session->registry_lock);
    effect = acquire_invocation_effect_locked(session);
    if (effect != NULL) {
        effect->invocation = step->invocation;
        effect->enter = step->kind == SCXML_STEP_INVOKE_ENTER;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        *out_error = "SCXML invocation effect storage is full";
        return SCXML_EXECUTE_FATAL;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!context->stage_effect(context->effect_user, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static bool enqueue_invocation_adapter_error(
    cflow_scxml_session_impl *session,
    const cflow_statechart_runtime_hook_context *context,
    cflow_scxml_adapter_status status, const char **out_error) {
    const bool null_value = false;
    const cflow_event_id id = status == CFLOW_SCXML_ADAPTER_ERROR_EXECUTION
        ? session->program->execution_error_event
        : session->program->communication_error_event;
    const cflow_event_view event = {id, &cmeta_type_bool, &null_value};
    if (id == 0u || context->enqueue_internal == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML invocation error Event is unavailable";
        return false;
    }
    if (!context->enqueue_internal(
            context->enqueue_user, &event, out_error)) {
        turbo_mutex_lock(&session->registry_lock);
        increment_u64(&session->invoke_stats.adapter_error_rejected);
        turbo_mutex_unlock(&session->registry_lock);
        return false;
    }
    return true;
}

static bool start_stable_invocations(
    void *user, const cflow_statechart_runtime_hook_context *context,
    const char **out_error) {
    cflow_scxml_session_impl *session = (cflow_scxml_session_impl *)user;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation stable context is invalid";
        return false;
    }
    for (index = 0u; index < session->program->invocation_count; ++index) {
        const scxml_invocation_descriptor *descriptor =
            &session->program->invocations[index];
        cflow_scxml_invoke_start_request request;
        cflow_statechart_effect_ticket adapter_ticket = {0};
        cflow_scxml_adapter_status status;
        const char *adapter_error = NULL;
        uint64_t token;
        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state !=
                SCXML_INVOCATION_PENDING) {
            turbo_mutex_unlock(&session->registry_lock);
            continue;
        }
        if (!context->is_active(
                context->configuration_user, descriptor->owner)) {
            session->invocation_rows[index] = (scxml_invocation_row){0};
            turbo_mutex_unlock(&session->registry_lock);
            continue;
        }
        token = session->next_invocation_token;
        if (token == 0u) {
            turbo_mutex_unlock(&session->registry_lock);
            *out_error = "SCXML invocation token space is exhausted";
            return false;
        }
        session->next_invocation_token = token == UINT64_MAX ? 0u : token + 1u;
        session->invocation_rows[index] = (scxml_invocation_row){
            .token = token, .state = SCXML_INVOCATION_STARTING};
        turbo_mutex_unlock(&session->registry_lock);

        request = (cflow_scxml_invoke_start_request){
            .token = token,
            .id = descriptor->id,
            .id_size = descriptor->id_size,
            .type = descriptor->type,
            .type_size = descriptor->type_size,
            .src = descriptor->src,
            .src_size = descriptor->src_size,
            .autoforward = descriptor->autoforward};
        status = session->invoke.prepare_start(
            session->invoke_user, &request, &adapter_ticket, &adapter_error);
        if (status == CFLOW_SCXML_ADAPTER_ACCEPTED &&
            adapter_ticket.commit != NULL && adapter_ticket.discard != NULL) {
            turbo_mutex_lock(&session->registry_lock);
            session->invocation_rows[index].state = SCXML_INVOCATION_ACTIVE;
            increment_u64(&session->invoke_stats.started);
            ++session->invoke_stats.active;
            turbo_mutex_unlock(&session->registry_lock);
            adapter_ticket.commit(adapter_ticket.user);
            continue;
        }
        turbo_mutex_lock(&session->registry_lock);
        session->invocation_rows[index].state = SCXML_INVOCATION_FAILED;
        increment_u64(&session->invoke_stats.start_failed);
        turbo_mutex_unlock(&session->registry_lock);
        if (status == CFLOW_SCXML_ADAPTER_INVALID_CONTRACT ||
            (status == CFLOW_SCXML_ADAPTER_ACCEPTED &&
             (adapter_ticket.commit == NULL ||
              adapter_ticket.discard == NULL))) {
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error
                : "SCXML invocation adapter returned an invalid start ticket";
            return false;
        }
        if (!enqueue_invocation_adapter_error(
                session, context, status, out_error))
            return false;
    }
    return true;
}

static bool execute_finalize_range(
    const scxml_block *block,
    const cflow_statechart_runtime_hook_context *context,
    size_t begin, size_t end, size_t depth, const char **out_error) {
    size_t index = begin;
    if (block == NULL || context == NULL || out_error == NULL ||
        depth > block->max_conditional_depth || begin > end ||
        end > block->step_storage_count) {
        if (out_error != NULL)
            *out_error = "SCXML finalize range is invalid";
        return false;
    }
    while (index < end) {
        const scxml_step *step = &block->steps[index];
        if (step->next <= index || step->next > end) {
            *out_error = "SCXML finalize step span is invalid";
            return false;
        }
        if (step->kind == SCXML_STEP_LOG) {
            if (step->label == NULL) {
                *out_error = "SCXML finalize log storage is invalid";
                return false;
            }
            TURBO_LOG_DEBUG(
                tlog_peek_default(), "cflow.scxml", step->label);
        } else if (step->kind == SCXML_STEP_IF) {
            const scxml_branch *selected = NULL;
            size_t branch;
            if (step->branch_first > block->branch_storage_count ||
                step->branch_count >
                    block->branch_storage_count - step->branch_first) {
                *out_error = "SCXML finalize branch span is invalid";
                return false;
            }
            for (branch = 0u; branch < step->branch_count; ++branch) {
                const scxml_branch *candidate =
                    &block->branches[step->branch_first + branch];
                if (candidate->step_begin < index + 1u ||
                    candidate->step_begin > candidate->step_end ||
                    candidate->step_end > step->next ||
                    (!candidate->unconditional && candidate->state == 0u)) {
                    *out_error = "SCXML finalize branch is invalid";
                    return false;
                }
                if (candidate->unconditional ||
                    context->is_active(
                        context->configuration_user, candidate->state)) {
                    selected = candidate;
                    break;
                }
            }
            if (selected != NULL) {
                size_t next_depth;
                if (!checked_add(depth, 1u, &next_depth) ||
                    !execute_finalize_range(
                        block, context, selected->step_begin,
                        selected->step_end, next_depth, out_error))
                    return false;
            }
        } else {
            *out_error = "SCXML finalize executable is invalid";
            return false;
        }
        index = step->next;
    }
    return true;
}

static bool execute_invocation_finalize(
    const scxml_invocation_descriptor *descriptor,
    const cflow_statechart_runtime_hook_context *context,
    const char **out_error) {
    const scxml_block *block = descriptor->finalize;
    if (block == NULL) return true;
    if (block->steps == NULL ||
        (block->branch_storage_count != 0u && block->branches == NULL) ||
        block->step_begin >= block->step_end ||
        block->step_end > block->step_storage_count ||
        context->is_active == NULL || context->configuration_user == NULL) {
        *out_error = "SCXML finalize block context is invalid";
        return false;
    }
    return execute_finalize_range(
        block, context, block->step_begin, block->step_end, 0u, out_error);
}

static bool forward_external_to_invocations(
    cflow_scxml_session_impl *session,
    const cflow_statechart_runtime_hook_context *context,
    const cflow_event_view *event, const char **out_error) {
    size_t index;
    for (index = 0u; index < session->program->invocation_count; ++index) {
        const scxml_invocation_descriptor *descriptor =
            &session->program->invocations[index];
        cflow_scxml_invoke_forward_request request;
        cflow_statechart_effect_ticket adapter_ticket = {0};
        cflow_scxml_adapter_status status;
        const char *adapter_error = NULL;
        uint64_t token = 0u;
        if (!descriptor->autoforward) continue;
        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state ==
                SCXML_INVOCATION_ACTIVE)
            token = session->invocation_rows[index].token;
        turbo_mutex_unlock(&session->registry_lock);
        if (token == 0u) continue;
        request = (cflow_scxml_invoke_forward_request){
            .token = token,
            .id = descriptor->id,
            .id_size = descriptor->id_size,
            .event = event};
        status = session->invoke.prepare_forward(
            session->invoke_user, &request, &adapter_ticket, &adapter_error);
        if (status == CFLOW_SCXML_ADAPTER_ACCEPTED &&
            adapter_ticket.commit != NULL && adapter_ticket.discard != NULL) {
            turbo_mutex_lock(&session->registry_lock);
            increment_u64(&session->invoke_stats.forwarded);
            turbo_mutex_unlock(&session->registry_lock);
            adapter_ticket.commit(adapter_ticket.user);
            continue;
        }
        turbo_mutex_lock(&session->registry_lock);
        increment_u64(&session->invoke_stats.forward_failed);
        turbo_mutex_unlock(&session->registry_lock);
        if (status == CFLOW_SCXML_ADAPTER_INVALID_CONTRACT ||
            (status == CFLOW_SCXML_ADAPTER_ACCEPTED &&
             (adapter_ticket.commit == NULL ||
              adapter_ticket.discard == NULL))) {
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error
                : "SCXML invocation adapter returned an invalid forward ticket";
            return false;
        }
        if (!enqueue_invocation_adapter_error(
                session, context, status, out_error))
            return false;
    }
    return true;
}

static cflow_statechart_external_preprocess_result
preprocess_invocation_external(
    void *user, const cflow_statechart_runtime_hook_context *context,
    const cflow_event_view *event, uint64_t source_token,
    const char **out_error) {
    cflow_scxml_session_impl *session = (cflow_scxml_session_impl *)user;
    const scxml_invocation_descriptor *source = NULL;
    size_t source_index = SIZE_MAX;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL || event == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation preprocess context is invalid";
        return CFLOW_STATECHART_EXTERNAL_PREPROCESS_FATAL;
    }
    if (source_token != 0u) {
        turbo_mutex_lock(&session->registry_lock);
        for (index = 0u; index < session->program->invocation_count; ++index) {
            const scxml_invocation_row *row =
                &session->invocation_rows[index];
            if (row->state == SCXML_INVOCATION_ACTIVE &&
                row->token == source_token) {
                source_index = index;
                break;
            }
        }
        if (source_index == SIZE_MAX) {
            increment_u64(&session->invoke_stats.returned_rejected);
            turbo_mutex_unlock(&session->registry_lock);
            return CFLOW_STATECHART_EXTERNAL_PREPROCESS_DROP;
        }
        turbo_mutex_unlock(&session->registry_lock);
        source = &session->program->invocations[source_index];
        if (!execute_invocation_finalize(source, context, out_error))
            return CFLOW_STATECHART_EXTERNAL_PREPROCESS_FATAL;
        if (event->id == source->done_event) {
            turbo_mutex_lock(&session->registry_lock);
            if (session->invocation_rows[source_index].state ==
                    SCXML_INVOCATION_ACTIVE &&
                session->invocation_rows[source_index].token ==
                    source_token) {
                session->invocation_rows[source_index] =
                    (scxml_invocation_row){0};
                if (session->invoke_stats.active != 0u)
                    --session->invoke_stats.active;
                increment_u64(&session->invoke_stats.completed);
            }
            turbo_mutex_unlock(&session->registry_lock);
        }
    }
    if (!forward_external_to_invocations(
            session, context, event, out_error))
        return CFLOW_STATECHART_EXTERNAL_PREPROCESS_FATAL;
    return CFLOW_STATECHART_EXTERNAL_PREPROCESS_CONTINUE;
}

static scxml_execute_outcome raise_adapter_error(
    cflow_scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    cflow_scxml_adapter_error_kind kind, const char **out_error) {
    const bool null_value = false;
    const cflow_event_id event =
        kind == CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION
            ? session->program->execution_error_event
            : session->program->communication_error_event;
    const cflow_event_view raised = {
        event, &cmeta_type_bool, &null_value};
    if (event == 0u ||
        !context->raise_internal(context->raise_user, &raised, out_error)) {
        if (event == 0u && out_error != NULL)
            *out_error = "SCXML reserved adapter error event is unavailable";
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_BLOCK_ABORTED;
}

static scxml_execute_outcome adapter_failure_outcome(
    cflow_scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    cflow_scxml_adapter_status status, const char *adapter_error,
    const char **out_error) {
    switch (status) {
        case CFLOW_SCXML_ADAPTER_ERROR_EXECUTION:
            return raise_adapter_error(
                session, context, CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION,
                out_error);
        case CFLOW_SCXML_ADAPTER_ERROR_COMMUNICATION:
        case CFLOW_SCXML_ADAPTER_FULL:
        case CFLOW_SCXML_ADAPTER_CLOSED:
            return raise_adapter_error(
                session, context, CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
                out_error);
        case CFLOW_SCXML_ADAPTER_INVALID_CONTRACT:
        case CFLOW_SCXML_ADAPTER_ACCEPTED:
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error : "SCXML Event I/O adapter contract violation";
            return SCXML_EXECUTE_FATAL;
    }
    *out_error = "SCXML Event I/O adapter returned an unknown status";
    return SCXML_EXECUTE_FATAL;
}

static scxml_execute_outcome execute_send(
    cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const bool null_value = false;
    const cflow_scxml_send_request *request = &descriptor->request.send;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed = NULL;
    const char *adapter_error = NULL;
    cflow_scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    bool duplicate = false;
    if (descriptor->internal_target && request->delay_ms == 0u) {
        const cflow_event_view raised = {
            descriptor->event_id, &cmeta_type_bool, &null_value};
        return context->raise_internal(
                   context->raise_user, &raised, out_error)
            ? SCXML_EXECUTE_CONTINUE : SCXML_EXECUTE_FATAL;
    }
    if (session == NULL || !session->has_event_io ||
        context->stage_effect == NULL) {
        *out_error = "SCXML send requires an owning Event I/O session";
        return SCXML_EXECUTE_FATAL;
    }
    turbo_mutex_lock(&session->registry_lock);
    prepared = acquire_prepared_effect_locked(session);
    if (prepared != NULL && request->delay_ms != 0u) {
        delayed = reserve_delayed_send_locked(
            session, request->id, request->id_size, &registry_index,
            &duplicate);
    }
    if (prepared == NULL || (request->delay_ms != 0u && delayed == NULL)) {
        if (prepared != NULL) prepared->in_use = false;
        turbo_mutex_unlock(&session->registry_lock);
        return raise_adapter_error(
            session, context,
            duplicate ? CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION
                      : CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
            out_error);
    }
    prepared->kind = request->delay_ms != 0u
        ? SCXML_PREPARED_DELAYED_SEND : SCXML_PREPARED_SEND;
    prepared->registry_index = registry_index;
    turbo_mutex_unlock(&session->registry_lock);

    status = session->event_io.prepare_send(
        session->adapter_user, request, &adapter_ticket, &adapter_error);
    if (status != CFLOW_SCXML_ADAPTER_ACCEPTED) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        return adapter_failure_outcome(
            session, context, status, adapter_error, out_error);
    }
    if (adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML Event I/O adapter returned an invalid ticket";
        return SCXML_EXECUTE_FATAL;
    }
    prepared->adapter_ticket = adapter_ticket;
    runtime_ticket = (cflow_statechart_effect_ticket){
        commit_prepared_effect, discard_prepared_effect, prepared};
    if (!context->stage_effect(
            context->effect_user, &runtime_ticket, out_error)) {
        discard_prepared_effect(prepared);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static scxml_execute_outcome execute_cancel(
    cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const cflow_scxml_cancel_request *request = &descriptor->request.cancel;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed;
    const char *adapter_error = NULL;
    cflow_scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    if (session == NULL || !session->has_event_io ||
        context->stage_effect == NULL) {
        *out_error = "SCXML cancel requires an owning Event I/O session";
        return SCXML_EXECUTE_FATAL;
    }
    turbo_mutex_lock(&session->registry_lock);
    delayed = find_delayed_send_locked(
        session, request->send_id, request->send_id_size, &registry_index);
    if (delayed == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return SCXML_EXECUTE_CONTINUE;
    }
    prepared = acquire_prepared_effect_locked(session);
    if (prepared == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return raise_adapter_error(
            session, context, CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION,
            out_error);
    }
    prepared->kind = SCXML_PREPARED_CANCEL;
    prepared->registry_index = registry_index;
    delayed->previous_state = delayed->state;
    delayed->state = SCXML_DELAYED_CANCEL_RESERVED;
    turbo_mutex_unlock(&session->registry_lock);

    status = session->event_io.prepare_cancel(
        session->adapter_user, request, &adapter_ticket, &adapter_error);
    if (status != CFLOW_SCXML_ADAPTER_ACCEPTED) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        return adapter_failure_outcome(
            session, context, status, adapter_error, out_error);
    }
    if (adapter_ticket.commit == NULL || adapter_ticket.discard == NULL) {
        turbo_mutex_lock(&session->registry_lock);
        rollback_prepared_effect_locked(prepared);
        turbo_mutex_unlock(&session->registry_lock);
        *out_error = "SCXML Event I/O adapter returned an invalid ticket";
        return SCXML_EXECUTE_FATAL;
    }
    prepared->adapter_ticket = adapter_ticket;
    runtime_ticket = (cflow_statechart_effect_ticket){
        commit_prepared_effect, discard_prepared_effect, prepared};
    if (!context->stage_effect(
            context->effect_user, &runtime_ticket, out_error)) {
        discard_prepared_effect(prepared);
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static bool evaluate_cmeta_executable_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const cflow_statechart_executable_context *context =
        (const cflow_statechart_executable_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL)
        return false;
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static scxml_execute_outcome raise_block_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const bool null_value = false;
    const cflow_event_view raised = {
        block->execution_error_event, &cmeta_type_bool, &null_value};
    if (block->execution_error_event == 0u ||
        !context->raise_internal(context->raise_user, &raised, out_error)) {
        if (block->execution_error_event == 0u)
            *out_error = "SCXML execution error event is unavailable";
        return SCXML_EXECUTE_FATAL;
    }
    return SCXML_EXECUTE_BLOCK_ABORTED;
}

static bool enqueue_condition_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    const bool null_value = false;
    const cflow_event_view raised = {
        block->execution_error_event, &cmeta_type_bool, &null_value};
    if (block->execution_error_event == 0u) {
        *out_error = "SCXML conditional error event is unavailable";
        return false;
    }
    return context->raise_internal(context->raise_user, &raised, out_error);
}

static scxml_execute_outcome execute_scxml_range(
    const scxml_block *block,
    cflow_scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    size_t begin, size_t end, size_t depth,
    bool abort_condition_error, const char **out_error) {
    const bool null_value = false;
    size_t index = begin;
    if (depth > block->max_conditional_depth || begin > end ||
        end > block->step_storage_count) {
        *out_error = "SCXML executable range is invalid";
        return SCXML_EXECUTE_FATAL;
    }
    while (index < end) {
        const scxml_step *step = &block->steps[index];
        if (step->next <= index || step->next > end) {
            *out_error = "SCXML executable step span is invalid";
            return SCXML_EXECUTE_FATAL;
        }
        if (step->kind == SCXML_STEP_RAISE) {
            const cflow_event_view raised = {
                step->event, &cmeta_type_bool, &null_value};
            if (!context->raise_internal(
                    context->raise_user, &raised, out_error))
                return SCXML_EXECUTE_FATAL;
        } else if (step->kind == SCXML_STEP_SEND ||
                   step->kind == SCXML_STEP_CANCEL) {
            scxml_execute_outcome outcome;
            if (block->effects == NULL ||
                step->effect >= block->effect_storage_count) {
                *out_error = "SCXML effect descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            outcome = step->kind == SCXML_STEP_SEND
                ? execute_send(
                      session, &block->effects[step->effect], context,
                      out_error)
                : execute_cancel(
                      session, &block->effects[step->effect], context,
                      out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
        } else if (step->kind == SCXML_STEP_INVOKE_ENTER ||
                   step->kind == SCXML_STEP_INVOKE_EXIT) {
            const scxml_execute_outcome outcome =
                execute_invocation_lifecycle(
                    block, session, step, context, out_error);
            if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
        } else if (step->kind == SCXML_STEP_LOG) {
            if (step->label == NULL) {
                *out_error = "SCXML log label storage is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            TURBO_LOG_DEBUG(
                tlog_peek_default(), "cflow.scxml", step->label);
        } else if (step->kind == SCXML_STEP_ASSIGN) {
            cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
            if (block->assignments == NULL ||
                step->assignment >= block->assignment_storage_count) {
                *out_error = "SCXML assignment descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            if (cflow_scxml_cmeta_assign_apply_with_system(
                    &block->assignments[step->assignment],
                    context->out_state, evaluate_cmeta_executable_active,
                    (void *)context, system_values,
                    &diagnostic) !=
                CFLOW_SCXML_CMETA_EXPR_OK)
                return raise_block_execution_error(block, context, out_error);
        } else if (step->kind == SCXML_STEP_FOREACH) {
            const scxml_foreach_descriptor *descriptor;
            cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
            cmeta_range range = {0};
            cmeta_range_cursor cursor = {0};
            cflow_scxml_cmeta_foreach_value value = {0};
            size_t length = 0u;
            size_t iteration;
            size_t next_depth;
            if (block->foreach_descriptors == NULL ||
                step->foreach_descriptor >= block->foreach_storage_count) {
                *out_error = "SCXML foreach descriptor is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            descriptor =
                &block->foreach_descriptors[step->foreach_descriptor];
            if (descriptor->step_begin != index + 1u ||
                descriptor->step_begin > descriptor->step_end ||
                descriptor->step_end > step->next ||
                !checked_add(depth, 1u, &next_depth)) {
                *out_error = "SCXML foreach step span is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            if (cflow_scxml_cmeta_foreach_open(
                    &descriptor->program, context->out_state,
                    &range, &length, &diagnostic) !=
                CFLOW_SCXML_CMETA_EXPR_OK)
                return raise_block_execution_error(block, context, out_error);
            if (length != 0u &&
                cflow_scxml_cmeta_foreach_value_init(
                    &descriptor->program, &value, &diagnostic) !=
                    CFLOW_SCXML_CMETA_EXPR_OK)
                return raise_block_execution_error(block, context, out_error);
            for (iteration = 0u; iteration < length; ++iteration) {
                scxml_execute_outcome outcome;
                if (cflow_scxml_cmeta_foreach_next(
                        &descriptor->program, context->out_state, &range,
                        &cursor, &value, iteration, length, &diagnostic) !=
                    CFLOW_SCXML_CMETA_EXPR_OK) {
                    cflow_scxml_cmeta_foreach_value_destroy(
                        &descriptor->program, &value);
                    return raise_block_execution_error(
                        block, context, out_error);
                }
                outcome = execute_scxml_range(
                    block, session, context, system_values,
                    descriptor->step_begin,
                    descriptor->step_end, next_depth, true, out_error);
                if (outcome != SCXML_EXECUTE_CONTINUE) {
                    cflow_scxml_cmeta_foreach_value_destroy(
                        &descriptor->program, &value);
                    return outcome;
                }
            }
            cflow_scxml_cmeta_foreach_value_destroy(
                &descriptor->program, &value);
        } else if (step->kind == SCXML_STEP_IF) {
            size_t branch;
            const scxml_branch *selected = NULL;
            if (step->branch_first > block->branch_storage_count ||
                step->branch_count >
                    block->branch_storage_count - step->branch_first) {
                *out_error = "SCXML conditional branch span is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            for (branch = 0u; branch < step->branch_count; ++branch) {
                const scxml_branch *candidate =
                    &block->branches[step->branch_first + branch];
                const bool has_cmeta_condition =
                    candidate->condition.impl != NULL;
                if (candidate->step_begin < index + 1u ||
                    candidate->step_begin > candidate->step_end ||
                    candidate->step_end > step->next ||
                    (candidate->unconditional &&
                     (candidate->state != 0u || has_cmeta_condition)) ||
                    (!candidate->unconditional &&
                     ((candidate->state == 0u) ==
                      (candidate->condition.impl == NULL)))) {
                    *out_error = "SCXML conditional branch is invalid";
                    return SCXML_EXECUTE_FATAL;
                }
                if (candidate->unconditional) {
                    selected = candidate;
                    break;
                }
                if (has_cmeta_condition) {
                    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
                    bool enabled = false;
                    if (cflow_scxml_cmeta_expr_evaluate_with_system(
                            &candidate->condition, context->out_state,
                            evaluate_cmeta_executable_active, (void *)context,
                            system_values,
                            &enabled, &diagnostic) !=
                        CFLOW_SCXML_CMETA_EXPR_OK) {
                        if (!enqueue_condition_execution_error(
                                block, context, out_error))
                            return SCXML_EXECUTE_FATAL;
                        if (abort_condition_error)
                            return SCXML_EXECUTE_BLOCK_ABORTED;
                        continue;
                    }
                    if (enabled) {
                        selected = candidate;
                        break;
                    }
                } else if (context->is_active(
                               context->configuration_user,
                               candidate->state)) {
                    selected = candidate;
                    break;
                }
            }
            if (selected != NULL) {
                size_t next_depth;
                scxml_execute_outcome outcome;
                if (!checked_add(depth, 1u, &next_depth))
                    return SCXML_EXECUTE_FATAL;
                outcome = execute_scxml_range(
                    block, session, context, system_values,
                    selected->step_begin,
                    selected->step_end, next_depth,
                    abort_condition_error, out_error);
                if (outcome != SCXML_EXECUTE_CONTINUE) return outcome;
            }
        } else {
            *out_error = "SCXML executable step is invalid";
            return SCXML_EXECUTE_FATAL;
        }
        index = step->next;
    }
    return SCXML_EXECUTE_CONTINUE;
}

static bool execute_scxml_block_impl(
    const scxml_block *block,
    cflow_scxml_session_impl *session,
    const cflow_statechart_executable_context *context,
    const char **out_error) {
    scxml_execute_outcome outcome;
    cflow_scxml_cmeta_expr_system_values system_values;
    const bool trivial_state =
        cmeta_type_require_traits(
            block != NULL ? block->state_type : NULL,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
        CMETA_OK;
    if (out_error != NULL) *out_error = NULL;
    if (block == NULL || block->steps == NULL ||
        (block->branch_storage_count != 0u && block->branches == NULL) ||
        (block->foreach_storage_count != 0u &&
         block->foreach_descriptors == NULL) ||
        block->step_begin >= block->step_end ||
        block->step_end > block->step_storage_count || context == NULL ||
        context->state == NULL || context->out_state == NULL ||
        context->raise_internal == NULL || context->is_active == NULL ||
        context->configuration_user == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML executable block context is invalid";
        return false;
    }
    if (!bind_current_event_system_values(
            session != NULL ? &session->system_values
                            : &block->system_values,
            block->event_names_by_id, block->event_name_count,
            context->event, &system_values)) {
        *out_error = "SCXML executable Event is not in the program map";
        return false;
    }
    if (trivial_state) {
        memcpy(context->out_state, context->state, block->state_type->size);
    } else if (cmeta_type_require_traits(
                   block->state_type,
                   CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                       CMETA_TRAIT_DESTROY) != CMETA_OK ||
               !block->state_type->traits->copy_construct(
                   context->out_state, context->state)) {
        *out_error = "SCXML state copy construction failed";
        return false;
    }
    outcome = execute_scxml_range(
        block, session, context, &system_values,
        block->step_begin, block->step_end,
        0u, false, out_error);
    if (outcome == SCXML_EXECUTE_FATAL) {
        if (!trivial_state) {
            block->state_type->traits->destroy(context->out_state);
        }
        return false;
    }
    if (outcome == SCXML_EXECUTE_BLOCK_ABORTED) {
        if (trivial_state) {
            memcpy(context->out_state, context->state,
                   block->state_type->size);
        } else {
            block->state_type->traits->destroy(context->out_state);
            if (!block->state_type->traits->copy_construct(
                    context->out_state, context->state)) {
                *out_error = "SCXML state rollback copy construction failed";
                return false;
            }
        }
    }
    return true;
}

static bool execute_scxml_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error) {
    return execute_scxml_block_impl(
        (const scxml_block *)user, NULL, context, out_error);
}

static bool execute_scxml_session_block(
    void *user, const cflow_statechart_executable_context *context,
    const char **out_error) {
    const scxml_session_binding_user *binding =
        (const scxml_session_binding_user *)user;
    if (binding == NULL || binding->session == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML session binding is invalid";
        return false;
    }
    return execute_scxml_block_impl(
        binding->block, binding->session, context, out_error);
}

static cflow_scxml_status resolve_condition_state(
    scxml_build *build, turbo_xml_node node,
    cflow_machine_state_id *out_state) {
    const turbo_xml_attribute condition = find_attribute(node, "cond");
    turbo_xml_string_view state_name;
    const scxml_name_ref *state;
    cflow_statechart_state_kind kind;
    if (condition.impl == NULL ||
        !parse_null_in_condition(
            turbo_xml_attribute_value(condition), &state_name)) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            condition.impl != NULL
                ? turbo_xml_attribute_location(condition)
                : turbo_xml_node_location(node),
            "null-model condition must be In(id)");
    }
    state = find_name_ref(
        build->state_names, build->state_name_index, state_name);
    if (state == NULL) {
        return scxml_fail(
            build, CFLOW_SCXML_UNKNOWN_TARGET,
            turbo_xml_attribute_location(condition),
            "In(id) names an unknown SCXML state");
    }
    if (state->id == 0u || state->id > build->state_index) {
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_attribute_location(condition),
            "In(id) resolved outside native state storage");
    }
    kind = build->states[state->id - 1u].kind;
    if (kind == CFLOW_STATECHART_INITIAL ||
        kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
        kind == CFLOW_STATECHART_HISTORY_DEEP) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_attribute_location(condition),
            "In(id) requires a declared real state");
    }
    *out_state = (cflow_machine_state_id)state->id;
    return CFLOW_SCXML_OK;
}

static bool resolve_cmeta_condition_state(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state) {
    const scxml_build *build = (const scxml_build *)user;
    const turbo_xml_string_view wanted = {name, name_size};
    const scxml_name_ref *state;
    cflow_statechart_state_kind kind;
    if (build == NULL || name == NULL || name_size == 0u ||
        out_state == NULL) {
        return false;
    }
    state = find_name_ref(
        build->state_names, build->state_name_index, wanted);
    if (state == NULL || state->id == 0u || state->id > build->state_index)
        return false;
    kind = build->states[state->id - 1u].kind;
    if (kind == CFLOW_STATECHART_INITIAL ||
        kind == CFLOW_STATECHART_HISTORY_SHALLOW ||
        kind == CFLOW_STATECHART_HISTORY_DEEP) {
        return false;
    }
    *out_state = (cflow_machine_state_id)state->id;
    return true;
}

static bool append_utf8_codepoint(
    char *output, size_t capacity, size_t *size, uint32_t codepoint) {
    size_t required;
    if (output == NULL || size == NULL || codepoint == 0u ||
        codepoint > UINT32_C(0x10ffff) ||
        (codepoint >= UINT32_C(0xd800) &&
         codepoint <= UINT32_C(0xdfff))) {
        return false;
    }
    required = codepoint <= UINT32_C(0x7f) ? 1u
             : codepoint <= UINT32_C(0x7ff) ? 2u
             : codepoint <= UINT32_C(0xffff) ? 3u
                                             : 4u;
    if (*size > capacity || required > capacity - *size) return false;
    if (required == 1u) {
        output[(*size)++] = (char)codepoint;
    } else if (required == 2u) {
        output[(*size)++] = (char)(UINT32_C(0xc0) | (codepoint >> 6u));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    } else if (required == 3u) {
        output[(*size)++] = (char)(UINT32_C(0xe0) | (codepoint >> 12u));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    } else {
        output[(*size)++] = (char)(UINT32_C(0xf0) | (codepoint >> 18u));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 12u) & 0x3fu));
        output[(*size)++] =
            (char)(UINT32_C(0x80) | ((codepoint >> 6u) & 0x3fu));
        output[(*size)++] = (char)(UINT32_C(0x80) | (codepoint & 0x3fu));
    }
    return true;
}

static cflow_scxml_status decode_cmeta_attribute_source(
    scxml_build *build, turbo_xml_attribute attribute,
    const char *subject, char **out_source, size_t *out_size) {
    const turbo_xml_string_view source = turbo_xml_attribute_value(attribute);
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    char *decoded;
    size_t input = 0u;
    size_t output = 0u;
    if (out_source == NULL || out_size == NULL || source.size == 0u)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(attribute),
                          "CMeta attribute must not be empty");
    if (source.size > build->cmeta_expression_limits.max_source_bytes)
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(attribute),
                          "CMeta attribute source byte limit exceeded");
    *out_source = NULL;
    *out_size = 0u;
    decoded = (char *)malloc(source.size);
    if (decoded == NULL)
        return scxml_fail(build, CFLOW_SCXML_ALLOCATION_FAILED,
                          turbo_xml_attribute_location(attribute),
                          "unable to decode CMeta attribute");
    while (input < source.size) {
        if (source.data[input] != '&') {
            decoded[output++] = source.data[input++];
            continue;
        }
        if (source.size - input >= 4u &&
            memcmp(source.data + input, "&lt;", 4u) == 0) {
            decoded[output++] = '<';
            input += 4u;
        } else if (source.size - input >= 4u &&
                   memcmp(source.data + input, "&gt;", 4u) == 0) {
            decoded[output++] = '>';
            input += 4u;
        } else if (source.size - input >= 5u &&
                   memcmp(source.data + input, "&amp;", 5u) == 0) {
            decoded[output++] = '&';
            input += 5u;
        } else if (source.size - input >= 6u &&
                   memcmp(source.data + input, "&quot;", 6u) == 0) {
            decoded[output++] = '"';
            input += 6u;
        } else if (source.size - input >= 6u &&
                   memcmp(source.data + input, "&apos;", 6u) == 0) {
            decoded[output++] = '\'';
            input += 6u;
        } else if (source.size - input >= 4u &&
                   source.data[input + 1u] == '#') {
            const bool hexadecimal =
                input + 2u < source.size &&
                (source.data[input + 2u] == 'x' ||
                 source.data[input + 2u] == 'X');
            const uint32_t base = hexadecimal ? 16u : 10u;
            size_t cursor = input + (hexadecimal ? 3u : 2u);
            uint32_t codepoint = 0u;
            bool has_digit = false;
            while (cursor < source.size && source.data[cursor] != ';') {
                const unsigned char ch =
                    (unsigned char)source.data[cursor];
                uint32_t digit;
                if (ch >= '0' && ch <= '9') digit = ch - '0';
                else if (hexadecimal && ch >= 'a' && ch <= 'f')
                    digit = UINT32_C(10) + ch - 'a';
                else if (hexadecimal && ch >= 'A' && ch <= 'F')
                    digit = UINT32_C(10) + ch - 'A';
                else break;
                if (codepoint > (UINT32_C(0x10ffff) - digit) / base)
                    break;
                codepoint = codepoint * base + digit;
                has_digit = true;
                ++cursor;
            }
            if (!has_digit || cursor >= source.size ||
                source.data[cursor] != ';' ||
                !append_utf8_codepoint(
                    decoded, source.size, &output, codepoint)) {
                free(decoded);
                return scxml_fail(
                    build, CFLOW_SCXML_INVALID_STRUCTURE,
                    turbo_xml_attribute_location(attribute),
                    "CMeta attribute has an invalid XML character reference");
            }
            input = cursor + 1u;
        } else {
            free(decoded);
            (void)snprintf(message, sizeof(message),
                           "CMeta %s has an unsupported XML entity reference",
                           subject != NULL ? subject : "attribute");
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(attribute),
                              message);
        }
    }
    *out_source = decoded;
    *out_size = output;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status compile_cmeta_condition_program(
    scxml_build *build, turbo_xml_attribute condition,
    cflow_scxml_cmeta_expr_program *program) {
    cflow_scxml_cmeta_expr_diagnostic expression_diagnostic = {0};
    cflow_scxml_cmeta_expr_status expression_status;
    cflow_scxml_status status;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    status = decode_cmeta_attribute_source(
        build, condition, "condition", &source, &source_size);
    if (status != CFLOW_SCXML_OK) return status;
    expression_status = cflow_scxml_cmeta_expr_compile(
        program, source, source_size,
        build->cmeta_root, resolve_cmeta_condition_state, build,
        &build->cmeta_expression_limits, &expression_diagnostic);
    free(source);
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK)
        return CFLOW_SCXML_OK;
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED)
        status = CFLOW_SCXML_LIMIT_EXCEEDED;
    else if (expression_status == CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED)
        status = CFLOW_SCXML_ALLOCATION_FAILED;
    else
        status = CFLOW_SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message),
        "CMeta condition byte %zu: %s", expression_diagnostic.byte_offset,
        expression_diagnostic.message[0] != '\0'
            ? expression_diagnostic.message
            : "expression compilation failed");
    return scxml_fail(build, status, turbo_xml_attribute_location(condition),
                      message);
}

static cflow_scxml_status compile_cmeta_condition(
    scxml_build *build, turbo_xml_attribute condition,
    scxml_guard_user *guard) {
    guard->data_model = SCXML_DATA_MODEL_CMETA;
    return compile_cmeta_condition_program(
        build, condition, &guard->value.expression);
}

static bool evaluate_cmeta_active_state(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const cflow_statechart_guard_context *context =
        (const cflow_statechart_guard_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL) {
        return false;
    }
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static bool evaluate_scxml_transition_guard_impl(
    const scxml_guard_user *guard,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    if (guard == NULL || context == NULL || context->state == NULL ||
        context->is_active == NULL || context->configuration_user == NULL ||
        out_enabled == NULL || out_error == NULL) {
        return false;
    }
    *out_error = NULL;
    if (guard->data_model == SCXML_DATA_MODEL_NULL) {
        *out_enabled = context->is_active(
            context->configuration_user, guard->value.state);
        return true;
    }
    if (guard->data_model == SCXML_DATA_MODEL_CMETA) {
        cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
        cflow_scxml_cmeta_expr_system_values current_system_values;
        if (!bind_current_event_system_values(
                system_values, guard->event_names_by_id,
                guard->event_name_count, context->event,
                &current_system_values)) {
            *out_error = "SCXML guard Event is not in the program map";
            return false;
        }
        if (cflow_scxml_cmeta_expr_evaluate_with_system(
                &guard->value.expression, context->state,
                evaluate_cmeta_active_state, (void *)context,
                &current_system_values, out_enabled, &diagnostic) ==
            CFLOW_SCXML_CMETA_EXPR_OK) {
            return true;
        }
        *out_error = "CMeta transition condition evaluation failed";
    }
    return false;
}

static bool evaluate_scxml_transition_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    const scxml_guard_user *guard = (const scxml_guard_user *)user;
    return evaluate_scxml_transition_guard_impl(
        guard, guard != NULL ? &guard->system_values : NULL,
        context, out_enabled, out_error);
}

static bool evaluate_scxml_session_transition_guard(
    void *user, const cflow_statechart_guard_context *context,
    bool *out_enabled, const char **out_error) {
    const scxml_session_guard_user *binding =
        (const scxml_session_guard_user *)user;
    if (binding == NULL || binding->session == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML session guard binding is invalid";
        return false;
    }
    return evaluate_scxml_transition_guard_impl(
        binding->guard, &binding->session->system_values,
        context, out_enabled, out_error);
}

static cflow_scxml_status emit_executable_node(scxml_build *build, turbo_xml_node node);

static cflow_scxml_status emit_raise_step(
    scxml_build *build, turbo_xml_node node) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    const scxml_name_ref *event = find_name_ref(
        build->event_names, build->event_name_count,
        turbo_xml_attribute_value(event_attribute));
    const size_t step = build->step_index;
    if (event == NULL) {
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_attribute_location(event_attribute),
            "raise event was not retained in the event map");
    }
    if (step >= build->step_capacity) {
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_node_location(node),
            "raise exceeded admitted step storage");
    }
    ++build->step_index;
    build->steps[step] = (scxml_step){
        SCXML_STEP_RAISE, build->step_index,
        (cflow_event_id)event->id, 0u, 0u};
    return CFLOW_SCXML_OK;
}

static bool retain_effect_attribute(scxml_build *build,
                                    turbo_xml_attribute attribute,
                                    const char **out_data,
                                    size_t *out_size) {
    turbo_xml_string_view value;
    size_t retained;
    char *stored;
    *out_data = NULL;
    *out_size = 0u;
    if (attribute.impl == NULL) return true;
    value = turbo_xml_attribute_value(attribute);
    if (!checked_add(value.size, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained >
            build->effect_storage_capacity - build->effect_storage_index) {
        return false;
    }
    stored = build->effect_storage + build->effect_storage_index;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    build->effect_storage_index += retained;
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static cflow_scxml_status emit_send_step(scxml_build *build,
                                         turbo_xml_node node) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    const turbo_xml_attribute target_attribute = find_attribute(node, "target");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute delay_attribute = find_attribute(node, "delay");
    const scxml_name_ref *event = find_name_ref(
        build->event_names, build->event_name_count,
        turbo_xml_attribute_value(event_attribute));
    const size_t step = build->step_index;
    const size_t effect = build->effect_index;
    scxml_effect_descriptor *descriptor;
    turbo_xml_string_view target;
    if (event == NULL || step >= build->step_capacity ||
        effect >= build->effect_capacity) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "send exceeded admitted descriptor storage");
    }
    descriptor = &build->effects[effect];
    descriptor->kind = SCXML_EFFECT_SEND;
    descriptor->event_id = (cflow_event_id)event->id;
    if (!retain_effect_attribute(
            build, event_attribute, &descriptor->request.send.event,
            &descriptor->request.send.event_size) ||
        !retain_effect_attribute(
            build, target_attribute, &descriptor->request.send.target,
            &descriptor->request.send.target_size) ||
        !retain_effect_attribute(
            build, type_attribute, &descriptor->request.send.type,
            &descriptor->request.send.type_size) ||
        !retain_effect_attribute(
            build, id_attribute, &descriptor->request.send.id,
            &descriptor->request.send.id_size) ||
        (delay_attribute.impl != NULL &&
         !parse_delay_ms(turbo_xml_attribute_value(delay_attribute),
                         &descriptor->request.send.delay_ms))) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "send descriptor mismatched admitted storage");
    }
    target = target_attribute.impl != NULL
                 ? turbo_xml_attribute_value(target_attribute)
                 : (turbo_xml_string_view){NULL, 0u};
    descriptor->internal_target =
        view_equal_raw(target, "#_internal") ||
        view_equal_raw(target, "_internal");
    ++build->effect_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_SEND,
        .next = build->step_index,
        .effect = effect};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_cancel_step(scxml_build *build,
                                           turbo_xml_node node) {
    const turbo_xml_attribute sendid_attribute = find_attribute(node, "sendid");
    const size_t step = build->step_index;
    const size_t effect = build->effect_index;
    scxml_effect_descriptor *descriptor;
    if (step >= build->step_capacity || effect >= build->effect_capacity) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "cancel exceeded admitted descriptor storage");
    }
    descriptor = &build->effects[effect];
    descriptor->kind = SCXML_EFFECT_CANCEL;
    if (!retain_effect_attribute(
            build, sendid_attribute, &descriptor->request.cancel.send_id,
            &descriptor->request.cancel.send_id_size)) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "cancel descriptor mismatched admitted storage");
    }
    ++build->effect_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_CANCEL,
        .next = build->step_index,
        .effect = effect};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_conditional_branch(
    scxml_build *build, turbo_xml_node node, scxml_branch *branch) {
    if (build->data_model == SCXML_DATA_MODEL_CMETA) {
        return compile_cmeta_condition_program(
            build, find_attribute(node, "cond"), &branch->condition);
    }
    return resolve_condition_state(build, node, &branch->state);
}

static cflow_scxml_status emit_conditional_step(
    scxml_build *build, turbo_xml_node node) {
    const size_t step = build->step_index;
    const size_t branch_first = build->branch_index;
    size_t branch_count;
    size_t current_branch = branch_first;
    size_t index;
    cflow_scxml_status status;
    if (!checked_add(element_child_count(node, SCXML_ELEMENT_ELSEIF),
                     element_child_count(node, SCXML_ELEMENT_ELSE),
                     &branch_count) ||
        !checked_add(branch_count, 1u, &branch_count) ||
        step >= build->step_capacity ||
        branch_first > build->branch_capacity ||
        branch_count > build->branch_capacity - branch_first) {
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_node_location(node),
            "conditional exceeded admitted storage");
    }
    ++build->step_index;
    build->branch_index += branch_count;
    status = emit_conditional_branch(
        build, node, &build->branches[current_branch]);
    if (status != CFLOW_SCXML_OK) return status;
    build->branches[current_branch].step_begin = build->step_index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (kind == SCXML_ELEMENT_ELSEIF || kind == SCXML_ELEMENT_ELSE) {
            build->branches[current_branch].step_end = build->step_index;
            ++current_branch;
            build->branches[current_branch].step_begin = build->step_index;
            if (kind == SCXML_ELEMENT_ELSE) {
                build->branches[current_branch].unconditional = true;
            } else {
                status = emit_conditional_branch(
                    build, child, &build->branches[current_branch]);
                if (status != CFLOW_SCXML_OK) return status;
            }
        } else {
            status = emit_executable_node(build, child);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    if (current_branch + 1u != branch_first + branch_count) {
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_node_location(node),
            "conditional branch emission count mismatched admission");
    }
    build->branches[current_branch].step_end = build->step_index;
    build->steps[step] = (scxml_step){
        SCXML_STEP_IF, build->step_index, 0u, branch_first, branch_count};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_log_step(scxml_build *build,
                                        turbo_xml_node node) {
    const turbo_xml_attribute label_attribute = find_attribute(node, "label");
    const turbo_xml_string_view label =
        label_attribute.impl != NULL
            ? turbo_xml_attribute_value(label_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    const size_t step = build->step_index;
    size_t retained_bytes;
    char *stored_label;
    if (!checked_add(label.size, 1u, &retained_bytes) ||
        step >= build->step_capacity ||
        build->log_storage_index > build->log_storage_capacity ||
        retained_bytes >
            build->log_storage_capacity - build->log_storage_index) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "log exceeded admitted storage");
    }
    stored_label = build->log_storage + build->log_storage_index;
    if (label.size != 0u)
        memcpy(stored_label, label.data, label.size);
    stored_label[label.size] = '\0';
    build->log_storage_index += retained_bytes;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_LOG,
        .next = build->step_index,
        .label = stored_label};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_assign_step(scxml_build *build,
                                           turbo_xml_node node) {
    const turbo_xml_attribute location_attribute =
        find_attribute(node, "location");
    const turbo_xml_attribute expression_attribute =
        find_attribute(node, "expr");
    const size_t step = build->step_index;
    const size_t assignment = build->assignment_index;
    cflow_scxml_cmeta_expr_diagnostic assignment_diagnostic = {0};
    cflow_scxml_cmeta_expr_status assignment_status;
    cflow_scxml_status status;
    char *location = NULL;
    size_t location_size = 0u;
    char *expression = NULL;
    size_t expression_size = 0u;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    if (step >= build->step_capacity ||
        assignment >= build->assignment_capacity)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "assign exceeded admitted descriptor storage");
    status = decode_cmeta_attribute_source(
        build, location_attribute, "assignment location",
        &location, &location_size);
    if (status != CFLOW_SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, expression_attribute, "assignment expression",
        &expression, &expression_size);
    if (status != CFLOW_SCXML_OK) {
        free(location);
        return status;
    }
    assignment_status = cflow_scxml_cmeta_assign_compile(
        &build->assignments[assignment], location, location_size,
        expression, expression_size, build->cmeta_root,
        resolve_cmeta_condition_state, build,
        &build->cmeta_expression_limits, &assignment_diagnostic);
    free(location);
    free(expression);
    if (assignment_status != CFLOW_SCXML_CMETA_EXPR_OK) {
        const cflow_scxml_status public_status =
            assignment_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
                ? CFLOW_SCXML_LIMIT_EXCEEDED
                : assignment_status ==
                          CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
                      ? CFLOW_SCXML_ALLOCATION_FAILED
                      : CFLOW_SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta assignment byte %zu: %s",
            assignment_diagnostic.byte_offset,
            assignment_diagnostic.message[0] != '\0'
                ? assignment_diagnostic.message
                : "assignment compilation failed");
        return scxml_fail(build, public_status,
                          turbo_xml_node_location(node), message);
    }
    ++build->assignment_index;
    ++build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_ASSIGN,
        .next = build->step_index,
        .assignment = assignment};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_foreach_step(scxml_build *build,
                                            turbo_xml_node node) {
    const turbo_xml_attribute array_attribute = find_attribute(node, "array");
    const turbo_xml_attribute item_attribute = find_attribute(node, "item");
    const turbo_xml_attribute index_attribute = find_attribute(node, "index");
    const size_t step = build->step_index;
    const size_t foreach_index = build->foreach_index;
    scxml_foreach_descriptor *descriptor;
    cflow_scxml_cmeta_expr_diagnostic foreach_diagnostic = {0};
    cflow_scxml_cmeta_expr_status foreach_status;
    char *array = NULL;
    size_t array_size = 0u;
    char *item = NULL;
    size_t item_size = 0u;
    char *index = NULL;
    size_t index_size = 0u;
    size_t child_index;
    cflow_scxml_status status;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    if (step >= build->step_capacity ||
        foreach_index >= build->foreach_capacity)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "foreach exceeded admitted descriptor storage");
    status = decode_cmeta_attribute_source(
        build, array_attribute, "foreach array", &array, &array_size);
    if (status != CFLOW_SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, item_attribute, "foreach item", &item, &item_size);
    if (status != CFLOW_SCXML_OK) {
        free(array);
        return status;
    }
    if (index_attribute.impl != NULL) {
        status = decode_cmeta_attribute_source(
            build, index_attribute, "foreach index", &index, &index_size);
        if (status != CFLOW_SCXML_OK) {
            free(array);
            free(item);
            return status;
        }
    }
    descriptor = &build->foreach_descriptors[foreach_index];
    foreach_status = cflow_scxml_cmeta_foreach_compile(
        &descriptor->program, array, array_size, item, item_size,
        index, index_size, build->cmeta_root,
        build->cmeta_expression_limits.max_path_depth,
        build->max_iterations, &foreach_diagnostic);
    free(array);
    free(item);
    free(index);
    if (foreach_status != CFLOW_SCXML_CMETA_EXPR_OK) {
        const cflow_scxml_status public_status =
            foreach_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
                ? CFLOW_SCXML_LIMIT_EXCEEDED
                : foreach_status ==
                          CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
                      ? CFLOW_SCXML_ALLOCATION_FAILED
                      : CFLOW_SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta foreach byte %zu: %s",
            foreach_diagnostic.byte_offset,
            foreach_diagnostic.message[0] != '\0'
                ? foreach_diagnostic.message
                : "foreach compilation failed");
        return scxml_fail(build, public_status,
                          turbo_xml_node_location(node), message);
    }
    ++build->step_index;
    ++build->foreach_index;
    descriptor->step_begin = build->step_index;
    for (child_index = 0u;
         child_index < turbo_xml_node_child_count(node); ++child_index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, child_index);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        status = emit_executable_node(build, child);
        if (status != CFLOW_SCXML_OK) return status;
    }
    descriptor->step_end = build->step_index;
    build->steps[step] = (scxml_step){
        .kind = SCXML_STEP_FOREACH,
        .next = build->step_index,
        .foreach_descriptor = foreach_index};
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_executable_node(
    scxml_build *build, turbo_xml_node node) {
    const scxml_element_kind kind = element_kind(node);
    if (kind == SCXML_ELEMENT_RAISE) return emit_raise_step(build, node);
    if (kind == SCXML_ELEMENT_SEND) return emit_send_step(build, node);
    if (kind == SCXML_ELEMENT_CANCEL) return emit_cancel_step(build, node);
    if (kind == SCXML_ELEMENT_LOG) return emit_log_step(build, node);
    if (kind == SCXML_ELEMENT_ASSIGN) return emit_assign_step(build, node);
    if (kind == SCXML_ELEMENT_FOREACH) return emit_foreach_step(build, node);
    if (kind == SCXML_ELEMENT_IF) return emit_conditional_step(build, node);
    return scxml_fail(
        build, CFLOW_SCXML_NATIVE_IR_REJECTED,
        turbo_xml_node_location(node),
        "admitted executable element could not be emitted");
}

static cflow_scxml_status emit_executable_block(
    scxml_build *build, turbo_xml_node node,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t first_step = build->step_index;
    const size_t first_log_byte = build->log_storage_index;
    const size_t first_effect = build->effect_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    size_t index;
    *out_executable = 0u;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        cflow_scxml_status status;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT)
            continue;
        status = emit_executable_node(build, child);
        if (status != CFLOW_SCXML_OK)
            return status;
    }
    if (build->step_index == first_step) return CFLOW_SCXML_OK;
    build->blocks[block_index] = (scxml_block){
        .state_type = build->data_model == SCXML_DATA_MODEL_CMETA
                          ? build->cmeta_root->storage_type
                          : &cmeta_type_bool,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable,
        build->data_model == SCXML_DATA_MODEL_CMETA
            ? build->cmeta_root->storage_type
            : &cmeta_type_bool,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL |
            (build->log_storage_index != first_log_byte ||
             build->effect_index != first_effect
                 ? CMETA_EFFECT_IO : CMETA_EFFECT_PURE),
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = execute_scxml_block};
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_invoke_lifecycle_block(
    scxml_build *build, size_t invocation, bool enter,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t step_index = build->step_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    if (invocation >= build->invocation_capacity ||
        step_index >= build->step_capacity) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          (turbo_xml_location){0u, 0u, 0u},
                          "invoke lifecycle block exceeded admitted storage");
    }
    ++build->step_index;
    build->steps[step_index] = (scxml_step){
        .kind = enter ? SCXML_STEP_INVOKE_ENTER : SCXML_STEP_INVOKE_EXIT,
        .next = build->step_index,
        .invocation = invocation};
    build->blocks[block_index] = (scxml_block){
        .state_type = build->data_model == SCXML_DATA_MODEL_CMETA
                          ? build->cmeta_root->storage_type
                          : &cmeta_type_bool,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable,
        build->data_model == SCXML_DATA_MODEL_CMETA
            ? build->cmeta_root->storage_type
            : &cmeta_type_bool,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL | CMETA_EFFECT_IO,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = execute_scxml_block};
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_finalize_block(
    scxml_build *build, turbo_xml_node node,
    const scxml_block **out_block) {
    const size_t block_index = build->block_index;
    const size_t first_step = build->step_index;
    size_t index;
    *out_block = NULL;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        cflow_scxml_status status;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        status = emit_executable_node(build, child);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (build->step_index == first_step) return CFLOW_SCXML_OK;
    build->blocks[block_index] = (scxml_block){
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    *out_block = &build->blocks[block_index];
    ++build->block_index;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_state_executables(scxml_build *build,
                                                 turbo_xml_node node,
                                                 size_t node_count) {
    const cflow_machine_state_id owner = node_id(build, node, node_count);
    uint32_t entry_order = 0u;
    uint32_t exit_order = 0u;
    size_t index;
    if (owner == 0u) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "SCXML state has no native owner for executable content");
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        cflow_scxml_status status;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_ONENTRY ||
            child_kind == SCXML_ELEMENT_ONEXIT) {
            cflow_statechart_executable_id executable = 0u;
            status = emit_executable_block(build, child, &executable);
            if (status != CFLOW_SCXML_OK) return status;
            if (executable != 0u) {
                const cflow_statechart_state_action_kind action_kind =
                    child_kind == SCXML_ELEMENT_ONENTRY
                        ? CFLOW_STATECHART_STATE_ACTION_ENTRY
                        : CFLOW_STATECHART_STATE_ACTION_EXIT;
                uint32_t *order = child_kind == SCXML_ELEMENT_ONENTRY
                    ? &entry_order : &exit_order;
                build->state_actions[build->state_action_index++] =
                    (cflow_statechart_state_action){
                        owner, action_kind, executable, (*order)++};
            }
        }
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        scxml_invocation_descriptor *descriptor;
        cflow_statechart_executable_id enter = 0u;
        cflow_statechart_executable_id exit = 0u;
        size_t child_index;
        cflow_scxml_status status;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            child_kind != SCXML_ELEMENT_INVOKE)
            continue;
        if (build->invocation_emit_index >= build->invocation_index) {
            return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                              turbo_xml_node_location(child),
                              "invoke emission exceeded declarations");
        }
        descriptor =
            &build->invocations[build->invocation_emit_index];
        if (descriptor->owner != owner ||
            descriptor->source_node != child.impl) {
            return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                              turbo_xml_node_location(child),
                              "invoke declaration order mismatched emission");
        }
        for (child_index = 0u;
             child_index < turbo_xml_node_child_count(child);
             ++child_index) {
            const turbo_xml_node content =
                turbo_xml_node_child_at(child, child_index);
            if (turbo_xml_node_type(content) == TURBO_XML_ELEMENT &&
                element_kind(content) == SCXML_ELEMENT_FINALIZE) {
                status = emit_finalize_block(
                    build, content, &descriptor->finalize);
                if (status != CFLOW_SCXML_OK) return status;
            }
        }
        status = emit_invoke_lifecycle_block(
            build, build->invocation_emit_index, true, &enter);
        if (status != CFLOW_SCXML_OK) return status;
        status = emit_invoke_lifecycle_block(
            build, build->invocation_emit_index, false, &exit);
        if (status != CFLOW_SCXML_OK) return status;
        build->state_actions[build->state_action_index++] =
            (cflow_statechart_state_action){
                owner, CFLOW_STATECHART_STATE_ACTION_ENTRY,
                enter, entry_order++};
        build->state_actions[build->state_action_index++] =
            (cflow_statechart_state_action){
                owner, CFLOW_STATECHART_STATE_ACTION_EXIT,
                exit, exit_order++};
        descriptor->source_node = NULL;
        ++build->invocation_emit_index;
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            (is_state_element(child_kind) ||
             child_kind == SCXML_ELEMENT_INITIAL ||
             child_kind == SCXML_ELEMENT_HISTORY)) {
            cflow_scxml_status status = emit_state_executables(
                build, child, node_count);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status resolve_target(scxml_build *build,
                                         turbo_xml_attribute target_attribute,
                                         cflow_machine_state_id *out_target) {
    turbo_xml_string_view target = {NULL, 0u};
    const scxml_name_ref *resolved;
    size_t cursor = 0u;
    if (target_attribute.impl == NULL) {
        *out_target = 0u;
        return CFLOW_SCXML_OK;
    }
    (void)token_next(turbo_xml_attribute_value(target_attribute), &cursor,
                     &target);
    resolved = find_name_ref(build->state_names, build->state_name_index,
                             target);
    if (resolved == NULL) {
        return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                          turbo_xml_attribute_location(target_attribute),
                          "transition target does not name a declared state");
    }
    *out_target = (cflow_machine_state_id)resolved->id;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_transition_token(
    scxml_build *build, cflow_machine_state_id source,
    turbo_xml_node transition_node, turbo_xml_string_view event_token,
    bool has_event, cflow_statechart_transition_id *out_transition) {
    const turbo_xml_attribute target_attribute =
        find_attribute(transition_node, "target");
    const turbo_xml_attribute type_attribute =
        find_attribute(transition_node, "type");
    const turbo_xml_attribute condition_attribute =
        find_attribute(transition_node, "cond");
    cflow_statechart_transition row;
    cflow_scxml_status status;

    memset(&row, 0, sizeof(row));
    row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
    row.source = source;
    row.kind = type_attribute.impl != NULL &&
                       view_equal_raw(turbo_xml_attribute_value(type_attribute),
                                      "internal")
                   ? CFLOW_STATECHART_TRANSITION_INTERNAL
                   : CFLOW_STATECHART_TRANSITION_EXTERNAL;
    if (type_attribute.impl != NULL &&
        !view_equal_raw(turbo_xml_attribute_value(type_attribute), "internal") &&
        !view_equal_raw(turbo_xml_attribute_value(type_attribute), "external")) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(type_attribute),
                          "transition type must be internal or external");
    }
    row.priority = (uint32_t)build->transition_index;
    row.document_order = (uint32_t)build->transition_index;
    status = resolve_target(build, target_attribute, &row.target);
    if (status != CFLOW_SCXML_OK) return status;
    if (!has_event) {
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
    } else {
        turbo_xml_string_view completed = {NULL, 0u};
        if (completion_token(event_token, &completed)) {
            const scxml_name_ref *state =
                find_name_ref(build->state_names, build->state_name_index,
                              completed);
            if (state == NULL) {
                return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                                  turbo_xml_node_location(transition_node),
                                  "done.state event names an unknown state");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_COMPLETION;
            row.completion = (cflow_machine_state_id)state->id;
        } else {
            const scxml_name_ref *event =
                find_name_ref(build->event_names, build->event_name_count,
                              event_token);
            if (event == NULL) {
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(transition_node),
                                  "SCXML event map invariant failed");
            }
            row.trigger = CFLOW_STATECHART_TRIGGER_EVENT;
            row.event = (cflow_event_id)event->id;
        }
    }
    if (condition_attribute.impl != NULL) {
        const size_t guard_index = build->guard_index;
        if (guard_index >= build->guard_capacity) {
            return scxml_fail(
                build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                turbo_xml_attribute_location(condition_attribute),
                "transition guard storage invariant failed");
        }
        if (build->data_model == SCXML_DATA_MODEL_CMETA) {
            status = compile_cmeta_condition(
                build, condition_attribute, &build->guard_users[guard_index]);
        } else {
            cflow_machine_state_id condition_state = 0u;
            status = resolve_condition_state(
                build, transition_node, &condition_state);
            if (status == CFLOW_SCXML_OK) {
                build->guard_users[guard_index].data_model =
                    SCXML_DATA_MODEL_NULL;
                build->guard_users[guard_index].value.state = condition_state;
            }
        }
        if (status != CFLOW_SCXML_OK) return status;
        row.guard = (cflow_statechart_guard_id)(guard_index + 1u);
        build->guards[guard_index] = (cflow_statechart_guard){
            row.guard,
            build->data_model == SCXML_DATA_MODEL_CMETA
                ? build->cmeta_root->storage_type
                : &cmeta_type_bool,
            CMETA_EFFECT_PURE,
            CMETA_PROP_STABLE | CMETA_PROP_NO_ALIAS};
        build->guard_bindings[guard_index] =
            (cflow_statechart_guard_binding){
                .id = row.guard,
                .user = &build->guard_users[guard_index],
                .contextual_fn = evaluate_scxml_transition_guard};
        ++build->guard_index;
    }
    build->transitions[build->transition_index++] = row;
    *out_transition = row.id;
    return CFLOW_SCXML_OK;
}

static const scxml_synthetic_initial *find_synthetic(
    const scxml_build *build, size_t synthetic_count,
    cflow_machine_state_id parent) {
    size_t low = 0u;
    size_t high = synthetic_count;
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        if (build->synthetic_initials[middle].parent < parent)
            low = middle + 1u;
        else
            high = middle;
    }
    return low < synthetic_count &&
                   build->synthetic_initials[low].parent == parent
               ? &build->synthetic_initials[low]
               : NULL;
}

static cflow_scxml_status emit_transitions(scxml_build *build,
                                           turbo_xml_node node,
                                           size_t node_count,
                                           size_t synthetic_count) {
    const cflow_machine_state_id source = node_id(build, node, node_count);
    const scxml_synthetic_initial *synthetic =
        find_synthetic(build, synthetic_count, source);
    size_t index;
    if (synthetic != NULL) {
        const scxml_name_ref *target =
            find_name_ref(build->state_names, build->state_name_index,
                          synthetic->target);
        cflow_statechart_transition row;
        if (target == NULL) {
            return scxml_fail(build, CFLOW_SCXML_UNKNOWN_TARGET,
                              synthetic->location,
                              "initial target does not name a declared state");
        }
        memset(&row, 0, sizeof(row));
        row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
        row.source = synthetic->state;
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
        row.target = (cflow_machine_state_id)target->id;
        row.kind = CFLOW_STATECHART_TRANSITION_EXTERNAL;
        row.priority = (uint32_t)build->transition_index;
        row.document_order = (uint32_t)build->transition_index;
        build->transitions[build->transition_index++] = row;
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind child_kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (child_kind == SCXML_ELEMENT_TRANSITION) {
            const turbo_xml_attribute event_attribute =
                find_attribute(child, "event");
            cflow_statechart_executable_id executable = 0u;
            cflow_scxml_status status = emit_executable_block(
                build, child, &executable);
            if (status != CFLOW_SCXML_OK) return status;
            if (event_attribute.impl == NULL) {
                cflow_statechart_transition_id transition = 0u;
                status = emit_transition_token(
                    build, source, child, (turbo_xml_string_view){NULL, 0u},
                    false, &transition);
                if (status != CFLOW_SCXML_OK) return status;
                if (executable != 0u) {
                    build->transition_actions[
                        build->transition_action_index++] =
                        (cflow_statechart_transition_action){
                            transition, executable, 0u};
                }
            } else {
                const turbo_xml_string_view value =
                    turbo_xml_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (token_next(value, &cursor, &token)) {
                    cflow_statechart_transition_id transition = 0u;
                    status = emit_transition_token(
                        build, source, child, token, true, &transition);
                    if (status != CFLOW_SCXML_OK) return status;
                    if (executable != 0u) {
                        build->transition_actions[
                            build->transition_action_index++] =
                            (cflow_statechart_transition_action){
                                transition, executable, 0u};
                    }
                }
            }
        } else if (is_state_element(child_kind) ||
                   child_kind == SCXML_ELEMENT_INITIAL ||
                   child_kind == SCXML_ELEMENT_HISTORY) {
            cflow_scxml_status status = emit_transitions(
                build, child, node_count, synthetic_count);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static void destroy_guard_users(scxml_guard_user *users, size_t count) {
    size_t index;
    if (users == NULL) return;
    for (index = 0u; index < count; ++index) {
        if (users[index].data_model == SCXML_DATA_MODEL_CMETA) {
            cflow_scxml_cmeta_expr_program_destroy(
                &users[index].value.expression);
        }
    }
}

static void destroy_assignments(
    cflow_scxml_cmeta_assign_program *assignments, size_t count) {
    size_t index;
    if (assignments == NULL) return;
    for (index = 0u; index < count; ++index)
        cflow_scxml_cmeta_assign_program_destroy(&assignments[index]);
}

static void destroy_branches(scxml_branch *branches, size_t count) {
    size_t index;
    if (branches == NULL) return;
    for (index = 0u; index < count; ++index)
        cflow_scxml_cmeta_expr_program_destroy(&branches[index].condition);
}

static void free_build(scxml_build *build) {
    free(build->states);
    free(build->transitions);
    free(build->guards);
    free(build->events);
    free(build->executables);
    free(build->state_actions);
    free(build->transition_actions);
    free(build->bindings);
    free(build->guard_bindings);
    destroy_guard_users(build->guard_users, build->guard_capacity);
    free(build->guard_users);
    free(build->blocks);
    free(build->steps);
    destroy_branches(build->branches, build->branch_capacity);
    free(build->branches);
    free(build->effects);
    destroy_assignments(build->assignments, build->assignment_capacity);
    free(build->assignments);
    free(build->foreach_descriptors);
    free(build->invocations);
    free(build->invocation_names);
    free(build->log_storage);
    free(build->effect_storage);
    free(build->invocation_storage);
    free(build->state_names);
    free(build->event_names);
    free(build->event_occurrences);
    free(build->node_refs);
    free(build->synthetic_initials);
    memset(build, 0, sizeof(*build));
}

static void *allocate_rows(size_t count, size_t element_size) {
    size_t bytes;
    if (count == 0u) return NULL;
    if (!checked_multiply(count, element_size, &bytes)) return NULL;
    return calloc(1u, bytes);
}

static void copy_program_names(scxml_program_name *destination,
                               const scxml_name_ref *source,
                               size_t count, char **cursor) {
    size_t index;
    for (index = 0u; index < count; ++index) {
        destination[index].name = *cursor;
        destination[index].size = source[index].name.size;
        destination[index].id = source[index].id;
        memcpy(*cursor, source[index].name.data, source[index].name.size);
        *cursor += source[index].name.size;
    }
}

cflow_scxml_limits cflow_scxml_default_limits(void) {
    const cflow_scxml_limits limits = {
        {16u * 1024u * 1024u, 1048576u, 1048576u, 256u,
         32u * 1024u * 1024u},
        CFLOW_SCXML_DEFAULT_MAX_STATES,
        CFLOW_SCXML_DEFAULT_MAX_EVENTS,
        CFLOW_SCXML_DEFAULT_MAX_TRANSITIONS,
        CFLOW_SCXML_DEFAULT_MAX_NAME_BYTES};
    return limits;
}

cflow_scxml_cmeta_compile_options_v1
cflow_scxml_cmeta_default_compile_options(const cmeta_data_desc *root) {
    const cflow_scxml_cmeta_expr_limits limits =
        cflow_scxml_cmeta_expr_default_limits();
    const cflow_scxml_cmeta_compile_options_v1 options = {
        .abi_version = CFLOW_SCXML_CMETA_COMPILE_OPTIONS_ABI_V1,
        .struct_size = sizeof(cflow_scxml_cmeta_compile_options_v1),
        .root = root,
        .max_source_bytes = limits.max_source_bytes,
        .max_instructions = limits.max_instructions,
        .max_operands = limits.max_operands,
        .max_expression_depth = limits.max_expression_depth,
        .max_path_depth = limits.max_path_depth,
        .max_literal_bytes = limits.max_literal_bytes,
        .max_string_bytes = limits.max_string_bytes,
        .max_iterations = CFLOW_SCXML_CMETA_DEFAULT_MAX_ITERATIONS
    };
    return options;
}

static cflow_scxml_status compile_scxml_model(
    cflow_scxml_program *out, const char *input, size_t input_size,
    const cflow_scxml_limits *limits_or_null,
    scxml_data_model data_model, const cmeta_data_desc *cmeta_root,
    const cflow_scxml_cmeta_expr_limits *cmeta_expression_limits,
    size_t cmeta_max_iterations,
    cflow_scxml_diagnostic *diagnostic) {
    cflow_scxml_limits limits = limits_or_null != NULL
                                    ? *limits_or_null
                                    : cflow_scxml_default_limits();
    turbo_xml_document document = {0};
    turbo_xml_diagnostic xml_diagnostic = {0};
    turbo_xml_node root;
    scxml_build build;
    scxml_counts counts = {0};
    cflow_scxml_program_impl *impl = NULL;
    cflow_statechart_definition definition;
    cflow_statechart_status native_status;
    cflow_scxml_status status;
    turbo_xml_attribute version;
    turbo_xml_attribute datamodel;
    turbo_xml_attribute document_name_attribute;
    turbo_xml_string_view document_name = {NULL, 0u};
    size_t index;
    size_t name_bytes = 0u;
    size_t retained_string_bytes = 0u;
    size_t action_ref_count = 0u;
    char *name_cursor;
    bool needs_execution_error = false;

    memset(&build, 0, sizeof(build));
    build.limits = limits;
    build.diagnostic = diagnostic;
    build.data_model = data_model;
    build.cmeta_root = cmeta_root;
    build.max_iterations = cmeta_max_iterations;
    if (cmeta_expression_limits != NULL)
        build.cmeta_expression_limits = *cmeta_expression_limits;
    if (diagnostic != NULL) memset(diagnostic, 0, sizeof(*diagnostic));
    if (out == NULL || out->impl != NULL || input == NULL || input_size == 0u ||
        limits.max_states == 0u || limits.max_events == 0u ||
        limits.max_transitions == 0u || limits.max_name_bytes == 0u) {
        return scxml_fail(&build, CFLOW_SCXML_INVALID_ARGUMENT,
                          (turbo_xml_location){0u, 0u, 0u},
                          "output/input and all SCXML limits must be valid");
    }
    switch (turbo_xml_parse(&document, input, input_size, &limits.xml,
                            &xml_diagnostic)) {
        case TURBO_XML_OK: break;
        case TURBO_XML_LIMIT_EXCEEDED:
            return scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        case TURBO_XML_ALLOCATION_FAILED:
            return scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
        default:
            return scxml_fail(&build, CFLOW_SCXML_XML_ERROR,
                              xml_diagnostic.location,
                              xml_diagnostic.message);
    }
    root = turbo_xml_document_root(&document);
    if (element_kind(root) != SCXML_ELEMENT_SCXML ||
        !view_equal_raw(turbo_xml_node_namespace_uri(root),
                        CFLOW_SCXML_NAMESPACE)) {
        status = scxml_fail(&build, CFLOW_SCXML_INVALID_NAMESPACE,
                            turbo_xml_node_location(root),
                            "root must be W3C SCXML scxml element");
        goto cleanup;
    }
    status = validate_element_attributes(&build, root, SCXML_ELEMENT_SCXML);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    version = find_attribute(root, "version");
    if (version.impl == NULL ||
        !view_equal_raw(turbo_xml_attribute_value(version), "1.0")) {
        status = scxml_fail(
            &build, CFLOW_SCXML_INVALID_VERSION,
            version.impl != NULL ? turbo_xml_attribute_location(version)
                                 : turbo_xml_node_location(root),
            "SCXML version must be 1.0");
        goto cleanup;
    }
    datamodel = find_attribute(root, "datamodel");
    if (data_model == SCXML_DATA_MODEL_NULL && datamodel.impl != NULL &&
        !view_equal_raw(turbo_xml_attribute_value(datamodel), "null")) {
        status = scxml_fail(&build, CFLOW_SCXML_UNSUPPORTED_DATAMODEL,
                            turbo_xml_attribute_location(datamodel),
                            "only the SCXML null data model is supported");
        goto cleanup;
    }
    if (data_model == SCXML_DATA_MODEL_CMETA &&
        (datamodel.impl == NULL ||
         !view_equal_raw(turbo_xml_attribute_value(datamodel), "cmeta"))) {
        status = scxml_fail(
            &build, CFLOW_SCXML_UNSUPPORTED_DATAMODEL,
            datamodel.impl != NULL ? turbo_xml_attribute_location(datamodel)
                                   : turbo_xml_node_location(root),
            "CMeta compilation requires datamodel='cmeta'");
        goto cleanup;
    }
    document_name_attribute = find_attribute(root, "name");
    if (document_name_attribute.impl != NULL) {
        document_name = turbo_xml_attribute_value(document_name_attribute);
        if (!is_xml_nmtoken(document_name)) {
            status = scxml_fail(
                &build, CFLOW_SCXML_INVALID_STRUCTURE,
                turbo_xml_attribute_location(document_name_attribute),
                "SCXML name must be one XML NMTOKEN");
            goto cleanup;
        }
    }
    status = analyze_state(&build, root, SCXML_ELEMENT_SCXML, true, &counts);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    needs_execution_error =
        counts.assignment_rows != 0u || counts.foreach_rows != 0u ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         counts.conditional_branches != 0u);
    if (((counts.requirements &
          (CFLOW_SCXML_REQUIREMENT_EVENT_IO |
           CFLOW_SCXML_REQUIREMENT_INVOKE)) != 0u ||
         needs_execution_error) &&
        !checked_add(counts.event_occurrences, 2u,
                     &counts.event_occurrences)) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "reserved SCXML error event count overflow");
        goto cleanup;
    }
    if (!checked_add(counts.state_action_rows,
                     counts.transition_action_rows,
                     &action_ref_count) ||
        counts.state_rows > limits.max_states ||
        counts.transition_rows > limits.max_transitions ||
        counts.state_rows > UINT32_MAX ||
        counts.transition_rows > UINT32_MAX ||
        counts.guard_rows > CFLOW_MACHINE_MAX_GUARDS ||
        counts.executable_blocks > CFLOW_MACHINE_MAX_ACTIONS ||
        counts.state_action_rows > CFLOW_STATECHART_MAX_ACTION_REFS ||
        counts.transition_action_rows > CFLOW_STATECHART_MAX_ACTION_REFS ||
        action_ref_count > CFLOW_STATECHART_MAX_ACTION_REFS) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "SCXML state or transition count exceeds limits");
        goto cleanup;
    }

    build.states = allocate_rows(counts.state_rows, sizeof(*build.states));
    build.transitions =
        allocate_rows(counts.transition_rows, sizeof(*build.transitions));
    build.guards = allocate_rows(counts.guard_rows, sizeof(*build.guards));
    build.events =
        allocate_rows(counts.event_occurrences, sizeof(*build.events));
    build.executables = allocate_rows(counts.executable_blocks,
                                      sizeof(*build.executables));
    build.state_actions = allocate_rows(counts.state_action_rows,
                                        sizeof(*build.state_actions));
    build.transition_actions = allocate_rows(
        counts.transition_action_rows, sizeof(*build.transition_actions));
    build.bindings = allocate_rows(counts.executable_blocks,
                                   sizeof(*build.bindings));
    build.guard_bindings = allocate_rows(
        counts.guard_rows, sizeof(*build.guard_bindings));
    build.guard_users = allocate_rows(
        counts.guard_rows, sizeof(*build.guard_users));
    build.blocks = allocate_rows(counts.block_rows,
                                 sizeof(*build.blocks));
    build.steps = allocate_rows(counts.executable_steps,
                                sizeof(*build.steps));
    build.branches = allocate_rows(
        counts.conditional_branches, sizeof(*build.branches));
    build.effects = allocate_rows(counts.effect_rows, sizeof(*build.effects));
    build.assignments = allocate_rows(
        counts.assignment_rows, sizeof(*build.assignments));
    build.foreach_descriptors = allocate_rows(
        counts.foreach_rows, sizeof(*build.foreach_descriptors));
    build.invocations = allocate_rows(
        counts.invocation_rows, sizeof(*build.invocations));
    build.invocation_names = allocate_rows(
        counts.invocation_rows, sizeof(*build.invocation_names));
    build.log_storage = counts.log_label_bytes != 0u
                            ? (char *)calloc(counts.log_label_bytes, 1u)
                            : NULL;
    build.effect_storage = counts.effect_string_bytes != 0u
                               ? (char *)calloc(counts.effect_string_bytes, 1u)
                               : NULL;
    build.invocation_storage = counts.invocation_string_bytes != 0u
                                   ? (char *)calloc(
                                         counts.invocation_string_bytes, 1u)
                                   : NULL;
    build.step_capacity = counts.executable_steps;
    build.branch_capacity = counts.conditional_branches;
    build.effect_capacity = counts.effect_rows;
    build.assignment_capacity = counts.assignment_rows;
    build.foreach_capacity = counts.foreach_rows;
    build.log_storage_capacity = counts.log_label_bytes;
    build.effect_storage_capacity = counts.effect_string_bytes;
    build.invocation_storage_capacity = counts.invocation_string_bytes;
    build.invocation_capacity = counts.invocation_rows;
    build.guard_capacity = counts.guard_rows;
    build.max_conditional_depth = counts.max_conditional_depth;
    build.requirements = counts.requirements;
    build.state_names =
        allocate_rows(counts.state_names, sizeof(*build.state_names));
    build.event_names =
        allocate_rows(counts.event_occurrences, sizeof(*build.event_names));
    build.event_occurrences = allocate_rows(
        counts.event_occurrences, sizeof(*build.event_occurrences));
    build.node_refs =
        allocate_rows(counts.node_refs, sizeof(*build.node_refs));
    build.synthetic_initials = allocate_rows(
        counts.synthetic_initials, sizeof(*build.synthetic_initials));
    if ((counts.state_rows != 0u && build.states == NULL) ||
        (counts.transition_rows != 0u && build.transitions == NULL) ||
        (counts.guard_rows != 0u &&
         (build.guards == NULL || build.guard_bindings == NULL ||
          build.guard_users == NULL)) ||
        (counts.event_occurrences != 0u &&
          (build.events == NULL || build.event_names == NULL ||
           build.event_occurrences == NULL)) ||
        (counts.executable_blocks != 0u &&
         (build.executables == NULL || build.bindings == NULL)) ||
        (counts.block_rows != 0u && build.blocks == NULL) ||
        (counts.executable_steps != 0u && build.steps == NULL) ||
        (counts.conditional_branches != 0u && build.branches == NULL) ||
        (counts.effect_rows != 0u && build.effects == NULL) ||
        (counts.assignment_rows != 0u && build.assignments == NULL) ||
        (counts.foreach_rows != 0u &&
         build.foreach_descriptors == NULL) ||
        (counts.invocation_rows != 0u &&
         (build.invocations == NULL || build.invocation_names == NULL)) ||
        (counts.log_label_bytes != 0u && build.log_storage == NULL) ||
        (counts.effect_string_bytes != 0u &&
         build.effect_storage == NULL) ||
        (counts.invocation_string_bytes != 0u &&
         build.invocation_storage == NULL) ||
        (counts.state_action_rows != 0u && build.state_actions == NULL) ||
        (counts.transition_action_rows != 0u &&
         build.transition_actions == NULL) ||
        (counts.state_names != 0u && build.state_names == NULL) ||
        (counts.node_refs != 0u && build.node_refs == NULL) ||
        (counts.synthetic_initials != 0u &&
         build.synthetic_initials == NULL)) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to allocate bounded SCXML declarations");
        goto cleanup;
    }
    status = emit_state(&build, root, 0u, true);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    qsort(build.state_names, build.state_name_index,
          sizeof(*build.state_names), compare_name_ref);
    {
        const scxml_name_ref *duplicate = find_earliest_duplicate(
            build.state_names, build.state_name_index);
        if (duplicate != NULL) {
            status = scxml_fail(&build, CFLOW_SCXML_DUPLICATE_ID,
                                duplicate->location,
                                "duplicate SCXML state id");
            goto cleanup;
        }
    }
    qsort(build.node_refs, build.node_ref_index, sizeof(*build.node_refs),
          compare_node_ref);
    status = emit_invocation_declarations(
        &build, root, build.node_ref_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    qsort(build.invocation_names, build.invocation_index,
          sizeof(*build.invocation_names), compare_name_ref);
    {
        const scxml_name_ref *duplicate = find_earliest_duplicate(
            build.invocation_names, build.invocation_index);
        if (duplicate != NULL) {
            status = scxml_fail(&build, CFLOW_SCXML_DUPLICATE_ID,
                                duplicate->location,
                                "duplicate SCXML invoke id");
            goto cleanup;
        }
    }
    status = collect_transition_events(&build, root);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    if ((build.requirements &
         (CFLOW_SCXML_REQUIREMENT_EVENT_IO |
          CFLOW_SCXML_REQUIREMENT_INVOKE)) != 0u ||
        needs_execution_error)
        collect_reserved_error_events(&build, turbo_xml_node_location(root));
    status = build_event_names(&build, build.event_occurrence_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    if (needs_execution_error) {
        const turbo_xml_string_view execution_name = {
            SCXML_ERROR_EXECUTION_EVENT,
            sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u};
        const scxml_name_ref *execution = find_name_ref(
            build.event_names, build.event_name_count, execution_name);
        if (execution == NULL) {
            status = scxml_fail(&build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                turbo_xml_node_location(root),
                                "reserved execution error event was not retained");
            goto cleanup;
        }
        build.execution_error_event = (cflow_event_id)execution->id;
    }
    status = resolve_invocation_events(&build);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_state_executables(&build, root, build.node_ref_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_transitions(&build, root, build.node_ref_index,
                              build.synthetic_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    if (build.effect_index != counts.effect_rows ||
        build.assignment_index != counts.assignment_rows ||
        build.foreach_index != counts.foreach_rows ||
        build.effect_storage_index != counts.effect_string_bytes ||
        build.invocation_index != counts.invocation_rows ||
        build.invocation_emit_index != counts.invocation_rows ||
        build.block_index != counts.block_rows ||
        build.invocation_storage_index !=
            counts.invocation_string_bytes) {
        status = scxml_fail(&build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                            turbo_xml_node_location(root),
                            "effect descriptor emission mismatched admission");
        goto cleanup;
    }
    if (!checked_add(name_bytes, document_name.size, &name_bytes)) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "retained SCXML name size overflow");
        goto cleanup;
    }
    for (index = 0u; index < build.state_name_index; ++index) {
        if (!checked_add(name_bytes, build.state_names[index].name.size,
                         &name_bytes)) {
            status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                turbo_xml_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    for (index = 0u; index < build.event_name_count; ++index) {
        if (!checked_add(name_bytes, build.event_names[index].name.size,
                         &name_bytes)) {
            status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                turbo_xml_node_location(root),
                                "retained SCXML name size overflow");
            goto cleanup;
        }
    }
    if (!checked_add(name_bytes, counts.log_label_bytes,
                     &retained_string_bytes) ||
        !checked_add(retained_string_bytes, counts.effect_string_bytes,
                     &retained_string_bytes) ||
        !checked_add(retained_string_bytes,
                     counts.invocation_string_bytes,
                     &retained_string_bytes)) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "retained SCXML string size overflow");
        goto cleanup;
    }
    if (retained_string_bytes > limits.max_name_bytes) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "retained SCXML strings exceed max_name_bytes");
        goto cleanup;
    }

    memset(&definition, 0, sizeof(definition));
    definition.state_type = data_model == SCXML_DATA_MODEL_CMETA
                                ? cmeta_root->storage_type
                                : &cmeta_type_bool;
    definition.states = build.states;
    definition.state_count = build.state_index;
    definition.events = build.events;
    definition.event_count = build.event_name_count;
    definition.guards = build.guards;
    definition.guard_count = build.guard_index;
    definition.executables = build.executables;
    definition.executable_count = build.executable_index;
    definition.transitions = build.transitions;
    definition.transition_count = build.transition_index;
    definition.state_actions = build.state_actions;
    definition.state_action_count = build.state_action_index;
    definition.transition_actions = build.transition_actions;
    definition.transition_action_count = build.transition_action_index;
    impl = (cflow_scxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to allocate SCXML program");
        goto cleanup;
    }
    native_status = cflow_statechart_build(&impl->statechart, &definition);
    if (native_status != CFLOW_STATECHART_OK) {
        char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
        (void)snprintf(message, sizeof(message),
                       "native Statechart rejected SCXML lowering (status=%d)",
                       (int)native_status);
        status = scxml_fail(&build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                            turbo_xml_node_location(root), message);
        goto cleanup;
    }
    impl->state_names = allocate_rows(build.state_name_index,
                                      sizeof(*impl->state_names));
    impl->event_names = allocate_rows(build.event_name_count,
                                      sizeof(*impl->event_names));
    impl->event_names_by_id = allocate_rows(
        build.event_name_count, sizeof(*impl->event_names_by_id));
    impl->name_storage = name_bytes != 0u ? (char *)malloc(name_bytes) : NULL;
    if ((build.state_name_index != 0u && impl->state_names == NULL) ||
        (build.event_name_count != 0u && impl->event_names == NULL) ||
        (build.event_name_count != 0u &&
         impl->event_names_by_id == NULL) ||
        (name_bytes != 0u && impl->name_storage == NULL)) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to retain SCXML name mappings");
        goto cleanup;
    }
    name_cursor = impl->name_storage;
    copy_program_names(impl->state_names, build.state_names,
                       build.state_name_index, &name_cursor);
    copy_program_names(impl->event_names, build.event_names,
                       build.event_name_count, &name_cursor);
    impl->document_name_size = document_name.size;
    if (document_name.size != 0u) {
        impl->document_name = name_cursor;
        memcpy(name_cursor, document_name.data, document_name.size);
        name_cursor += document_name.size;
    } else {
        impl->document_name = "";
    }
    impl->state_name_count = build.state_name_index;
    impl->event_name_count = build.event_name_count;
    impl->data_model = data_model;
    impl->cmeta_root = cmeta_root;
    impl->requirements = build.requirements;
    if ((build.requirements &
         (CFLOW_SCXML_REQUIREMENT_EVENT_IO |
          CFLOW_SCXML_REQUIREMENT_INVOKE)) != 0u ||
        needs_execution_error) {
        const turbo_xml_string_view execution_name = {
            SCXML_ERROR_EXECUTION_EVENT,
            sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u};
        const turbo_xml_string_view communication_name = {
            SCXML_ERROR_COMMUNICATION_EVENT,
            sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u};
        const scxml_name_ref *execution = find_name_ref(
            build.event_names, build.event_name_count, execution_name);
        const scxml_name_ref *communication = find_name_ref(
            build.event_names, build.event_name_count, communication_name);
        if (execution == NULL || communication == NULL) {
            status = scxml_fail(&build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                turbo_xml_node_location(root),
                                "reserved SCXML error events were not retained");
            goto cleanup;
        }
        impl->execution_error_event = (cflow_event_id)execution->id;
        impl->communication_error_event = (cflow_event_id)communication->id;
    }
    impl->bindings = build.bindings;
    impl->binding_count = build.executable_index;
    impl->guard_bindings = build.guard_bindings;
    impl->guard_users = build.guard_users;
    impl->guard_binding_count = build.guard_index;
    impl->blocks = build.blocks;
    impl->steps = build.steps;
    impl->branches = build.branches;
    impl->branch_count = build.branch_index;
    impl->effects = build.effects;
    impl->assignments = build.assignments;
    impl->assignment_count = build.assignment_index;
    impl->foreach_descriptors = build.foreach_descriptors;
    impl->foreach_count = build.foreach_index;
    impl->invocations = build.invocations;
    impl->invocation_count = build.invocation_index;
    impl->log_storage = build.log_storage;
    impl->effect_storage = build.effect_storage;
    impl->invocation_storage = build.invocation_storage;
    for (index = 0u; index < impl->guard_binding_count; ++index) {
        impl->guard_users[index].event_names_by_id =
            impl->event_names_by_id;
        impl->guard_users[index].event_name_count = impl->event_name_count;
        impl->guard_users[index].system_values.name =
            (cflow_scxml_cmeta_expr_string_view){
                impl->document_name, impl->document_name_size};
    }
    for (index = 0u; index < impl->binding_count; ++index) {
        scxml_block *block =
            (scxml_block *)impl->bindings[index].user;
        if (block != NULL) {
            block->event_names_by_id = impl->event_names_by_id;
            block->event_name_count = impl->event_name_count;
            block->system_values.name =
                (cflow_scxml_cmeta_expr_string_view){
                    impl->document_name, impl->document_name_size};
        }
    }
    build.bindings = NULL;
    build.guard_bindings = NULL;
    build.guard_users = NULL;
    build.blocks = NULL;
    build.steps = NULL;
    build.branches = NULL;
    build.effects = NULL;
    build.assignments = NULL;
    build.foreach_descriptors = NULL;
    build.invocations = NULL;
    build.log_storage = NULL;
    build.effect_storage = NULL;
    build.invocation_storage = NULL;
    impl->null_value = false;
    qsort(impl->state_names, impl->state_name_count,
          sizeof(*impl->state_names), compare_program_name);
    qsort(impl->event_names, impl->event_name_count,
          sizeof(*impl->event_names), compare_program_name);
    for (index = 0u; index < impl->event_name_count; ++index) {
        const uint64_t id = impl->event_names[index].id;
        if (id == 0u || id > impl->event_name_count ||
            impl->event_names_by_id[id - 1u] != NULL) {
            status = scxml_fail(
                &build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                turbo_xml_node_location(root),
                "SCXML event ID map invariant failed");
            goto cleanup;
        }
        impl->event_names_by_id[id - 1u] = &impl->event_names[index];
    }
    out->impl = impl;
    impl = NULL;
    status = CFLOW_SCXML_OK;

cleanup:
    if (impl != NULL) {
        cflow_statechart_destroy(&impl->statechart);
        free(impl->state_names);
        free(impl->event_names);
        free(impl->event_names_by_id);
        free(impl->bindings);
        free(impl->guard_bindings);
        destroy_guard_users(impl->guard_users, impl->guard_binding_count);
        free(impl->guard_users);
        free(impl->blocks);
        free(impl->steps);
        destroy_branches(impl->branches, impl->branch_count);
        free(impl->branches);
        free(impl->effects);
        destroy_assignments(impl->assignments, impl->assignment_count);
        free(impl->assignments);
        free(impl->foreach_descriptors);
        free(impl->invocations);
        free(impl->name_storage);
        free(impl->log_storage);
        free(impl->effect_storage);
        free(impl->invocation_storage);
        free(impl);
    }
    free_build(&build);
    turbo_xml_document_destroy(&document);
    return status;
}

static cflow_scxml_cmeta_expr_limits cmeta_expression_limits_from_options(
    const cflow_scxml_cmeta_compile_options_v1 *options) {
    const cflow_scxml_cmeta_expr_limits limits = {
        options->max_source_bytes,
        options->max_instructions,
        options->max_operands,
        options->max_expression_depth,
        options->max_path_depth,
        options->max_literal_bytes,
        options->max_string_bytes
    };
    return limits;
}

static bool cmeta_state_type_supported(const cmeta_type_desc *type) {
    return cmeta_type_require_traits(
               type,
               CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
               CMETA_OK ||
           cmeta_type_require_traits(
               type,
               CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                   CMETA_TRAIT_DESTROY) == CMETA_OK;
}

cflow_scxml_status cflow_scxml_compile(
    cflow_scxml_program *out, const char *input, size_t input_size,
    const cflow_scxml_limits *limits,
    cflow_scxml_diagnostic *diagnostic) {
    return compile_scxml_model(
        out, input, input_size, limits, SCXML_DATA_MODEL_NULL, NULL, NULL,
        0u, diagnostic);
}

cflow_scxml_status cflow_scxml_compile_cmeta(
    cflow_scxml_program *out, const char *input, size_t input_size,
    const cflow_scxml_limits *limits,
    const cflow_scxml_cmeta_compile_options_v1 *options,
    cflow_scxml_diagnostic *diagnostic) {
    const size_t legacy_size =
        offsetof(cflow_scxml_cmeta_compile_options_v1, max_iterations);
    bool has_current_tail;
    size_t max_iterations;
    cflow_scxml_cmeta_expr_limits expression_limits;
    has_current_tail = options != NULL &&
        options->struct_size >= sizeof(*options);
    if (options == NULL ||
        options->abi_version !=
            CFLOW_SCXML_CMETA_COMPILE_OPTIONS_ABI_V1 ||
        (options->struct_size != legacy_size && !has_current_tail) ||
        !cmeta_data_desc_valid(options->root) ||
        options->root->kind != CMETA_DATA_STRUCT ||
        options->root->storage_type == NULL ||
        !cmeta_type_desc_valid(options->root->storage_type) ||
        !cmeta_state_type_supported(options->root->storage_type)) {
        if (diagnostic != NULL) {
            memset(diagnostic, 0, sizeof(*diagnostic));
            diagnostic->status = CFLOW_SCXML_INVALID_ARGUMENT;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", "invalid CMeta compile provider");
        }
        return CFLOW_SCXML_INVALID_ARGUMENT;
    }
    max_iterations = has_current_tail
        ? options->max_iterations
        : CFLOW_SCXML_CMETA_DEFAULT_MAX_ITERATIONS;
    expression_limits = cmeta_expression_limits_from_options(options);
    if (!cflow_scxml_cmeta_expr_limits_valid(&expression_limits) ||
        max_iterations == 0u) {
        if (diagnostic != NULL) {
            memset(diagnostic, 0, sizeof(*diagnostic));
            diagnostic->status = CFLOW_SCXML_INVALID_ARGUMENT;
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", "invalid CMeta expression limits");
        }
        return CFLOW_SCXML_INVALID_ARGUMENT;
    }
    return compile_scxml_model(
        out, input, input_size, limits, SCXML_DATA_MODEL_CMETA,
        options->root, &expression_limits, max_iterations, diagnostic);
}

void cflow_scxml_program_destroy(cflow_scxml_program *program) {
    cflow_scxml_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (cflow_scxml_program_impl *)program->impl;
    cflow_statechart_destroy(&impl->statechart);
    free(impl->state_names);
    free(impl->event_names);
    free(impl->event_names_by_id);
    free(impl->bindings);
    free(impl->guard_bindings);
    destroy_guard_users(impl->guard_users, impl->guard_binding_count);
    free(impl->guard_users);
    free(impl->blocks);
    free(impl->steps);
    destroy_branches(impl->branches, impl->branch_count);
    free(impl->branches);
    free(impl->effects);
    destroy_assignments(impl->assignments, impl->assignment_count);
    free(impl->assignments);
    free(impl->foreach_descriptors);
    free(impl->invocations);
    free(impl->name_storage);
    free(impl->log_storage);
    free(impl->effect_storage);
    free(impl->invocation_storage);
    free(impl);
    program->impl = NULL;
}

const cflow_statechart *cflow_scxml_program_statechart(
    const cflow_scxml_program *program) {
    const cflow_scxml_program_impl *impl =
        program != NULL ? (const cflow_scxml_program_impl *)program->impl : NULL;
    return impl != NULL ? &impl->statechart : NULL;
}

static const scxml_program_name *find_program_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size) {
    size_t low = 0u;
    size_t high = count;
    const turbo_xml_string_view wanted = {name, name_size};
    while (low < high) {
        const size_t middle = low + (high - low) / 2u;
        const turbo_xml_string_view value = {
            names[middle].name, names[middle].size};
        if (compare_view(value, wanted) < 0) low = middle + 1u;
        else high = middle;
    }
    return low < count && names[low].size == name_size &&
                   memcmp(names[low].name, name, name_size) == 0
               ? &names[low]
               : NULL;
}

bool cflow_scxml_program_state_id(const cflow_scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_machine_state_id *out_id) {
    const cflow_scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const cflow_scxml_program_impl *)program->impl;
    found = find_program_name(impl->state_names, impl->state_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_machine_state_id)found->id;
    return true;
}

bool cflow_scxml_program_event_id(const cflow_scxml_program *program,
                                  const char *name, size_t name_size,
                                  cflow_event_id *out_id) {
    const cflow_scxml_program_impl *impl;
    const scxml_program_name *found;
    if (program == NULL || program->impl == NULL || name == NULL ||
        name_size == 0u || out_id == NULL) return false;
    impl = (const cflow_scxml_program_impl *)program->impl;
    found = find_program_name(impl->event_names, impl->event_name_count,
                              name, name_size);
    if (found == NULL) return false;
    *out_id = (cflow_event_id)found->id;
    return true;
}

const void *cflow_scxml_program_initial_state(
    const cflow_scxml_program *program) {
    const cflow_scxml_program_impl *impl =
        program != NULL ? (const cflow_scxml_program_impl *)program->impl : NULL;
    return impl != NULL && impl->data_model == SCXML_DATA_MODEL_NULL
               ? &impl->null_value
               : NULL;
}

bool cflow_scxml_program_event(const cflow_scxml_program *program,
                               const char *name, size_t name_size,
                               cflow_event_view *out_event) {
    const cflow_scxml_program_impl *impl;
    cflow_event_id id;
    if (out_event == NULL ||
        !cflow_scxml_program_event_id(program, name, name_size, &id)) {
        return false;
    }
    impl = (const cflow_scxml_program_impl *)program->impl;
    *out_event = (cflow_event_view){id, &cmeta_type_bool, &impl->null_value};
    return true;
}

bool cflow_scxml_program_requirements(
    const cflow_scxml_program *program, uint32_t *out_requirements) {
    const cflow_scxml_program_impl *impl = program != NULL
        ? (const cflow_scxml_program_impl *)program->impl : NULL;
    if (impl == NULL || out_requirements == NULL) return false;
    *out_requirements = impl->requirements;
    return true;
}

bool cflow_scxml_program_runtime_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_executable_binding **out_bindings,
    size_t *out_count) {
    if (program == NULL || program->impl == NULL || out_bindings == NULL ||
        out_count == NULL) {
        return false;
    }
    {
        const cflow_scxml_program_impl *impl =
            (const cflow_scxml_program_impl *)program->impl;
        if (impl->requirements != CFLOW_SCXML_REQUIREMENT_NONE)
            return false;
        *out_bindings = impl->bindings;
        *out_count = impl->binding_count;
    }
    return true;
}


static bool event_io_adapter_valid(
    const cflow_scxml_event_io_adapter_v1 *adapter) {
    const uint64_t known = CFLOW_SCXML_EVENT_IO_CAP_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_CANCEL;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V1 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND) != 0u &&
        (adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) == 0u)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_CANCEL) != 0u &&
        ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u ||
         adapter->prepare_cancel == NULL))
        return false;
    return true;
}

static bool invoke_adapter_valid(
    const cflow_scxml_invoke_adapter_v1 *adapter) {
    const uint64_t known = CFLOW_SCXML_INVOKE_CAP_START |
        CFLOW_SCXML_INVOKE_CAP_CANCEL | CFLOW_SCXML_INVOKE_CAP_FORWARD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static void session_close_adapter(cflow_scxml_session_impl *impl) {
    if (impl != NULL && impl->has_event_io &&
        !atomic_exchange_explicit(
            &impl->adapter_close_called, true, memory_order_acq_rel))
        impl->event_io.close(impl->adapter_user);
    if (impl != NULL && impl->has_invoke &&
        !atomic_exchange_explicit(
            &impl->invoke_close_called, true, memory_order_acq_rel))
        impl->invoke.close(impl->invoke_user);
}

static void session_free_storage(cflow_scxml_session_impl *impl) {
    if (impl == NULL) return;
    free(impl->prepared_effects);
    free(impl->invocation_effects);
    free(impl->invocation_rows);
    free(impl->delayed_sends);
    free(impl->guard_users);
    free(impl->guard_bindings);
    free(impl->binding_users);
    free(impl->bindings);
    free(impl->system_name);
}

static cflow_statechart_runtime_status cflow_scxml_session_init_model(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    scxml_data_model data_model, const void *cmeta_initial_state) {
    cflow_scxml_session_impl *impl;
    const cflow_scxml_program_impl *program;
    cflow_statechart_instance_config native_config;
    cflow_statechart_runtime_hooks runtime_hooks = {0};
    cflow_statechart_runtime_status status;
    turbo_uuid_t session_uuid;
    size_t invocation_effect_capacity = 0u;
    size_t index;
    bool requires_forward = false;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !event_io_adapter_valid(config->event_io) ||
        !invoke_adapter_valid(config->invoke))
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    program = (const cflow_scxml_program_impl *)config->program->impl;
    if (program->data_model != data_model ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         cmeta_initial_state == NULL)) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_EVENT_IO) != 0u) {
        if (config->event_io == NULL || config->effect_capacity == 0u ||
            config->adapter_internal_event_capacity == 0u ||
            (config->event_io->capabilities &
             CFLOW_SCXML_EVENT_IO_CAP_SEND) == 0u) {
            return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
        }
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_DELAYED_SEND) != 0u &&
        (config->delayed_send_capacity == 0u ||
         (config->event_io->capabilities &
          CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u)) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_CANCEL) != 0u &&
        (config->event_io->capabilities &
         CFLOW_SCXML_EVENT_IO_CAP_CANCEL) == 0u) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_INVOKE) != 0u &&
        (config->invoke == NULL || config->effect_capacity == 0u ||
         config->adapter_internal_event_capacity == 0u ||
         config->invocation_capacity < program->invocation_count ||
         (config->invoke->capabilities &
          (CFLOW_SCXML_INVOKE_CAP_START | CFLOW_SCXML_INVOKE_CAP_CANCEL)) !=
             (CFLOW_SCXML_INVOKE_CAP_START |
              CFLOW_SCXML_INVOKE_CAP_CANCEL))) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    for (index = 0u; index < program->invocation_count; ++index) {
        if (program->invocations[index].autoforward) {
            requires_forward = true;
            break;
        }
    }
    if (requires_forward &&
        (config->invoke->capabilities & CFLOW_SCXML_INVOKE_CAP_FORWARD) == 0u)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    if (config->invoke != NULL &&
        !checked_add(config->effect_capacity, 1u,
                     &invocation_effect_capacity))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    impl = (cflow_scxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    impl->program = program;
    if (data_model == SCXML_DATA_MODEL_CMETA) {
        if (turbo_uuid_v4_generate(&session_uuid) != TURBO_OK ||
            turbo_uuid_format(
                &session_uuid, impl->session_id,
                sizeof(impl->session_id)) != TURBO_OK) {
            free(impl);
            return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
        }
        if (program->document_name_size != 0u) {
            impl->system_name =
                (char *)malloc(program->document_name_size);
            if (impl->system_name == NULL) {
                free(impl);
                return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
            }
            memcpy(impl->system_name, program->document_name,
                   program->document_name_size);
        }
        impl->system_values.name =
            (cflow_scxml_cmeta_expr_string_view){
                program->document_name_size != 0u ? impl->system_name : "",
                program->document_name_size};
        impl->system_values.session_id =
            (cflow_scxml_cmeta_expr_string_view){
                impl->session_id, TURBO_UUID_STRING_LENGTH};
        impl->guard_binding_count = program->guard_binding_count;
        impl->guard_bindings =
            (cflow_statechart_guard_binding *)allocate_rows(
                impl->guard_binding_count, sizeof(*impl->guard_bindings));
        impl->guard_users = (scxml_session_guard_user *)allocate_rows(
            impl->guard_binding_count, sizeof(*impl->guard_users));
    }
    impl->binding_count = program->binding_count;
    impl->bindings = (cflow_statechart_executable_binding *)allocate_rows(
        impl->binding_count, sizeof(*impl->bindings));
    impl->binding_users = (scxml_session_binding_user *)allocate_rows(
        impl->binding_count, sizeof(*impl->binding_users));
    impl->delayed_send_capacity = config->delayed_send_capacity;
    impl->delayed_sends = (scxml_delayed_send *)allocate_rows(
        impl->delayed_send_capacity, sizeof(*impl->delayed_sends));
    impl->prepared_effect_capacity = config->effect_capacity;
    impl->prepared_effects = (scxml_prepared_effect *)allocate_rows(
        impl->prepared_effect_capacity, sizeof(*impl->prepared_effects));
    impl->invocation_capacity = config->invocation_capacity;
    impl->invocation_rows = (scxml_invocation_row *)allocate_rows(
        impl->invocation_capacity, sizeof(*impl->invocation_rows));
    /* One bounded probe row lets the native effect journal remain the sole
       authority for EFFECT_JOURNAL_FULL. A rejected ticket releases it
       immediately and it is never retained beyond the failed stage call. */
    impl->invocation_effect_capacity = invocation_effect_capacity;
    impl->invocation_effects =
        (scxml_invocation_lifecycle_effect *)allocate_rows(
            impl->invocation_effect_capacity,
            sizeof(*impl->invocation_effects));
    if ((impl->binding_count != 0u &&
         (impl->bindings == NULL || impl->binding_users == NULL)) ||
        (impl->guard_binding_count != 0u &&
         (impl->guard_bindings == NULL || impl->guard_users == NULL))) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    }
    if ((impl->delayed_send_capacity != 0u &&
         impl->delayed_sends == NULL) ||
        (impl->prepared_effect_capacity != 0u &&
         impl->prepared_effects == NULL) ||
        (impl->invocation_capacity != 0u &&
         impl->invocation_rows == NULL) ||
        (impl->invocation_effect_capacity != 0u &&
         impl->invocation_effects == NULL)) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    }
    turbo_mutex_init(&impl->registry_lock);
    if (impl->registry_lock == NULL) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    }
    for (index = 0u; index < impl->binding_count; ++index) {
        impl->binding_users[index] = (scxml_session_binding_user){
            (const scxml_block *)program->bindings[index].user, impl};
        impl->bindings[index] = (cflow_statechart_executable_binding){
            .id = program->bindings[index].id,
            .user = &impl->binding_users[index],
            .contextual_fn = execute_scxml_session_block};
    }
    for (index = 0u; index < impl->guard_binding_count; ++index) {
        impl->guard_users[index] = (scxml_session_guard_user){
            &program->guard_users[index], impl};
        impl->guard_bindings[index] = (cflow_statechart_guard_binding){
            .id = program->guard_bindings[index].id,
            .user = &impl->guard_users[index],
            .contextual_fn = evaluate_scxml_session_transition_guard};
    }
    if (config->event_io != NULL) {
        impl->event_io = *config->event_io;
        impl->adapter_user = config->adapter_user;
        impl->has_event_io = true;
    }
    if (config->invoke != NULL) {
        impl->invoke = *config->invoke;
        impl->invoke_user = config->invoke_user;
        impl->has_invoke = true;
    }
    impl->next_invocation_token = UINT64_C(1);
    atomic_init(&impl->adapter_close_called, false);
    atomic_init(&impl->invoke_close_called, false);
    if (impl->has_invoke) {
        runtime_hooks = (cflow_statechart_runtime_hooks){
            .abi_version = CFLOW_STATECHART_RUNTIME_HOOKS_ABI_V1,
            .struct_size = sizeof(runtime_hooks),
            .on_stable = start_stable_invocations,
            .preprocess_external = preprocess_invocation_external};
    }
    native_config = (cflow_statechart_instance_config){
        .statechart = &program->statechart,
        .initial_state = data_model == SCXML_DATA_MODEL_CMETA
                             ? cmeta_initial_state
                             : &program->null_value,
        .guards = data_model == SCXML_DATA_MODEL_CMETA
                      ? impl->guard_bindings : program->guard_bindings,
        .guard_count = data_model == SCXML_DATA_MODEL_CMETA
                           ? impl->guard_binding_count
                           : program->guard_binding_count,
        .executables = impl->bindings,
        .executable_count = impl->binding_count,
        .external_event_capacity = config->external_event_capacity,
        .internal_event_capacity = config->internal_event_capacity,
        .completion_capacity = config->completion_capacity,
        .microstep_limit = config->microstep_limit,
        .max_storage_bytes = config->max_storage_bytes,
        .executor = config->executor,
        .clock = config->clock,
        .timer_capacity = config->timer_capacity,
        .effect_capacity = config->effect_capacity,
        .adapter_internal_event_capacity =
            config->adapter_internal_event_capacity,
        .runtime_hooks = impl->has_invoke ? &runtime_hooks : NULL,
        .runtime_hook_user = impl};
    status = cflow_statechart_instance_init(&impl->instance, &native_config);
    if (status != CFLOW_STATECHART_RUNTIME_OK) {
        session_close_adapter(impl);
        turbo_mutex_destroy(&impl->registry_lock);
        session_free_storage(impl);
        free(impl);
        return status;
    }
    session->impl = impl;
    return CFLOW_STATECHART_RUNTIME_OK;
}

cflow_statechart_runtime_status cflow_scxml_session_init(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config) {
    return cflow_scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_NULL, NULL);
}

cflow_statechart_runtime_status cflow_scxml_session_init_cmeta(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_cmeta_session_options_v1 *options) {
    if (options == NULL ||
        options->abi_version !=
            CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    return cflow_scxml_session_init_model(
        session, config, SCXML_DATA_MODEL_CMETA, options->initial_state);
}

cflow_mailbox_status cflow_scxml_session_try_send(
    cflow_scxml_session *session, const cflow_event_view *event) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_try_send(&impl->instance, event)
        : CFLOW_MAILBOX_INVALID_ARGUMENT;
}

cflow_mailbox_status cflow_scxml_session_report_invoke_event(
    cflow_scxml_session *session, uint64_t token,
    const cflow_event_view *event) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    cflow_mailbox_status status;
    bool live = false;
    size_t index;
    if (impl == NULL || !impl->has_invoke || token == 0u || event == NULL)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->registry_lock);
    for (index = 0u; index < impl->program->invocation_count; ++index) {
        if (impl->invocation_rows[index].state == SCXML_INVOCATION_ACTIVE &&
            impl->invocation_rows[index].token == token) {
            live = true;
            break;
        }
    }
    if (!live) {
        increment_u64(&impl->invoke_stats.returned_rejected);
        turbo_mutex_unlock(&impl->registry_lock);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    turbo_mutex_unlock(&impl->registry_lock);
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    turbo_mutex_lock(&impl->registry_lock);
    if (status == CFLOW_MAILBOX_OK)
        increment_u64(&impl->invoke_stats.returned_accepted);
    else
        increment_u64(&impl->invoke_stats.returned_rejected);
    turbo_mutex_unlock(&impl->registry_lock);
    return status;
}

cflow_mailbox_status cflow_scxml_session_report_adapter_error(
    cflow_scxml_session *session,
    cflow_scxml_adapter_error_kind kind) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    const scxml_program_name *event;
    const char *name;
    size_t name_size;
    cflow_event_view reported;
    if (impl == NULL) return CFLOW_MAILBOX_INVALID_ARGUMENT;
    if (kind == CFLOW_SCXML_ADAPTER_ERROR_KIND_EXECUTION) {
        name = SCXML_ERROR_EXECUTION_EVENT;
        name_size = sizeof(SCXML_ERROR_EXECUTION_EVENT) - 1u;
    } else if (kind == CFLOW_SCXML_ADAPTER_ERROR_KIND_COMMUNICATION) {
        name = SCXML_ERROR_COMMUNICATION_EVENT;
        name_size = sizeof(SCXML_ERROR_COMMUNICATION_EVENT) - 1u;
    } else {
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    event = find_program_name(
        impl->program->event_names, impl->program->event_name_count,
        name, name_size);
    if (event == NULL) return CFLOW_MAILBOX_INVALID_ARGUMENT;
    reported = (cflow_event_view){
        (cflow_event_id)event->id, &cmeta_type_bool,
        &impl->program->null_value};
    return cflow_statechart_instance_try_send_internal(
        &impl->instance, &reported);
}

bool cflow_scxml_session_report_send_done(
    cflow_scxml_session *session, const char *send_id, size_t send_id_size) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    scxml_delayed_send *row;
    if (impl == NULL || send_id == NULL || send_id_size == 0u)
        return false;
    turbo_mutex_lock(&impl->registry_lock);
    row = find_delayed_send_locked(impl, send_id, send_id_size, NULL);
    if (row == NULL || row->state != SCXML_DELAYED_ACTIVE) {
        turbo_mutex_unlock(&impl->registry_lock);
        return false;
    }
    *row = (scxml_delayed_send){0};
    turbo_mutex_unlock(&impl->registry_lock);
    return true;
}

void cflow_scxml_session_close(cflow_scxml_session *session) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    if (impl == NULL) return;
    cflow_statechart_instance_close(&impl->instance);
    session_close_adapter(impl);
}

void cflow_scxml_session_cancel(cflow_scxml_session *session) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    if (impl == NULL) return;
    cflow_statechart_instance_cancel(&impl->instance);
    session_close_adapter(impl);
}

bool cflow_scxml_session_get_stats(
    const cflow_scxml_session *session,
    cflow_statechart_instance_stats *out) {
    const cflow_scxml_session_impl *impl = session != NULL
        ? (const cflow_scxml_session_impl *)session->impl : NULL;
    return impl != NULL &&
        cflow_statechart_instance_get_stats(&impl->instance, out);
}

bool cflow_scxml_session_get_invoke_stats(
    const cflow_scxml_session *session, cflow_scxml_invoke_stats *out) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    if (impl == NULL || out == NULL) return false;
    turbo_mutex_lock(&impl->registry_lock);
    *out = impl->invoke_stats;
    turbo_mutex_unlock(&impl->registry_lock);
    return true;
}

const char *cflow_scxml_session_error(
    const cflow_scxml_session *session) {
    const cflow_scxml_session_impl *impl = session != NULL
        ? (const cflow_scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_error(&impl->instance) : NULL;
}

cflow_statechart_runtime_status cflow_scxml_session_destroy(
    cflow_scxml_session *session) {
    cflow_scxml_session_impl *impl;
    cflow_statechart_runtime_status status;
    if (session == NULL) return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    impl = (cflow_scxml_session_impl *)session->impl;
    if (impl == NULL) return CFLOW_STATECHART_RUNTIME_OK;
    cflow_statechart_instance_close(&impl->instance);
    session_close_adapter(impl);
    if (impl->has_event_io &&
        !impl->event_io.is_quiescent(impl->adapter_user))
        return CFLOW_STATECHART_RUNTIME_WOULD_BLOCK;
    if (impl->has_invoke &&
        !impl->invoke.is_quiescent(impl->invoke_user))
        return CFLOW_STATECHART_RUNTIME_WOULD_BLOCK;
    status = cflow_statechart_instance_destroy(&impl->instance);
    if (status != CFLOW_STATECHART_RUNTIME_OK) return status;
    turbo_mutex_destroy(&impl->registry_lock);
    session_free_storage(impl);
    free(impl);
    session->impl = NULL;
    return CFLOW_STATECHART_RUNTIME_OK;
}

bool cflow_scxml_program_guard_bindings(
    const cflow_scxml_program *program,
    const cflow_statechart_guard_binding **out_bindings,
    size_t *out_count) {
    if (program == NULL || program->impl == NULL || out_bindings == NULL ||
        out_count == NULL) {
        return false;
    }
    {
        const cflow_scxml_program_impl *impl =
            (const cflow_scxml_program_impl *)program->impl;
        *out_bindings = impl->guard_bindings;
        *out_count = impl->guard_binding_count;
    }
    return true;
}
