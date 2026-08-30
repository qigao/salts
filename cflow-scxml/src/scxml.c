#include <cflow/scxml.h>
#include <tlog.h>
#include <turbo/thread.h>
#include <turbo_uuid.h>

#include "cmeta_expr.h"
#include "cmeta_assign.h"
#include "cmeta_foreach.h"
#include "cmeta_location.h"

#include <ctype.h>
#include <inttypes.h>
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
static const uint64_t SCXML_EXTERNAL_METADATA_TOKEN_BIT =
    UINT64_C(1) << 63u;

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
    SCXML_STEP_INVOKE_EXIT,
    SCXML_STEP_LATE_INITIALIZE
} scxml_step_kind;

typedef enum scxml_effect_kind {
    SCXML_EFFECT_SEND = 1,
    SCXML_EFFECT_CANCEL
} scxml_effect_kind;

typedef struct scxml_payload_descriptor {
    const char *name;
    size_t name_size;
    cflow_scxml_cmeta_expr_program expression;
} scxml_payload_descriptor;

typedef struct scxml_content_descriptor {
    cflow_scxml_content_kind kind;
    const char *bytes;
    size_t byte_count;
    cflow_scxml_cmeta_location location;
} scxml_content_descriptor;

typedef struct scxml_effect_descriptor {
    scxml_effect_kind kind;
    cflow_event_id event_id;
    bool internal_target;
    bool has_event_expr;
    bool has_target_expr;
    bool has_type_expr;
    bool has_delay_expr;
    bool has_send_id_expr;
    bool has_id_location;
    size_t payload_first;
    size_t payload_count;
    cflow_scxml_cmeta_expr_program event_expr;
    cflow_scxml_cmeta_expr_program target_expr;
    cflow_scxml_cmeta_expr_program type_expr;
    cflow_scxml_cmeta_expr_program delay_expr;
    cflow_scxml_cmeta_expr_program send_id_expr;
    cflow_scxml_cmeta_expr_program data_expr;
    cflow_scxml_cmeta_location id_location;
    scxml_content_descriptor content;
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
    size_t assignment_count;
    size_t late_initializer;
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
    const scxml_payload_descriptor *payloads;
    const scxml_foreach_descriptor *foreach_descriptors;
    const struct scxml_invocation_descriptor *invocations;
    size_t step_begin;
    size_t step_end;
    size_t step_storage_count;
    size_t branch_storage_count;
    size_t effect_storage_count;
    size_t assignment_storage_count;
    size_t payload_storage_count;
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
    size_t owner_id_size;
    size_t dynamic_id_max_size;
    size_t dynamic_done_name_max_size;
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
    bool has_type_expr;
    bool has_src_expr;
    bool has_id_location;
    size_t payload_first;
    size_t payload_count;
    cflow_scxml_cmeta_expr_program type_expr;
    cflow_scxml_cmeta_expr_program src_expr;
    cflow_scxml_cmeta_expr_program data_expr;
    cflow_scxml_cmeta_location id_location;
    scxml_content_descriptor content;
} scxml_invocation_descriptor;

typedef struct scxml_done_data_descriptor {
    cflow_machine_state_id parent;
    cflow_machine_state_id final_state;
    cflow_scxml_cmeta_expr_program expression;
    scxml_content_descriptor content;
} scxml_done_data_descriptor;

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
    size_t transition_target_rows;
    size_t event_descriptor_rows;
    size_t event_descriptor_guard_rows;
    size_t event_descriptor_action_rows;
    size_t event_descriptor_target_rows;
    size_t guard_rows;
    size_t event_occurrences;
    size_t executable_blocks;
    size_t block_rows;
    size_t executable_steps;
    size_t log_label_bytes;
    size_t effect_rows;
    size_t payload_rows;
    size_t max_payload_entries;
    size_t dynamic_expression_rows;
    size_t assignment_rows;
    size_t data_initializer_rows;
    size_t late_initializer_rows;
    size_t done_data_rows;
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
    cflow_statechart_transition_target *transition_targets;
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
    scxml_payload_descriptor *payloads;
    cflow_scxml_cmeta_assign_program *assignments;
    scxml_foreach_descriptor *foreach_descriptors;
    scxml_invocation_descriptor *invocations;
    scxml_done_data_descriptor *done_data;
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
    size_t transition_target_index;
    size_t guard_index;
    size_t event_occurrence_index;
    size_t event_name_count;
    size_t executable_index;
    size_t block_index;
    size_t step_index;
    size_t branch_index;
    size_t effect_index;
    size_t payload_index;
    size_t assignment_index;
    size_t foreach_index;
    size_t log_storage_index;
    size_t effect_storage_index;
    size_t step_capacity;
    size_t branch_capacity;
    size_t effect_capacity;
    size_t payload_capacity;
    size_t assignment_capacity;
    size_t foreach_capacity;
    size_t max_iterations;
    size_t log_storage_capacity;
    size_t effect_storage_capacity;
    size_t guard_capacity;
    size_t transition_target_capacity;
    size_t max_conditional_depth;
    size_t state_action_index;
    size_t transition_action_index;
    size_t invocation_index;
    size_t invocation_emit_index;
    size_t invocation_storage_index;
    size_t invocation_storage_capacity;
    size_t invocation_capacity;
    size_t done_data_index;
    size_t done_data_capacity;
    size_t late_initializer_index;
    uint32_t requirements;
    bool late_binding;
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
    size_t effect_count;
    scxml_payload_descriptor *payloads;
    size_t payload_count;
    size_t max_payload_entries;
    cflow_scxml_cmeta_assign_program *assignments;
    size_t assignment_count;
    size_t data_initializer_count;
    size_t late_initializer_count;
    scxml_foreach_descriptor *foreach_descriptors;
    size_t foreach_count;
    scxml_invocation_descriptor *invocations;
    size_t invocation_count;
    scxml_done_data_descriptor *done_data;
    size_t done_data_count;
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

typedef enum scxml_late_initializer_phase {
    SCXML_LATE_INITIALIZER_NEVER = 0,
    SCXML_LATE_INITIALIZER_PENDING,
    SCXML_LATE_INITIALIZER_DONE
} scxml_late_initializer_phase;

typedef struct scxml_late_initializer_state {
    scxml_late_initializer_phase phase;
} scxml_late_initializer_state;

typedef enum scxml_delayed_state {
    SCXML_DELAYED_FREE = 0,
    SCXML_DELAYED_RESERVED,
    SCXML_DELAYED_ACTIVE,
    SCXML_DELAYED_CANCEL_RESERVED
} scxml_delayed_state;

typedef struct scxml_delayed_send {
    const char *id;
    size_t id_size;
    char generated_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
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
    SCXML_INVOCATION_START_RESERVED,
    SCXML_INVOCATION_FAIL_RESERVED,
    SCXML_INVOCATION_ACTIVE,
    SCXML_INVOCATION_FAILED
} scxml_invocation_state;

typedef struct scxml_invocation_row {
    uint64_t token;
    const char *id;
    size_t id_size;
    char owned_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    bool owns_id;
    scxml_invocation_state state;
} scxml_invocation_row;

typedef enum scxml_invocation_effect_kind {
    SCXML_INVOCATION_EFFECT_ENTER = 1,
    SCXML_INVOCATION_EFFECT_EXIT,
    SCXML_INVOCATION_EFFECT_START,
    SCXML_INVOCATION_EFFECT_FAIL
} scxml_invocation_effect_kind;

typedef struct scxml_invocation_lifecycle_effect {
    cflow_scxml_session_impl *session;
    cflow_statechart_effect_ticket adapter_ticket;
    uint64_t token;
    size_t invocation;
    scxml_invocation_effect_kind kind;
    bool in_use;
} scxml_invocation_lifecycle_effect;

typedef union scxml_event_data_storage {
    long double floating_alignment;
    void *pointer_alignment;
    uint64_t integer_alignment;
    unsigned char bytes[CFLOW_SCXML_EVENT_DATA_CAPACITY];
} scxml_event_data_storage;

