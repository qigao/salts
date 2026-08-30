#ifndef CFLOW_SCXML_CMETA_EXPR_H
#define CFLOW_SCXML_CMETA_EXPR_H

#include <cflow/machine.h>
#include <cmeta/data.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CFLOW_SCXML_CMETA_EXPR_DIAGNOSTIC_CAPACITY 192u

typedef enum cflow_scxml_cmeta_expr_status {
    CFLOW_SCXML_CMETA_EXPR_OK = 0,
    CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
    CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR,
    CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
    CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
    CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
    CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED,
    CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR
} cflow_scxml_cmeta_expr_status;

typedef struct cflow_scxml_cmeta_expr_limits {
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
} cflow_scxml_cmeta_expr_limits;

typedef struct cflow_scxml_cmeta_expr_diagnostic {
    cflow_scxml_cmeta_expr_status status;
    size_t byte_offset;
    char message[CFLOW_SCXML_CMETA_EXPR_DIAGNOSTIC_CAPACITY];
} cflow_scxml_cmeta_expr_diagnostic;

typedef struct cflow_scxml_cmeta_expr_program {
    void *impl;
} cflow_scxml_cmeta_expr_program;

typedef struct cflow_scxml_cmeta_expr_string_view {
    const char *data;
    size_t size;
} cflow_scxml_cmeta_expr_string_view;

/** Call-scoped immutable SCXML system strings; no member may be retained. */
typedef struct cflow_scxml_cmeta_expr_system_values {
    cflow_scxml_cmeta_expr_string_view name;
    cflow_scxml_cmeta_expr_string_view session_id;
    cflow_scxml_cmeta_expr_string_view event_name;
    cflow_scxml_cmeta_expr_string_view event_type;
    cflow_scxml_cmeta_expr_string_view event_send_id;
    cflow_scxml_cmeta_expr_string_view event_origin;
    cflow_scxml_cmeta_expr_string_view event_origin_type;
    cflow_scxml_cmeta_expr_string_view event_invoke_id;
    cflow_scxml_cmeta_expr_string_view event_data;
    cflow_scxml_cmeta_expr_string_view scxml_location;
} cflow_scxml_cmeta_expr_system_values;

typedef enum cflow_scxml_cmeta_expr_value_kind {
    CFLOW_SCXML_CMETA_EXPR_VALUE_INVALID = 0,
    CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL,
    CFLOW_SCXML_CMETA_EXPR_VALUE_SINT,
    CFLOW_SCXML_CMETA_EXPR_VALUE_UINT,
    CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT,
    CFLOW_SCXML_CMETA_EXPR_VALUE_STRING
} cflow_scxml_cmeta_expr_value_kind;

/** One scalar result; string bytes remain borrowed only until state mutation. */
typedef struct cflow_scxml_cmeta_expr_value {
    cflow_scxml_cmeta_expr_value_kind kind;
    union {
        bool boolean;
        int64_t sint;
        uint64_t uint;
        double number;
        struct {
            const char *data;
            size_t size;
        } string;
    } data;
} cflow_scxml_cmeta_expr_value;

typedef bool (*cflow_scxml_cmeta_expr_resolve_state_fn)(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state);

typedef bool (*cflow_scxml_cmeta_expr_is_active_fn)(
    void *user, cflow_machine_state_id state, bool *out_active);

cflow_scxml_cmeta_expr_limits cflow_scxml_cmeta_expr_default_limits(void);
bool cflow_scxml_cmeta_expr_limits_valid(
    const cflow_scxml_cmeta_expr_limits *limits);

/*
 * Private CFlowScxml foundation API. The root descriptor and every descriptor
 * reachable from a compiled path remain borrowed until program destruction.
 */
cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_compile(
    cflow_scxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_compile_value(
    cflow_scxml_cmeta_expr_program *out,
    const char *source, size_t source_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_value_kind
cflow_scxml_cmeta_expr_program_value_kind(
    const cflow_scxml_cmeta_expr_program *program);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate_with_system(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate_value(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    cflow_scxml_cmeta_expr_value *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status
cflow_scxml_cmeta_expr_evaluate_value_with_system(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_value *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

void cflow_scxml_cmeta_expr_program_destroy(
    cflow_scxml_cmeta_expr_program *program);

#endif /* CFLOW_SCXML_CMETA_EXPR_H */
