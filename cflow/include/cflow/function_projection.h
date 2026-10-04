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
    /** Explicit Graph semantic intent; never inferred from the C return type. */
    cflow_op op;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
    cmeta_callable callable;
    const cmeta_type_desc *input_type;
    /**
     * Graph value type after the operator. MAP/TRANSFORM use the reflected
     * return type; FILTER preserves the input element type while the callable
     * itself returns canonical bool.
     */
    const cmeta_type_desc *output_type;
} cflow_function_projection;

/*
 * Explicit logical value adapter for native Service-style functions whose exact
 * C ABI is not itself a unary value signature. The producer supplies the
 * Request/Response CMeta descriptors and an adapter using the existing
 * cmeta_callable.invoke(self,out,args) ABI.
 *
 * Unlike cflow_function_projection, callable.meta.sig intentionally remains
 * CMETA_SIG_INVALID and resolve remains NULL. The typed-adapter projection is
 * the explicit admission marker; ordinary invalid-signature callables are not
 * admitted through normal Graph APIs.
 */
typedef struct cflow_function_typed_adapter_projection {
    size_t size;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
    cmeta_callable callable;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
} cflow_function_typed_adapter_projection;

/*
 * Control-plane admission artifact for reflected actions that are not Graph
 * operators. It proves semantic/ABI/callable consistency only; the consuming
 * runtime owns argument binding, transactional lifecycle and scheduling.
 *
 * Descriptor pointers, callable code and borrowed captures follow the same
 * provider-lifetime rules as cflow_function_projection.
 */
typedef struct cflow_function_action_projection {
    size_t size;
    const cmeta_function_desc *function;
    const cmeta_function_abi_desc *abi;
    cmeta_callable callable;
} cflow_function_action_projection;

const char *cflow_function_projection_status_string(
    cflow_function_projection_status status);

cflow_function_projection_status cflow_function_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    cflow_op op,
    cflow_function_projection *out);

cflow_function_projection_status
cflow_function_typed_adapter_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_function_typed_adapter_projection *out);

bool cflow_function_typed_adapter_projection_valid(
    const cflow_function_typed_adapter_projection *projection);

bool cflow_graph_add_function_typed_adapter_projection(
    cflow_graph *graph,
    const cflow_function_typed_adapter_projection *projection);

/*
 * Explicit typed FILTER adapter. Reuses the ABI-stable typed-adapter
 * projection artifact: input_type/output_type are both the preserved Graph
 * element type, while function->return_type is canonical bool.
 */
cflow_function_projection_status
cflow_function_typed_filter_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    cflow_function_typed_adapter_projection *out);

bool cflow_function_typed_filter_projection_valid(
    const cflow_function_typed_adapter_projection *projection);

bool cflow_graph_add_function_typed_filter_projection(
    cflow_graph *graph,
    const cflow_function_typed_adapter_projection *projection);

/*
 * Explicit homogeneous T(T,T)->T REDUCE adapter. The reflected result must
 * explicitly publish CMETA_RESULT_VALUE; REDUCE never infers value ownership
 * from C spelling or ABI carrier. Sequential admission does not require
 * associativity. Parallel eligibility remains a separate Plan property gate.
 */
cflow_function_projection_status
cflow_function_typed_reduce_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    const cmeta_type_desc *value_type,
    cflow_function_typed_adapter_projection *out);

bool cflow_function_typed_reduce_projection_valid(
    const cflow_function_typed_adapter_projection *projection);

bool cflow_graph_add_function_typed_reduce_projection(
    cflow_graph *graph,
    const cflow_function_typed_adapter_projection *projection);

cflow_function_projection_status cflow_function_action_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    cflow_function_action_projection *out);

bool cflow_function_projection_valid(
    const cflow_function_projection *projection);

bool cflow_function_action_projection_valid(
    const cflow_function_action_projection *projection);

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