typedef struct scxml_external_event_metadata_row {
    cflow_scxml_session_impl *session;
    uint64_t token;
    bool in_use;
    size_t send_id_size;
    size_t origin_size;
    size_t origin_type_size;
    size_t invoke_id_size;
    size_t data_size;
    const cmeta_data_desc *data_schema;
    bool data_object_live;
    char send_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char origin_type[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char invoke_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char data[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    scxml_event_data_storage data_object;
} scxml_external_event_metadata_row;

static const scxml_program_name *find_program_name(
    const scxml_program_name *names, size_t count,
    const char *name, size_t name_size);

static cflow_scxml_status compile_cmeta_value_program(
    scxml_build *build, turbo_xml_attribute attribute,
    const char *subject, cflow_scxml_cmeta_expr_program *program,
    cflow_scxml_cmeta_expr_value_kind required_kind);

static cflow_scxml_status compile_cmeta_owned_string_location(
    scxml_build *build, turbo_xml_attribute attribute,
    const char *subject, cflow_scxml_cmeta_location *out);

static cflow_scxml_status compile_cmeta_payload_token(
    scxml_build *build, turbo_xml_string_view source,
    turbo_xml_location location, const char *subject,
    cflow_scxml_cmeta_expr_program *program);

static cflow_scxml_status compile_cmeta_content_expression(
    scxml_build *build, turbo_xml_attribute expression,
    const char *subject, scxml_content_descriptor *content,
    cflow_scxml_cmeta_expr_program *scalar_program);

static bool scalar_value_to_text(
    const cflow_scxml_cmeta_expr_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size);

static bool payload_value_from_cmeta(
    const cflow_scxml_cmeta_expr_value *source,
    cflow_scxml_payload_value *destination);

static bool materialize_content_descriptor(
    const scxml_content_descriptor *descriptor, const void *state,
    cflow_scxml_content_view *out);

static bool payload_v2_to_v3(
    cflow_scxml_session_impl *session,
    const cflow_scxml_payload_view *source,
    cflow_scxml_payload_view_v3 *out);

static bool copy_event_data_object(const cmeta_data_desc *schema,
                                   void *destination,
                                   const void *source);

struct cflow_scxml_session_impl {
    const cflow_scxml_program_impl *program;
    cflow_statechart_instance instance;
    cflow_statechart_executable_binding *bindings;
    scxml_session_binding_user *binding_users;
    size_t binding_count;
    cflow_statechart_guard_binding *guard_bindings;
    scxml_session_guard_user *guard_users;
    size_t guard_binding_count;
    scxml_late_initializer_state *late_initializers;
    size_t late_initializer_count;
    bool late_initializer_ticket_pending;
    char *system_name;
    char session_id[TURBO_UUID_STRING_SIZE];
    char scxml_location[sizeof("#_scxml_") - 1u +
                        TURBO_UUID_STRING_SIZE];
    char current_event_name[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_send_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_origin[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_origin_type[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_invoke_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char current_event_data[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    const cmeta_data_desc *current_event_data_schema;
    bool current_event_data_object_live;
    scxml_event_data_storage current_event_data_object;
    cflow_scxml_cmeta_expr_system_values system_values;
    cflow_scxml_event_io_adapter_v1 event_io;
    cflow_scxml_event_io_adapter_v2 event_io_v2;
    cflow_scxml_event_io_adapter_v3 event_io_v3;
    void *adapter_user;
    turbo_mutex_t registry_lock;
    scxml_delayed_send *delayed_sends;
    size_t delayed_send_capacity;
    scxml_prepared_effect *prepared_effects;
    size_t prepared_effect_capacity;
    cflow_scxml_invoke_adapter_v1 invoke;
    cflow_scxml_invoke_adapter_v2 invoke_v2;
    cflow_scxml_invoke_adapter_v3 invoke_v3;
    void *invoke_user;
    scxml_invocation_row *invocation_rows;
    size_t invocation_capacity;
    scxml_invocation_lifecycle_effect *invocation_effects;
    size_t invocation_effect_capacity;
    cflow_scxml_invoke_stats invoke_stats;
    uint64_t next_send_token;
    uint64_t next_invocation_token;
    scxml_external_event_metadata_row *external_metadata_rows;
    size_t external_metadata_capacity;
    cflow_scxml_payload_entry *payload_scratch;
    cflow_scxml_payload_entry_v3 *payload_scratch_v3;
    size_t payload_scratch_capacity;
    uint64_t next_external_metadata_token;
    bool has_event_io;
    bool has_invoke;
    uint32_t event_io_abi;
    uint32_t invoke_abi;
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
    SCXML_ELEMENT_FINALIZE,
    SCXML_ELEMENT_CONTENT,
    SCXML_ELEMENT_PARAM,
    SCXML_ELEMENT_DATAMODEL,
    SCXML_ELEMENT_DATA,
    SCXML_ELEMENT_DONEDATA
} scxml_element_kind;

static bool checked_add(size_t left, size_t right, size_t *out) {
    if (out == NULL || left > SIZE_MAX - right) return false;
    *out = left + right;
    return true;
}

static bool payload_scalar_to_text(
    const cflow_scxml_payload_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size) {
    cflow_scxml_cmeta_expr_value converted = {0};
    if (value == NULL) return false;
    switch (value->kind) {
        case CFLOW_SCXML_PAYLOAD_VALUE_BOOL:
            converted.kind = CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL;
            converted.data.boolean = value->data.boolean;
            break;
        case CFLOW_SCXML_PAYLOAD_VALUE_SINT:
            converted.kind = CFLOW_SCXML_CMETA_EXPR_VALUE_SINT;
            converted.data.sint = value->data.sint;
            break;
        case CFLOW_SCXML_PAYLOAD_VALUE_UINT:
            converted.kind = CFLOW_SCXML_CMETA_EXPR_VALUE_UINT;
            converted.data.uint = value->data.uint;
            break;
        case CFLOW_SCXML_PAYLOAD_VALUE_FLOAT:
            converted.kind = CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT;
            converted.data.number = value->data.number;
            break;
        case CFLOW_SCXML_PAYLOAD_VALUE_STRING:
            converted.kind = CFLOW_SCXML_CMETA_EXPR_VALUE_STRING;
            converted.data.string.data = value->data.string.data;
            converted.data.string.size = value->data.string.size;
            break;
        default: return false;
    }
    return scalar_value_to_text(
        &converted, storage, capacity, out_data, out_size);
}

static bool attach_event_content(
    cflow_scxml_session_impl *session,
    scxml_external_event_metadata_row *row,
    const cflow_scxml_content_view *content) {
    const char *data = NULL;
    size_t data_size = 0u;
    if (session == NULL || row == NULL || content == NULL || !row->in_use)
        return false;
    if (content->kind == CFLOW_SCXML_CONTENT_INVALID) return true;
    if (content->kind == CFLOW_SCXML_CONTENT_SCALAR) {
        if (!payload_scalar_to_text(
                &content->scalar, row->data, sizeof(row->data),
                &data, &data_size))
            return false;
        if (data_size != 0u) memmove(row->data, data, data_size);
        row->data[data_size] = '\0';
        row->data_size = data_size;
        return true;
    }
    if (content->kind == CFLOW_SCXML_CONTENT_TEXT_UTF8 ||
        content->kind == CFLOW_SCXML_CONTENT_XML_UTF8) {
        if (content->byte_count > CFLOW_SCXML_EVENT_METADATA_CAPACITY ||
            (content->byte_count != 0u && content->bytes == NULL))
            return false;
        if (content->byte_count != 0u)
            memcpy(row->data, content->bytes, content->byte_count);
        row->data[content->byte_count] = '\0';
        row->data_size = content->byte_count;
        return true;
    }
    if (content->kind != CFLOW_SCXML_CONTENT_CMETA ||
        session->program->cmeta_root == NULL ||
        content->schema != session->program->cmeta_root ||
        content->object == NULL ||
        !copy_event_data_object(
            content->schema, row->data_object.bytes, content->object))
        return false;
    row->data_schema = content->schema;
    row->data_object_live = true;
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
    if (view_equal_raw(name, "content")) return SCXML_ELEMENT_CONTENT;
    if (view_equal_raw(name, "param")) return SCXML_ELEMENT_PARAM;
    if (view_equal_raw(name, "datamodel")) return SCXML_ELEMENT_DATAMODEL;
    if (view_equal_raw(name, "data")) return SCXML_ELEMENT_DATA;
    if (view_equal_raw(name, "donedata")) return SCXML_ELEMENT_DONEDATA;
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
                   view_equal_raw(name, "binding") ||
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
                   view_equal_raw(name, "eventexpr") ||
                   view_equal_raw(name, "target") ||
                   view_equal_raw(name, "targetexpr") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "typeexpr") ||
                   view_equal_raw(name, "id") ||
                   view_equal_raw(name, "idlocation") ||
                   view_equal_raw(name, "delay") ||
                   view_equal_raw(name, "delayexpr") ||
                   view_equal_raw(name, "namelist");
        case SCXML_ELEMENT_CANCEL:
            return view_equal_raw(name, "sendid") ||
                   view_equal_raw(name, "sendidexpr");
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
                   view_equal_raw(name, "idlocation") ||
                   view_equal_raw(name, "type") ||
                   view_equal_raw(name, "typeexpr") ||
                   view_equal_raw(name, "src") ||
                   view_equal_raw(name, "srcexpr") ||
                   view_equal_raw(name, "namelist") ||
                   view_equal_raw(name, "autoforward");
        case SCXML_ELEMENT_CONTENT:
            return view_equal_raw(name, "expr");
        case SCXML_ELEMENT_PARAM:
            return view_equal_raw(name, "name") ||
                   view_equal_raw(name, "expr") ||
                   view_equal_raw(name, "location");
        case SCXML_ELEMENT_DATA:
            return view_equal_raw(name, "id") ||
                   view_equal_raw(name, "expr") ||
                   view_equal_raw(name, "src");
        case SCXML_ELEMENT_DATAMODEL:
        case SCXML_ELEMENT_DONEDATA:
            return false;
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

static bool normalize_event_descriptor(turbo_xml_string_view token,
                                       turbo_xml_string_view *out_base,
                                       bool *out_match_all) {
    const char *wildcard;
    if (out_base == NULL || out_match_all == NULL || token.size == 0u)
        return false;
    *out_base = token;
    *out_match_all = false;
    if (token.size == 1u && token.data[0] == '*') {
        out_base->size = 0u;
        *out_match_all = true;
        return true;
    }
    wildcard = (const char *)memchr(token.data, '*', token.size);
    if (wildcard != NULL) {
        if ((size_t)(wildcard - token.data) + 1u != token.size ||
            token.size < 3u || token.data[token.size - 2u] != '.')
            return false;
        out_base->size -= 2u;
    } else if (token.data[token.size - 1u] == '.') {
        --out_base->size;
    }
    return out_base->size != 0u;
}

static bool event_descriptor_matches(turbo_xml_string_view descriptor,
                                     turbo_xml_string_view event_name) {
    turbo_xml_string_view base;
    bool match_all;
    if (!normalize_event_descriptor(descriptor, &base, &match_all))
        return false;
    if (match_all) return true;
    return event_name.size == base.size
        ? memcmp(event_name.data, base.data, base.size) == 0
        : event_name.size > base.size &&
              memcmp(event_name.data, base.data, base.size) == 0 &&
              event_name.data[base.size] == '.';
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
    return is_xml_ncname(*state_name);
}

static bool completion_descriptor_matches(
    turbo_xml_string_view descriptor, turbo_xml_string_view state_name) {
    static const char prefix[] = "done.state.";
    turbo_xml_string_view base = {NULL, 0u};
    const size_t prefix_size = sizeof(prefix) - 1u;
    size_t event_size;
    bool match_all = false;
    size_t index;
    if (state_name.size > SIZE_MAX - prefix_size) return false;
    event_size = prefix_size + state_name.size;
    if (!normalize_event_descriptor(descriptor, &base, &match_all))
        return false;
    if (match_all) return true;
    if (base.size > event_size) return false;
    for (index = 0u; index < base.size; ++index) {
        const char expected = index < prefix_size
            ? prefix[index] : state_name.data[index - prefix_size];
        if (base.data[index] != expected) return false;
    }
    if (base.size == event_size) return true;
    return base.size < prefix_size
        ? prefix[base.size] == '.'
        : state_name.data[base.size - prefix_size] == '.';
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

static cflow_scxml_status validate_empty_effect(
    scxml_build *build, turbo_xml_node node, const char *message);

static bool cmeta_content_kind_is_scalar(cmeta_data_kind kind) {
    return kind == CMETA_DATA_BOOL || kind == CMETA_DATA_SINT ||
           kind == CMETA_DATA_UINT || kind == CMETA_DATA_FLOAT ||
           kind == CMETA_DATA_STRING || kind == CMETA_DATA_ENUM;
}

static cflow_scxml_status inspect_inline_content(
    scxml_build *build, turbo_xml_node content,
    cflow_scxml_content_kind *out_kind, size_t *out_size) {
    size_t index;
    turbo_xml_status xml_status;
    bool has_markup = false;
    if (build == NULL || out_kind == NULL || out_size == NULL)
        return CFLOW_SCXML_INVALID_ARGUMENT;
    for (index = 0u; index < turbo_xml_node_child_count(content); ++index) {
        if (turbo_xml_node_type(turbo_xml_node_child_at(content, index)) !=
            TURBO_XML_TEXT) {
            has_markup = true;
            break;
        }
    }
    xml_status = turbo_xml_serialize_children(
        content, NULL, 0u, build->limits.max_name_bytes, out_size);
    if (xml_status != TURBO_XML_OK) {
        return scxml_fail(
            build,
            xml_status == TURBO_XML_LIMIT_EXCEEDED
                ? CFLOW_SCXML_LIMIT_EXCEEDED
                : xml_status == TURBO_XML_ALLOCATION_FAILED
                    ? CFLOW_SCXML_ALLOCATION_FAILED
                    : CFLOW_SCXML_INVALID_STRUCTURE,
            turbo_xml_node_location(content),
            "SCXML inline content serialization failed");
    }
    *out_kind = has_markup ? CFLOW_SCXML_CONTENT_XML_UTF8
                           : CFLOW_SCXML_CONTENT_TEXT_UTF8;
    return CFLOW_SCXML_OK;
}

static bool content_expression_requires_v3(
    const scxml_build *build, turbo_xml_attribute expression) {
    cflow_scxml_cmeta_location location = {0};
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    const turbo_xml_string_view source =
        turbo_xml_attribute_value(expression);
    return build != NULL && expression.impl != NULL &&
        cflow_scxml_cmeta_location_compile(
            &location, source.data, source.size, build->cmeta_root,
            build->cmeta_expression_limits.max_path_depth, false,
            &diagnostic) == CFLOW_SCXML_CMETA_EXPR_OK &&
        location.value != NULL &&
        !cmeta_content_kind_is_scalar(location.value->kind);
}

static cflow_scxml_status analyze_param(
    scxml_build *build, turbo_xml_node node) {
    const turbo_xml_attribute name = find_attribute(node, "name");
    const turbo_xml_attribute expression = find_attribute(node, "expr");
    const turbo_xml_attribute location = find_attribute(node, "location");
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_PARAM);
    if (status != CFLOW_SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "param requires the CMeta data model");
    if (name.impl == NULL ||
        is_empty_view(turbo_xml_attribute_value(name)) ||
        ((expression.impl == NULL) == (location.impl == NULL)))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "param requires a name and exactly one expr or location");
    if (validate_empty_effect(build, node,
                              "param cannot contain child content") !=
        CFLOW_SCXML_OK)
        return CFLOW_SCXML_INVALID_STRUCTURE;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_send_content(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts,
    bool *out_has_data, bool *out_requires_v3,
    size_t *out_param_count) {
    size_t index;
    bool found = false;
    *out_has_data = false;
    *out_requires_v3 = false;
    *out_param_count = 0u;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        scxml_element_kind child_kind;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child)))) {
            continue;
        }
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT) {
            cflow_scxml_status namespace_status = require_scxml_element(
                build, child, &child_kind);
            if (namespace_status != CFLOW_SCXML_OK)
                return namespace_status;
        } else {
            child_kind = SCXML_ELEMENT_UNKNOWN;
        }
        if (child_kind == SCXML_ELEMENT_PARAM) {
            cflow_scxml_status status;
            if (found)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "send content is mutually exclusive with param");
            status = analyze_param(build, child);
            if (status != CFLOW_SCXML_OK) return status;
            if (!checked_add(*out_param_count, 1u, out_param_count))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(child),
                                  "send param count overflow");
            continue;
        }
        if (child_kind != SCXML_ELEMENT_CONTENT) {
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_node_location(child),
                              "send accepts only param or bounded scalar content");
        }
        if (found || *out_param_count != 0u) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "send content is unique and mutually exclusive with param");
        }
        found = true;
        {
            const turbo_xml_attribute expression =
                find_attribute(child, "expr");
            size_t content_size = 0u;
            size_t retained = 0u;
            cflow_scxml_content_kind content_kind;
            cflow_scxml_status status = validate_element_attributes(
                build, child, SCXML_ELEMENT_CONTENT);
            if (status != CFLOW_SCXML_OK) return status;
            status = inspect_inline_content(
                build, child, &content_kind, &content_size);
            if (status != CFLOW_SCXML_OK) return status;
            if (expression.impl != NULL) {
                if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                    is_empty_view(turbo_xml_attribute_value(expression))) {
                    return scxml_fail(
                        build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                        turbo_xml_attribute_location(expression),
                        "send content expr requires the CMeta data model");
                }
                if (content_size != 0u) {
                    return scxml_fail(
                        build, CFLOW_SCXML_INVALID_STRUCTURE,
                        turbo_xml_node_location(child),
                        "content expr cannot have inline child content");
                }
                *out_requires_v3 = content_expression_requires_v3(
                    build, expression);
            } else {
                if (!checked_add(content_size, 1u, &retained) ||
                    !checked_add(counts->effect_string_bytes, retained,
                                 &counts->effect_string_bytes)) {
                    return scxml_fail(
                        build, CFLOW_SCXML_LIMIT_EXCEEDED,
                        turbo_xml_node_location(child),
                        "send inline content storage size overflow");
                }
                *out_requires_v3 = true;
            }
        }
    }
    *out_has_data = found;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_done_data(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts) {
    size_t index;
    bool found_content = false;
    bool has_expression = false;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_DONEDATA);
    if (status != CFLOW_SCXML_OK) return status;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        turbo_xml_attribute expression;
        size_t content_size = 0u;
        size_t retained = 0u;
        cflow_scxml_content_kind content_kind;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            element_kind(child) != SCXML_ELEMENT_CONTENT)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "donedata admits exactly one content child");
        expression = find_attribute(child, "expr");
        if (found_content)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "donedata accepts exactly one content child");
        found_content = true;
        status = validate_element_attributes(
            build, child, SCXML_ELEMENT_CONTENT);
        if (status != CFLOW_SCXML_OK) return status;
        status = inspect_inline_content(
            build, child, &content_kind, &content_size);
        if (status != CFLOW_SCXML_OK) return status;
        if (expression.impl != NULL) {
            has_expression = true;
            if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                is_empty_view(turbo_xml_attribute_value(expression)))
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_node_location(child),
                    "donedata content expr requires the CMeta data model");
            if (content_size != 0u)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "donedata content expr cannot have inline children");
            if (content_expression_requires_v3(build, expression))
                return scxml_fail(
                    build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                    turbo_xml_attribute_location(expression),
                    "structured donedata requires the completion payload ABI");
        } else {
            if (content_size > CFLOW_SCXML_EVENT_METADATA_CAPACITY ||
                !checked_add(content_size, 1u, &retained) ||
                !checked_add(counts->effect_string_bytes, retained,
                             &counts->effect_string_bytes))
                return scxml_fail(
                    build, CFLOW_SCXML_LIMIT_EXCEEDED,
                    turbo_xml_node_location(child),
                    "donedata inline content exceeds the Event metadata bound");
        }
    }
    if (!found_content)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "donedata requires one content child");
    if (!checked_add(counts->done_data_rows, 1u,
                     &counts->done_data_rows) ||
        (has_expression &&
         !checked_add(counts->dynamic_expression_rows, 1u,
                      &counts->dynamic_expression_rows)))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "donedata descriptor count overflow");
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status validate_empty_effect(
    scxml_build *build, turbo_xml_node node, const char *message) {
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(child), message);
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_send(scxml_build *build,
                                       turbo_xml_node node,
                                       scxml_counts *counts) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    const turbo_xml_attribute event_expr_attribute =
        find_attribute(node, "eventexpr");
    const turbo_xml_attribute target_attribute = find_attribute(node, "target");
    const turbo_xml_attribute target_expr_attribute =
        find_attribute(node, "targetexpr");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute type_expr_attribute =
        find_attribute(node, "typeexpr");
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute delay_attribute = find_attribute(node, "delay");
    const turbo_xml_attribute delay_expr_attribute =
        find_attribute(node, "delayexpr");
    const turbo_xml_attribute idlocation_attribute =
        find_attribute(node, "idlocation");
    const turbo_xml_attribute namelist_attribute =
        find_attribute(node, "namelist");
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
    bool has_data = false;
    bool content_requires_v3 = false;
    size_t payload_count = 0u;
    size_t param_count = 0u;
    size_t token_cursor = 0u;
    turbo_xml_string_view token;
    size_t child_index;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_SEND);
    if (status != CFLOW_SCXML_OK) return status;
    if ((event_attribute.impl != NULL && event_expr_attribute.impl != NULL) ||
        (event_attribute.impl != NULL && !is_xml_nmtoken(event))) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            event_attribute.impl != NULL
                ? turbo_xml_attribute_location(event_attribute)
                : turbo_xml_node_location(node),
            "send event and eventexpr are mutually exclusive");
    }
    if ((target_attribute.impl != NULL && target_expr_attribute.impl != NULL) ||
        (type_attribute.impl != NULL && type_expr_attribute.impl != NULL) ||
        (delay_attribute.impl != NULL && delay_expr_attribute.impl != NULL) ||
        (id_attribute.impl != NULL && idlocation_attribute.impl != NULL))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "send literal and expression attributes are mutually exclusive");
    if ((event_expr_attribute.impl != NULL ||
         target_expr_attribute.impl != NULL ||
         type_expr_attribute.impl != NULL ||
         delay_expr_attribute.impl != NULL ||
         idlocation_attribute.impl != NULL ||
         namelist_attribute.impl != NULL) &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "send expressions require the CMeta data model");
    if (idlocation_attribute.impl != NULL &&
        is_empty_view(turbo_xml_attribute_value(idlocation_attribute)))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(idlocation_attribute),
                          "send idlocation must be non-empty");
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
    if (delay_ms != 0u && id_attribute.impl == NULL &&
        idlocation_attribute.impl == NULL) {
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_attribute_location(delay_attribute),
                          "delayed send requires id or idlocation");
    }
    status = analyze_send_content(
        build, node, counts, &has_data, &content_requires_v3,
        &param_count);
    if (status != CFLOW_SCXML_OK) return status;
    if (!has_data && event_attribute.impl == NULL &&
        event_expr_attribute.impl == NULL)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "send requires exactly one event, eventexpr, or content");
    if (has_data &&
        (namelist_attribute.impl != NULL || param_count != 0u))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "send content is mutually exclusive with namelist and param");
    if (namelist_attribute.impl != NULL) {
        const turbo_xml_string_view namelist =
            turbo_xml_attribute_value(namelist_attribute);
        while (token_next(namelist, &token_cursor, &token)) {
            if (!checked_add(payload_count, 1u, &payload_count) ||
                !count_retained_view(token,
                                     &counts->effect_string_bytes))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(
                                      namelist_attribute),
                                  "send namelist storage size overflow");
        }
        if (payload_count == 0u)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(
                                  namelist_attribute),
                              "send namelist must contain one location");
    }
    for (child_index = 0u;
         child_index < turbo_xml_node_child_count(node); ++child_index) {
        const turbo_xml_node child =
            turbo_xml_node_child_at(node, child_index);
        turbo_xml_attribute name;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            element_kind(child) != SCXML_ELEMENT_PARAM)
            continue;
        name = find_attribute(child, "name");
        if (!count_retained_view(turbo_xml_attribute_value(name),
                                 &counts->effect_string_bytes))
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(name),
                              "send param name storage size overflow");
    }
    if (!checked_add(payload_count, param_count, &payload_count) ||
        payload_count > CFLOW_SCXML_PAYLOAD_MAX_ENTRIES ||
        !checked_add(counts->payload_rows, payload_count,
                     &counts->payload_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "send payload entry limit exceeded");
    if (payload_count > counts->max_payload_entries)
        counts->max_payload_entries = payload_count;
    internal_target = view_equal_raw(target, "#_internal") ||
                      view_equal_raw(target, "_internal");
    if (has_data && internal_target &&
        (delay_attribute.impl != NULL || delay_expr_attribute.impl != NULL))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "internal send content cannot be delayed");
    if (payload_count != 0u && internal_target)
        return scxml_fail(
            build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
            turbo_xml_node_location(node),
            "internal named-object payloads are not admitted by this scalar profile");
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        (event_attribute.impl != NULL &&
         !checked_add(counts->event_occurrences, 1u,
                      &counts->event_occurrences)) ||
        (event_attribute.impl != NULL &&
         !count_retained_view(event, &counts->effect_string_bytes)) ||
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
    if ((event_expr_attribute.impl != NULL ||
         target_expr_attribute.impl != NULL ||
         type_expr_attribute.impl != NULL ||
         delay_expr_attribute.impl != NULL || has_data ||
         payload_count != 0u) &&
        !checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "send expression count overflow");
    if ((target_expr_attribute.impl == NULL && !internal_target) ||
        delay_ms != 0u || delay_expr_attribute.impl != NULL ||
        payload_count != 0u)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_EVENT_IO;
    if (payload_count != 0u ||
        (has_data && !content_requires_v3 &&
         target_expr_attribute.impl == NULL && !internal_target))
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_PAYLOAD;
    if (content_requires_v3 &&
        (target_expr_attribute.impl != NULL || !internal_target))
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_CONTENT_V3;
    if (delay_ms != 0u || delay_expr_attribute.impl != NULL)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_DELAYED_SEND;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_cancel(scxml_build *build,
                                         turbo_xml_node node,
                                         scxml_counts *counts) {
    const turbo_xml_attribute sendid_attribute = find_attribute(node, "sendid");
    const turbo_xml_attribute sendid_expr_attribute =
        find_attribute(node, "sendidexpr");
    const turbo_xml_string_view sendid =
        sendid_attribute.impl != NULL
            ? turbo_xml_attribute_value(sendid_attribute)
            : (turbo_xml_string_view){NULL, 0u};
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_CANCEL);
    if (status != CFLOW_SCXML_OK) return status;
    if ((sendid_attribute.impl == NULL) ==
            (sendid_expr_attribute.impl == NULL) ||
        (sendid_attribute.impl != NULL && !is_xml_ncname(sendid))) {
        return scxml_fail(
            build, CFLOW_SCXML_INVALID_STRUCTURE,
            sendid_attribute.impl != NULL
                ? turbo_xml_attribute_location(sendid_attribute)
                : turbo_xml_node_location(node),
            "cancel requires exactly one sendid or sendidexpr");
    }
    if (sendid_expr_attribute.impl != NULL &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_attribute_location(sendid_expr_attribute),
                          "cancel sendidexpr requires the CMeta data model");
    status = validate_empty_effect(build, node, "cancel must be empty");
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(counts->executable_steps, 1u,
                     &counts->executable_steps) ||
        !checked_add(counts->effect_rows, 1u, &counts->effect_rows) ||
        (sendid_attribute.impl != NULL &&
         !count_retained_view(sendid, &counts->effect_string_bytes))) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "cancel descriptor storage size overflow");
    }
    if (sendid_expr_attribute.impl != NULL &&
        !checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "cancel expression count overflow");
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
    size_t target_count = 0u;
    size_t descriptor_count = 0u;
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
        while (token_next(value, &cursor, &token)) ++target_count;
        if (target_count == 0u) {
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(target_attribute),
                              "transition target must contain an IDREF");
        }
    }
    token_count = 0u;
    if (event_attribute.impl != NULL) {
        value = turbo_xml_attribute_value(event_attribute);
        cursor = 0u;
        while (token_next(value, &cursor, &token)) {
            turbo_xml_string_view completed = {NULL, 0u};
            turbo_xml_string_view descriptor_base = {NULL, 0u};
            bool match_all = false;
            ++token_count;
            if (completion_token(token, &completed)) continue;
            if (!normalize_event_descriptor(
                    token, &descriptor_base, &match_all)) {
                return scxml_fail(
                    build, CFLOW_SCXML_INVALID_STRUCTURE,
                    turbo_xml_attribute_location(event_attribute),
                    "event descriptor wildcard must be '*' or a trailing '.*'");
            }
            ++descriptor_count;
            if (!match_all &&
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
    if (!checked_add(counts->event_descriptor_rows, descriptor_count,
                     &counts->event_descriptor_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "event descriptor count overflow");
    }
    if (!checked_add(counts->transition_rows, token_count,
                     &counts->transition_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "transition count overflow");
    }
    if (target_count != 0u) {
        size_t emitted_target_count;
        if (!checked_multiply(target_count, token_count,
                              &emitted_target_count) ||
            !checked_add(counts->transition_target_rows,
                         emitted_target_count,
                         &counts->transition_target_rows)) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(target_attribute),
                              "transition target count overflow");
        }
        if (!checked_multiply(target_count, descriptor_count,
                              &emitted_target_count) ||
            !checked_add(counts->event_descriptor_target_rows,
                         emitted_target_count,
                         &counts->event_descriptor_target_rows)) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(target_attribute),
                              "descriptor target count overflow");
        }
    }
    if (condition_attribute.impl != NULL &&
        !checked_add(counts->guard_rows, token_count,
                     &counts->guard_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(condition_attribute),
                          "transition guard count overflow");
    }
    if (condition_attribute.impl != NULL &&
        !checked_add(counts->event_descriptor_guard_rows,
                     descriptor_count,
                     &counts->event_descriptor_guard_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(condition_attribute),
                          "descriptor guard count overflow");
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
    if (nonempty &&
        !checked_add(counts->event_descriptor_action_rows,
                     descriptor_count,
                     &counts->event_descriptor_action_rows)) {
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "descriptor action count overflow");
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
    static const size_t max_token_digits =
        sizeof("18446744073709551615") - 1u;
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute src_attribute = find_attribute(node, "src");
    const turbo_xml_attribute autoforward_attribute =
        find_attribute(node, "autoforward");
    const turbo_xml_attribute type_expr_attribute =
        find_attribute(node, "typeexpr");
    const turbo_xml_attribute src_expr_attribute =
        find_attribute(node, "srcexpr");
    const turbo_xml_attribute idlocation_attribute =
        find_attribute(node, "idlocation");
    const turbo_xml_attribute namelist_attribute =
        find_attribute(node, "namelist");
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
    size_t dynamic_id_size = 0u;
    size_t dynamic_done_size = 0u;
    size_t index;
    size_t finalize_count = 0u;
    size_t content_count = 0u;
    bool content_requires_v3 = false;
    size_t param_count = 0u;
    size_t payload_count = 0u;
    size_t token_cursor = 0u;
    turbo_xml_string_view token;
    cflow_scxml_cmeta_location compiled_id_location = {0};
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_INVOKE);
    if (status != CFLOW_SCXML_OK) return status;
    if (id_attribute.impl != NULL && idlocation_attribute.impl != NULL)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "invoke id and idlocation are mutually exclusive");
    if ((type_expr_attribute.impl != NULL ||
         src_expr_attribute.impl != NULL ||
         idlocation_attribute.impl != NULL ||
         namelist_attribute.impl != NULL) &&
        build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "invoke expressions require the CMeta data model");
    if ((type_attribute.impl != NULL && type_expr_attribute.impl != NULL) ||
        (src_attribute.impl != NULL && src_expr_attribute.impl != NULL))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "invoke literal and expression attributes are mutually exclusive");
    if (idlocation_attribute.impl != NULL) {
        status = compile_cmeta_owned_string_location(
            build, idlocation_attribute, "invoke idlocation",
            &compiled_id_location);
        if (status != CFLOW_SCXML_OK) return status;
        if (!checked_add(owner_id.size, 1u, &dynamic_id_size) ||
            !checked_add(dynamic_id_size, max_token_digits,
                         &dynamic_id_size) ||
            dynamic_id_size > CFLOW_SCXML_EVENT_METADATA_CAPACITY ||
            !checked_add(done_prefix_size, dynamic_id_size,
                         &dynamic_done_size) ||
            dynamic_done_size > CFLOW_SCXML_EVENT_METADATA_CAPACITY) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_attribute_location(
                                  idlocation_attribute),
                              "dynamic invoke id or done Event exceeds metadata limit");
        }
    }
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
        if (child_kind == SCXML_ELEMENT_PARAM) {
            turbo_xml_attribute name;
            status = analyze_param(build, child);
            if (status != CFLOW_SCXML_OK) return status;
            name = find_attribute(child, "name");
            if (!count_retained_view(
                    turbo_xml_attribute_value(name),
                    &counts->invocation_string_bytes) ||
                !checked_add(param_count, 1u, &param_count))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_node_location(child),
                                  "invoke param storage size overflow");
            continue;
        }
        if (child_kind == SCXML_ELEMENT_CONTENT) {
            const turbo_xml_attribute expression =
                find_attribute(child, "expr");
            size_t content_size = 0u;
            size_t retained = 0u;
            cflow_scxml_content_kind content_kind;
            status = validate_element_attributes(
                build, child, SCXML_ELEMENT_CONTENT);
            if (status != CFLOW_SCXML_OK) return status;
            if (++content_count > 1u)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "invoke may contain at most one content child");
            status = inspect_inline_content(
                build, child, &content_kind, &content_size);
            if (status != CFLOW_SCXML_OK) return status;
            if (expression.impl != NULL) {
                if (build->data_model != SCXML_DATA_MODEL_CMETA ||
                    is_empty_view(turbo_xml_attribute_value(expression)))
                    return scxml_fail(
                        build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                        turbo_xml_node_location(child),
                        "invoke content expr requires the CMeta data model");
                if (content_size != 0u)
                    return scxml_fail(
                        build, CFLOW_SCXML_INVALID_STRUCTURE,
                        turbo_xml_node_location(child),
                        "invoke content expr cannot have inline children");
                content_requires_v3 = content_expression_requires_v3(
                    build, expression);
            } else {
                if (!checked_add(content_size, 1u, &retained) ||
                    !checked_add(counts->invocation_string_bytes, retained,
                                 &counts->invocation_string_bytes))
                    return scxml_fail(
                        build, CFLOW_SCXML_LIMIT_EXCEEDED,
                        turbo_xml_node_location(child),
                        "invoke inline content storage size overflow");
                content_requires_v3 = true;
            }
            continue;
        }
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
    if (content_count != 0u &&
        (src_attribute.impl != NULL || src_expr_attribute.impl != NULL ||
         namelist_attribute.impl != NULL || param_count != 0u))
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "invoke content is mutually exclusive with src, namelist, and param");
    if (namelist_attribute.impl != NULL && param_count != 0u)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "invoke namelist and param are mutually exclusive");
    if (namelist_attribute.impl != NULL) {
        const turbo_xml_string_view namelist =
            turbo_xml_attribute_value(namelist_attribute);
        while (token_next(namelist, &token_cursor, &token)) {
            if (!checked_add(payload_count, 1u, &payload_count) ||
                !count_retained_view(
                    token, &counts->invocation_string_bytes))
                return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                  turbo_xml_attribute_location(
                                      namelist_attribute),
                                  "invoke namelist storage size overflow");
        }
        if (payload_count == 0u)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(
                                  namelist_attribute),
                              "invoke namelist must contain one location");
    }
    if (!checked_add(payload_count, param_count, &payload_count) ||
        payload_count > CFLOW_SCXML_PAYLOAD_MAX_ENTRIES ||
        !checked_add(counts->payload_rows, payload_count,
                     &counts->payload_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "invoke payload entry limit exceeded");
    if (payload_count > counts->max_payload_entries)
        counts->max_payload_entries = payload_count;
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
    if ((type_expr_attribute.impl != NULL || src_expr_attribute.impl != NULL ||
         content_count != 0u || payload_count != 0u) &&
        !checked_add(counts->dynamic_expression_rows, 1u,
                     &counts->dynamic_expression_rows))
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_node_location(node),
                          "invoke expression count overflow");
    counts->requirements |= CFLOW_SCXML_REQUIREMENT_INVOKE;
    if (idlocation_attribute.impl != NULL)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_INVOKE_IDLOCATION;
    if ((content_count != 0u && !content_requires_v3) ||
        payload_count != 0u)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_INVOKE_PAYLOAD;
    if (content_requires_v3)
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_INVOKE_CONTENT_V3;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status analyze_datamodel(
    scxml_build *build, turbo_xml_node node, scxml_counts *counts) {
    size_t index;
    size_t data_count = 0u;
    cflow_scxml_status status = validate_element_attributes(
        build, node, SCXML_ELEMENT_DATAMODEL);
    if (status != CFLOW_SCXML_OK) return status;
    if (build->data_model != SCXML_DATA_MODEL_CMETA)
        return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                          turbo_xml_node_location(node),
                          "data declarations require the CMeta data model");
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        turbo_xml_attribute id;
        turbo_xml_attribute expression;
        turbo_xml_attribute source;
        size_t child_index;
        if (turbo_xml_node_type(child) == TURBO_XML_COMMENT ||
            (turbo_xml_node_type(child) == TURBO_XML_TEXT &&
             is_xml_whitespace(turbo_xml_node_value(child))))
            continue;
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
            element_kind(child) != SCXML_ELEMENT_DATA)
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "datamodel accepts only data children");
        id = find_attribute(child, "id");
        expression = find_attribute(child, "expr");
        source = find_attribute(child, "src");
        status = validate_element_attributes(
            build, child, SCXML_ELEMENT_DATA);
        if (status != CFLOW_SCXML_OK) return status;
        if (source.impl != NULL)
            return scxml_fail(build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                              turbo_xml_attribute_location(source),
                              "CMeta data src requires an external resource loader");
        if (id.impl == NULL || expression.impl == NULL ||
            is_empty_view(turbo_xml_attribute_value(id)) ||
            is_empty_view(turbo_xml_attribute_value(expression)))
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(child),
                              "CMeta data requires nonempty id and expr attributes");
        for (child_index = 0u;
             child_index < turbo_xml_node_child_count(child); ++child_index) {
            const turbo_xml_node content =
                turbo_xml_node_child_at(child, child_index);
            if (turbo_xml_node_type(content) == TURBO_XML_COMMENT ||
                (turbo_xml_node_type(content) == TURBO_XML_TEXT &&
                 is_xml_whitespace(turbo_xml_node_value(content))))
                continue;
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_node_location(content),
                              "CMeta data expr cannot have child content");
        }
        if (!checked_add(counts->assignment_rows, 1u,
                         &counts->assignment_rows) ||
            !checked_add(counts->data_initializer_rows, 1u,
                         &counts->data_initializer_rows))
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_node_location(child),
                              "CMeta data initializer count overflow");
        if (!checked_add(data_count, 1u, &data_count))
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_node_location(child),
                              "CMeta data declaration count overflow");
    }
    if (build->late_binding && data_count != 0u) {
        if (!checked_add(counts->late_initializer_rows, 1u,
                         &counts->late_initializer_rows) ||
            !checked_add(counts->executable_blocks, 1u,
                         &counts->executable_blocks) ||
            !checked_add(counts->block_rows, 1u,
                         &counts->block_rows) ||
            !checked_add(counts->executable_steps, 1u,
                         &counts->executable_steps) ||
            !checked_add(counts->state_action_rows, 1u,
                         &counts->state_action_rows)) {
            return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                              turbo_xml_node_location(node),
                              "late data initializer storage overflow");
        }
        counts->requirements |= CFLOW_SCXML_REQUIREMENT_LATE_BINDING;
    }
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

    if (element_child_count(node, SCXML_ELEMENT_DATAMODEL) > 1u)
        return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                          turbo_xml_node_location(node),
                          "a state may contain at most one datamodel");

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
            size_t initial_target_count = 1u;
            if (initial_attribute.impl != NULL) {
                const turbo_xml_string_view initial_value =
                    turbo_xml_attribute_value(initial_attribute);
                turbo_xml_string_view initial_token;
                size_t initial_cursor = 0u;
                initial_target_count = 0u;
                while (token_next(initial_value, &initial_cursor,
                                  &initial_token))
                    ++initial_target_count;
                if (initial_target_count == 0u) {
                    return scxml_fail(
                        build, CFLOW_SCXML_INVALID_STRUCTURE,
                        turbo_xml_attribute_location(initial_attribute),
                        "initial must contain an IDREF");
                }
            }
            if (!checked_add(counts->state_rows, 1u, &counts->state_rows) ||
                !checked_add(counts->synthetic_initials, 1u,
                             &counts->synthetic_initials) ||
                !checked_add(counts->transition_rows, 1u,
                             &counts->transition_rows) ||
                !checked_add(counts->transition_target_rows,
                             initial_target_count,
                             &counts->transition_target_rows)) {
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
        } else if (child_kind == SCXML_ELEMENT_DATAMODEL) {
            if (kind == SCXML_ELEMENT_FINAL ||
                kind == SCXML_ELEMENT_INITIAL ||
                kind == SCXML_ELEMENT_HISTORY)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "datamodel is allowed only in scxml, state, or parallel");
            status = analyze_datamodel(build, child, counts);
        } else if (child_kind == SCXML_ELEMENT_DONEDATA) {
            if (kind != SCXML_ELEMENT_FINAL)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "donedata is allowed only inside final");
            if (element_child_count(node, SCXML_ELEMENT_DONEDATA) != 1u)
                return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                                  turbo_xml_node_location(child),
                                  "final accepts at most one donedata child");
            status = analyze_done_data(build, child, counts);
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

