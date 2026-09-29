#include <cflow/function_projection.h>

#include "graph_internal.h"

#include <string.h>

static bool projection_reflection_pair_valid(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi) {
    return cmeta_function_desc_valid(function) &&
           cmeta_function_abi_desc_valid(abi) &&
           cmeta_function_desc_equal(function, abi->function);
}

static cflow_function_projection_status projection_adapter_admit(
    const cmeta_function_desc *function,
    cmeta_callable adapter,
    cmeta_callable *out_bound) {
    const cmeta_sig_desc *signature;
    cmeta_callable bound;
    size_t parameter;

    if (!cmeta_callable_bind(adapter, &bound))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;

    signature = cmeta_fn_signature(bound.meta);
    if (signature == NULL ||
        signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count != function->param_count)
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;

    if (!cmeta_type_equal(signature->return_type, function->return_type))
        return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;

    for (parameter = 0u; parameter < function->param_count; ++parameter) {
        const cmeta_param_desc *param =
            cmeta_function_param(function, parameter);
        if (param == NULL ||
            !cmeta_type_equal(signature->params[parameter], param->type))
            return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;
    }

    if (bound.meta.effects != function->effects ||
        bound.meta.properties != function->properties)
        return CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH;

    if (out_bound != NULL) *out_bound = bound;
    return CFLOW_FUNCTION_PROJECTION_OK;
}

static bool projection_operator_supported(cflow_op op) {
    return op == CFLOW_OP_MAP ||
           op == CFLOW_OP_TRANSFORM ||
           op == CFLOW_OP_FILTER;
}

static bool projection_shape_supported(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cflow_op op) {
    const cmeta_param_desc *param;

    if (function == NULL || abi == NULL || !projection_operator_supported(op) ||
        function->param_count != 1u ||
        function->return_type == NULL ||
        function->return_type->kind == CMETA_T_VOID)
        return false;

    param = cmeta_function_param(function, 0u);
    if (param == NULL ||
        param->flags != CMETA_PARAM_IN ||
        cmeta_function_param_abi(abi, 0u) == CMETA_ABI_UNSPECIFIED ||
        cmeta_function_param_abi(abi, 0u) == CMETA_ABI_OPAQUE ||
        abi->return_carrier == CMETA_ABI_UNSPECIFIED ||
        abi->return_carrier == CMETA_ABI_OPAQUE ||
        abi->return_carrier == CMETA_ABI_VOID)
        return false;

    if (op == CFLOW_OP_FILTER)
        return cmeta_type_equal(function->return_type, &cmeta_type_bool);

    return true;
}

static const cmeta_type_desc *projection_graph_output_type(
    const cmeta_function_desc *function,
    const cmeta_param_desc *param,
    cflow_op op) {
    if (!function || !param) return NULL;
    return op == CFLOW_OP_FILTER ? param->type : function->return_type;
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
     * The caller explicitly selects semantic intent. MAP/TRANSFORM use the
     * reflected return value as Graph output; FILTER requires a bool predicate
     * but preserves the input element type. Do not infer FILTER intent merely
     * because a function returns bool.
     */
    if (!projection_operator_supported(op))
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR;
    if (!projection_shape_supported(function, abi, op))
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;

    param = cmeta_function_param(function, 0u);

    {
        const cflow_function_projection_status adapter_status =
            projection_adapter_admit(function, adapter, &bound);
        if (adapter_status != CFLOW_FUNCTION_PROJECTION_OK)
            return adapter_status;
    }
    signature = cmeta_fn_signature(bound.meta);
    if (signature == NULL ||
        !cflow_op_signature_allowed(op, bound.meta.sig))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;

    out->size = sizeof(*out);
    out->op = op;
    out->function = function;
    out->abi = abi;
    out->callable = bound;
    out->input_type = param->type;
    out->output_type = projection_graph_output_type(function, param, op);
    return CFLOW_FUNCTION_PROJECTION_OK;
}



