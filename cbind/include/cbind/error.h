#ifndef CBIND_ERROR_H
#define CBIND_ERROR_H

#include <cbind/status.h>
#include <cmeta/data.h>
#include <cserde/status.h>

#include <stddef.h>
#include <stdint.h>

enum { CBIND_ERROR_ABI_VERSION = 1u };

typedef struct cbind_error {
    size_t struct_size;
    uint32_t abi_version;
    cbind_status status;
    cserde_status source_status;
    const cmeta_data_desc *shape;
    const cmeta_data_field_desc *field;
    size_t depth;
    cmeta_status target_status;
} cbind_error;

#define CBIND_ERROR_INIT \
    { sizeof(cbind_error), CBIND_ERROR_ABI_VERSION, CBIND_OK, \
      CSERDE_OK, NULL, NULL, 0u, CMETA_OK }

#endif /* CBIND_ERROR_H */