static cflow_scxml_status retain_invocation_inline_content(
    scxml_build *build, turbo_xml_node node,
    scxml_content_descriptor *content) {
    size_t retained;
    size_t actual = 0u;
    char *stored;
    cflow_scxml_status status = inspect_inline_content(
        build, node, &content->kind, &content->byte_count);
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(content->byte_count, 1u, &retained) ||
        (stored = reserve_invocation_storage(build, retained)) == NULL)
        return scxml_fail(
            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
            turbo_xml_node_location(node),
            "invoke inline content storage mismatched admission");
    content->bytes = stored;
    if (turbo_xml_serialize_children(
            node, stored, retained, build->limits.max_name_bytes,
            &actual) != TURBO_XML_OK || actual != content->byte_count)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "invoke inline content changed during emission");
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_invocation_declarations(
    scxml_build *build, turbo_xml_node node, size_t node_count) {
    static const char generated_separator[] = ".invoke.";
    static const char done_prefix[] = "done.invoke.";
    static const size_t max_token_digits =
        sizeof("18446744073709551615") - 1u;
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
        turbo_xml_attribute type_expr_attribute;
        turbo_xml_attribute src_attribute;
        turbo_xml_attribute src_expr_attribute;
        turbo_xml_attribute idlocation_attribute;
        turbo_xml_attribute namelist_attribute;
        turbo_xml_attribute autoforward_attribute;
        turbo_xml_location id_location;
        char *generated;
        size_t generated_size;
        size_t retained_size;
        char ordinal_buffer[3u * sizeof(size_t) + 1u];
        int ordinal_size;
        char *done_name;
        cflow_scxml_status expression_status;
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
        type_expr_attribute = find_attribute(child, "typeexpr");
        src_attribute = find_attribute(child, "src");
        src_expr_attribute = find_attribute(child, "srcexpr");
        idlocation_attribute = find_attribute(child, "idlocation");
        namelist_attribute = find_attribute(child, "namelist");
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
        descriptor->owner_id_size = owner_id.size;
        descriptor->payload_first = build->payload_index;
        descriptor->done_name = done_name;
        descriptor->done_name_size = generated_size;
        descriptor->source_node = child.impl;
        descriptor->autoforward =
            autoforward_attribute.impl != NULL &&
            view_equal_raw(
                turbo_xml_attribute_value(autoforward_attribute), "true");
        if (idlocation_attribute.impl != NULL) {
            if (!checked_add(owner_id.size, 1u,
                             &descriptor->dynamic_id_max_size) ||
                !checked_add(descriptor->dynamic_id_max_size,
                             max_token_digits,
                             &descriptor->dynamic_id_max_size) ||
                !checked_add(sizeof(done_prefix) - 1u,
                             descriptor->dynamic_id_max_size,
                             &descriptor->dynamic_done_name_max_size) ||
                descriptor->dynamic_id_max_size >
                    CFLOW_SCXML_EVENT_METADATA_CAPACITY ||
                descriptor->dynamic_done_name_max_size >
                    CFLOW_SCXML_EVENT_METADATA_CAPACITY) {
                return scxml_fail(
                    build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                    turbo_xml_attribute_location(idlocation_attribute),
                    "dynamic invoke id budget mismatched admission");
            }
            expression_status = compile_cmeta_owned_string_location(
                build, idlocation_attribute, "invoke idlocation",
                &descriptor->id_location);
            if (expression_status != CFLOW_SCXML_OK)
                return expression_status;
            descriptor->has_id_location = true;
        }
        if (type_expr_attribute.impl != NULL) {
            expression_status = compile_cmeta_value_program(
                build, type_expr_attribute, "invoke typeexpr",
                &descriptor->type_expr,
                CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
            if (expression_status != CFLOW_SCXML_OK)
                return expression_status;
            descriptor->has_type_expr = true;
        }
        if (src_expr_attribute.impl != NULL) {
            expression_status = compile_cmeta_value_program(
                build, src_expr_attribute, "invoke srcexpr",
                &descriptor->src_expr,
                CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
            if (expression_status != CFLOW_SCXML_OK)
                return expression_status;
            descriptor->has_src_expr = true;
        }
        if (namelist_attribute.impl != NULL) {
            const turbo_xml_string_view namelist =
                turbo_xml_attribute_value(namelist_attribute);
            size_t cursor = 0u;
            turbo_xml_string_view token;
            while (token_next(namelist, &cursor, &token)) {
                scxml_payload_descriptor *payload;
                if (build->payload_index >= build->payload_capacity)
                    return scxml_fail(
                        build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                        turbo_xml_attribute_location(namelist_attribute),
                        "invoke payload emission exceeded admission");
                payload = &build->payloads[build->payload_index];
                if (!retain_invocation_view(
                        build, token, &payload->name,
                        &payload->name_size))
                    return scxml_fail(
                        build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                        turbo_xml_attribute_location(namelist_attribute),
                        "invoke namelist storage mismatched admission");
                expression_status = compile_cmeta_payload_token(
                    build, token,
                    turbo_xml_attribute_location(namelist_attribute),
                    "invoke namelist", &payload->expression);
                if (expression_status != CFLOW_SCXML_OK)
                    return expression_status;
                ++build->payload_index;
            }
        }
        {
            size_t payload_child_index;
            for (payload_child_index = 0u;
                 payload_child_index < turbo_xml_node_child_count(child);
                 ++payload_child_index) {
                const turbo_xml_node payload_child =
                    turbo_xml_node_child_at(child, payload_child_index);
                const scxml_element_kind payload_kind =
                    element_kind(payload_child);
                if (turbo_xml_node_type(payload_child) !=
                    TURBO_XML_ELEMENT)
                    continue;
                if (payload_kind == SCXML_ELEMENT_PARAM) {
                    const turbo_xml_attribute name =
                        find_attribute(payload_child, "name");
                    const turbo_xml_attribute expression =
                        find_attribute(payload_child, "expr");
                    const turbo_xml_attribute location =
                        find_attribute(payload_child, "location");
                    scxml_payload_descriptor *payload;
                    if (build->payload_index >= build->payload_capacity)
                        return scxml_fail(
                            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                            turbo_xml_node_location(payload_child),
                            "invoke param emission exceeded admission");
                    payload = &build->payloads[build->payload_index];
                    if (!retain_invocation_view(
                            build, turbo_xml_attribute_value(name),
                            &payload->name, &payload->name_size))
                        return scxml_fail(
                            build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                            turbo_xml_attribute_location(name),
                            "invoke param storage mismatched admission");
                    if (location.impl != NULL)
                        expression_status = compile_cmeta_payload_token(
                            build, turbo_xml_attribute_value(location),
                            turbo_xml_attribute_location(location),
                            "invoke param location", &payload->expression);
                    else
                        expression_status = compile_cmeta_value_program(
                            build, expression, "invoke param",
                            &payload->expression,
                            CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID);
                    if (expression_status != CFLOW_SCXML_OK)
                        return expression_status;
                    ++build->payload_index;
                } else if (payload_kind == SCXML_ELEMENT_CONTENT) {
                    if (find_attribute(payload_child, "expr").impl != NULL) {
                        expression_status = compile_cmeta_content_expression(
                            build, find_attribute(payload_child, "expr"),
                            "invoke content", &descriptor->content,
                            &descriptor->data_expr);
                    } else {
                        expression_status = retain_invocation_inline_content(
                            build, payload_child, &descriptor->content);
                    }
                    if (expression_status != CFLOW_SCXML_OK)
                        return expression_status;
                }
            }
        }
        descriptor->payload_count =
            build->payload_index - descriptor->payload_first;
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
                        turbo_xml_string_view descriptor_base = {NULL, 0u};
                        bool match_all = false;
                        if (!normalize_event_descriptor(
                                token, &descriptor_base, &match_all)) {
                            return scxml_fail(
                                build, CFLOW_SCXML_INVALID_STRUCTURE,
                                turbo_xml_attribute_location(event_attribute),
                                "invalid event descriptor");
                        }
                        if (match_all) continue;
                        build->event_occurrences[build->event_occurrence_index] =
                            (scxml_name_ref){
                                descriptor_base,
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
                   (child_kind == SCXML_ELEMENT_SEND &&
                    find_attribute(child, "event").impl != NULL)) {
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
    bool copy_generated_id, size_t *out_index, bool *out_duplicate) {
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
    if (free_row == NULL || id == NULL || id_size == 0u ||
        (copy_generated_id &&
         id_size > CFLOW_SCXML_EVENT_METADATA_CAPACITY))
        return NULL;
    *free_row = (scxml_delayed_send){0};
    if (copy_generated_id) {
        memcpy(free_row->generated_id, id, id_size);
        free_row->generated_id[id_size] = '\0';
        free_row->id = free_row->generated_id;
    } else {
        free_row->id = id;
    }
    free_row->id_size = id_size;
    free_row->state = SCXML_DELAYED_RESERVED;
    *out_index = free_index;
    return free_row;
}

static void rollback_prepared_effect_locked(scxml_prepared_effect *effect) {
    cflow_scxml_session_impl *session = effect->session;
    if (effect->registry_index != SIZE_MAX &&
        effect->registry_index < session->delayed_send_capacity) {
        scxml_delayed_send *row =
            &session->delayed_sends[effect->registry_index];
        if (effect->kind == SCXML_PREPARED_DELAYED_SEND) {
            if (row->state == SCXML_DELAYED_RESERVED) {
                *row = (scxml_delayed_send){0};
            } else if (row->state == SCXML_DELAYED_CANCEL_RESERVED &&
                       row->previous_state == SCXML_DELAYED_RESERVED) {
                row->previous_state = SCXML_DELAYED_FREE;
            }
        } else if (effect->kind == SCXML_PREPARED_CANCEL &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED) {
            if (row->previous_state == SCXML_DELAYED_FREE) {
                *row = (scxml_delayed_send){0};
            } else {
                row->state = row->previous_state;
                row->previous_state = SCXML_DELAYED_FREE;
            }
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
        } else if (effect->kind == SCXML_PREPARED_DELAYED_SEND &&
                   row->state == SCXML_DELAYED_CANCEL_RESERVED &&
                   row->previous_state == SCXML_DELAYED_RESERVED) {
            row->previous_state = SCXML_DELAYED_ACTIVE;
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
    cflow_scxml_invoke_cancel_request request;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_scxml_adapter_status status;
    const char *adapter_error = NULL;
    scxml_invocation_effect_kind kind;
    uint64_t token = 0u;
    char id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u] = {0};
    const char *cancel_id = id;
    size_t id_size = 0u;
    bool cancel = false;
    bool start = false;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    if (effect->invocation >= session->program->invocation_count) return;
    kind = effect->kind;
    if (kind == SCXML_INVOCATION_EFFECT_START)
        adapter_ticket = effect->adapter_ticket;
    turbo_mutex_lock(&session->registry_lock);
    if (effect->invocation < session->invocation_capacity) {
        scxml_invocation_row *row =
            &session->invocation_rows[effect->invocation];
        if (effect->kind == SCXML_INVOCATION_EFFECT_ENTER) {
            *row = (scxml_invocation_row){
                .state = SCXML_INVOCATION_PENDING};
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_EXIT) {
            if (row->state == SCXML_INVOCATION_ACTIVE) {
                token = row->token;
                cancel = token != 0u;
                id_size = row->id_size;
                if (row->owns_id) {
                    if (id_size != 0u)
                        memcpy(id, row->id, id_size + 1u);
                } else {
                    cancel_id = row->id;
                }
                if (session->invoke_stats.active != 0u)
                    --session->invoke_stats.active;
            }
            *row = (scxml_invocation_row){0};
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_START &&
                   row->state == SCXML_INVOCATION_START_RESERVED &&
                   row->token == effect->token) {
            row->state = SCXML_INVOCATION_ACTIVE;
            increment_u64(&session->invoke_stats.started);
            ++session->invoke_stats.active;
            start = true;
        } else if (effect->kind == SCXML_INVOCATION_EFFECT_FAIL &&
                   row->state == SCXML_INVOCATION_FAIL_RESERVED &&
                   row->token == effect->token) {
            row->state = SCXML_INVOCATION_FAILED;
            increment_u64(&session->invoke_stats.start_failed);
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    if (start) {
        adapter_ticket.commit(adapter_ticket.user);
        return;
    }
    if (kind == SCXML_INVOCATION_EFFECT_START &&
        adapter_ticket.discard != NULL) {
        adapter_ticket.discard(adapter_ticket.user);
        return;
    }
    if (!cancel) return;
    request = (cflow_scxml_invoke_cancel_request){
        .token = token, .id = cancel_id, .id_size = id_size};
    status = (session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
                  ? session->invoke_v3.prepare_cancel
              : session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2
                  ? session->invoke_v2.prepare_cancel
                  : session->invoke.prepare_cancel)(
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
    cflow_statechart_effect_ticket adapter_ticket = {0};
    bool discard_adapter = false;
    if (effect == NULL || !effect->in_use || effect->session == NULL) return;
    session = effect->session;
    turbo_mutex_lock(&session->registry_lock);
    if (effect->invocation < session->invocation_capacity &&
        (effect->kind == SCXML_INVOCATION_EFFECT_START ||
         effect->kind == SCXML_INVOCATION_EFFECT_FAIL)) {
        scxml_invocation_row *row =
            &session->invocation_rows[effect->invocation];
        const scxml_invocation_state reserved =
            effect->kind == SCXML_INVOCATION_EFFECT_START
                ? SCXML_INVOCATION_START_RESERVED
                : SCXML_INVOCATION_FAIL_RESERVED;
        if (row->state == reserved && row->token == effect->token)
            *row = (scxml_invocation_row){
                .state = SCXML_INVOCATION_PENDING};
        if (effect->kind == SCXML_INVOCATION_EFFECT_START) {
            adapter_ticket = effect->adapter_ticket;
            discard_adapter = adapter_ticket.discard != NULL;
        }
    }
    effect->in_use = false;
    turbo_mutex_unlock(&session->registry_lock);
    if (discard_adapter) adapter_ticket.discard(adapter_ticket.user);
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
        effect->kind = step->kind == SCXML_STEP_INVOKE_ENTER
            ? SCXML_INVOCATION_EFFECT_ENTER
            : SCXML_INVOCATION_EFFECT_EXIT;
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

static bool evaluate_runtime_hook_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const cflow_statechart_runtime_hook_context *context =
        (const cflow_statechart_runtime_hook_context *)user;
    if (context == NULL || context->is_active == NULL ||
        context->configuration_user == NULL || out_active == NULL)
        return false;
    *out_active = context->is_active(context->configuration_user, state);
    return true;
}

static bool evaluate_invocation_string(
    const cflow_scxml_cmeta_expr_program *program,
    cflow_scxml_session_impl *session,
    const cflow_statechart_runtime_hook_context *context,
    const char **out_data, size_t *out_size) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_value value = {0};
    if (program == NULL || session == NULL || context == NULL ||
        context->state == NULL || out_data == NULL || out_size == NULL ||
        cflow_scxml_cmeta_expr_evaluate_value_with_system(
            program, context->state, evaluate_runtime_hook_active,
            (void *)context, &session->system_values, &value,
            &diagnostic) != CFLOW_SCXML_CMETA_EXPR_OK ||
        value.kind != CFLOW_SCXML_CMETA_EXPR_VALUE_STRING ||
        value.data.string.size == 0u)
        return false;
    *out_data = value.data.string.data;
    *out_size = value.data.string.size;
    return true;
}

static bool evaluate_invocation_value(
    const cflow_scxml_cmeta_expr_program *program,
    cflow_scxml_session_impl *session,
    const cflow_statechart_runtime_hook_context *context,
    cflow_scxml_payload_value *out) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_value value = {0};
    return program != NULL && session != NULL && context != NULL &&
        context->state != NULL && out != NULL &&
        cflow_scxml_cmeta_expr_evaluate_value_with_system(
            program, context->state, evaluate_runtime_hook_active,
            (void *)context, &session->system_values, &value,
            &diagnostic) == CFLOW_SCXML_CMETA_EXPR_OK &&
        payload_value_from_cmeta(&value, out);
}

static bool materialize_invocation_payload(
    cflow_scxml_session_impl *session,
    const scxml_invocation_descriptor *invocation,
    const cflow_statechart_runtime_hook_context *context,
    cflow_scxml_payload_view *out) {
    size_t index;
    if (session == NULL || session->program == NULL ||
        invocation == NULL || context == NULL || out == NULL ||
        invocation->payload_first > session->program->payload_count ||
        invocation->payload_count >
            session->program->payload_count - invocation->payload_first ||
        invocation->payload_count > session->payload_scratch_capacity ||
        (invocation->payload_count != 0u &&
         (session->program->payloads == NULL ||
          session->payload_scratch == NULL)))
        return false;
    *out = (cflow_scxml_payload_view){0};
    if (invocation->content.kind == CFLOW_SCXML_CONTENT_SCALAR) {
        out->kind = CFLOW_SCXML_PAYLOAD_CONTENT;
        return evaluate_invocation_value(
            &invocation->data_expr, session, context, &out->content);
    }
    if (invocation->payload_count == 0u) return true;
    for (index = 0u; index < invocation->payload_count; ++index) {
        const scxml_payload_descriptor *descriptor =
            &session->program->payloads[
                invocation->payload_first + index];
        cflow_scxml_payload_entry *entry =
            &session->payload_scratch[index];
        if (!evaluate_invocation_value(
                &descriptor->expression, session, context,
                &entry->value))
            return false;
        entry->name = descriptor->name;
        entry->name_size = descriptor->name_size;
    }
    out->kind = CFLOW_SCXML_PAYLOAD_NAMED;
    out->entries = session->payload_scratch;
    out->entry_count = invocation->payload_count;
    return true;
}

static bool materialize_invocation_payload_v3(
    cflow_scxml_session_impl *session,
    const scxml_invocation_descriptor *invocation,
    const cflow_statechart_runtime_hook_context *context,
    cflow_scxml_payload_view_v3 *out) {
    cflow_scxml_payload_view scalar = {0};
    if (session == NULL || invocation == NULL || context == NULL ||
        out == NULL)
        return false;
    *out = (cflow_scxml_payload_view_v3){0};
    if (invocation->content.kind != CFLOW_SCXML_CONTENT_INVALID &&
        invocation->content.kind != CFLOW_SCXML_CONTENT_SCALAR) {
        out->kind = CFLOW_SCXML_PAYLOAD_CONTENT;
        return materialize_content_descriptor(
            &invocation->content, context->state, &out->content);
    }
    return materialize_invocation_payload(
               session, invocation, context, &scalar) &&
           payload_v2_to_v3(session, &scalar, out);
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
        cflow_scxml_invoke_start_request_v2 request_v2 = {0};
        cflow_scxml_invoke_start_request_v3 request_v3 = {0};
        cflow_scxml_payload_view payload = {0};
        cflow_scxml_payload_view_v3 payload_v3 = {0};
        cflow_statechart_effect_ticket adapter_ticket = {0};
        cflow_scxml_adapter_status status;
        const char *adapter_error = NULL;
        const char *dynamic_type = descriptor->type;
        size_t dynamic_type_size = descriptor->type_size;
        const char *dynamic_src = descriptor->src;
        size_t dynamic_src_size = descriptor->src_size;
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
        turbo_mutex_unlock(&session->registry_lock);
        if ((descriptor->has_type_expr &&
             !evaluate_invocation_string(
                 &descriptor->type_expr, session, context,
                 &dynamic_type, &dynamic_type_size)) ||
            (descriptor->has_src_expr &&
             !evaluate_invocation_string(
                 &descriptor->src_expr, session, context,
                 &dynamic_src, &dynamic_src_size)) ||
            ((descriptor->content.kind != CFLOW_SCXML_CONTENT_INVALID ||
              descriptor->payload_count != 0u) &&
             !(session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
                   ? materialize_invocation_payload_v3(
                         session, descriptor, context, &payload_v3)
                   : materialize_invocation_payload(
                         session, descriptor, context, &payload)))) {
            turbo_mutex_lock(&session->registry_lock);
            session->invocation_rows[index].state =
                SCXML_INVOCATION_FAILED;
            increment_u64(&session->invoke_stats.start_failed);
            turbo_mutex_unlock(&session->registry_lock);
            if (!enqueue_invocation_adapter_error(
                    session, context,
                    CFLOW_SCXML_ADAPTER_ERROR_EXECUTION, out_error))
                return false;
            continue;
        }
        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state !=
                SCXML_INVOCATION_PENDING) {
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
            .token = token,
            .id = descriptor->id,
            .id_size = descriptor->id_size,
            .state = SCXML_INVOCATION_START_RESERVED};
        turbo_mutex_unlock(&session->registry_lock);

        request = (cflow_scxml_invoke_start_request){
            .token = token,
            .id = descriptor->id,
            .id_size = descriptor->id_size,
            .type = dynamic_type,
            .type_size = dynamic_type_size,
            .src = dynamic_src,
            .src_size = dynamic_src_size,
            .autoforward = descriptor->autoforward};
        if (session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3) {
            request_v3.base = request;
            request_v3.payload = payload_v3;
            status = session->invoke_v3.prepare_start(
                session->invoke_user, &request_v3, &adapter_ticket,
                &adapter_error);
        } else if (session->invoke_abi ==
                   CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2) {
            request_v2.base = request;
            request_v2.payload = payload;
            status = session->invoke_v2.prepare_start(
                session->invoke_user, &request_v2, &adapter_ticket,
                &adapter_error);
        } else {
            if (payload.kind != CFLOW_SCXML_PAYLOAD_NONE) {
                status = CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
                adapter_error =
                    "SCXML invocation payload requires a v2 adapter";
            } else {
                status = session->invoke.prepare_start(
                    session->invoke_user, &request, &adapter_ticket,
                    &adapter_error);
            }
        }
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

static bool restore_invocation_id_location(
    const scxml_invocation_descriptor *descriptor,
    const void *published_state, void *staged_state,
    const char *id, size_t id_size, bool *out_restored) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    const unsigned char *previous = NULL;
    size_t previous_size = 0u;
    unsigned char *destination;
    cmeta_status restore_status;
    if (out_restored != NULL) *out_restored = true;
    if (descriptor == NULL || published_state == NULL ||
        staged_state == NULL || id == NULL || out_restored == NULL ||
        !descriptor->has_id_location ||
        descriptor->id_location.value == NULL)
        return false;
    if (cmeta_data_buffer_read(
            descriptor->id_location.value,
            (const unsigned char *)published_state +
                descriptor->id_location.offset,
            CFLOW_SCXML_EVENT_METADATA_CAPACITY,
            &previous, &previous_size) != CMETA_OK)
        return false;
    if (cflow_scxml_cmeta_location_assign_owned_string(
            &descriptor->id_location, staged_state, id, id_size,
            CFLOW_SCXML_EVENT_METADATA_CAPACITY,
            &diagnostic) == CFLOW_SCXML_CMETA_EXPR_OK)
        return true;
    destination = (unsigned char *)staged_state +
        descriptor->id_location.offset;
    if (previous_size == 0u) {
        restore_status = cmeta_data_buffer_restore_zero(
            descriptor->id_location.value, destination);
        *out_restored = restore_status == CMETA_OK;
    } else {
        *out_restored =
            cflow_scxml_cmeta_location_assign_owned_string(
                &descriptor->id_location, staged_state,
                (const char *)previous, previous_size,
                CFLOW_SCXML_EVENT_METADATA_CAPACITY,
                &diagnostic) == CFLOW_SCXML_CMETA_EXPR_OK;
    }
    return false;
}

static bool stage_invocation_result(
    cflow_scxml_session_impl *session, size_t invocation,
    uint64_t token, const char *id, size_t id_size, bool own_id,
    scxml_invocation_effect_kind kind,
    const cflow_statechart_effect_ticket *adapter_ticket,
    const cflow_statechart_stable_transaction_context *context,
    const char **out_error) {
    scxml_invocation_lifecycle_effect *effect;
    cflow_statechart_effect_ticket ticket;
    scxml_invocation_row *row;
    const bool owns_adapter_ticket =
        kind == SCXML_INVOCATION_EFFECT_START && adapter_ticket != NULL &&
        adapter_ticket->discard != NULL;
    if (session == NULL || invocation >= session->invocation_capacity ||
        token == 0u ||
        (own_id && id_size > CFLOW_SCXML_EVENT_METADATA_CAPACITY) ||
        (id_size != 0u && id == NULL) ||
        (kind != SCXML_INVOCATION_EFFECT_START &&
         kind != SCXML_INVOCATION_EFFECT_FAIL) ||
        context == NULL || context->stage_effect == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation result reservation is invalid";
        if (owns_adapter_ticket)
            adapter_ticket->discard(adapter_ticket->user);
        return false;
    }
    turbo_mutex_lock(&session->registry_lock);
    row = &session->invocation_rows[invocation];
    effect = row->state == SCXML_INVOCATION_PENDING
        ? acquire_invocation_effect_locked(session) : NULL;
    if (effect != NULL) {
        *row = (scxml_invocation_row){
            .token = token,
            .id_size = id_size,
            .owns_id = own_id,
            .state = kind == SCXML_INVOCATION_EFFECT_START
                ? SCXML_INVOCATION_START_RESERVED
                : SCXML_INVOCATION_FAIL_RESERVED};
        if (own_id) {
            if (id_size != 0u) memcpy(row->owned_id, id, id_size);
            row->owned_id[id_size] = '\0';
            row->id = row->owned_id;
        } else {
            row->id = id;
        }
        effect->invocation = invocation;
        effect->token = token;
        effect->kind = kind;
        if (adapter_ticket != NULL)
            effect->adapter_ticket = *adapter_ticket;
    }
    turbo_mutex_unlock(&session->registry_lock);
    if (effect == NULL) {
        *out_error = "SCXML invocation effect storage is full";
        if (owns_adapter_ticket)
            adapter_ticket->discard(adapter_ticket->user);
        return false;
    }
    ticket = (cflow_statechart_effect_ticket){
        commit_invocation_lifecycle, discard_invocation_lifecycle, effect};
    if (!context->stage_effect(context->effect_user, &ticket, out_error)) {
        discard_invocation_lifecycle(effect);
        return false;
    }
    return true;
}

static cflow_statechart_stable_transaction_result
start_stable_invocations_transaction(
    void *user,
    const cflow_statechart_stable_transaction_context *context,
    const char **out_error) {
    cflow_scxml_session_impl *session = (cflow_scxml_session_impl *)user;
    cflow_statechart_runtime_hook_context evaluation;
    size_t index;
    bool changed = false;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || context == NULL ||
        context->published_state == NULL || context->staged_state == NULL ||
        context->is_active == NULL || context->configuration_user == NULL ||
        context->raise_internal == NULL || context->stage_effect == NULL ||
        out_error == NULL) {
        if (out_error != NULL)
            *out_error = "SCXML invocation stable transaction is invalid";
        return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
    }
    evaluation = (cflow_statechart_runtime_hook_context){
        .state = context->staged_state,
        .configuration_version = context->configuration_version,
        .is_active = context->is_active,
        .configuration_user = context->configuration_user,
        .enqueue_internal = context->raise_internal,
        .enqueue_user = context->raise_user};
    for (index = 0u; index < session->program->invocation_count; ++index) {
        const scxml_invocation_descriptor *descriptor =
            &session->program->invocations[index];
        cflow_scxml_invoke_start_request request;
        cflow_scxml_invoke_start_request_v2 request_v2 = {0};
        cflow_scxml_invoke_start_request_v3 request_v3 = {0};
        cflow_scxml_payload_view payload = {0};
        cflow_scxml_payload_view_v3 payload_v3 = {0};
        cflow_statechart_effect_ticket adapter_ticket = {0};
        cflow_scxml_adapter_status status;
        const char *adapter_error = NULL;
        const char *dynamic_type = descriptor->type;
        size_t dynamic_type_size = descriptor->type_size;
        const char *dynamic_src = descriptor->src;
        size_t dynamic_src_size = descriptor->src_size;
        char generated_id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
        const char *id = descriptor->id;
        size_t id_size = descriptor->id_size;
        uint64_t token;
        int written;
        bool restored = true;

        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state !=
                SCXML_INVOCATION_PENDING) {
            turbo_mutex_unlock(&session->registry_lock);
            continue;
        }
        turbo_mutex_unlock(&session->registry_lock);
        if (!context->is_active(
                context->configuration_user, descriptor->owner))
            continue;

        turbo_mutex_lock(&session->registry_lock);
        token = session->next_invocation_token;
        if (token != 0u)
            session->next_invocation_token =
                token == UINT64_MAX ? 0u : token + 1u;
        turbo_mutex_unlock(&session->registry_lock);
        if (token == 0u) {
            *out_error = "SCXML invocation token space is exhausted";
            return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
        }
        if (descriptor->has_id_location) {
            written = snprintf(
                generated_id, sizeof(generated_id), "%.*s.%" PRIu64,
                (int)descriptor->owner_id_size, descriptor->id, token);
            if (written < 0 || (size_t)written >= sizeof(generated_id) ||
                (size_t)written > descriptor->dynamic_id_max_size) {
                *out_error = "SCXML dynamic invocation ID exceeds its bound";
                return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
            }
            id = generated_id;
            id_size = (size_t)written;
            if (!restore_invocation_id_location(
                    descriptor, context->published_state,
                    context->staged_state, id, id_size, &restored)) {
                if (!restored) {
                    *out_error =
                        "SCXML invocation idlocation rollback failed";
                    return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
                }
                if (!stage_invocation_result(
                        session, index, token, NULL, 0u, true,
                        SCXML_INVOCATION_EFFECT_FAIL, NULL,
                        context, out_error) ||
                    !enqueue_invocation_adapter_error(
                        session, &evaluation,
                        CFLOW_SCXML_ADAPTER_ERROR_EXECUTION, out_error))
                    return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
                changed = true;
                continue;
            }
        }
        if ((descriptor->has_type_expr &&
             !evaluate_invocation_string(
                 &descriptor->type_expr, session, &evaluation,
                 &dynamic_type, &dynamic_type_size)) ||
            (descriptor->has_src_expr &&
             !evaluate_invocation_string(
                 &descriptor->src_expr, session, &evaluation,
                 &dynamic_src, &dynamic_src_size)) ||
            ((descriptor->content.kind != CFLOW_SCXML_CONTENT_INVALID ||
              descriptor->payload_count != 0u) &&
             !(session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
                   ? materialize_invocation_payload_v3(
                         session, descriptor, &evaluation, &payload_v3)
                   : materialize_invocation_payload(
                         session, descriptor, &evaluation, &payload)))) {
            if (!stage_invocation_result(
                    session, index, token, id, id_size,
                    descriptor->has_id_location,
                    SCXML_INVOCATION_EFFECT_FAIL, NULL,
                    context, out_error) ||
                !enqueue_invocation_adapter_error(
                    session, &evaluation,
                    CFLOW_SCXML_ADAPTER_ERROR_EXECUTION, out_error))
                return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
            changed = true;
            continue;
        }
        request = (cflow_scxml_invoke_start_request){
            .token = token,
            .id = id,
            .id_size = id_size,
            .type = dynamic_type,
            .type_size = dynamic_type_size,
            .src = dynamic_src,
            .src_size = dynamic_src_size,
            .autoforward = descriptor->autoforward};
        if (session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3) {
            request_v3.base = request;
            request_v3.payload = payload_v3;
            status = session->invoke_v3.prepare_start(
                session->invoke_user, &request_v3, &adapter_ticket,
                &adapter_error);
        } else if (session->invoke_abi ==
                   CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2) {
            request_v2.base = request;
            request_v2.payload = payload;
            status = session->invoke_v2.prepare_start(
                session->invoke_user, &request_v2, &adapter_ticket,
                &adapter_error);
        } else if (payload.kind != CFLOW_SCXML_PAYLOAD_NONE) {
            status = CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
            adapter_error = "SCXML invocation payload requires a v2 adapter";
        } else {
            status = session->invoke.prepare_start(
                session->invoke_user, &request, &adapter_ticket,
                &adapter_error);
        }
        if (status == CFLOW_SCXML_ADAPTER_ACCEPTED &&
            adapter_ticket.commit != NULL && adapter_ticket.discard != NULL) {
            if (!stage_invocation_result(
                    session, index, token, id, id_size,
                    descriptor->has_id_location,
                    SCXML_INVOCATION_EFFECT_START, &adapter_ticket,
                    context, out_error))
                return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
            changed = true;
            continue;
        }
        if (status == CFLOW_SCXML_ADAPTER_INVALID_CONTRACT ||
            status == CFLOW_SCXML_ADAPTER_ACCEPTED) {
            *out_error = adapter_error != NULL && adapter_error[0] != '\0'
                ? adapter_error
                : "SCXML invocation adapter returned an invalid start ticket";
            return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
        }
        if (!stage_invocation_result(
                session, index, token, id, id_size,
                descriptor->has_id_location,
                SCXML_INVOCATION_EFFECT_FAIL, NULL,
                context, out_error) ||
            !enqueue_invocation_adapter_error(
                session, &evaluation, status, out_error))
            return CFLOW_STATECHART_STABLE_TRANSACTION_FATAL;
        changed = true;
    }
    return changed ? CFLOW_STATECHART_STABLE_TRANSACTION_COMMIT
                   : CFLOW_STATECHART_STABLE_TRANSACTION_NOOP;
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
        char id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u] = {0};
        const char *request_id = NULL;
        size_t id_size = 0u;
        if (!descriptor->autoforward) continue;
        turbo_mutex_lock(&session->registry_lock);
        if (session->invocation_rows[index].state ==
                SCXML_INVOCATION_ACTIVE) {
            token = session->invocation_rows[index].token;
            id_size = session->invocation_rows[index].id_size;
            if (session->invocation_rows[index].owns_id) {
                if (id_size != 0u)
                    memcpy(id, session->invocation_rows[index].id,
                           id_size + 1u);
                request_id = id;
            } else {
                request_id = session->invocation_rows[index].id;
            }
        }
        turbo_mutex_unlock(&session->registry_lock);
        if (token == 0u) continue;
        request = (cflow_scxml_invoke_forward_request){
            .token = token,
            .id = request_id,
            .id_size = id_size,
            .event = event};
        status = (session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
                      ? session->invoke_v3.prepare_forward
                  : session->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2
                      ? session->invoke_v2.prepare_forward
                      : session->invoke.prepare_forward)(
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

static const scxml_program_name *find_state_program_name_by_id(
    const cflow_scxml_program_impl *program, cflow_machine_state_id id) {
    size_t index;
    if (program == NULL || id == 0u) return NULL;
    for (index = 0u; index < program->state_name_count; ++index) {
        if (program->state_names[index].id == id)
            return &program->state_names[index];
    }
    return NULL;
}

static bool copy_event_data_object(const cmeta_data_desc *schema,
                                   void *destination,
                                   const void *source) {
    const cmeta_type_desc *type;
    if (!cmeta_data_desc_valid(schema) || schema->kind != CMETA_DATA_STRUCT ||
        schema->storage_type == NULL || destination == NULL || source == NULL)
        return false;
    type = schema->storage_type;
    if (type->size > CFLOW_SCXML_EVENT_DATA_CAPACITY ||
        type->align > _Alignof(scxml_event_data_storage))
        return false;
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_TRIVIAL_COPY |
                      CMETA_TRAIT_TRIVIAL_DESTROY) == CMETA_OK) {
        memcpy(destination, source, type->size);
        return true;
    }
    return cmeta_type_require_traits(
               type, CMETA_TRAIT_COPY | CMETA_TRAIT_DESTROY) == CMETA_OK &&
        type->traits->copy_construct(destination, source);
}

static void destroy_event_data_object(const cmeta_data_desc *schema,
                                      void *object) {
    const cmeta_type_desc *type = schema != NULL ? schema->storage_type : NULL;
    if (type == NULL || object == NULL) return;
    if (cmeta_type_require_traits(
            type, CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK &&
        cmeta_type_require_traits(type, CMETA_TRAIT_DESTROY) == CMETA_OK)
        type->traits->destroy(object);
}

static void clear_current_event_metadata(cflow_scxml_session_impl *session) {
    static const cflow_scxml_cmeta_expr_string_view empty = {"", 0u};
    if (session->current_event_data_object_live) {
        destroy_event_data_object(
            session->current_event_data_schema,
            session->current_event_data_object.bytes);
        session->current_event_data_object_live = false;
    }
    session->current_event_data_schema = NULL;
    session->system_values.event_data_schema = NULL;
    session->system_values.event_data_object = NULL;
    session->system_values.event_send_id = empty;
    session->system_values.event_origin = empty;
    session->system_values.event_origin_type = empty;
    session->system_values.event_invoke_id = empty;
    session->system_values.event_data = empty;
}

typedef struct scxml_active_query {
    cflow_statechart_is_active_fn function;
    void *user;
} scxml_active_query;

static bool evaluate_hook_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    const scxml_active_query *query = (const scxml_active_query *)user;
    if (query == NULL || query->function == NULL || out_active == NULL)
        return false;
    *out_active = query->function(query->user, state);
    return true;
}

static bool bind_completion_done_data(
    cflow_scxml_session_impl *session,
    const cflow_statechart_runtime_hook_context *context,
    cflow_machine_state_id completion, const char **out_error) {
    const scxml_active_query active_query = {
        context != NULL ? context->is_active : NULL,
        context != NULL ? context->configuration_user : NULL};
    size_t index;
    for (index = 0u; index < session->program->done_data_count; ++index) {
        const scxml_done_data_descriptor *descriptor =
            &session->program->done_data[index];
        cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
        cflow_scxml_cmeta_expr_value value = {0};
        const char *data = NULL;
        size_t data_size = 0u;
        bool active;
        if (descriptor->parent != completion) continue;
        if (context == NULL || context->state == NULL ||
            context->is_active == NULL) {
            *out_error = "SCXML donedata active-state query failed";
            return false;
        }
        active = context->is_active(
            context->configuration_user, descriptor->final_state);
        if (!active) continue;
        if (descriptor->content.kind == CFLOW_SCXML_CONTENT_SCALAR) {
            if (cflow_scxml_cmeta_expr_evaluate_value_with_system(
                    &descriptor->expression, context->state,
                    evaluate_hook_active, (void *)&active_query,
                    &session->system_values, &value, &diagnostic) !=
                    CFLOW_SCXML_CMETA_EXPR_OK ||
                !scalar_value_to_text(
                    &value, session->current_event_data,
                    sizeof(session->current_event_data), &data, &data_size)) {
                const bool payload = false;
                const cflow_event_view execution_error = {
                    session->program->execution_error_event,
                    &cmeta_type_bool, &payload};
                if (session->program->execution_error_event == 0u ||
                    context->enqueue_internal == NULL ||
                    !context->enqueue_internal(
                        context->enqueue_user, &execution_error, out_error)) {
                    if (*out_error == NULL)
                        *out_error = "SCXML donedata expression failed";
                    return false;
                }
                return true;
            }
        } else if (descriptor->content.kind ==
                       CFLOW_SCXML_CONTENT_TEXT_UTF8 ||
                   descriptor->content.kind ==
                       CFLOW_SCXML_CONTENT_XML_UTF8) {
            data = descriptor->content.bytes;
            data_size = descriptor->content.byte_count;
        } else {
            const bool payload = false;
            const cflow_event_view execution_error = {
                session->program->execution_error_event,
                &cmeta_type_bool, &payload};
            if (session->program->execution_error_event == 0u ||
                context->enqueue_internal == NULL ||
                !context->enqueue_internal(
                    context->enqueue_user, &execution_error, out_error)) {
                if (*out_error == NULL)
                    *out_error = "SCXML donedata expression failed";
                return false;
            }
            return true;
        }
        if (data != session->current_event_data && data_size != 0u)
            memmove(session->current_event_data, data, data_size);
        session->current_event_data[data_size] = '\0';
        session->system_values.event_data =
            (cflow_scxml_cmeta_expr_string_view){
                session->current_event_data, data_size};
        return true;
    }
    return true;
}

static bool observe_scxml_event(
    void *user, const cflow_statechart_runtime_hook_context *context,
    const cflow_statechart_observed_event *event, const char **out_error) {
    static const char external_type[] = "external";
    static const char internal_type[] = "internal";
    static const char platform_type[] = "platform";
    cflow_scxml_session_impl *session = (cflow_scxml_session_impl *)user;
    const scxml_program_name *name = NULL;
    size_t index;
    if (out_error != NULL) *out_error = NULL;
    if (session == NULL || event == NULL || out_error == NULL) {
        if (out_error != NULL) *out_error = "SCXML Event observation is invalid";
        return false;
    }
    clear_current_event_metadata(session);
    if (event->kind == CFLOW_STATECHART_OBSERVED_COMPLETION) {
        const scxml_program_name *state = find_state_program_name_by_id(
            session->program, event->completion);
        int written;
        if (state == NULL) {
            session->system_values.event_name =
                (cflow_scxml_cmeta_expr_string_view){"", 0u};
            session->system_values.event_type =
                (cflow_scxml_cmeta_expr_string_view){
                    internal_type, sizeof(internal_type) - 1u};
            return true;
        }
        if (state->size > sizeof(session->current_event_name) -
                              sizeof("done.state.")) {
            *out_error = "SCXML completion Event name exceeds metadata bound";
            return false;
        }
        written = snprintf(
            session->current_event_name,
            sizeof(session->current_event_name), "done.state.%.*s",
            (int)state->size, state->name);
        if (written < 0 ||
            (size_t)written >= sizeof(session->current_event_name)) {
            *out_error = "SCXML completion Event name exceeds metadata bound";
            return false;
        }
        session->system_values.event_name =
            (cflow_scxml_cmeta_expr_string_view){
                session->current_event_name, (size_t)written};
        session->system_values.event_type =
            (cflow_scxml_cmeta_expr_string_view){
                internal_type, sizeof(internal_type) - 1u};
        return bind_completion_done_data(
            session, context, event->completion, out_error);
    }
    if (event->event == NULL || event->event->id == 0u ||
        event->event->id > session->program->event_name_count) {
        *out_error = "SCXML observed Event is outside the program map";
        return false;
    }
    name = session->program->event_names_by_id[event->event->id - 1u];
    if (name == NULL) {
        *out_error = "SCXML observed Event name is unavailable";
        return false;
    }
    session->system_values.event_name =
        (cflow_scxml_cmeta_expr_string_view){name->name, name->size};
    if (event->kind == CFLOW_STATECHART_OBSERVED_EXTERNAL) {
        session->system_values.event_type =
            (cflow_scxml_cmeta_expr_string_view){
                external_type, sizeof(external_type) - 1u};
    } else if (event->event->id == session->program->execution_error_event ||
               event->event->id ==
                   session->program->communication_error_event) {
        session->system_values.event_type =
            (cflow_scxml_cmeta_expr_string_view){
                platform_type, sizeof(platform_type) - 1u};
    } else {
        session->system_values.event_type =
            (cflow_scxml_cmeta_expr_string_view){
                internal_type, sizeof(internal_type) - 1u};
    }

    if (event->source_token == 0u ||
        (event->kind != CFLOW_STATECHART_OBSERVED_EXTERNAL &&
         (event->source_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) == 0u))
        return true;
    turbo_mutex_lock(&session->registry_lock);
    if ((event->source_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) != 0u) {
        scxml_external_event_metadata_row *row = NULL;
        bool row_data_live;
        for (index = 0u; index < session->external_metadata_capacity; ++index) {
            if (session->external_metadata_rows[index].in_use &&
                session->external_metadata_rows[index].token ==
                    event->source_token) {
                row = &session->external_metadata_rows[index];
                break;
            }
        }
        if (row == NULL) {
            turbo_mutex_unlock(&session->registry_lock);
            *out_error = "SCXML external Event metadata token is stale";
            return false;
        }
#define SCXML_COPY_CURRENT(field)                                           \
        do {                                                                \
            if (row->field##_size != 0u)                                    \
                memcpy(session->current_event_##field, row->field,           \
                       row->field##_size);                                   \
            session->current_event_##field[row->field##_size] = '\0';       \
            session->system_values.event_##field =                          \
                (cflow_scxml_cmeta_expr_string_view){                        \
                    session->current_event_##field, row->field##_size};      \
        } while (0)
        SCXML_COPY_CURRENT(send_id);
        SCXML_COPY_CURRENT(origin);
        SCXML_COPY_CURRENT(origin_type);
        SCXML_COPY_CURRENT(invoke_id);
        SCXML_COPY_CURRENT(data);
#undef SCXML_COPY_CURRENT
        row_data_live = row->data_object_live;
        if (!row_data_live) {
            memset(row, 0, sizeof(*row));
            turbo_mutex_unlock(&session->registry_lock);
            return true;
        }
        row->in_use = false;
        turbo_mutex_unlock(&session->registry_lock);
        if (!copy_event_data_object(
                row->data_schema,
                session->current_event_data_object.bytes,
                row->data_object.bytes)) {
            destroy_event_data_object(
                row->data_schema, row->data_object.bytes);
            turbo_mutex_lock(&session->registry_lock);
            memset(row, 0, sizeof(*row));
            turbo_mutex_unlock(&session->registry_lock);
            *out_error = "SCXML structured Event data copy failed";
            return false;
        }
        session->current_event_data_schema = row->data_schema;
        session->current_event_data_object_live = true;
        session->system_values.event_data =
            (cflow_scxml_cmeta_expr_string_view){NULL, 0u};
        session->system_values.event_data_schema = row->data_schema;
        session->system_values.event_data_object =
            session->current_event_data_object.bytes;
        destroy_event_data_object(row->data_schema, row->data_object.bytes);
        turbo_mutex_lock(&session->registry_lock);
        memset(row, 0, sizeof(*row));
        turbo_mutex_unlock(&session->registry_lock);
        return true;
    } else {
        for (index = 0u; index < session->program->invocation_count; ++index) {
            if (session->invocation_rows[index].state ==
                    SCXML_INVOCATION_ACTIVE &&
                session->invocation_rows[index].token ==
                    event->source_token) {
                static const char done_prefix[] = "done.invoke.";
                const scxml_invocation_descriptor *descriptor =
                    &session->program->invocations[index];
                const scxml_invocation_row *invocation =
                    &session->invocation_rows[index];
                if (invocation->id_size >
                    CFLOW_SCXML_EVENT_METADATA_CAPACITY) {
                    turbo_mutex_unlock(&session->registry_lock);
                    *out_error = "SCXML invoke ID exceeds metadata bound";
                    return false;
                }
                memcpy(session->current_event_invoke_id, invocation->id,
                       invocation->id_size);
                session->current_event_invoke_id[invocation->id_size] = '\0';
                session->system_values.event_invoke_id =
                    (cflow_scxml_cmeta_expr_string_view){
                        session->current_event_invoke_id,
                        invocation->id_size};
                if (descriptor->has_id_location &&
                    event->event->id == descriptor->done_event) {
                    size_t dynamic_name_size;
                    if (!checked_add(
                            sizeof(done_prefix) - 1u,
                            invocation->id_size, &dynamic_name_size) ||
                        dynamic_name_size >
                            CFLOW_SCXML_EVENT_METADATA_CAPACITY) {
                        turbo_mutex_unlock(&session->registry_lock);
                        *out_error =
                            "SCXML dynamic done Event exceeds metadata bound";
                        return false;
                    }
                    memcpy(session->current_event_name, done_prefix,
                           sizeof(done_prefix) - 1u);
                    memcpy(session->current_event_name +
                               sizeof(done_prefix) - 1u,
                           invocation->id, invocation->id_size);
                    session->current_event_name[dynamic_name_size] = '\0';
                    session->system_values.event_name =
                        (cflow_scxml_cmeta_expr_string_view){
                            session->current_event_name,
                            dynamic_name_size};
                }
                break;
            }
        }
    }
    turbo_mutex_unlock(&session->registry_lock);
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
    if (source_token != 0u &&
        (source_token & SCXML_EXTERNAL_METADATA_TOKEN_BIT) == 0u) {
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

static bool evaluate_cmeta_executable_active(
    void *user, cflow_machine_state_id state, bool *out_active);

static bool scxml_metadata_field_valid(const char *data, size_t size) {
    return size <= CFLOW_SCXML_EVENT_METADATA_CAPACITY &&
        (size == 0u || data != NULL);
}

static scxml_external_event_metadata_row *reserve_event_metadata(
    cflow_scxml_session_impl *session,
    const cflow_scxml_event_metadata *metadata,
    uint64_t *out_token) {
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token;
    size_t index;
    if (session == NULL || metadata == NULL || out_token == NULL ||
        !scxml_metadata_field_valid(metadata->send_id,
                                    metadata->send_id_size) ||
        !scxml_metadata_field_valid(metadata->origin,
                                    metadata->origin_size) ||
        !scxml_metadata_field_valid(metadata->origin_type,
                                    metadata->origin_type_size) ||
        !scxml_metadata_field_valid(metadata->invoke_id,
                                    metadata->invoke_id_size) ||
        !scxml_metadata_field_valid(metadata->data, metadata->data_size))
        return NULL;
    turbo_mutex_lock(&session->registry_lock);
    for (index = 0u; index < session->external_metadata_capacity; ++index) {
        if (!session->external_metadata_rows[index].in_use &&
            !session->external_metadata_rows[index].data_object_live) {
            row = &session->external_metadata_rows[index];
            break;
        }
    }
    if (row == NULL) {
        turbo_mutex_unlock(&session->registry_lock);
        return NULL;
    }
    token = session->next_external_metadata_token;
    session->next_external_metadata_token =
        SCXML_EXTERNAL_METADATA_TOKEN_BIT |
        (((token & ~SCXML_EXTERNAL_METADATA_TOKEN_BIT) + 1u) &
         ~SCXML_EXTERNAL_METADATA_TOKEN_BIT);
    if (session->next_external_metadata_token ==
        SCXML_EXTERNAL_METADATA_TOKEN_BIT)
        session->next_external_metadata_token |= UINT64_C(1);
    row->session = session;
    row->token = token;
    row->in_use = true;
#define SCXML_RETAIN_METADATA(field)                                      \
    do {                                                                  \
        row->field##_size = metadata->field##_size;                       \
        if (metadata->field##_size != 0u)                                 \
            memcpy(row->field, metadata->field, metadata->field##_size);  \
        row->field[metadata->field##_size] = '\0';                        \
    } while (0)
    SCXML_RETAIN_METADATA(send_id);
    SCXML_RETAIN_METADATA(origin);
    SCXML_RETAIN_METADATA(origin_type);
    SCXML_RETAIN_METADATA(invoke_id);
    SCXML_RETAIN_METADATA(data);
#undef SCXML_RETAIN_METADATA
    turbo_mutex_unlock(&session->registry_lock);
    *out_token = token;
    return row;
}

static void release_event_metadata(void *user) {
    scxml_external_event_metadata_row *row =
        (scxml_external_event_metadata_row *)user;
    cflow_scxml_session_impl *session = row != NULL ? row->session : NULL;
    if (session == NULL) return;
    bool data_live;
    const cmeta_data_desc *schema;
    turbo_mutex_lock(&session->registry_lock);
    if (!row->in_use) {
        turbo_mutex_unlock(&session->registry_lock);
        return;
    }
    data_live = row->data_object_live;
    if (!data_live) {
        memset(row, 0, sizeof(*row));
        turbo_mutex_unlock(&session->registry_lock);
        return;
    }
    row->in_use = false;
    schema = row->data_schema;
    turbo_mutex_unlock(&session->registry_lock);
    destroy_event_data_object(schema, row->data_object.bytes);
    turbo_mutex_lock(&session->registry_lock);
    memset(row, 0, sizeof(*row));
    turbo_mutex_unlock(&session->registry_lock);
}

static void commit_event_metadata(void *user) {
    (void)user;
}

static scxml_execute_outcome raise_block_execution_error(
    const scxml_block *block,
    const cflow_statechart_executable_context *context,
    const char **out_error);

static bool evaluate_effect_value(
    const cflow_scxml_cmeta_expr_program *program,
    const cflow_statechart_executable_context *context,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_value *out) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    return cflow_scxml_cmeta_expr_evaluate_value_with_system(
               program, context->out_state,
               evaluate_cmeta_executable_active, (void *)context,
               system_values, out, &diagnostic) ==
           CFLOW_SCXML_CMETA_EXPR_OK;
}

static bool payload_value_from_cmeta(
    const cflow_scxml_cmeta_expr_value *source,
    cflow_scxml_payload_value *destination) {
    if (source == NULL || destination == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    switch (source->kind) {
        case CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL:
            destination->kind = CFLOW_SCXML_PAYLOAD_VALUE_BOOL;
            destination->data.boolean = source->data.boolean;
            return true;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_SINT:
            destination->kind = CFLOW_SCXML_PAYLOAD_VALUE_SINT;
            destination->data.sint = source->data.sint;
            return true;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_UINT:
            destination->kind = CFLOW_SCXML_PAYLOAD_VALUE_UINT;
            destination->data.uint = source->data.uint;
            return true;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT:
            destination->kind = CFLOW_SCXML_PAYLOAD_VALUE_FLOAT;
            destination->data.number = source->data.number;
            return true;
        case CFLOW_SCXML_CMETA_EXPR_VALUE_STRING:
            destination->kind = CFLOW_SCXML_PAYLOAD_VALUE_STRING;
            destination->data.string.data = source->data.string.data;
            destination->data.string.size = source->data.string.size;
            return source->data.string.data != NULL ||
                   source->data.string.size == 0u;
        default: return false;
    }
}

static bool materialize_effect_named_payload(
    const scxml_block *block, cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *effect,
    const cflow_statechart_executable_context *context,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_payload_view *out) {
    size_t index;
    if (block == NULL || session == NULL || effect == NULL ||
        context == NULL || out == NULL ||
        effect->payload_first > block->payload_storage_count ||
        effect->payload_count >
            block->payload_storage_count - effect->payload_first ||
        effect->payload_count > session->payload_scratch_capacity ||
        (effect->payload_count != 0u &&
         (block->payloads == NULL || session->payload_scratch == NULL)))
        return false;
    *out = (cflow_scxml_payload_view){0};
    if (effect->payload_count == 0u) return true;
    for (index = 0u; index < effect->payload_count; ++index) {
        const scxml_payload_descriptor *descriptor =
            &block->payloads[effect->payload_first + index];
        cflow_scxml_cmeta_expr_value value = {0};
        cflow_scxml_payload_entry *entry =
            &session->payload_scratch[index];
        if (!evaluate_effect_value(
                &descriptor->expression, context, system_values, &value) ||
            !payload_value_from_cmeta(&value, &entry->value))
            return false;
        entry->name = descriptor->name;
        entry->name_size = descriptor->name_size;
    }
    out->kind = CFLOW_SCXML_PAYLOAD_NAMED;
    out->entries = session->payload_scratch;
    out->entry_count = effect->payload_count;
    return true;
}

static bool scalar_content_from_payload_value(
    const cflow_scxml_payload_value *value,
    cflow_scxml_content_view *out) {
    if (value == NULL || out == NULL ||
        value->kind == CFLOW_SCXML_PAYLOAD_VALUE_INVALID)
        return false;
    *out = (cflow_scxml_content_view){
        .kind = CFLOW_SCXML_CONTENT_SCALAR,
        .scalar = *value};
    return true;
}

static bool materialize_content_descriptor(
    const scxml_content_descriptor *descriptor, const void *state,
    cflow_scxml_content_view *out) {
    if (descriptor == NULL || out == NULL) return false;
    *out = (cflow_scxml_content_view){0};
    if (descriptor->kind == CFLOW_SCXML_CONTENT_TEXT_UTF8 ||
        descriptor->kind == CFLOW_SCXML_CONTENT_XML_UTF8) {
        if (descriptor->byte_count != 0u && descriptor->bytes == NULL)
            return false;
        out->kind = descriptor->kind;
        out->bytes = descriptor->bytes;
        out->byte_count = descriptor->byte_count;
        return true;
    }
    if (descriptor->kind == CFLOW_SCXML_CONTENT_CMETA) {
        if (state == NULL || descriptor->location.value == NULL)
            return false;
        out->kind = CFLOW_SCXML_CONTENT_CMETA;
        out->schema = descriptor->location.value;
        out->object = (const unsigned char *)state +
                      descriptor->location.offset;
        return true;
    }
    return false;
}

static bool payload_v2_to_v3(
    cflow_scxml_session_impl *session,
    const cflow_scxml_payload_view *source,
    cflow_scxml_payload_view_v3 *out) {
    size_t index;
    if (session == NULL || source == NULL || out == NULL ||
        source->entry_count > session->payload_scratch_capacity ||
        (source->entry_count != 0u && session->payload_scratch_v3 == NULL))
        return false;
    *out = (cflow_scxml_payload_view_v3){.kind = source->kind};
    if (source->kind == CFLOW_SCXML_PAYLOAD_CONTENT)
        return scalar_content_from_payload_value(
            &source->content, &out->content);
    if (source->kind == CFLOW_SCXML_PAYLOAD_NONE) return true;
    if (source->kind != CFLOW_SCXML_PAYLOAD_NAMED) return false;
    for (index = 0u; index < source->entry_count; ++index) {
        cflow_scxml_payload_entry_v3 *entry =
            &session->payload_scratch_v3[index];
        entry->name = source->entries[index].name;
        entry->name_size = source->entries[index].name_size;
        if (!scalar_content_from_payload_value(
                &source->entries[index].value, &entry->value))
            return false;
    }
    out->entries = session->payload_scratch_v3;
    out->entry_count = source->entry_count;
    return true;
}

static bool scalar_value_to_text(
    const cflow_scxml_cmeta_expr_value *value, char *storage,
    size_t capacity, const char **out_data, size_t *out_size) {
    int written;
    if (value == NULL || storage == NULL || capacity == 0u ||
        out_data == NULL || out_size == NULL)
        return false;
    if (value->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_STRING) {
        if (value->data.string.size > CFLOW_SCXML_EVENT_METADATA_CAPACITY)
            return false;
        *out_data = value->data.string.data;
        *out_size = value->data.string.size;
        return true;
    }
    if (value->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL)
        written = snprintf(storage, capacity, "%s",
                           value->data.boolean ? "true" : "false");
    else if (value->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT)
        written = snprintf(storage, capacity, "%lld",
                           (long long)value->data.sint);
    else if (value->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT)
        written = snprintf(storage, capacity, "%llu",
                           (unsigned long long)value->data.uint);
    else if (value->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT)
        written = snprintf(storage, capacity, "%.17g", value->data.number);
    else
        return false;
    if (written < 0 || (size_t)written >= capacity ||
        (size_t)written > CFLOW_SCXML_EVENT_METADATA_CAPACITY)
        return false;
    *out_data = storage;
    *out_size = (size_t)written;
    return true;
}

static bool materialize_send_id_location(
    cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor, void *staged_state,
    cflow_scxml_send_request *request, char *storage, size_t capacity) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    uint64_t token;
    int written;
    if (!descriptor->has_id_location) return true;
    if (session == NULL || staged_state == NULL || request == NULL ||
        storage == NULL || capacity == 0u)
        return false;
    token = session->next_send_token;
    if (token == 0u) return false;
    written = snprintf(
        storage, capacity, "send.%s.%" PRIu64, session->session_id, token);
    if (written < 0 || (size_t)written >= capacity) return false;
    session->next_send_token = token == UINT64_MAX ? 0u : token + 1u;
    if (cflow_scxml_cmeta_location_assign_owned_string(
            &descriptor->id_location, staged_state, storage,
            (size_t)written, CFLOW_SCXML_EVENT_METADATA_CAPACITY,
            &diagnostic) != CFLOW_SCXML_CMETA_EXPR_OK)
        return false;
    request->id = storage;
    request->id_size = (size_t)written;
    return true;
}

static scxml_execute_outcome execute_send(
    const scxml_block *block,
    cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    const char **out_error) {
    const bool null_value = false;
    cflow_scxml_send_request materialized = descriptor->request.send;
    const cflow_scxml_send_request *request = &materialized;
    cflow_scxml_send_request_v2 request_v2 = {0};
    cflow_scxml_send_request_v3 request_v3 = {0};
    cflow_scxml_payload_view payload = {0};
    cflow_scxml_payload_view_v3 payload_v3 = {0};
    cflow_scxml_content_view internal_content = {0};
    cflow_scxml_cmeta_expr_value value = {0};
    cflow_scxml_event_metadata metadata = {0};
    char data_storage[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    char id_storage[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
    const scxml_program_name *dynamic_event = NULL;
    bool internal_target = descriptor->internal_target;
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed = NULL;
    const char *adapter_error = NULL;
    cflow_scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    bool duplicate = false;
    if (descriptor->has_event_expr) {
        if (session == NULL || !evaluate_effect_value(
                &descriptor->event_expr, context, system_values, &value) ||
            value.kind != CFLOW_SCXML_CMETA_EXPR_VALUE_STRING ||
            !is_xml_nmtoken((turbo_xml_string_view){
                value.data.string.data, value.data.string.size})) {
            return raise_block_execution_error(block, context, out_error);
        }
        dynamic_event = find_program_name(
            session->program->event_names, session->program->event_name_count,
            value.data.string.data, value.data.string.size);
        if (dynamic_event == NULL)
            return raise_block_execution_error(block, context, out_error);
        materialized.event = value.data.string.data;
        materialized.event_size = value.data.string.size;
    }
    if (descriptor->has_target_expr) {
        if (!evaluate_effect_value(
                &descriptor->target_expr, context, system_values, &value) ||
            value.kind != CFLOW_SCXML_CMETA_EXPR_VALUE_STRING)
            return raise_block_execution_error(block, context, out_error);
        materialized.target = value.data.string.data;
        materialized.target_size = value.data.string.size;
        internal_target =
            (value.data.string.size == sizeof("#_internal") - 1u &&
             memcmp(value.data.string.data, "#_internal",
                    sizeof("#_internal") - 1u) == 0) ||
            (value.data.string.size == sizeof("_internal") - 1u &&
             memcmp(value.data.string.data, "_internal",
                    sizeof("_internal") - 1u) == 0);
    }
    if (descriptor->has_type_expr) {
        if (!evaluate_effect_value(
                &descriptor->type_expr, context, system_values, &value) ||
            value.kind != CFLOW_SCXML_CMETA_EXPR_VALUE_STRING ||
            value.data.string.size == 0u)
            return raise_block_execution_error(block, context, out_error);
        materialized.type = value.data.string.data;
        materialized.type_size = value.data.string.size;
    }
    if (descriptor->has_delay_expr) {
        if (!evaluate_effect_value(
                &descriptor->delay_expr, context, system_values, &value) ||
            (value.kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT &&
             value.data.sint < 0))
            return raise_block_execution_error(block, context, out_error);
        materialized.delay_ms =
            value.kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT
                ? value.data.uint : (uint64_t)value.data.sint;
    }
    if (descriptor->content.kind == CFLOW_SCXML_CONTENT_SCALAR) {
        if (!evaluate_effect_value(
                &descriptor->data_expr, context, system_values, &value))
            return raise_block_execution_error(block, context, out_error);
        if (internal_target) {
            if (!scalar_value_to_text(
                    &value, data_storage, sizeof(data_storage),
                    &metadata.data, &metadata.data_size))
                return raise_block_execution_error(
                    block, context, out_error);
        } else {
            payload.kind = CFLOW_SCXML_PAYLOAD_CONTENT;
            if (!payload_value_from_cmeta(&value, &payload.content))
                return raise_block_execution_error(
                    block, context, out_error);
        }
    } else if (descriptor->content.kind != CFLOW_SCXML_CONTENT_INVALID) {
        if (internal_target) {
            if (descriptor->content.kind == CFLOW_SCXML_CONTENT_CMETA) {
                if (!materialize_content_descriptor(
                        &descriptor->content, context->out_state,
                        &internal_content))
                    return raise_block_execution_error(
                        block, context, out_error);
            } else {
                if ((descriptor->content.kind !=
                         CFLOW_SCXML_CONTENT_TEXT_UTF8 &&
                     descriptor->content.kind !=
                         CFLOW_SCXML_CONTENT_XML_UTF8) ||
                    descriptor->content.byte_count >
                        CFLOW_SCXML_EVENT_METADATA_CAPACITY)
                    return raise_block_execution_error(
                        block, context, out_error);
                metadata.data = descriptor->content.bytes;
                metadata.data_size = descriptor->content.byte_count;
            }
        } else {
            payload_v3.kind = CFLOW_SCXML_PAYLOAD_CONTENT;
            if (!materialize_content_descriptor(
                    &descriptor->content, context->out_state,
                    &payload_v3.content))
                return raise_block_execution_error(block, context, out_error);
        }
    }
    if (descriptor->payload_count != 0u) {
        if (internal_target ||
            !materialize_effect_named_payload(
                block, session, descriptor, context, system_values,
                &payload))
            return raise_block_execution_error(block, context, out_error);
    }
    if (descriptor->content.kind != CFLOW_SCXML_CONTENT_INVALID &&
        internal_target &&
        materialized.delay_ms != 0u)
        return raise_block_execution_error(block, context, out_error);
    if (!materialize_send_id_location(
            session, descriptor, context->out_state, &materialized,
            id_storage, sizeof(id_storage)))
        return raise_block_execution_error(block, context, out_error);
    if (internal_target && request->delay_ms == 0u) {
        const cflow_event_id event_id = dynamic_event != NULL
            ? (cflow_event_id)dynamic_event->id : descriptor->event_id;
        const cflow_event_view raised = {
            event_id, &cmeta_type_bool, &null_value};
        if (descriptor->content.kind == CFLOW_SCXML_CONTENT_INVALID)
            return context->raise_internal(
                       context->raise_user, &raised, out_error)
                ? SCXML_EXECUTE_CONTINUE : SCXML_EXECUTE_FATAL;
        if (session == NULL || context->raise_internal_tagged == NULL ||
            context->stage_effect == NULL) {
            *out_error = "SCXML payload send requires tagged internal Events";
            return SCXML_EXECUTE_FATAL;
        }
        {
            uint64_t token = 0u;
            scxml_external_event_metadata_row *metadata_row =
                reserve_event_metadata(session, &metadata, &token);
            cflow_statechart_effect_ticket metadata_ticket;
            if (metadata_row == NULL)
                return raise_block_execution_error(block, context, out_error);
            if (internal_content.kind != CFLOW_SCXML_CONTENT_INVALID &&
                !attach_event_content(
                    session, metadata_row, &internal_content)) {
                release_event_metadata(metadata_row);
                return raise_block_execution_error(
                    block, context, out_error);
            }
            if (!context->raise_internal_tagged(
                    context->raise_user, &raised, token, out_error)) {
                release_event_metadata(metadata_row);
                return SCXML_EXECUTE_FATAL;
            }
            metadata_ticket = (cflow_statechart_effect_ticket){
                commit_event_metadata, release_event_metadata, metadata_row};
            if (!context->stage_effect(
                    context->effect_user, &metadata_ticket, out_error)) {
                release_event_metadata(metadata_row);
                return SCXML_EXECUTE_FATAL;
            }
        }
        return SCXML_EXECUTE_CONTINUE;
    }
    if (session == NULL || !session->has_event_io ||
        context->stage_effect == NULL) {
        *out_error = "SCXML send requires an owning Event I/O session";
        return SCXML_EXECUTE_FATAL;
    }
    if (payload.kind != CFLOW_SCXML_PAYLOAD_NONE &&
        session->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V1)
        return raise_block_execution_error(block, context, out_error);
    if (payload_v3.kind != CFLOW_SCXML_PAYLOAD_NONE &&
        session->event_io_abi != CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3)
        return raise_block_execution_error(block, context, out_error);
    if (session->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3 &&
        payload_v3.kind == CFLOW_SCXML_PAYLOAD_NONE &&
        !payload_v2_to_v3(session, &payload, &payload_v3))
        return raise_block_execution_error(block, context, out_error);
    turbo_mutex_lock(&session->registry_lock);
    prepared = acquire_prepared_effect_locked(session);
    if (prepared != NULL && request->delay_ms != 0u) {
        delayed = reserve_delayed_send_locked(
            session, request->id, request->id_size,
            descriptor->has_id_location, &registry_index, &duplicate);
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

    if (session->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3) {
        request_v3.base = materialized;
        request_v3.payload = payload_v3;
        status = session->event_io_v3.prepare_send(
            session->adapter_user, &request_v3, &adapter_ticket,
            &adapter_error);
    } else if (session->event_io_abi ==
               CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2) {
        request_v2.base = materialized;
        request_v2.payload = payload;
        status = session->event_io_v2.prepare_send(
            session->adapter_user, &request_v2, &adapter_ticket,
            &adapter_error);
    } else {
        status = session->event_io.prepare_send(
            session->adapter_user, request, &adapter_ticket, &adapter_error);
    }
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
    const scxml_block *block,
    cflow_scxml_session_impl *session,
    const scxml_effect_descriptor *descriptor,
    const cflow_statechart_executable_context *context,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    const char **out_error) {
    cflow_scxml_cancel_request materialized = descriptor->request.cancel;
    const cflow_scxml_cancel_request *request = &materialized;
    cflow_scxml_cmeta_expr_value value = {0};
    cflow_statechart_effect_ticket adapter_ticket = {0};
    cflow_statechart_effect_ticket runtime_ticket;
    scxml_prepared_effect *prepared;
    scxml_delayed_send *delayed;
    const char *adapter_error = NULL;
    cflow_scxml_adapter_status status;
    size_t registry_index = SIZE_MAX;
    if (descriptor->has_send_id_expr) {
        if (!evaluate_effect_value(
                &descriptor->send_id_expr, context, system_values, &value) ||
            value.kind != CFLOW_SCXML_CMETA_EXPR_VALUE_STRING ||
            value.data.string.size == 0u)
            return raise_block_execution_error(block, context, out_error);
        materialized.send_id = value.data.string.data;
        materialized.send_id_size = value.data.string.size;
    }
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

    status = (session->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3
                  ? session->event_io_v3.prepare_cancel
              : session->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2
                  ? session->event_io_v2.prepare_cancel
                  : session->event_io.prepare_cancel)(
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

/* All first entries in one microstep share this ticket so their data and
   markers have the same publication boundary. */
static void commit_late_initializer(void *user) {
    cflow_scxml_session_impl *session =
        (cflow_scxml_session_impl *)user;
    size_t index;
    if (session == NULL || !session->late_initializer_ticket_pending) return;
    for (index = 0u; index < session->late_initializer_count; ++index) {
        if (session->late_initializers[index].phase ==
            SCXML_LATE_INITIALIZER_PENDING)
            session->late_initializers[index].phase =
                SCXML_LATE_INITIALIZER_DONE;
    }
    session->late_initializer_ticket_pending = false;
}

static void discard_late_initializer(void *user) {
    cflow_scxml_session_impl *session =
        (cflow_scxml_session_impl *)user;
    size_t index;
    if (session == NULL || !session->late_initializer_ticket_pending) return;
    for (index = 0u; index < session->late_initializer_count; ++index) {
        if (session->late_initializers[index].phase ==
            SCXML_LATE_INITIALIZER_PENDING)
            session->late_initializers[index].phase =
                SCXML_LATE_INITIALIZER_NEVER;
    }
    session->late_initializer_ticket_pending = false;
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
                      block, session, &block->effects[step->effect], context,
                      system_values, out_error)
                : execute_cancel(
                      block, session, &block->effects[step->effect], context,
                      system_values, out_error);
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
        } else if (step->kind == SCXML_STEP_LATE_INITIALIZE) {
            scxml_late_initializer_state *initializer;
            cflow_statechart_effect_ticket ticket;
            size_t assignment;
            if (session == NULL || context->stage_effect == NULL ||
                block->assignments == NULL ||
                step->late_initializer >= session->late_initializer_count ||
                step->assignment > block->assignment_storage_count ||
                step->assignment_count >
                    block->assignment_storage_count - step->assignment) {
                *out_error = "SCXML late initializer context is invalid";
                return SCXML_EXECUTE_FATAL;
            }
            initializer =
                &session->late_initializers[step->late_initializer];
            if (initializer->phase == SCXML_LATE_INITIALIZER_DONE) {
                index = step->next;
                continue;
            }
            if (initializer->phase != SCXML_LATE_INITIALIZER_NEVER) {
                *out_error =
                    "SCXML late initializer transaction is already pending";
                return SCXML_EXECUTE_FATAL;
            }
            initializer->phase = SCXML_LATE_INITIALIZER_PENDING;
            if (!session->late_initializer_ticket_pending) {
                session->late_initializer_ticket_pending = true;
                ticket = (cflow_statechart_effect_ticket){
                    commit_late_initializer, discard_late_initializer,
                    session};
                if (!context->stage_effect(
                        context->effect_user, &ticket, out_error)) {
                    initializer->phase = SCXML_LATE_INITIALIZER_NEVER;
                    session->late_initializer_ticket_pending = false;
                    return SCXML_EXECUTE_FATAL;
                }
            }
            for (assignment = 0u;
                 assignment < step->assignment_count; ++assignment) {
                cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
                if (cflow_scxml_cmeta_assign_apply_with_system(
                        &block->assignments[step->assignment + assignment],
                        context->out_state, evaluate_cmeta_executable_active,
                        (void *)context, system_values, &diagnostic) !=
                    CFLOW_SCXML_CMETA_EXPR_OK) {
                    *out_error =
                        "SCXML late data initializer evaluation failed";
                    return SCXML_EXECUTE_FATAL;
                }
            }
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
    if (session != NULL)
        system_values.event_name = session->system_values.event_name;
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
    *out_state = (cflow_machine_state_id)state->id;
    return CFLOW_SCXML_OK;
}

static bool resolve_cmeta_condition_state(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state) {
    const scxml_build *build = (const scxml_build *)user;
    const turbo_xml_string_view wanted = {name, name_size};
    const scxml_name_ref *state;
    if (build == NULL || name == NULL || name_size == 0u ||
        out_state == NULL) {
        return false;
    }
    state = find_name_ref(
        build->state_names, build->state_name_index, wanted);
    if (state == NULL || state->id == 0u || state->id > build->state_index)
        return false;
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

static cflow_scxml_status compile_cmeta_value_program(
    scxml_build *build, turbo_xml_attribute attribute,
    const char *subject, cflow_scxml_cmeta_expr_program *program,
    cflow_scxml_cmeta_expr_value_kind required_kind) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_status expression_status;
    cflow_scxml_status status;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    status = decode_cmeta_attribute_source(
        build, attribute, subject, &source, &source_size);
    if (status != CFLOW_SCXML_OK) return status;
    expression_status = cflow_scxml_cmeta_expr_compile_value(
        program, source, source_size, build->cmeta_root,
        resolve_cmeta_condition_state, build,
        &build->cmeta_expression_limits, &diagnostic);
    free(source);
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK &&
        (required_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID ||
         cflow_scxml_cmeta_expr_program_value_kind(program) == required_kind))
        return CFLOW_SCXML_OK;
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK) {
        cflow_scxml_cmeta_expr_program_destroy(program);
        expression_status = CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH;
        (void)snprintf(diagnostic.message, sizeof(diagnostic.message),
                       "%s", "expression result type is not supported here");
    }
    status = expression_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
        ? CFLOW_SCXML_LIMIT_EXCEEDED
        : expression_status == CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
            ? CFLOW_SCXML_ALLOCATION_FAILED
            : CFLOW_SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s",
        subject, diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "expression compilation failed");
    return scxml_fail(build, status, turbo_xml_attribute_location(attribute),
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
    bool preserve_observed_event_name,
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
        if (preserve_observed_event_name)
            current_system_values.event_name = system_values->event_name;
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
        false, context, out_enabled, out_error);
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
        true, context, out_enabled, out_error);
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

static bool retain_effect_view(scxml_build *build,
                               turbo_xml_string_view value,
                               const char **out_data,
                               size_t *out_size) {
    size_t retained;
    char *stored;
    *out_data = NULL;
    *out_size = 0u;
    if (value.data == NULL) return true;
    if (!checked_add(value.size, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained >
            build->effect_storage_capacity - build->effect_storage_index)
        return false;
    stored = build->effect_storage + build->effect_storage_index;
    if (value.size != 0u) memcpy(stored, value.data, value.size);
    stored[value.size] = '\0';
    build->effect_storage_index += retained;
    *out_data = stored;
    *out_size = value.size;
    return true;
}

static cflow_scxml_status retain_effect_inline_content(
    scxml_build *build, turbo_xml_node node,
    scxml_content_descriptor *content) {
    size_t retained;
    size_t actual = 0u;
    cflow_scxml_status status = inspect_inline_content(
        build, node, &content->kind, &content->byte_count);
    if (status != CFLOW_SCXML_OK) return status;
    if (!checked_add(content->byte_count, 1u, &retained) ||
        build->effect_storage_index > build->effect_storage_capacity ||
        retained > build->effect_storage_capacity -
                       build->effect_storage_index)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "SCXML inline content storage mismatched admission");
    content->bytes = build->effect_storage + build->effect_storage_index;
    if (turbo_xml_serialize_children(
            node, (char *)content->bytes, retained,
            build->limits.max_name_bytes, &actual) != TURBO_XML_OK ||
        actual != content->byte_count)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "SCXML inline content changed during emission");
    build->effect_storage_index += retained;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status compile_cmeta_payload_token(
    scxml_build *build, turbo_xml_string_view source,
    turbo_xml_location location, const char *subject,
    cflow_scxml_cmeta_expr_program *program) {
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_location compiled_location = {0};
    cflow_scxml_cmeta_expr_status expression_status;
    cflow_scxml_status status;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    if (source.size > build->cmeta_expression_limits.max_source_bytes)
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED, location,
                          "CMeta payload location byte limit exceeded");
    expression_status = cflow_scxml_cmeta_location_compile(
        &compiled_location, source.data, source.size, build->cmeta_root,
        build->cmeta_expression_limits.max_path_depth, false,
        &diagnostic);
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK) {
        diagnostic = (cflow_scxml_cmeta_expr_diagnostic){0};
        expression_status = cflow_scxml_cmeta_expr_compile_value(
            program, source.data, source.size, build->cmeta_root,
            resolve_cmeta_condition_state, build,
            &build->cmeta_expression_limits, &diagnostic);
    }
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK)
        return CFLOW_SCXML_OK;
    status = expression_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
        ? CFLOW_SCXML_LIMIT_EXCEEDED
        : expression_status == CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
            ? CFLOW_SCXML_ALLOCATION_FAILED
            : CFLOW_SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s", subject,
        diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "expression compilation failed");
    return scxml_fail(build, status, location, message);
}

static cflow_scxml_status compile_cmeta_content_expression(
    scxml_build *build, turbo_xml_attribute expression,
    const char *subject, scxml_content_descriptor *content,
    cflow_scxml_cmeta_expr_program *scalar_program) {
    const turbo_xml_string_view source =
        turbo_xml_attribute_value(expression);
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_location location = {0};
    if (cflow_scxml_cmeta_location_compile(
            &location, source.data, source.size, build->cmeta_root,
            build->cmeta_expression_limits.max_path_depth, false,
            &diagnostic) == CFLOW_SCXML_CMETA_EXPR_OK &&
        location.value != NULL &&
        !cmeta_content_kind_is_scalar(location.value->kind)) {
        content->kind = CFLOW_SCXML_CONTENT_CMETA;
        content->location = location;
        return CFLOW_SCXML_OK;
    }
    {
        const cflow_scxml_status status = compile_cmeta_value_program(
            build, expression, subject, scalar_program,
            CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID);
        if (status == CFLOW_SCXML_OK)
            content->kind = CFLOW_SCXML_CONTENT_SCALAR;
        return status;
    }
}

static cflow_scxml_status compile_cmeta_owned_string_location(
    scxml_build *build, turbo_xml_attribute attribute,
    const char *subject, cflow_scxml_cmeta_location *out) {
    const turbo_xml_string_view source = turbo_xml_attribute_value(attribute);
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_status expression_status;
    cflow_scxml_status status;
    const cmeta_data_buffer_ops *ops;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    if (source.size > build->cmeta_expression_limits.max_source_bytes)
        return scxml_fail(build, CFLOW_SCXML_LIMIT_EXCEEDED,
                          turbo_xml_attribute_location(attribute),
                          "CMeta location byte limit exceeded");
    expression_status = cflow_scxml_cmeta_location_compile(
        out, source.data, source.size, build->cmeta_root,
        build->cmeta_expression_limits.max_path_depth, true, &diagnostic);
    ops = expression_status == CFLOW_SCXML_CMETA_EXPR_OK &&
                  out->value->kind == CMETA_DATA_STRING
        ? cmeta_data_buffer_ops_of(out->value) : NULL;
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK &&
        (ops == NULL || ops->ownership != CMETA_DATA_BUFFER_OWNED)) {
        expression_status = CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH;
        diagnostic.status = expression_status;
        diagnostic.byte_offset = 0u;
        (void)snprintf(
            diagnostic.message, sizeof(diagnostic.message),
            "%s must name a writable owned CMeta string", subject);
    }
    if (expression_status == CFLOW_SCXML_CMETA_EXPR_OK)
        return CFLOW_SCXML_OK;
    *out = (cflow_scxml_cmeta_location){0};
    status = expression_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
        ? CFLOW_SCXML_LIMIT_EXCEEDED
        : expression_status == CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
            ? CFLOW_SCXML_ALLOCATION_FAILED
            : CFLOW_SCXML_INVALID_STRUCTURE;
    (void)snprintf(
        message, sizeof(message), "CMeta %s byte %zu: %s", subject,
        diagnostic.byte_offset,
        diagnostic.message[0] != '\0'
            ? diagnostic.message : "location compilation failed");
    return scxml_fail(build, status,
                      turbo_xml_attribute_location(attribute), message);
}

static cflow_scxml_status emit_send_step(scxml_build *build,
                                         turbo_xml_node node) {
    const turbo_xml_attribute event_attribute = find_attribute(node, "event");
    const turbo_xml_attribute event_expr_attribute =
        find_attribute(node, "eventexpr");
    const turbo_xml_attribute target_attribute = find_attribute(node, "target");
    const turbo_xml_attribute target_expr_attribute =
        find_attribute(node, "targetexpr");
    const turbo_xml_attribute type_attribute = find_attribute(node, "type");
    const turbo_xml_attribute type_expr_attribute =
        find_attribute(node, "typeexpr");
    const turbo_xml_attribute id_attribute = find_attribute(node, "id");
    const turbo_xml_attribute delay_attribute = find_attribute(node, "delay");
    const turbo_xml_attribute delay_expr_attribute =
        find_attribute(node, "delayexpr");
    const turbo_xml_attribute idlocation_attribute =
        find_attribute(node, "idlocation");
    const turbo_xml_attribute namelist_attribute =
        find_attribute(node, "namelist");
    const scxml_name_ref *event = event_attribute.impl != NULL
        ? find_name_ref(build->event_names, build->event_name_count,
                        turbo_xml_attribute_value(event_attribute))
        : NULL;
    const size_t step = build->step_index;
    const size_t effect = build->effect_index;
    scxml_effect_descriptor *descriptor;
    turbo_xml_string_view target;
    size_t child_index;
    cflow_scxml_status status;
    if ((event_attribute.impl != NULL && event == NULL) ||
        step >= build->step_capacity ||
        effect >= build->effect_capacity) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(node),
                          "send exceeded admitted descriptor storage");
    }
    descriptor = &build->effects[effect];
    descriptor->kind = SCXML_EFFECT_SEND;
    descriptor->event_id = event != NULL ? (cflow_event_id)event->id : 0u;
    descriptor->payload_first = build->payload_index;
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
    if (event_expr_attribute.impl != NULL) {
        descriptor->has_event_expr = true;
        status = compile_cmeta_value_program(
            build, event_expr_attribute, "send eventexpr",
            &descriptor->event_expr,
            CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (target_expr_attribute.impl != NULL) {
        descriptor->has_target_expr = true;
        status = compile_cmeta_value_program(
            build, target_expr_attribute, "send targetexpr",
            &descriptor->target_expr,
            CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (type_expr_attribute.impl != NULL) {
        descriptor->has_type_expr = true;
        status = compile_cmeta_value_program(
            build, type_expr_attribute, "send typeexpr",
            &descriptor->type_expr,
            CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (delay_expr_attribute.impl != NULL) {
        cflow_scxml_cmeta_expr_value_kind kind;
        descriptor->has_delay_expr = true;
        status = compile_cmeta_value_program(
            build, delay_expr_attribute, "send delayexpr",
            &descriptor->delay_expr,
            CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID);
        if (status != CFLOW_SCXML_OK) return status;
        kind = cflow_scxml_cmeta_expr_program_value_kind(
            &descriptor->delay_expr);
        if (kind != CFLOW_SCXML_CMETA_EXPR_VALUE_SINT &&
            kind != CFLOW_SCXML_CMETA_EXPR_VALUE_UINT) {
            cflow_scxml_cmeta_expr_program_destroy(&descriptor->delay_expr);
            descriptor->has_delay_expr = false;
            return scxml_fail(build, CFLOW_SCXML_INVALID_STRUCTURE,
                              turbo_xml_attribute_location(
                                  delay_expr_attribute),
                              "send delayexpr must produce an integer millisecond value");
        }
    }
    if (idlocation_attribute.impl != NULL) {
        descriptor->has_id_location = true;
        status = compile_cmeta_owned_string_location(
            build, idlocation_attribute, "send idlocation",
            &descriptor->id_location);
        if (status != CFLOW_SCXML_OK) return status;
    }
    if (namelist_attribute.impl != NULL) {
        const turbo_xml_string_view namelist =
            turbo_xml_attribute_value(namelist_attribute);
        size_t cursor = 0u;
        turbo_xml_string_view token;
        while (token_next(namelist, &cursor, &token)) {
            scxml_payload_descriptor *payload;
            if (build->payload_index >= build->payload_capacity)
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_attribute_location(
                                      namelist_attribute),
                                  "send payload emission exceeded admission");
            payload = &build->payloads[build->payload_index];
            if (!retain_effect_view(
                    build, token, &payload->name, &payload->name_size))
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_attribute_location(
                                      namelist_attribute),
                                  "send namelist storage mismatched admission");
            status = compile_cmeta_payload_token(
                build, token, turbo_xml_attribute_location(
                                  namelist_attribute),
                "send namelist", &payload->expression);
            if (status != CFLOW_SCXML_OK) return status;
            ++build->payload_index;
        }
    }
    for (child_index = 0u;
         child_index < turbo_xml_node_child_count(node); ++child_index) {
        const turbo_xml_node child =
            turbo_xml_node_child_at(node, child_index);
        const scxml_element_kind kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT)
            continue;
        if (kind == SCXML_ELEMENT_PARAM) {
            const turbo_xml_attribute name = find_attribute(child, "name");
            const turbo_xml_attribute expression =
                find_attribute(child, "expr");
            const turbo_xml_attribute location =
                find_attribute(child, "location");
            scxml_payload_descriptor *payload;
            if (build->payload_index >= build->payload_capacity)
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(child),
                                  "send param emission exceeded admission");
            payload = &build->payloads[build->payload_index];
            if (!retain_effect_view(
                    build, turbo_xml_attribute_value(name),
                    &payload->name, &payload->name_size))
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_attribute_location(name),
                                  "send param storage mismatched admission");
            if (location.impl != NULL)
                status = compile_cmeta_payload_token(
                    build, turbo_xml_attribute_value(location),
                    turbo_xml_attribute_location(location),
                    "send param location", &payload->expression);
            else
                status = compile_cmeta_value_program(
                    build, expression, "send param", &payload->expression,
                    CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID);
            if (status != CFLOW_SCXML_OK) return status;
            ++build->payload_index;
            continue;
        }
        if (kind != SCXML_ELEMENT_CONTENT) continue;
        if (find_attribute(child, "expr").impl != NULL) {
            status = compile_cmeta_content_expression(
                build, find_attribute(child, "expr"), "send content",
                &descriptor->content, &descriptor->data_expr);
        } else {
            status = retain_effect_inline_content(
                build, child, &descriptor->content);
        }
        if (status != CFLOW_SCXML_OK) return status;
    }
    descriptor->payload_count =
        build->payload_index - descriptor->payload_first;
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
    const turbo_xml_attribute sendid_expr_attribute =
        find_attribute(node, "sendidexpr");
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
    if (sendid_expr_attribute.impl != NULL) {
        const cflow_scxml_status status = compile_cmeta_value_program(
            build, sendid_expr_attribute, "cancel sendidexpr",
            &descriptor->send_id_expr,
            CFLOW_SCXML_CMETA_EXPR_VALUE_STRING);
        if (status != CFLOW_SCXML_OK) return status;
        descriptor->has_send_id_expr = true;
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

static cflow_scxml_status emit_data_initializer(
    scxml_build *build, turbo_xml_node data) {
    const turbo_xml_attribute location = find_attribute(data, "id");
    const turbo_xml_attribute expression = find_attribute(data, "expr");
    cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
    cflow_scxml_cmeta_expr_status expression_status;
    cflow_scxml_status status;
    char *location_source = NULL;
    size_t location_size = 0u;
    char *expression_source = NULL;
    size_t expression_size = 0u;
    char message[CFLOW_SCXML_DIAGNOSTIC_CAPACITY];
    if (build->assignment_index >= build->assignment_capacity)
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(data),
                          "data initializer exceeded admitted storage");
    status = decode_cmeta_attribute_source(
        build, location, "data id", &location_source, &location_size);
    if (status != CFLOW_SCXML_OK) return status;
    status = decode_cmeta_attribute_source(
        build, expression, "data expr", &expression_source,
        &expression_size);
    if (status != CFLOW_SCXML_OK) {
        free(location_source);
        return status;
    }
    expression_status = cflow_scxml_cmeta_assign_compile(
        &build->assignments[build->assignment_index],
        location_source, location_size, expression_source, expression_size,
        build->cmeta_root, resolve_cmeta_condition_state, build,
        &build->cmeta_expression_limits, &diagnostic);
    free(location_source);
    free(expression_source);
    if (expression_status != CFLOW_SCXML_CMETA_EXPR_OK) {
        const cflow_scxml_status public_status =
            expression_status == CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED
                ? CFLOW_SCXML_LIMIT_EXCEEDED
                : expression_status ==
                          CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED
                      ? CFLOW_SCXML_ALLOCATION_FAILED
                      : CFLOW_SCXML_INVALID_STRUCTURE;
        (void)snprintf(
            message, sizeof(message), "CMeta data initializer byte %zu: %s",
            diagnostic.byte_offset,
            diagnostic.message[0] != '\0'
                ? diagnostic.message : "initializer compilation failed");
        return scxml_fail(build, public_status,
                          turbo_xml_node_location(data), message);
    }
    ++build->assignment_index;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_datamodel_assignments(
    scxml_build *build, turbo_xml_node datamodel,
    size_t *out_first, size_t *out_count) {
    const size_t first = build->assignment_index;
    size_t index;
    for (index = 0u; index < turbo_xml_node_child_count(datamodel); ++index) {
        const turbo_xml_node data =
            turbo_xml_node_child_at(datamodel, index);
        cflow_scxml_status status;
        if (turbo_xml_node_type(data) != TURBO_XML_ELEMENT ||
            element_kind(data) != SCXML_ELEMENT_DATA)
            continue;
        status = emit_data_initializer(build, data);
        if (status != CFLOW_SCXML_OK) return status;
    }
    *out_first = first;
    *out_count = build->assignment_index - first;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_data_initializers(
    scxml_build *build, turbo_xml_node node) {
    size_t index;
    if (build->late_binding) return CFLOW_SCXML_OK;
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        const scxml_element_kind kind = element_kind(child);
        if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT) continue;
        if (kind == SCXML_ELEMENT_DATAMODEL) {
            size_t first;
            size_t count;
            const cflow_scxml_status status = emit_datamodel_assignments(
                build, child, &first, &count);
            (void)first;
            (void)count;
            if (status != CFLOW_SCXML_OK) return status;
        } else if (is_state_element(kind) ||
                   kind == SCXML_ELEMENT_INITIAL ||
                   kind == SCXML_ELEMENT_HISTORY) {
            const cflow_scxml_status status =
                emit_data_initializers(build, child);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_done_data(
    scxml_build *build, turbo_xml_node node, size_t node_count,
    cflow_machine_state_id parent) {
    const scxml_element_kind kind = element_kind(node);
    const cflow_machine_state_id current = node_id(build, node, node_count);
    size_t index;
    if (kind == SCXML_ELEMENT_FINAL) {
        for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
            const turbo_xml_node child = turbo_xml_node_child_at(node, index);
            size_t content_index;
            if (turbo_xml_node_type(child) != TURBO_XML_ELEMENT ||
                element_kind(child) != SCXML_ELEMENT_DONEDATA)
                continue;
            if (build->done_data_index >= build->done_data_capacity)
                return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                                  turbo_xml_node_location(child),
                                  "donedata emission exceeded declarations");
            for (content_index = 0u;
                 content_index < turbo_xml_node_child_count(child);
                 ++content_index) {
                const turbo_xml_node content =
                    turbo_xml_node_child_at(child, content_index);
                scxml_done_data_descriptor *descriptor;
                cflow_scxml_status status;
                if (turbo_xml_node_type(content) != TURBO_XML_ELEMENT ||
                    element_kind(content) != SCXML_ELEMENT_CONTENT)
                    continue;
                descriptor = &build->done_data[build->done_data_index];
                descriptor->parent = parent;
                descriptor->final_state = current;
                if (find_attribute(content, "expr").impl != NULL) {
                    status = compile_cmeta_content_expression(
                        build, find_attribute(content, "expr"),
                        "donedata content", &descriptor->content,
                        &descriptor->expression);
                } else {
                    status = retain_effect_inline_content(
                        build, content, &descriptor->content);
                }
                if (status != CFLOW_SCXML_OK) return status;
                ++build->done_data_index;
            }
        }
    }
    for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
        const turbo_xml_node child = turbo_xml_node_child_at(node, index);
        if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
            is_state_element(element_kind(child))) {
            cflow_scxml_status status = emit_done_data(
                build, child, node_count, current);
            if (status != CFLOW_SCXML_OK) return status;
        }
    }
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
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
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
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
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
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = first_step,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    *out_block = &build->blocks[block_index];
    ++build->block_index;
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_late_initializer_block(
    scxml_build *build, turbo_xml_node datamodel,
    cflow_statechart_executable_id *out_executable) {
    const size_t block_index = build->block_index;
    const size_t executable_index = build->executable_index;
    const size_t step_index = build->step_index;
    const size_t initializer_index = build->late_initializer_index;
    const cflow_statechart_executable_id executable =
        (cflow_statechart_executable_id)(executable_index + 1u);
    size_t assignment_first;
    size_t assignment_count;
    cflow_scxml_status status;
    *out_executable = 0u;
    status = emit_datamodel_assignments(
        build, datamodel, &assignment_first, &assignment_count);
    if (status != CFLOW_SCXML_OK || assignment_count == 0u) return status;
    if (build->step_index >= build->step_capacity) {
        return scxml_fail(build, CFLOW_SCXML_NATIVE_IR_REJECTED,
                          turbo_xml_node_location(datamodel),
                          "late initializer exceeded admitted storage");
    }
    build->steps[step_index] = (scxml_step){
        .kind = SCXML_STEP_LATE_INITIALIZE,
        .next = step_index + 1u,
        .assignment = assignment_first,
        .assignment_count = assignment_count,
        .late_initializer = initializer_index};
    ++build->step_index;
    build->blocks[block_index] = (scxml_block){
        .state_type = build->cmeta_root->storage_type,
        .steps = build->steps,
        .branches = build->branches,
        .effects = build->effects,
        .assignments = build->assignments,
        .payloads = build->payloads,
        .foreach_descriptors = build->foreach_descriptors,
        .invocations = build->invocations,
        .step_begin = step_index,
        .step_end = build->step_index,
        .step_storage_count = build->step_capacity,
        .branch_storage_count = build->branch_capacity,
        .effect_storage_count = build->effect_capacity,
        .assignment_storage_count = build->assignment_capacity,
        .payload_storage_count = build->payload_capacity,
        .foreach_storage_count = build->foreach_capacity,
        .invocation_storage_count = build->invocation_capacity,
        .execution_error_event = build->execution_error_event,
        .max_conditional_depth = build->max_conditional_depth};
    build->executables[executable_index] = (cflow_statechart_executable){
        executable, build->cmeta_root->storage_type,
        CMETA_EFFECT_STATEFUL | CMETA_EFFECT_MAY_FAIL,
        CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS};
    build->bindings[executable_index] = (cflow_statechart_executable_binding){
        .id = executable,
        .user = &build->blocks[block_index],
        .contextual_fn = execute_scxml_block};
    ++build->late_initializer_index;
    ++build->executable_index;
    ++build->block_index;
    *out_executable = executable;
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
    if (build->late_binding) {
        for (index = 0u; index < turbo_xml_node_child_count(node); ++index) {
            const turbo_xml_node child = turbo_xml_node_child_at(node, index);
            if (turbo_xml_node_type(child) == TURBO_XML_ELEMENT &&
                element_kind(child) == SCXML_ELEMENT_DATAMODEL) {
                cflow_statechart_executable_id executable = 0u;
                cflow_scxml_status status = emit_late_initializer_block(
                    build, child, &executable);
                if (status != CFLOW_SCXML_OK) return status;
                if (executable != 0u) {
                    build->state_actions[build->state_action_index++] =
                        (cflow_statechart_state_action){
                            owner, CFLOW_STATECHART_STATE_ACTION_ENTRY,
                            executable, entry_order++};
                }
                break;
            }
        }
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

static cflow_scxml_status emit_transition_targets(
    scxml_build *build, cflow_statechart_transition_id transition,
    turbo_xml_string_view value, turbo_xml_location location) {
    turbo_xml_string_view target;
    size_t cursor = 0u;
    uint32_t order = 0u;
    while (token_next(value, &cursor, &target)) {
        const scxml_name_ref *resolved = find_name_ref(
            build->state_names, build->state_name_index, target);
        if (resolved == NULL) {
            return scxml_fail(
                build, CFLOW_SCXML_UNKNOWN_TARGET, location,
                "transition target does not name a declared state");
        }
        if (build->transition_target_index >=
            build->transition_target_capacity) {
            return scxml_fail(
                build, CFLOW_SCXML_NATIVE_IR_REJECTED, location,
                "transition target emission exceeded admission");
        }
        build->transition_targets[build->transition_target_index++] =
            (cflow_statechart_transition_target){
                transition, (cflow_machine_state_id)resolved->id, order++};
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_transition_token(
    scxml_build *build, cflow_machine_state_id source,
    turbo_xml_node transition_node, turbo_xml_string_view event_token,
    bool has_event, cflow_machine_state_id completion_override,
    cflow_statechart_transition_id *out_transition) {
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
    if (completion_override != 0u) {
        row.trigger = CFLOW_STATECHART_TRIGGER_COMPLETION;
        row.completion = completion_override;
    } else if (!has_event) {
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
    if (target_attribute.impl != NULL) {
        status = emit_transition_targets(
            build, row.id, turbo_xml_attribute_value(target_attribute),
            turbo_xml_attribute_location(target_attribute));
        if (status != CFLOW_SCXML_OK) return status;
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

static cflow_scxml_status emit_transition_with_action(
    scxml_build *build, cflow_machine_state_id source,
    turbo_xml_node transition_node, turbo_xml_string_view event_name,
    bool has_event, cflow_statechart_executable_id executable) {
    cflow_statechart_transition_id transition = 0u;
    cflow_scxml_status status = emit_transition_token(
        build, source, transition_node, event_name, has_event, 0u,
        &transition);
    if (status != CFLOW_SCXML_OK) return status;
    if (executable != 0u) {
        build->transition_actions[build->transition_action_index++] =
            (cflow_statechart_transition_action){
                transition, executable, 0u};
    }
    return CFLOW_SCXML_OK;
}

static cflow_scxml_status emit_completion_transition_with_action(
    scxml_build *build, cflow_machine_state_id source,
    turbo_xml_node transition_node, cflow_machine_state_id completion,
    cflow_statechart_executable_id executable) {
    cflow_statechart_transition_id transition = 0u;
    const turbo_xml_string_view empty = {NULL, 0u};
    cflow_scxml_status status = emit_transition_token(
        build, source, transition_node, empty, true, completion,
        &transition);
    if (status != CFLOW_SCXML_OK) return status;
    if (executable != 0u)
        build->transition_actions[build->transition_action_index++] =
            (cflow_statechart_transition_action){
                transition, executable, 0u};
    return CFLOW_SCXML_OK;
}

static bool emitted_state_can_complete(
    const scxml_build *build, cflow_machine_state_id state) {
    size_t index;
    for (index = 0u; index < build->state_index; ++index) {
        if (build->states[index].id != state) continue;
        return build->states[index].kind == CFLOW_STATECHART_COMPOUND ||
               build->states[index].kind == CFLOW_STATECHART_PARALLEL;
    }
    return false;
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
        cflow_statechart_transition row;
        cflow_scxml_status status;
        memset(&row, 0, sizeof(row));
        row.id = (cflow_statechart_transition_id)(build->transition_index + 1u);
        row.source = synthetic->state;
        row.trigger = CFLOW_STATECHART_TRIGGER_EVENTLESS;
        row.kind = CFLOW_STATECHART_TRANSITION_EXTERNAL;
        row.priority = (uint32_t)build->transition_index;
        row.document_order = (uint32_t)build->transition_index;
        status = emit_transition_targets(
            build, row.id, synthetic->target, synthetic->location);
        if (status != CFLOW_SCXML_OK) return status;
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
                status = emit_transition_with_action(
                    build, source, child, (turbo_xml_string_view){NULL, 0u},
                    false, executable);
                if (status != CFLOW_SCXML_OK) return status;
            } else {
                const turbo_xml_string_view value =
                    turbo_xml_attribute_value(event_attribute);
                turbo_xml_string_view token;
                size_t cursor = 0u;
                while (token_next(value, &cursor, &token)) {
                    turbo_xml_string_view completed = {NULL, 0u};
                    if (!completion_token(token, &completed)) continue;
                    status = emit_transition_with_action(
                        build, source, child, token, true, executable);
                    if (status != CFLOW_SCXML_OK) return status;
                }
                for (cursor = 0u; cursor < build->event_name_count;
                     ++cursor) {
                    const turbo_xml_string_view event_name =
                        build->event_names[cursor].name;
                    turbo_xml_string_view descriptor;
                    size_t descriptor_cursor = 0u;
                    bool matches = false;
                    while (token_next(value, &descriptor_cursor,
                                      &descriptor)) {
                        turbo_xml_string_view completed = {NULL, 0u};
                        if (!completion_token(descriptor, &completed) &&
                            event_descriptor_matches(
                                descriptor, event_name)) {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches) continue;
                    status = emit_transition_with_action(
                        build, source, child, event_name, true, executable);
                    if (status != CFLOW_SCXML_OK) return status;
                }
                for (cursor = 0u; cursor < build->state_name_index;
                     ++cursor) {
                    const scxml_name_ref *completed_state =
                        &build->state_names[cursor];
                    turbo_xml_string_view descriptor;
                    size_t descriptor_cursor = 0u;
                    bool matches = false;
                    if (!emitted_state_can_complete(
                            build,
                            (cflow_machine_state_id)completed_state->id))
                        continue;
                    while (token_next(value, &descriptor_cursor,
                                      &descriptor)) {
                        turbo_xml_string_view exact = {NULL, 0u};
                        if (!completion_token(descriptor, &exact) &&
                            completion_descriptor_matches(
                                descriptor, completed_state->name)) {
                            matches = true;
                            break;
                        }
                    }
                    if (!matches) continue;
                    status = emit_completion_transition_with_action(
                        build, source, child,
                        (cflow_machine_state_id)completed_state->id,
                        executable);
                    if (status != CFLOW_SCXML_OK) return status;
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

static void destroy_effects(scxml_effect_descriptor *effects, size_t count) {
    size_t index;
    if (effects == NULL) return;
    for (index = 0u; index < count; ++index) {
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].event_expr);
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].target_expr);
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].type_expr);
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].delay_expr);
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].send_id_expr);
        cflow_scxml_cmeta_expr_program_destroy(&effects[index].data_expr);
    }
}

static void destroy_payloads(scxml_payload_descriptor *payloads,
                             size_t count) {
    size_t index;
    if (payloads == NULL) return;
    for (index = 0u; index < count; ++index)
        cflow_scxml_cmeta_expr_program_destroy(
            &payloads[index].expression);
}

static void destroy_invocations(
    scxml_invocation_descriptor *invocations, size_t count) {
    size_t index;
    if (invocations == NULL) return;
    for (index = 0u; index < count; ++index) {
        cflow_scxml_cmeta_expr_program_destroy(
            &invocations[index].type_expr);
        cflow_scxml_cmeta_expr_program_destroy(
            &invocations[index].src_expr);
        cflow_scxml_cmeta_expr_program_destroy(
            &invocations[index].data_expr);
    }
}

static void destroy_done_data(
    scxml_done_data_descriptor *descriptors, size_t count) {
    size_t index;
    if (descriptors == NULL) return;
    for (index = 0u; index < count; ++index)
        cflow_scxml_cmeta_expr_program_destroy(
            &descriptors[index].expression);
}

static void free_build(scxml_build *build) {
    free(build->states);
    free(build->transitions);
    free(build->transition_targets);
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
    destroy_effects(build->effects, build->effect_capacity);
    free(build->effects);
    destroy_payloads(build->payloads, build->payload_capacity);
    free(build->payloads);
    destroy_assignments(build->assignments, build->assignment_capacity);
    free(build->assignments);
    free(build->foreach_descriptors);
    destroy_invocations(build->invocations, build->invocation_capacity);
    free(build->invocations);
    destroy_done_data(build->done_data, build->done_data_capacity);
    free(build->done_data);
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
    cflow_statechart_definition_v2 definition_v2;
    cflow_statechart_status native_status;
    cflow_scxml_status status;
    turbo_xml_attribute version;
    turbo_xml_attribute datamodel;
    turbo_xml_attribute binding;
    turbo_xml_attribute document_name_attribute;
    turbo_xml_string_view document_name = {NULL, 0u};
    size_t index;
    size_t name_bytes = 0u;
    size_t retained_string_bytes = 0u;
    size_t action_ref_count = 0u;
    size_t transition_capacity = 0u;
    size_t transition_target_capacity = 0u;
    size_t guard_capacity = 0u;
    size_t transition_action_capacity = 0u;
    size_t descriptor_extra_multiplier = 0u;
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
    binding = find_attribute(root, "binding");
    if (binding.impl != NULL &&
        !view_equal_raw(turbo_xml_attribute_value(binding), "early")) {
        if (!view_equal_raw(turbo_xml_attribute_value(binding), "late")) {
            status = scxml_fail(&build, CFLOW_SCXML_INVALID_STRUCTURE,
                                turbo_xml_attribute_location(binding),
                                "binding must be 'early' or 'late'");
            goto cleanup;
        }
        if (data_model != SCXML_DATA_MODEL_CMETA) {
            status = scxml_fail(
                &build, CFLOW_SCXML_UNSUPPORTED_FEATURE,
                turbo_xml_attribute_location(binding),
                "binding='late' requires the CMeta data model");
            goto cleanup;
        }
        build.late_binding = true;
    }
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
        counts.dynamic_expression_rows != 0u ||
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
    transition_capacity = counts.transition_rows;
    transition_target_capacity = counts.transition_target_rows;
    guard_capacity = counts.guard_rows;
    transition_action_capacity = counts.transition_action_rows;
    if (!checked_add(counts.event_occurrences, counts.state_names,
                     &descriptor_extra_multiplier)) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "event descriptor expansion bound overflow");
        goto cleanup;
    }
    if (descriptor_extra_multiplier != 0u)
        --descriptor_extra_multiplier;
    {
        size_t extra;
        if (!checked_multiply(counts.event_descriptor_rows,
                              descriptor_extra_multiplier, &extra) ||
            !checked_add(transition_capacity, extra,
                         &transition_capacity) ||
            !checked_multiply(counts.event_descriptor_target_rows,
                              descriptor_extra_multiplier, &extra) ||
            !checked_add(transition_target_capacity, extra,
                         &transition_target_capacity) ||
            !checked_multiply(counts.event_descriptor_guard_rows,
                              descriptor_extra_multiplier, &extra) ||
            !checked_add(guard_capacity, extra, &guard_capacity) ||
            !checked_multiply(counts.event_descriptor_action_rows,
                              descriptor_extra_multiplier, &extra) ||
            !checked_add(transition_action_capacity, extra,
                         &transition_action_capacity)) {
            status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                                turbo_xml_node_location(root),
                                "event descriptor expansion overflow");
            goto cleanup;
        }
    }
    if (!checked_add(counts.state_action_rows,
                     transition_action_capacity,
                     &action_ref_count) ||
        counts.state_rows > limits.max_states ||
        transition_capacity > limits.max_transitions ||
        counts.state_rows > UINT32_MAX ||
        transition_capacity > UINT32_MAX ||
        transition_target_capacity > CFLOW_STATECHART_MAX_TARGET_REFS ||
        guard_capacity > CFLOW_MACHINE_MAX_GUARDS ||
        counts.executable_blocks > CFLOW_MACHINE_MAX_ACTIONS ||
        counts.state_action_rows > CFLOW_STATECHART_MAX_ACTION_REFS ||
        transition_action_capacity > CFLOW_STATECHART_MAX_ACTION_REFS ||
        action_ref_count > CFLOW_STATECHART_MAX_ACTION_REFS) {
        status = scxml_fail(&build, CFLOW_SCXML_LIMIT_EXCEEDED,
                            turbo_xml_node_location(root),
                            "SCXML state or transition count exceeds limits");
        goto cleanup;
    }

    build.states = allocate_rows(counts.state_rows, sizeof(*build.states));
    build.transitions =
        allocate_rows(transition_capacity, sizeof(*build.transitions));
    build.transition_targets = allocate_rows(
        transition_target_capacity, sizeof(*build.transition_targets));
    build.guards = allocate_rows(guard_capacity, sizeof(*build.guards));
    build.events =
        allocate_rows(counts.event_occurrences, sizeof(*build.events));
    build.executables = allocate_rows(counts.executable_blocks,
                                      sizeof(*build.executables));
    build.state_actions = allocate_rows(counts.state_action_rows,
                                        sizeof(*build.state_actions));
    build.transition_actions = allocate_rows(
        transition_action_capacity, sizeof(*build.transition_actions));
    build.bindings = allocate_rows(counts.executable_blocks,
                                   sizeof(*build.bindings));
    build.guard_bindings = allocate_rows(
        guard_capacity, sizeof(*build.guard_bindings));
    build.guard_users = allocate_rows(
        guard_capacity, sizeof(*build.guard_users));
    build.blocks = allocate_rows(counts.block_rows,
                                 sizeof(*build.blocks));
    build.steps = allocate_rows(counts.executable_steps,
                                sizeof(*build.steps));
    build.branches = allocate_rows(
        counts.conditional_branches, sizeof(*build.branches));
    build.effects = allocate_rows(counts.effect_rows, sizeof(*build.effects));
    build.payloads = allocate_rows(
        counts.payload_rows, sizeof(*build.payloads));
    build.assignments = allocate_rows(
        counts.assignment_rows, sizeof(*build.assignments));
    build.foreach_descriptors = allocate_rows(
        counts.foreach_rows, sizeof(*build.foreach_descriptors));
    build.invocations = allocate_rows(
        counts.invocation_rows, sizeof(*build.invocations));
    build.invocation_names = allocate_rows(
        counts.invocation_rows, sizeof(*build.invocation_names));
    build.done_data = allocate_rows(
        counts.done_data_rows, sizeof(*build.done_data));
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
    build.payload_capacity = counts.payload_rows;
    build.assignment_capacity = counts.assignment_rows;
    build.foreach_capacity = counts.foreach_rows;
    build.log_storage_capacity = counts.log_label_bytes;
    build.effect_storage_capacity = counts.effect_string_bytes;
    build.invocation_storage_capacity = counts.invocation_string_bytes;
    build.invocation_capacity = counts.invocation_rows;
    build.done_data_capacity = counts.done_data_rows;
    build.guard_capacity = guard_capacity;
    build.transition_target_capacity = transition_target_capacity;
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
        (transition_capacity != 0u && build.transitions == NULL) ||
        (transition_target_capacity != 0u &&
         build.transition_targets == NULL) ||
        (guard_capacity != 0u &&
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
        (counts.payload_rows != 0u && build.payloads == NULL) ||
        (counts.assignment_rows != 0u && build.assignments == NULL) ||
        (counts.foreach_rows != 0u &&
         build.foreach_descriptors == NULL) ||
        (counts.invocation_rows != 0u &&
         (build.invocations == NULL || build.invocation_names == NULL)) ||
        (counts.done_data_rows != 0u && build.done_data == NULL) ||
        (counts.log_label_bytes != 0u && build.log_storage == NULL) ||
        (counts.effect_string_bytes != 0u &&
         build.effect_storage == NULL) ||
        (counts.invocation_string_bytes != 0u &&
         build.invocation_storage == NULL) ||
        (counts.state_action_rows != 0u && build.state_actions == NULL) ||
        (transition_action_capacity != 0u &&
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
    status = emit_data_initializers(&build, root);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_done_data(&build, root, build.node_ref_index, 0u);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_state_executables(&build, root, build.node_ref_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    status = emit_transitions(&build, root, build.node_ref_index,
                              build.synthetic_index);
    if (status != CFLOW_SCXML_OK) goto cleanup;
    if (build.effect_index != counts.effect_rows ||
        build.payload_index != counts.payload_rows ||
        build.assignment_index != counts.assignment_rows ||
        build.foreach_index != counts.foreach_rows ||
        build.effect_storage_index != counts.effect_string_bytes ||
        build.invocation_index != counts.invocation_rows ||
        build.invocation_emit_index != counts.invocation_rows ||
        build.done_data_index != counts.done_data_rows ||
        build.late_initializer_index != counts.late_initializer_rows ||
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
    definition_v2 = (cflow_statechart_definition_v2){
        .abi_version = CFLOW_STATECHART_DEFINITION_ABI_V2,
        .struct_size = sizeof(definition_v2),
        .base = definition,
        .transition_targets = build.transition_targets,
        .transition_target_count = build.transition_target_index};
    impl = (cflow_scxml_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) {
        status = scxml_fail(&build, CFLOW_SCXML_ALLOCATION_FAILED,
                            turbo_xml_node_location(root),
                            "unable to allocate SCXML program");
        goto cleanup;
    }
    native_status = cflow_statechart_build_v2(
        &impl->statechart, &definition_v2);
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
    impl->effect_count = build.effect_index;
    impl->payloads = build.payloads;
    impl->payload_count = build.payload_index;
    impl->max_payload_entries = counts.max_payload_entries;
    impl->assignments = build.assignments;
    impl->assignment_count = build.assignment_index;
    impl->data_initializer_count = counts.late_initializer_rows != 0u
        ? 0u : counts.data_initializer_rows;
    impl->late_initializer_count = counts.late_initializer_rows;
    impl->foreach_descriptors = build.foreach_descriptors;
    impl->foreach_count = build.foreach_index;
    impl->invocations = build.invocations;
    impl->invocation_count = build.invocation_index;
    impl->done_data = build.done_data;
    impl->done_data_count = build.done_data_index;
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
    build.payloads = NULL;
    build.assignments = NULL;
    build.foreach_descriptors = NULL;
    build.invocations = NULL;
    build.done_data = NULL;
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
        destroy_effects(impl->effects, impl->effect_count);
        free(impl->effects);
        destroy_payloads(impl->payloads, impl->payload_count);
        free(impl->payloads);
        destroy_assignments(impl->assignments, impl->assignment_count);
        free(impl->assignments);
        free(impl->foreach_descriptors);
        destroy_invocations(impl->invocations, impl->invocation_count);
        free(impl->invocations);
        destroy_done_data(impl->done_data, impl->done_data_count);
        free(impl->done_data);
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
    destroy_effects(impl->effects, impl->effect_count);
    free(impl->effects);
    destroy_payloads(impl->payloads, impl->payload_count);
    free(impl->payloads);
    destroy_assignments(impl->assignments, impl->assignment_count);
    free(impl->assignments);
    free(impl->foreach_descriptors);
    destroy_invocations(impl->invocations, impl->invocation_count);
    free(impl->invocations);
    destroy_done_data(impl->done_data, impl->done_data_count);
    free(impl->done_data);
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

static bool event_io_adapter_v2_valid(
    const cflow_scxml_event_io_adapter_v2 *adapter) {
    const uint64_t known = CFLOW_SCXML_EVENT_IO_CAP_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_CANCEL |
        CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD) != 0u &&
        (adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) == 0u)
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

static bool event_io_adapter_v3_valid(
    const cflow_scxml_event_io_adapter_v3 *adapter) {
    const uint64_t known = CFLOW_SCXML_EVENT_IO_CAP_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND |
        CFLOW_SCXML_EVENT_IO_CAP_CANCEL |
        CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD |
        CFLOW_SCXML_EVENT_IO_CAP_CONTENT_V3;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) != 0u &&
        adapter->prepare_send == NULL)
        return false;
    if ((adapter->capabilities &
         (CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD |
          CFLOW_SCXML_EVENT_IO_CAP_CONTENT_V3)) != 0u &&
        (adapter->capabilities & CFLOW_SCXML_EVENT_IO_CAP_SEND) == 0u)
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

static bool invoke_adapter_v2_valid(
    const cflow_scxml_invoke_adapter_v2 *adapter) {
    const uint64_t known = CFLOW_SCXML_INVOKE_CAP_START |
        CFLOW_SCXML_INVOKE_CAP_CANCEL | CFLOW_SCXML_INVOKE_CAP_FORWARD |
        CFLOW_SCXML_INVOKE_CAP_PAYLOAD;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_PAYLOAD) != 0u &&
        (adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_START) == 0u)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static bool invoke_adapter_v3_valid(
    const cflow_scxml_invoke_adapter_v3 *adapter) {
    const uint64_t known = CFLOW_SCXML_INVOKE_CAP_START |
        CFLOW_SCXML_INVOKE_CAP_CANCEL | CFLOW_SCXML_INVOKE_CAP_FORWARD |
        CFLOW_SCXML_INVOKE_CAP_PAYLOAD |
        CFLOW_SCXML_INVOKE_CAP_CONTENT_V3;
    if (adapter == NULL) return true;
    if (adapter->abi_version != CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3 ||
        adapter->struct_size < sizeof(*adapter) ||
        (adapter->capabilities & ~known) != 0u ||
        adapter->close == NULL || adapter->is_quiescent == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_START) != 0u &&
        adapter->prepare_start == NULL)
        return false;
    if ((adapter->capabilities &
         (CFLOW_SCXML_INVOKE_CAP_PAYLOAD |
          CFLOW_SCXML_INVOKE_CAP_CONTENT_V3)) != 0u &&
        (adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_START) == 0u)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_CANCEL) != 0u &&
        adapter->prepare_cancel == NULL)
        return false;
    if ((adapter->capabilities & CFLOW_SCXML_INVOKE_CAP_FORWARD) != 0u &&
        adapter->prepare_forward == NULL)
        return false;
    return true;
}