cflow_function_projection_status
cflow_function_typed_adapter_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    const cmeta_type_desc *output_type,
    cflow_function_typed_adapter_projection *out) {
    if (out == NULL)
        return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    if (!cmeta_function_desc_valid(function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION;
    if (!cmeta_function_abi_desc_valid(abi) ||
        !cmeta_function_desc_equal(function, abi->function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ABI;
    if (!cmeta_type_desc_valid(input_type) || input_type->size == 0u ||
        !cmeta_type_desc_valid(output_type) || output_type->size == 0u)
        return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;
    if (!cflow_graph_explicit_adapter_callable_valid(adapter))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;
    if (adapter.meta.effects != function->effects ||
        adapter.meta.properties != function->properties)
        return CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH;

    out->size = sizeof(*out);
    out->function = function;
    out->abi = abi;
    out->callable = adapter;
    out->input_type = input_type;
    out->output_type = output_type;
    return CFLOW_FUNCTION_PROJECTION_OK;
}

bool cflow_function_typed_adapter_projection_valid(
    const cflow_function_typed_adapter_projection *projection) {
    return projection != NULL &&
           projection->size >= sizeof(*projection) &&
           projection_reflection_pair_valid(
               projection->function, projection->abi) &&
           cmeta_type_desc_valid(projection->input_type) &&
           projection->input_type->size != 0u &&
           cmeta_type_desc_valid(projection->output_type) &&
           projection->output_type->size != 0u &&
           cflow_graph_explicit_adapter_callable_valid(projection->callable) &&
           projection->callable.meta.effects ==
               projection->function->effects &&
           projection->callable.meta.properties ==
               projection->function->properties;
}

bool cflow_graph_add_function_typed_adapter_projection(
    cflow_graph *graph,
    const cflow_function_typed_adapter_projection *projection) {
    if (graph == NULL ||
        !cflow_function_typed_adapter_projection_valid(projection))
        return false;
    return cflow_graph_add_explicit_map_adapter(
        graph,
        projection->callable,
        projection->input_type,
        projection->output_type);
}

cflow_function_projection_status
cflow_function_typed_filter_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    const cmeta_type_desc *input_type,
    cflow_function_typed_adapter_projection *out) {
    if (out == NULL)
        return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    if (!cmeta_function_desc_valid(function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION;
    if (!cmeta_function_abi_desc_valid(abi) ||
        !cmeta_function_desc_equal(function, abi->function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ABI;
    if (function->param_count != 1u ||
        !cmeta_type_equal(function->return_type, &cmeta_type_bool))
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;
    {
        const cmeta_param_desc *param = cmeta_function_param(function, 0u);
        const cmeta_abi_carrier carrier = cmeta_function_param_abi(abi, 0u);
        if (param == NULL ||
            (param->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_IN ||
            carrier == CMETA_ABI_UNSPECIFIED ||
            carrier == CMETA_ABI_OPAQUE)
            return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;
    }
    if (abi->return_carrier == CMETA_ABI_UNSPECIFIED ||
        abi->return_carrier == CMETA_ABI_OPAQUE ||
        abi->return_carrier == CMETA_ABI_VOID)
        return CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE;
    if (!cmeta_type_desc_valid(input_type) || input_type->size == 0u)
        return CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH;
    if (!cflow_graph_explicit_adapter_callable_valid(adapter))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER;
    if (adapter.meta.effects != function->effects ||
        adapter.meta.properties != function->properties)
        return CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH;

    out->size = sizeof(*out);
    out->function = function;
    out->abi = abi;
    out->callable = adapter;
    out->input_type = input_type;
    out->output_type = input_type;
    return CFLOW_FUNCTION_PROJECTION_OK;
}

bool cflow_function_typed_filter_projection_valid(
    const cflow_function_typed_adapter_projection *projection) {
    return projection != NULL &&
           cflow_function_typed_adapter_projection_valid(projection) &&
           projection->function->param_count == 1u &&
           cmeta_type_equal(
               projection->function->return_type, &cmeta_type_bool) &&
           cmeta_type_equal(
               projection->input_type, projection->output_type);
}

bool cflow_graph_add_function_typed_filter_projection(
    cflow_graph *graph,
    const cflow_function_typed_adapter_projection *projection) {
    if (graph == NULL ||
        !cflow_function_typed_filter_projection_valid(projection))
        return false;
    return cflow_graph_add_explicit_typed_adapter(
        graph,
        CFLOW_OP_FILTER,
        projection->callable,
        projection->input_type,
        projection->output_type);
}

cflow_function_projection_status cflow_function_action_projection_admit(
    const cmeta_function_desc *function,
    const cmeta_function_abi_desc *abi,
    cmeta_callable adapter,
    cflow_function_action_projection *out) {
    cmeta_callable bound;
    cflow_function_projection_status status;

    if (out == NULL)
        return CFLOW_FUNCTION_PROJECTION_INVALID_ARGUMENT;
    memset(out, 0, sizeof(*out));

    if (!cmeta_function_desc_valid(function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_REFLECTION;
    if (!cmeta_function_abi_desc_valid(abi) ||
        !cmeta_function_desc_equal(function, abi->function))
        return CFLOW_FUNCTION_PROJECTION_INVALID_ABI;

    status = projection_adapter_admit(function, adapter, &bound);
    if (status != CFLOW_FUNCTION_PROJECTION_OK)
        return status;

    out->size = sizeof(*out);
    out->function = function;
    out->abi = abi;
    out->callable = bound;
    return CFLOW_FUNCTION_PROJECTION_OK;
}

bool cflow_function_action_projection_valid(
    const cflow_function_action_projection *projection) {
    const cmeta_sig_desc *signature;
    size_t parameter;

    if (projection == NULL || projection->size < sizeof(*projection) ||
        !projection_reflection_pair_valid(
            projection->function, projection->abi) ||
        !cmeta_callable_contract_valid(projection->callable))
        return false;

    signature = cmeta_fn_signature(projection->callable.meta);
    if (signature == NULL ||
        signature->protocol != CMETA_FN_PROTOCOL_VALUE ||
        signature->param_count != projection->function->param_count ||
        !cmeta_type_equal(signature->return_type,
                          projection->function->return_type) ||
        projection->callable.meta.effects !=
            projection->function->effects ||
        projection->callable.meta.properties !=
            projection->function->properties)
        return false;

    for (parameter = 0u; parameter < projection->function->param_count;
         ++parameter) {
        const cmeta_param_desc *param =
            cmeta_function_param(projection->function, parameter);
        if (param == NULL ||
            !cmeta_type_equal(signature->params[parameter], param->type))
            return false;
    }
    return true;
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
           cmeta_type_equal(signature->return_type,
                            projection->function->return_type) &&
           cmeta_type_equal(projection->input_type,
                            projection->function->params[0].type) &&
           cmeta_type_equal(
               projection->output_type,
               projection_graph_output_type(
                   projection->function,
                   &projection->function->params[0],
                   projection->op)) &&
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