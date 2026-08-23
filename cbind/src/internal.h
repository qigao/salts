#ifndef CBIND_INTERNAL_H
#define CBIND_INTERNAL_H

#include <cbind/cbind.h>

#include <stdbool.h>
#include <stddef.h>

#define CBIND_FIELD_END(type, member) \
    (offsetof(type, member) + sizeof(((type *)0)->member))

bool cbind_context_valid(const cbind_context *context);
bool cbind_error_valid(const cbind_error *error);
void cbind_error_clear(cbind_error *error);
void cbind_error_set(cbind_error *error,
                     cbind_status status,
                     cserde_status source_status,
                     const cmeta_data_desc *shape,
                     const cmeta_data_field_desc *field,
                     size_t depth);

#endif /* CBIND_INTERNAL_H */