static bool session_adapters_v2_valid(
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v2 *adapters) {
    if (adapters == NULL) return true;
    return config != NULL &&
        adapters->abi_version == CFLOW_SCXML_SESSION_ADAPTERS_ABI_V2 &&
        adapters->struct_size >= sizeof(*adapters) &&
        event_io_adapter_v2_valid(adapters->event_io) &&
        invoke_adapter_v2_valid(adapters->invoke) &&
        !(config->event_io != NULL && adapters->event_io != NULL) &&
        !(config->invoke != NULL && adapters->invoke != NULL);
}

static bool session_adapters_v3_valid(
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v3 *adapters) {
    if (adapters == NULL) return true;
    return config != NULL &&
        adapters->abi_version == CFLOW_SCXML_SESSION_ADAPTERS_ABI_V3 &&
        adapters->struct_size >= sizeof(*adapters) &&
        event_io_adapter_v3_valid(adapters->event_io) &&
        invoke_adapter_v3_valid(adapters->invoke) &&
        !(config->event_io != NULL && adapters->event_io != NULL) &&
        !(config->invoke != NULL && adapters->invoke != NULL);
}

static void session_close_adapter(cflow_scxml_session_impl *impl) {
    if (impl != NULL && impl->has_event_io &&
        !atomic_exchange_explicit(
            &impl->adapter_close_called, true, memory_order_acq_rel))
        (impl->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3
             ? impl->event_io_v3.close
         : impl->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2
             ? impl->event_io_v2.close
             : impl->event_io.close)(impl->adapter_user);
    if (impl != NULL && impl->has_invoke &&
        !atomic_exchange_explicit(
            &impl->invoke_close_called, true, memory_order_acq_rel))
        (impl->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
             ? impl->invoke_v3.close
         : impl->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2
             ? impl->invoke_v2.close
             : impl->invoke.close)(impl->invoke_user);
}

