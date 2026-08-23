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
} cbind_context;

#endif /* CBIND_CONTEXT_H */
