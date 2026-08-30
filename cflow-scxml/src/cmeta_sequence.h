#ifndef CFLOW_SCXML_CMETA_SEQUENCE_H
#define CFLOW_SCXML_CMETA_SEQUENCE_H

#include "cmeta_expr.h"

#include <cmeta/range.h>

typedef struct cflow_scxml_cmeta_sequence_program {
    const cmeta_data_desc *root;
    const cmeta_type_desc *container_type;
    const cmeta_type_desc *element_type;
    size_t offset;
    size_t storage_size;
} cflow_scxml_cmeta_sequence_program;

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_sequence_compile(
    cflow_scxml_cmeta_sequence_program *out,
    const char *location, size_t location_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

/**
 * Open one borrowed sequence Range and snapshot its current length.
 *
 * The Range and every value produced by it remain governed by the provider's
 * source lifetime and version contract. Failure leaves both outputs unchanged.
 */
cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_sequence_open(
    const cflow_scxml_cmeta_sequence_program *program,
    const void *root_object, cmeta_range *out_range, size_t *out_length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

#endif /* CFLOW_SCXML_CMETA_SEQUENCE_H */
