#ifndef CFLOW_SCXML_CMETA_ASSIGN_H
#define CFLOW_SCXML_CMETA_ASSIGN_H

#include "cmeta_expr.h"

typedef struct cflow_scxml_cmeta_assign_program {
    void *impl;
} cflow_scxml_cmeta_assign_program;

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_compile(
    cflow_scxml_cmeta_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

/* Mutates only staged_root. Callers discard the whole staged object on error. */
cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_apply(
    const cflow_scxml_cmeta_assign_program *program,
    void *staged_root,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_apply_with_system(
    const cflow_scxml_cmeta_assign_program *program,
    void *staged_root,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

void cflow_scxml_cmeta_assign_program_destroy(
    cflow_scxml_cmeta_assign_program *program);

#endif /* CFLOW_SCXML_CMETA_ASSIGN_H */
