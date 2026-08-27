#ifndef CFLOW_SOURCES_INTERNAL_H
#define CFLOW_SOURCES_INTERNAL_H

#include <cflow/sources.h>
#include <cmeta/status.h>

cmeta_status cflow_source_from_array_checked(
    cflow_source *out,
    const cmeta_type_desc *type,
    const void *data,
    size_t count);

cmeta_status cflow_source_from_range_checked(cflow_source *out,
                                              cmeta_range range,
                                              const char **out_error);

#endif /* CFLOW_SOURCES_INTERNAL_H */
