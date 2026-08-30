#ifndef CFLOW_SCXML_CMETA_FOREACH_H
#define CFLOW_SCXML_CMETA_FOREACH_H

#include "cmeta_location.h"
#include "cmeta_sequence.h"

typedef struct cflow_scxml_cmeta_foreach_program {
    cflow_scxml_cmeta_sequence_program sequence;
    cflow_scxml_cmeta_location item;
    cflow_scxml_cmeta_location index;
    size_t max_iterations;
    bool has_index;
    bool managed_item;
} cflow_scxml_cmeta_foreach_program;

typedef struct cflow_scxml_cmeta_foreach_value {
    void *allocation;
    void *storage;
    bool live;
} cflow_scxml_cmeta_foreach_value;

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_compile(
    cflow_scxml_cmeta_foreach_program *out,
    const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    size_t max_iterations,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_open(
    const cflow_scxml_cmeta_foreach_program *program,
    void *staged_root, cmeta_range *out_range, size_t *out_length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_value_init(
    const cflow_scxml_cmeta_foreach_program *program,
    cflow_scxml_cmeta_foreach_value *value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

void cflow_scxml_cmeta_foreach_value_destroy(
    const cflow_scxml_cmeta_foreach_program *program,
    cflow_scxml_cmeta_foreach_value *value);

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_next(
    const cflow_scxml_cmeta_foreach_program *program,
    void *staged_root, const cmeta_range *range,
    cmeta_range_cursor *cursor, cflow_scxml_cmeta_foreach_value *value,
    size_t iteration, size_t length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

#endif
