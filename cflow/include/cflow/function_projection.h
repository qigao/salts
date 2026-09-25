#ifndef CFLOW_FUNCTION_PROJECTION_H
#define CFLOW_FUNCTION_PROJECTION_H

#include <cflow/graph.h>
#include <cmeta/function.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum cflow_function_projection_status {
    CFLOW_FUNCTION_PROJECTION_OK = 0,
    CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT,
    CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION,
    CFLOW_FUNCTION_PROJECTION_INVALID_ABI,
    CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR,
    CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE,
    CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER,
    CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH,
    CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH
} cflow_function_projection_status;

/*
 * Control-plane admission artifact.
 *
 * The reflected descriptors remain available for semantic identity and
 * diagnostics, but Graph/Plan execution copies only the already-bound callable
 * and concrete value types. Runtime evaluation performs no reflection lookup.
 * All descriptor pointers, adapter code and borrowed captures remain borrowed;
 * copying a callable does not retain its code module. The caller must keep
 * their providers loaded until this projection and every derived Graph/Plan,
 * active run and value needing provider-owned trait callbacks are finished.
 * Destroying the projection alone does not make a plugin safe to unload.
 */
typedef struct cflow_function_projection {
    size_t size;
    cflow_op op;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
    cmeta_callable callable;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
} cflow_function_projection;

const char *cflow_function_projection_status_string(
    cflow_function_projection_status status);

cflow_function_projection_status cflow_function_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    cflow_op op,
    cflow_function_projection *out);

bool cflow_function_projection_valid(
    const cflow_function_projection *projection);

bool cflow_graph_add_function_projection(
    cflow_graph *graph,
    const cflow_function_projection *projection);

#ifdef __cplusplus
} /* extern "C" */
#endif

#ifndef __cplusplus

/*
 * Generate an exact C adapter for a reflected function without repeating its
 * signature. The function itself must belong to the finite CMeta callable
 * signature universe; unsupported C function types fail at adapter generation
 * rather than being dynamically invoked.
 *
 * The adapter inherits effects/properties from FunctionMeta(name). Admission
 * still verifies the bound callable against FunctionMeta/FunctionAbi.
 */
#define CFLOW_REFLECTED_ADAPTER_NAME_I(name) cflow_reflected_adapter_##name
#define CFLOW_REFLECTED_ADAPTER_NAME(name) CFLOW_REFLECTED_ADAPTER_NAME_I(name)

#define CFLOW_REFLECTED_ADAPTER(name) \
    static inline cmeta_callable CFLOW_REFLECTED_ADAPTER_NAME(name)(void) { \
        const cmeta_function_desc *function__ = FunctionMeta(name); \
        cmeta_callable adapter__ = {0}; \
        adapter__.meta = CMETA_WRAP_TYPED_ANY(name); \
        adapter__.meta.effects = function__ ? function__->effects : CMETA_EFFECT_UNKNOWN; \
        adapter__.meta.properties = function__ ? function__->properties : CMETA_PROP_NONE; \
        adapter__.invoke = CMETA_TYPED_INVOKER_ANY(name); \
        adapter__.generate = NULL; \
        adapter__.dispatch = CMETA_CALLABLE_DISPATCH_CANONICAL_RAW; \
        adapter__.capture_size = 0u; \
        return adapter__; \
    } \
    typedef char name##__cflow_reflected_adapter_complete[1]

#define CFLOW_REFLECTED_CALLABLE(name) CFLOW_REFLECTED_ADAPTER_NAME(name)()

#endif /* !__cplusplus */

#endif /* CFLOW_FUNCTION_PROJECTION_H */
