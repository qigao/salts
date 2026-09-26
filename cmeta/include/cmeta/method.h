#ifndef CMETA_METHOD_H
#define CMETA_METHOD_H

#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cmeta_receiver_method {
    const char *name;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
} cmeta_receiver_method;

typedef struct cmeta_receiver_method_set {
    size_t size;
    const cmeta_type_desc *receiver_type;
    const cmeta_receiver_method *methods;
    size_t method_count;
} cmeta_receiver_method_set;

bool cmeta_receiver_method_set_valid(const cmeta_receiver_method_set *set);

const cmeta_receiver_method *
cmeta_receiver_method_find(const cmeta_receiver_method_set *set,
                           const char *name);

typedef enum cmeta_receiver_resolve_status {
    CMETA_RECEIVER_RESOLVE_OK = 0,
    CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT = 1,
    CMETA_RECEIVER_RESOLVE_INVALID_METHOD_SET = 2,
    CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH = 3,
    CMETA_RECEIVER_RESOLVE_METHOD_NOT_FOUND = 4,
    CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH = 5,
    CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH = 6
} cmeta_receiver_resolve_status;

#define CMETA_RECEIVER_ARGUMENT_NONE ((size_t)-1)

typedef struct cmeta_receiver_resolution {
    size_t size;
    const cmeta_receiver_method *method;
    size_t argument_index;
} cmeta_receiver_resolution;

#define CMETA_RECEIVER_RESOLUTION_INIT \
    { sizeof(cmeta_receiver_resolution), NULL, CMETA_RECEIVER_ARGUMENT_NONE }

cmeta_receiver_resolve_status
cmeta_receiver_method_resolve(
    const cmeta_receiver_method_set *set,
    const cmeta_type_desc *receiver_type,
    const char *method_name,
    const cmeta_type_desc *const *argument_types,
    size_t argument_count,
    cmeta_receiver_resolution *out);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_METHOD_H */