static void session_free_storage(cflow_scxml_session_impl *impl) {
    size_t index;
    if (impl == NULL) return;
    if (impl->current_event_data_object_live) {
        destroy_event_data_object(
            impl->current_event_data_schema,
            impl->current_event_data_object.bytes);
        impl->current_event_data_object_live = false;
    }
    if (impl->external_metadata_rows != NULL) {
        for (index = 0u; index < impl->external_metadata_capacity; ++index) {
            scxml_external_event_metadata_row *row =
                &impl->external_metadata_rows[index];
            if (row->data_object_live) {
                destroy_event_data_object(
                    row->data_schema, row->data_object.bytes);
                row->data_object_live = false;
            }
        }
    }
    free(impl->late_initializers);
    free(impl->prepared_effects);
    free(impl->external_metadata_rows);
    free(impl->invocation_effects);
    free(impl->invocation_rows);
    free(impl->delayed_sends);
    free(impl->guard_users);
    free(impl->guard_bindings);
    free(impl->binding_users);
    free(impl->bindings);
    free(impl->system_name);
    free(impl->payload_scratch);
    free(impl->payload_scratch_v3);
}

static bool initialization_state_is_active(
    void *user, cflow_machine_state_id state, bool *out_active) {
    (void)user;
    (void)state;
    if (out_active == NULL) return false;
    *out_active = false;
    return true;
}

