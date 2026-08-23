#ifndef CBIND_CONTEXT_H
#define CBIND_CONTEXT_H

#include <stddef.h>
#include <stdint.h>

enum { CBIND_CONTEXT_ABI_VERSION = 1u };

typedef struct cbind_context {
    size_t struct_size;
    uint32_t abi_version;
    void *scratch;
    size_t scratch_size;
    size_t max_depth;
    size_t max_container_items;
} cbind_context;

#define CBIND_CONTEXT_INIT(scratch_ptr, scratch_bytes, depth_limit) \
    { sizeof(cbind_context), CBIND_CONTEXT_ABI_VERSION, \
      (scratch_ptr), (scratch_bytes), (depth_limit), 0u }

#define CBIND_CONTEXT_WITH_CONTAINERS_INIT(                              \
    scratch_ptr, scratch_bytes, depth_limit, item_limit)                 \
    { sizeof(cbind_context), CBIND_CONTEXT_ABI_VERSION,                  \
      (scratch_ptr), (scratch_bytes), (depth_limit), (item_limit) }

#endif /* CBIND_CONTEXT_H */
