#ifndef CMETA_OPERATION_H
#define CMETA_OPERATION_H

#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Immutable borrowed index row. FunctionAbi owns the sole FunctionDesc link;
 * a row grants no execution authority or object/module retention. */
typedef struct cmeta_receiver_operation {
    const char *name;
    const cmeta_function_abi_desc *abi;
} cmeta_receiver_operation;

typedef struct cmeta_receiver_operation_set {
    size_t size;
    const cmeta_type_desc *receiver_type;
    const cmeta_receiver_operation *operations;
    size_t operation_count;

    /*
     * Optional canonical generic operation namespace.
     *
     * NULL means ordinary receiver operations whose semantic owner is fully
     * identified by receiver_type. Generic operation sets (for example
     * CSTL List.add) publish the canonical constructor descriptor instead of
     * a display/name string. Equality is semantic through stable_id, never
     * descriptor address identity.
     */
    const cmeta_generic_desc *owner;
} cmeta_receiver_operation_set;

/** Validate one reflected receiver operation independent of any owning set. */
bool cmeta_receiver_operation_reflection_valid(
    const cmeta_receiver_operation *operation);

/** Validate receiver/owner identity and unique aliases. O(N^2) time, O(1)
 * extra storage, excluding validation within each canonical descriptor. */
bool cmeta_receiver_operation_set_valid(const cmeta_receiver_operation_set *set);

/** Return a borrowed row or NULL for an invalid set/name or missing alias. */
const cmeta_receiver_operation *
cmeta_receiver_operation_find(const cmeta_receiver_operation_set *set,
                              const char *name);

typedef enum cmeta_receiver_resolve_status {
    CMETA_RECEIVER_RESOLVE_OK = 0,
    CMETA_RECEIVER_RESOLVE_INVALID_ARGUMENT = 1,
    CMETA_RECEIVER_RESOLVE_INVALID_OPERATION_SET = 2,
    CMETA_RECEIVER_RESOLVE_RECEIVER_TYPE_MISMATCH = 3,
    CMETA_RECEIVER_RESOLVE_OPERATION_NOT_FOUND = 4,
    CMETA_RECEIVER_RESOLVE_ARITY_MISMATCH = 5,
    CMETA_RECEIVER_RESOLVE_ARGUMENT_TYPE_MISMATCH = 6,
    CMETA_RECEIVER_RESOLVE_OWNER_MISMATCH = 7
} cmeta_receiver_resolve_status;

#define CMETA_RECEIVER_ARGUMENT_NONE ((size_t)-1)

typedef struct cmeta_receiver_resolution {
    size_t size;
    const cmeta_receiver_operation *operation;
    size_t argument_index;
} cmeta_receiver_resolution;

#define CMETA_RECEIVER_RESOLUTION_INIT \
    { sizeof(cmeta_receiver_resolution), NULL, CMETA_RECEIVER_ARGUMENT_NONE }

/** Resolve argument types excluding the receiver. A NULL owner allows an
 * unqualified receiver call; a non-NULL owner must match set->owner semantically.
 * Initialize out with CMETA_RECEIVER_RESOLUTION_INIT. On failure a valid out
 * contains no operation; argument_index identifies an argument type mismatch.
 * All input metadata and the result row are borrowed for their owner's lifetime.
 * Immutable metadata may be shared; mutable storage requires caller exclusion. */
cmeta_receiver_resolve_status
cmeta_receiver_operation_resolve(
    const cmeta_receiver_operation_set *set,
    const cmeta_type_desc *receiver_type,
    const cmeta_generic_desc *owner,
    const char *operation_name,
    const cmeta_type_desc *const *argument_types,
    size_t argument_count,
    cmeta_receiver_resolution *out);

#ifdef __cplusplus
}
#endif

#endif /* CMETA_OPERATION_H */
