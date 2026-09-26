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

#ifdef __cplusplus
}
#endif

#endif /* CMETA_METHOD_H */