static cflow_statechart_runtime_status initialize_cmeta_state(
    const cflow_scxml_program_impl *program, const void *initial_state,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    void **out_state, bool *out_managed) {
    const cmeta_type_desc *type;
    void *state;
    bool managed;
    size_t index;
    if (program == NULL || program->cmeta_root == NULL ||
        program->cmeta_root->storage_type == NULL || initial_state == NULL ||
        system_values == NULL || out_state == NULL || out_managed == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    type = program->cmeta_root->storage_type;
    managed = cmeta_type_require_traits(
                  type, CMETA_TRAIT_TRIVIAL_COPY |
                            CMETA_TRAIT_TRIVIAL_DESTROY) != CMETA_OK;
    state = malloc(type->size);
    if (state == NULL) return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    if (managed) {
        if (cmeta_type_require_traits(
                type, CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE |
                          CMETA_TRAIT_DESTROY) != CMETA_OK ||
            !type->traits->copy_construct(state, initial_state)) {
            free(state);
            return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
        }
    } else {
        memcpy(state, initial_state, type->size);
    }
    for (index = 0u; index < program->data_initializer_count; ++index) {
        cflow_scxml_cmeta_expr_diagnostic diagnostic = {0};
        if (cflow_scxml_cmeta_assign_apply_with_system(
                &program->assignments[index], state,
                initialization_state_is_active, NULL, system_values,
                &diagnostic) != CFLOW_SCXML_CMETA_EXPR_OK) {
            if (managed) type->traits->destroy(state);
            free(state);
            return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
        }
    }
    *out_state = state;
    *out_managed = managed;
    return CFLOW_STATECHART_RUNTIME_OK;
}

static void destroy_initialized_cmeta_state(
    const cflow_scxml_program_impl *program, void *state, bool managed) {
    if (state == NULL || program == NULL || program->cmeta_root == NULL ||
        program->cmeta_root->storage_type == NULL)
        return;
    if (managed)
        program->cmeta_root->storage_type->traits->destroy(state);
    free(state);
}

static cflow_statechart_runtime_status cflow_scxml_session_init_model(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v2 *adapters,
    const cflow_scxml_session_adapters_v3 *adapters_v3,
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
    const cflow_scxml_event_io_adapter_v2 *event_io_v2 = NULL;
    const cflow_scxml_invoke_adapter_v2 *invoke_v2 = NULL;
    const cflow_scxml_event_io_adapter_v3 *event_io_v3 = NULL;
    const cflow_scxml_invoke_adapter_v3 *invoke_v3 = NULL;
    uint64_t event_io_capabilities = 0u;
    uint64_t invoke_capabilities = 0u;
    void *initialized_cmeta_state = NULL;
    bool initialized_cmeta_state_managed = false;
    if (session == NULL || session->impl != NULL || config == NULL ||
        config->program == NULL || config->program->impl == NULL ||
        !event_io_adapter_valid(config->event_io) ||
        !invoke_adapter_valid(config->invoke) ||
        !session_adapters_v2_valid(config, adapters) ||
        !session_adapters_v3_valid(config, adapters_v3) ||
        (adapters != NULL && adapters_v3 != NULL))
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    event_io_v2 = adapters != NULL ? adapters->event_io : NULL;
    invoke_v2 = adapters != NULL ? adapters->invoke : NULL;
    event_io_v3 = adapters_v3 != NULL ? adapters_v3->event_io : NULL;
    invoke_v3 = adapters_v3 != NULL ? adapters_v3->invoke : NULL;
    event_io_capabilities = event_io_v3 != NULL
        ? event_io_v3->capabilities
        : event_io_v2 != NULL ? event_io_v2->capabilities
        : config->event_io != NULL ? config->event_io->capabilities : 0u;
    invoke_capabilities = invoke_v3 != NULL
        ? invoke_v3->capabilities
        : invoke_v2 != NULL ? invoke_v2->capabilities
        : config->invoke != NULL ? config->invoke->capabilities : 0u;
    program = (const cflow_scxml_program_impl *)config->program->impl;
    if (program->data_model != data_model ||
        (data_model == SCXML_DATA_MODEL_CMETA &&
         cmeta_initial_state == NULL)) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if (program->late_initializer_count != 0u &&
        config->effect_capacity == 0u) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_EVENT_IO) != 0u) {
        if ((config->event_io == NULL && event_io_v2 == NULL &&
             event_io_v3 == NULL) ||
            config->effect_capacity == 0u ||
            config->adapter_internal_event_capacity == 0u ||
            (event_io_capabilities &
             CFLOW_SCXML_EVENT_IO_CAP_SEND) == 0u) {
            return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
        }
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_DELAYED_SEND) != 0u &&
        (config->delayed_send_capacity == 0u ||
         (event_io_capabilities &
          CFLOW_SCXML_EVENT_IO_CAP_DELAYED_SEND) == 0u)) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_CANCEL) != 0u &&
        (event_io_capabilities &
         CFLOW_SCXML_EVENT_IO_CAP_CANCEL) == 0u) {
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    }
    if ((program->requirements & CFLOW_SCXML_REQUIREMENT_INVOKE) != 0u &&
        ((config->invoke == NULL && invoke_v2 == NULL &&
          invoke_v3 == NULL) ||
         config->effect_capacity == 0u ||
         config->adapter_internal_event_capacity == 0u ||
         config->invocation_capacity < program->invocation_count ||
         (invoke_capabilities &
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
        (invoke_capabilities & CFLOW_SCXML_INVOKE_CAP_FORWARD) == 0u)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    if ((config->invoke != NULL || invoke_v2 != NULL ||
         invoke_v3 != NULL) &&
        !checked_add(config->effect_capacity, 1u,
                     &invocation_effect_capacity))
        return CFLOW_STATECHART_RUNTIME_LIMIT_EXCEEDED;
    if (((program->requirements & CFLOW_SCXML_REQUIREMENT_PAYLOAD) != 0u &&
         ((event_io_v2 == NULL && event_io_v3 == NULL) ||
          (event_io_capabilities & CFLOW_SCXML_EVENT_IO_CAP_PAYLOAD) == 0u)) ||
        ((program->requirements &
          CFLOW_SCXML_REQUIREMENT_INVOKE_PAYLOAD) != 0u &&
         ((invoke_v2 == NULL && invoke_v3 == NULL) ||
          (invoke_capabilities & CFLOW_SCXML_INVOKE_CAP_PAYLOAD) == 0u)))
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    if (((program->requirements & CFLOW_SCXML_REQUIREMENT_CONTENT_V3) != 0u &&
         (event_io_v3 == NULL ||
          (event_io_capabilities & CFLOW_SCXML_EVENT_IO_CAP_CONTENT_V3) == 0u)) ||
        ((program->requirements &
          CFLOW_SCXML_REQUIREMENT_INVOKE_CONTENT_V3) != 0u &&
         (invoke_v3 == NULL ||
          (invoke_capabilities & CFLOW_SCXML_INVOKE_CAP_CONTENT_V3) == 0u)))
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    impl = (cflow_scxml_session_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL) return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    impl->program = program;
    if (turbo_uuid_v4_generate(&session_uuid) != TURBO_OK ||
        turbo_uuid_format(
            &session_uuid, impl->session_id,
            sizeof(impl->session_id)) != TURBO_OK ||
        snprintf(impl->scxml_location, sizeof(impl->scxml_location),
                 "#_scxml_%s", impl->session_id) < 0) {
        free(impl);
        return CFLOW_STATECHART_RUNTIME_INVALID_CONFIGURATION;
    }
    impl->system_values.session_id =
        (cflow_scxml_cmeta_expr_string_view){
            impl->session_id, TURBO_UUID_STRING_LENGTH};
    impl->system_values.scxml_location =
        (cflow_scxml_cmeta_expr_string_view){
            impl->scxml_location, strlen(impl->scxml_location)};
    clear_current_event_metadata(impl);
    if (data_model == SCXML_DATA_MODEL_CMETA) {
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
    impl->late_initializer_count = program->late_initializer_count;
    impl->late_initializers =
        (scxml_late_initializer_state *)allocate_rows(
            impl->late_initializer_count,
            sizeof(*impl->late_initializers));
    impl->delayed_send_capacity = config->delayed_send_capacity;
    impl->delayed_sends = (scxml_delayed_send *)allocate_rows(
        impl->delayed_send_capacity, sizeof(*impl->delayed_sends));
    impl->prepared_effect_capacity = config->effect_capacity;
    impl->prepared_effects = (scxml_prepared_effect *)allocate_rows(
        impl->prepared_effect_capacity, sizeof(*impl->prepared_effects));
    impl->external_metadata_capacity = config->external_event_capacity;
    impl->external_metadata_rows =
        (scxml_external_event_metadata_row *)allocate_rows(
            impl->external_metadata_capacity,
            sizeof(*impl->external_metadata_rows));
    impl->payload_scratch_capacity = program->max_payload_entries;
    impl->payload_scratch =
        (cflow_scxml_payload_entry *)allocate_rows(
            impl->payload_scratch_capacity,
            sizeof(*impl->payload_scratch));
    impl->payload_scratch_v3 =
        (cflow_scxml_payload_entry_v3 *)allocate_rows(
            impl->payload_scratch_capacity,
            sizeof(*impl->payload_scratch_v3));
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
         (impl->guard_bindings == NULL || impl->guard_users == NULL)) ||
        (impl->late_initializer_count != 0u &&
         impl->late_initializers == NULL)) {
        session_free_storage(impl);
        free(impl);
        return CFLOW_STATECHART_RUNTIME_ALLOCATION_FAILED;
    }
    if ((impl->delayed_send_capacity != 0u &&
         impl->delayed_sends == NULL) ||
        (impl->prepared_effect_capacity != 0u &&
         impl->prepared_effects == NULL) ||
        (impl->external_metadata_capacity != 0u &&
         impl->external_metadata_rows == NULL) ||
        (impl->payload_scratch_capacity != 0u &&
         (impl->payload_scratch == NULL ||
          impl->payload_scratch_v3 == NULL)) ||
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
        impl->event_io_abi = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V1;
    } else if (event_io_v2 != NULL) {
        impl->event_io_v2 = *event_io_v2;
        impl->adapter_user = adapters->event_io_user;
        impl->has_event_io = true;
        impl->event_io_abi = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2;
    } else if (event_io_v3 != NULL) {
        impl->event_io_v3 = *event_io_v3;
        impl->adapter_user = adapters_v3->event_io_user;
        impl->has_event_io = true;
        impl->event_io_abi = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3;
    }
    if (config->invoke != NULL) {
        impl->invoke = *config->invoke;
        impl->invoke_user = config->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1;
    } else if (invoke_v2 != NULL) {
        impl->invoke_v2 = *invoke_v2;
        impl->invoke_user = adapters->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2;
    } else if (invoke_v3 != NULL) {
        impl->invoke_v3 = *invoke_v3;
        impl->invoke_user = adapters_v3->invoke_user;
        impl->has_invoke = true;
        impl->invoke_abi = CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3;
    }
    impl->next_send_token = UINT64_C(1);
    impl->next_invocation_token = UINT64_C(1);
    impl->next_external_metadata_token =
        SCXML_EXTERNAL_METADATA_TOKEN_BIT | UINT64_C(1);
    atomic_init(&impl->adapter_close_called, false);
    atomic_init(&impl->invoke_close_called, false);
    runtime_hooks = (cflow_statechart_runtime_hooks){
        .abi_version =
            (program->requirements &
             CFLOW_SCXML_REQUIREMENT_INVOKE_IDLOCATION) != 0u
                ? CFLOW_STATECHART_RUNTIME_HOOKS_ABI_V3
                : CFLOW_STATECHART_RUNTIME_HOOKS_ABI_V2,
        .struct_size = sizeof(runtime_hooks),
        .on_stable = impl->has_invoke &&
                (program->requirements &
                 CFLOW_SCXML_REQUIREMENT_INVOKE_IDLOCATION) == 0u
            ? start_stable_invocations : NULL,
        .preprocess_external =
            impl->has_invoke ? preprocess_invocation_external : NULL,
        .on_event = observe_scxml_event,
        .on_stable_transaction = impl->has_invoke &&
                (program->requirements &
                 CFLOW_SCXML_REQUIREMENT_INVOKE_IDLOCATION) != 0u
            ? start_stable_invocations_transaction : NULL};
    if (data_model == SCXML_DATA_MODEL_CMETA &&
        program->data_initializer_count != 0u) {
        status = initialize_cmeta_state(
            program, cmeta_initial_state, &impl->system_values,
            &initialized_cmeta_state, &initialized_cmeta_state_managed);
        if (status != CFLOW_STATECHART_RUNTIME_OK) {
            session_close_adapter(impl);
            turbo_mutex_destroy(&impl->registry_lock);
            session_free_storage(impl);
            free(impl);
            return status;
        }
    }
    native_config = (cflow_statechart_instance_config){
        .statechart = &program->statechart,
        .initial_state = data_model == SCXML_DATA_MODEL_CMETA
                             ? (initialized_cmeta_state != NULL
                                    ? initialized_cmeta_state
                                    : cmeta_initial_state)
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
        .runtime_hooks = &runtime_hooks,
        .runtime_hook_user = impl};
    status = cflow_statechart_instance_init(&impl->instance, &native_config);
    destroy_initialized_cmeta_state(
        program, initialized_cmeta_state, initialized_cmeta_state_managed);
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
        session, config, NULL, NULL, SCXML_DATA_MODEL_NULL, NULL);
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
        session, config, NULL, NULL, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_statechart_runtime_status cflow_scxml_session_init_v2(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v2 *adapters) {
    return cflow_scxml_session_init_model(
        session, config, adapters, NULL, SCXML_DATA_MODEL_NULL, NULL);
}

