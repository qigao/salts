#ifndef CFLOW_RESULT_STORAGE_H
#define CFLOW_RESULT_STORAGE_H

#include "value_storage.h"

#include <cflow/adapters.h>

typedef struct cflow_managed_result_header {
    void *allocation;
} cflow_managed_result_header;

static inline bool cflow_result_storage_allocate(
    const cmeta_type_desc *type,
    size_t capacity,
    void **out_allocation,
    unsigned char **out_data) {
    size_t payload_bytes;
    size_t overhead;
    void *allocation;
    uintptr_t start;
    uintptr_t aligned;
    cflow_managed_result_header *header;

    if (!out_allocation || !out_data) return false;
    *out_allocation = NULL;
    *out_data = NULL;
    if (!cmeta_type_desc_valid(type) || !type->size ||
        !cflow_value_type_supported(type) ||
        (type->size && capacity > SIZE_MAX / type->size))
        return false;
    if (!capacity) return true;
    payload_bytes = capacity * type->size;

    if (cflow_value_storage_type_supported(type)) {
        allocation = malloc(payload_bytes);
        if (!allocation) return false;
        *out_allocation = allocation;
        *out_data = (unsigned char *)allocation;
        return true;
    }

    if (!type->align || (type->align & (type->align - 1u)) != 0u ||
        sizeof(*header) > SIZE_MAX - (type->align - 1u))
        return false;
    overhead = sizeof(*header) + type->align - 1u;
    if (payload_bytes > SIZE_MAX - overhead) return false;
    allocation = malloc(payload_bytes + overhead);
    if (!allocation) return false;
    start = (uintptr_t)allocation + sizeof(*header);
    if (start > UINTPTR_MAX - (type->align - 1u)) {
        free(allocation);
        return false;
    }
    aligned = (start + type->align - 1u) &
              ~((uintptr_t)type->align - 1u);
    header = (cflow_managed_result_header *)aligned - 1;
    header->allocation = allocation;
    *out_allocation = allocation;
    *out_data = (unsigned char *)aligned;
    return true;
}

static inline void cflow_result_storage_destroy(cflow_result *result) {
    if (!result) return;
    if (result->data && result->type &&
        cflow_value_lifecycle_type_supported(result->type) &&
        !cflow_value_storage_type_supported(result->type)) {
        cflow_managed_result_header *header =
            (cflow_managed_result_header *)result->data - 1;
        for (size_t index = 0u; index < result->count; ++index) {
            cflow_value_destroy(
                result->type,
                (unsigned char *)result->data + index * result->type->size);
        }
        free(header->allocation);
    } else {
        free(result->data);
    }
    memset(result, 0, sizeof(*result));
}

#endif
