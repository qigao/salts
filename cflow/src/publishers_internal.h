#ifndef CFLOW_PUBLISHERS_INTERNAL_H
#define CFLOW_PUBLISHERS_INTERNAL_H

#include <cflow/publishers.h>
#include <cmeta/status.h>

cmeta_status cflow_publisher_from_array_checked(
    cflow_publisher *out,
    const cmeta_type_desc *type,
    const void *data,
    size_t count);

cmeta_status cflow_publisher_from_range_checked(cflow_publisher *out,
                                              cmeta_range range,
                                              const char **out_error);

#endif /* CFLOW_PUBLISHERS_INTERNAL_H */