cflow_statechart_runtime_status cflow_scxml_session_init_cmeta_v2(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_cmeta_session_options_v1 *options,
    const cflow_scxml_session_adapters_v2 *adapters) {
    if (options == NULL ||
        options->abi_version !=
            CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    return cflow_scxml_session_init_model(
        session, config, adapters, NULL, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_statechart_runtime_status cflow_scxml_session_init_v3(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_session_adapters_v3 *adapters) {
    return cflow_scxml_session_init_model(
        session, config, NULL, adapters, SCXML_DATA_MODEL_NULL, NULL);
}

cflow_statechart_runtime_status cflow_scxml_session_init_cmeta_v3(
    cflow_scxml_session *session,
    const cflow_scxml_session_config *config,
    const cflow_scxml_cmeta_session_options_v1 *options,
    const cflow_scxml_session_adapters_v3 *adapters) {
    if (options == NULL ||
        options->abi_version != CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1 ||
        options->struct_size < sizeof(*options) ||
        options->initial_state == NULL)
        return CFLOW_STATECHART_RUNTIME_INVALID_ARGUMENT;
    return cflow_scxml_session_init_model(
        session, config, NULL, adapters, SCXML_DATA_MODEL_CMETA,
        options->initial_state);
}

cflow_mailbox_status cflow_scxml_session_try_send(
    cflow_scxml_session *session, const cflow_event_view *event) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    return impl != NULL
        ? cflow_statechart_instance_try_send(&impl->instance, event)
        : CFLOW_MAILBOX_INVALID_ARGUMENT;
}

cflow_mailbox_status cflow_scxml_session_try_send_v2(
    cflow_scxml_session *session, const cflow_event_view *event,
    const cflow_scxml_event_metadata *metadata) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token = 0u;
    cflow_mailbox_status status;
    if (impl == NULL || event == NULL || metadata == NULL ||
        !scxml_metadata_field_valid(
            metadata->send_id, metadata->send_id_size) ||
        !scxml_metadata_field_valid(
            metadata->origin, metadata->origin_size) ||
        !scxml_metadata_field_valid(
            metadata->origin_type, metadata->origin_type_size) ||
        !scxml_metadata_field_valid(
            metadata->invoke_id, metadata->invoke_id_size) ||
        !scxml_metadata_field_valid(metadata->data, metadata->data_size))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    row = reserve_event_metadata(impl, metadata, &token);
    if (row == NULL) {
        return CFLOW_MAILBOX_FULL;
    }
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    if (status != CFLOW_MAILBOX_OK) {
        release_event_metadata(row);
    }
    return status;
}

cflow_mailbox_status cflow_scxml_session_try_send_v3(
    cflow_scxml_session *session, const cflow_event_view *event,
    const cflow_scxml_event_metadata_v3 *metadata) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    scxml_external_event_metadata_row *row = NULL;
    uint64_t token = 0u;
    cflow_mailbox_status status;
    if (impl == NULL || event == NULL || metadata == NULL ||
        metadata->abi_version != CFLOW_SCXML_EVENT_METADATA_ABI_V3 ||
        metadata->struct_size < sizeof(*metadata) ||
        !scxml_metadata_field_valid(
            metadata->base.send_id, metadata->base.send_id_size) ||
        !scxml_metadata_field_valid(
            metadata->base.origin, metadata->base.origin_size) ||
        !scxml_metadata_field_valid(
            metadata->base.origin_type, metadata->base.origin_type_size) ||
        !scxml_metadata_field_valid(
            metadata->base.invoke_id, metadata->base.invoke_id_size) ||
        !scxml_metadata_field_valid(
            metadata->base.data, metadata->base.data_size) ||
        (metadata->data.kind != CFLOW_SCXML_CONTENT_INVALID &&
         metadata->base.data_size != 0u))
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    row = reserve_event_metadata(impl, &metadata->base, &token);
    if (row == NULL) return CFLOW_MAILBOX_FULL;
    if (!attach_event_content(impl, row, &metadata->data)) {
        release_event_metadata(row);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, event, token);
    if (status != CFLOW_MAILBOX_OK) release_event_metadata(row);
    return status;
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

cflow_mailbox_status cflow_scxml_session_report_invoke_done(
    cflow_scxml_session *session, uint64_t token) {
    cflow_scxml_session_impl *impl = session != NULL
        ? (cflow_scxml_session_impl *)session->impl : NULL;
    const bool null_value = false;
    cflow_event_view event = {0};
    cflow_mailbox_status status;
    size_t index;
    if (impl == NULL || !impl->has_invoke || token == 0u)
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    turbo_mutex_lock(&impl->registry_lock);
    for (index = 0u; index < impl->program->invocation_count; ++index) {
        if (impl->invocation_rows[index].state == SCXML_INVOCATION_ACTIVE &&
            impl->invocation_rows[index].token == token) {
            event = (cflow_event_view){
                impl->program->invocations[index].done_event,
                &cmeta_type_bool, &null_value};
            break;
        }
    }
    if (event.id == 0u) {
        increment_u64(&impl->invoke_stats.returned_rejected);
        turbo_mutex_unlock(&impl->registry_lock);
        return CFLOW_MAILBOX_INVALID_ARGUMENT;
    }
    turbo_mutex_unlock(&impl->registry_lock);
    status = cflow_statechart_instance_try_send_tagged(
        &impl->instance, &event, token);
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
    if (row == NULL ||
        (row->state != SCXML_DELAYED_ACTIVE &&
         (row->state != SCXML_DELAYED_CANCEL_RESERVED ||
          row->previous_state != SCXML_DELAYED_ACTIVE))) {
        turbo_mutex_unlock(&impl->registry_lock);
        return false;
    }
    if (row->state == SCXML_DELAYED_CANCEL_RESERVED)
        row->previous_state = SCXML_DELAYED_FREE;
    else
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
        !(impl->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3
              ? impl->event_io_v3.is_quiescent
          : impl->event_io_abi == CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V2
              ? impl->event_io_v2.is_quiescent
              : impl->event_io.is_quiescent)(
            impl->adapter_user))
        return CFLOW_STATECHART_RUNTIME_WOULD_BLOCK;
    if (impl->has_invoke &&
        !(impl->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V3
              ? impl->invoke_v3.is_quiescent
          : impl->invoke_abi == CFLOW_SCXML_INVOKE_ADAPTER_ABI_V2
              ? impl->invoke_v2.is_quiescent
              : impl->invoke.is_quiescent)(
            impl->invoke_user))
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
