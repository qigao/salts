#include <cflow/function_projection.h>

#include <string.h>

static bool projection_reflection_pair_valid(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi) {
    return cmeta_function_desc_valid(function) &&
           cmeta_function_abi_desc_valid(abi) &&
           cmeta_function_desc_equal(function, abi->function);
}

static bool projection_shape_supported(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cflow_op op) {
    const cmeta_param_desc *param;

    if (function == NULL || abi == NULL || op != CFLOW_OP_MAP ||
        function->param_count != 1u ||
        function->return_type == NULL ||
        function->return_type->kind == CMETA_T_VOID)
        return false;

    param = cmeta_function_param(function, 0u);
    return param != NULL &&
           param->flags == CMETA_PARAM_IN &&
           cmeta_function_param_abi(abi, 0u) != CMETA_ABI_UNSPECIFIED &&
           cmeta_function_param_abi(abi, 0u) != CMETA_ABI_OPAQUE &&
           abi->return_carrier != CMETA_ABI_UNSPECIFIED &&
           abi->return_carrier != CMETA_ABI_OPAQUE &&
           abi->return_carrier != CMETA_ABI_VOID;
}

const char *cflow_function_projection_status_string(
    cflow_function_projection_status status) {
    switch (status) {
        case CFLOW_FUNCTION_PROJECTION_OK: return "ok";
        case CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT:
            return "invalid argument";
        case CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION:
            return "invalid function reflection";
        case CFLOW_FUNCTION_PROJECTION_INVALID_ABI:
            return "invalid function ABI";
        case CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR:
            return "unsupported CFlow operator";
        case CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE:
            return "unsupported reflected function shape";
        case CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER:
            return "invalid executable adapter";
        case CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH:
            return "adapter type mismatch";
        case CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH:
            return "adapter semantic contract mismatch";
    }
    return "unknown projection status";
}

cflow_function_projection_status cflow_function_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    cflow_op op,
    cflow_function_projection *out) {
    const cmeta_param_desc *param;
    const cmeta_sig_desc *signature;
    cmeta_callable bound;

    if (out == NULL)
        return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    if (!cmeta_function_desc_valid(function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION;
    if (!cmeta_function_abi_desc_valid(abi) ||
        !cmeta_function_desc_equal(function, abi->function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ABI;

    /*
     * First proven execution shape is a unary value transform. Do not infer
     * FILTER/REDUCE/FLAT_MAP intent from a C signature alone.
     */
    if (op != CFLOW_OP_MAP)
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR;
    if (!projection_shape_supported(function, abi, op))
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;

    param = cmeta_function_param(function, 0u);

    if (!cmeta_callable_bind(adapter, &bound))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;
    signature = cmeta_fn_signature(bound.meta);
    if (signature == NULL ||
        signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count != 1u ||
        !cflow_op_signature_allowed(op, bound.meta.sig))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;

    if (!cmeta_type_equal(signature->params[0], param->type) ||
        !cmeta_type_equal(signature->return_type, function->return_type))
        return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;

    if (bound.meta.effects != function->effects ||
        bound.meta.properties != function->properties)
        return CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH;

    out->size = sizeof(*out);
    out->op = op;
    out->function = function;
    out->abi = abi;
    out->callable = bound;
    out->input_type = param->type;
    out->output_type = function->return_type;
    return CFLOW_FUNCTION_PROJECTION_OK;
}

bool cflow_function_projection_valid(
    const cflow_function_projection *projection) {
    const cmeta_sig_desc *signature;

    if (projection == NULL || projection->size < sizeof(*projection) ||
        !projection_reflection_pair_valid(
            projection->function, projection->abi) ||
        !projection_shape_supported(
            projection->function, projection->abi, projection->op) ||
        !cmeta_callable_contract_valid(projection->callable))
        return false;

    signature = cmeta_fn_signature(projection->callable.meta);
    return signature != NULL &&
           signature->protocol == CMETA_FN_PROTOCOL_VALUE &&
           signature->param_count == 1u &&
           cflow_op_signature_allowed(projection->op,
                                      projection->callable.meta.sig) &&
           cmeta_type_equal(signature->params[0], projection->input_type) &&
           cmeta_type_equal(signature->return_type, projection->output_type) &&
           cmeta_type_equal(projection->input_type,
                            projection->function->params[0].type) &&
           cmeta_type_equal(projection->output_type,
                            projection->function->return_type) &&
           projection->callable.meta.effects ==
               projection->function->effects &&
           projection->callable.meta.properties ==
               projection->function->properties;
}

bool cflow_graph_add_function_projection(
    cflow_graph *graph,
    const cflow_function_projection *projection) {
    if (graph == NULL || !cflow_function_projection_valid(projection))
        return false;
    return cflow_graph_add(
        graph, projection->op, projection->callable, NULL);
}
