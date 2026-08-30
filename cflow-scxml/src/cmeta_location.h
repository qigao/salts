#ifndef CFLOW_SCXML_CMETA_LOCATION_H
#define CFLOW_SCXML_CMETA_LOCATION_H

#include "cmeta_expr.h"

typedef struct cflow_scxml_cmeta_location {
    const cmeta_data_desc *root;
    const cmeta_data_desc *value;
    size_t offset;
    size_t storage_size;
} cflow_scxml_cmeta_location;

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_location_compile(
    cflow_scxml_cmeta_location *out,
    const char *path, size_t path_size,
    const cmeta_data_desc *root, size_t max_depth,
    bool writable,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

cflow_scxml_cmeta_expr_status
cflow_scxml_cmeta_location_assign_owned_string(
    const cflow_scxml_cmeta_location *location, void *root,
    const char *data, size_t size, size_t max_bytes,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic);

#endif
