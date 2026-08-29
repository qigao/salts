#ifndef CFLOW_SCXML_CMETA_EXPR_H
#define CFLOW_SCXML_CMETA_EXPR_H

#include <cflow/machine.h>
#include <cmeta/data.h>

#include <stdbool.h>
#include <stddef.h>

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
} cflow_scxml_cmeta_expr_limits;

typedef struct cflow_scxml_cmeta_expr_diagnostic {
    cflow_scxml_cmeta_expr_status status;
    size_t byte_offset;
    char message[CFLOW_SCXML_CMETA_EXPR_DIAGNOSTIC_CAPACITY];
} cflow_scxml_cmeta_expr_diagnostic;

typedef struct cflow_scxml_cmeta_expr_program {
    void *impl;
} cflow_scxml_cmeta_expr_program;

typedef bool (*cflow_scxml_cmeta_expr_resolve_state_fn)(
    void *user, const char *name, size_t name_size,
    cflow_machine_state_id *out_state);

typedef bool (*cflow_scxml_cmeta_expr_is_active_fn)(
    void *user, cflow_machine_state_id state, bool *out_active);

cflow_scxml_cmeta_expr_limits cflow_scxml_cmeta_expr_default_limits(void);

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

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_expr_evaluate(
    const cflow_scxml_cmeta_expr_program *program,
    const void *root_object,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    bool *out_value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

void cflow_scxml_cmeta_expr_program_destroy(
    cflow_scxml_cmeta_expr_program *program);

#endif /* CFLOW_SCXML_CMETA_EXPR_H */
