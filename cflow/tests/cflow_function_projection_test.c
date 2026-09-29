#include <cflow/function_projection.h>
#include <cflow/adapters.h>
#include <cflow/effect.h>
#include <cflow/plan.h>

#include "tinytest.h"

#include <stdbool.h>
#include <string.h>

FunctionDecl(value, int, cflow_projection_local,
    (int, request, CMETA_PARAM_IN));

int cflow_projection_local(int request) {
    return request + 1;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_local);

FunctionDecl(value, int, cflow_projection_mock,
    (int, request, CMETA_PARAM_IN));

int cflow_projection_mock(int request) {
    return request + 100;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_mock);

FunctionDecl(value, bool, cflow_projection_positive,
    (int, request, CMETA_PARAM_IN));

bool cflow_projection_positive(int request) {
    return request > 0;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_positive);

FunctionDecl(value, bool, cflow_projection_even,
    (int, request, CMETA_PARAM_IN));

bool cflow_projection_even(int request) {
    return (request % 2) == 0;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_even);

FunctionDecl(stateful, int, cflow_projection_stateful,
    (int, request, CMETA_PARAM_IN));

int cflow_projection_stateful(int request) {
    return request * 2;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_stateful);

FunctionDecl(value, long, cflow_projection_type_mismatch,
    (int, request, CMETA_PARAM_IN));

long cflow_projection_type_mismatch(int request) {
    return (long)request;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_type_mismatch);

FunctionDecl(value, long, cflow_projection_binary,
    (long, left, CMETA_PARAM_IN),
    (long, right, CMETA_PARAM_IN));

long cflow_projection_binary(long left, long right) {
    return left + right;
}

CFLOW_REFLECTED_ADAPTER(cflow_projection_binary);

FunctionDeclAsAbi(fallible, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cflow_projection_out,
    (int *, output, CMETA_PARAM_OUT,
     &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

Function0Decl(value, int, cflow_projection_zero);

typedef struct cflow_service_request {
    int value;
} cflow_service_request;

typedef struct cflow_service_response {
    int value;
} cflow_service_response;

static const cmeta_type_traits cflow_service_value_traits = {
    .flags = CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY
};

static const cmeta_type_desc cflow_service_request_type = {
    .name = "cflow_service_request",
    .size = sizeof(cflow_service_request),
    .align = _Alignof(cflow_service_request),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_service_value_traits,
    .identity = NULL
};

static const cmeta_type_desc cflow_service_response_type = {
    .name = "cflow_service_response",
    .size = sizeof(cflow_service_response),
    .align = _Alignof(cflow_service_response),
    .kind = CMETA_T_OBJECT,
    .pointee = NULL,
    .traits = &cflow_service_value_traits,
    .identity = NULL
};

static const cmeta_type_desc cflow_service_request_ptr_type = {
    .name = "cflow_service_request *",
    .size = sizeof(cflow_service_request *),
    .align = _Alignof(cflow_service_request *),
    .kind = CMETA_T_POINTER,
    .pointee = &cflow_service_request_type,
    .traits = NULL,
    .identity = NULL
};

static const cmeta_type_desc cflow_service_response_ptr_type = {
    .name = "cflow_service_response *",
    .size = sizeof(cflow_service_response *),
    .align = _Alignof(cflow_service_response *),
    .kind = CMETA_T_POINTER,
    .pointee = &cflow_service_response_type,
    .traits = NULL,
    .identity = NULL
};

FunctionDeclAsAbi(fallible, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cflow_projection_service,
    (cflow_service_request *, request, CMETA_PARAM_IN,
     &cflow_service_request_ptr_type, CMETA_ABI_OBJECT_POINTER),
    (cflow_service_response *, response, CMETA_PARAM_OUT,
     &cflow_service_response_ptr_type, CMETA_ABI_OBJECT_POINTER));

int cflow_projection_service(
    cflow_service_request *request,
    cflow_service_response *response) {
    if (request == NULL || response == NULL) return -1;
    response->value = request->value + 7;
    return 0;
}

static bool cflow_service_local_invoke(
    const cmeta_callable *self,
    void *out,
    const void *const *args) {
    cflow_service_response response = {0};
    int status;
    (void)self;
    if (out == NULL || args == NULL || args[0] == NULL)
        return false;
    status = cflow_projection_service(
        (cflow_service_request *)args[0], &response);
    if (status != 0) return false;
    *(cflow_service_response *)out = response;
    return true;
}

static bool cflow_service_mock_invoke(
    const cmeta_callable *self,
    void *out,
    const void *const *args) {
    const cflow_service_request *request;
    (void)self;
    if (out == NULL || args == NULL || args[0] == NULL)
        return false;
    request = (const cflow_service_request *)args[0];
    ((cflow_service_response *)out)->value = request->value + 100;
    return true;
}

static cmeta_callable cflow_service_adapter(cmeta_callable_invoke_fn invoke) {
    cmeta_callable adapter = {0};
    const cmeta_function_desc *function =
        FunctionMeta(cflow_projection_service);
    adapter.meta.effects = function->effects;
    adapter.meta.properties = function->properties;
    adapter.invoke = invoke;
    adapter.dispatch = CMETA_CALLABLE_DISPATCH_ADAPTER;
    return adapter;
}

static void check_int_result(
    const cflow_result *result,
    const int *expected,
    size_t count) {
    check_not_null(result);
    check_equal(result->count, count);
    check_true(cmeta_type_equal(result->type, &cmeta_type_int));
    check_equal(result->data, expected, count * sizeof(*expected));
}

suite("CFlow reflected function projection") {
    it("admits explicit Request to Response adapters without a finite callable signature") {
        cflow_function_typed_adapter_projection local_projection = {0};
        cflow_function_typed_adapter_projection mock_projection = {0};
        cflow_graph local_graph = {0};
        cflow_graph mock_graph = {0};
        cflow_plan local_plan = {0};
        cflow_plan mock_plan = {0};
        cflow_result local_result = {0};
        cflow_result mock_result = {0};
        cflow_result local_compiled = {0};
        cflow_result mock_compiled = {0};
        const cflow_subgraph *local_root;
        const cflow_subgraph *mock_root;
        const cflow_service_request input[] = {{1}, {2}, {3}};
        const cflow_service_response expected_local[] = {{8}, {9}, {10}};
        const cflow_service_response expected_mock[] = {{101}, {102}, {103}};

        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                cflow_service_adapter(cflow_service_local_invoke),
                &cflow_service_request_type,
                &cflow_service_response_type,
                &local_projection),
            CFLOW_FUNCTION_PROJECTION_OK);
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                cflow_service_adapter(cflow_service_mock_invoke),
                &cflow_service_request_type,
                &cflow_service_response_type,
                &mock_projection),
            CFLOW_FUNCTION_PROJECTION_OK);

        check_true(cflow_function_typed_adapter_projection_valid(
            &local_projection));
        check_true(cflow_function_typed_adapter_projection_valid(
            &mock_projection));
        check_equal(local_projection.callable.meta.sig, CMETA_SIG_INVALID);
        check_equal(mock_projection.callable.meta.sig, CMETA_SIG_INVALID);
        check_null(local_projection.callable.resolve);
        check_null(mock_projection.callable.resolve);
        check_false(cmeta_callable_contract_valid(local_projection.callable));

        cflow_graph_init(&local_graph, &cflow_service_request_type);
        cflow_graph_init(&mock_graph, &cflow_service_request_type);
        check_true(cflow_graph_add_function_typed_adapter_projection(
            &local_graph, &local_projection));
        check_true(cflow_graph_add_function_typed_adapter_projection(
            &mock_graph, &mock_projection));

        local_root = cflow_graph_subgraph(&local_graph, local_graph.root);
        mock_root = cflow_graph_subgraph(&mock_graph, mock_graph.root);
        check_not_null(local_root);
        check_not_null(mock_root);
        check_equal(local_root->node_count, (size_t)2u);
        check_equal(mock_root->node_count, (size_t)2u);
        check_equal(local_root->nodes[1].op, CFLOW_OP_MAP);
        check_equal(mock_root->nodes[1].op, CFLOW_OP_MAP);
        check_equal(
            local_root->nodes[1].param_kind,
            CFLOW_NODE_PARAM_TYPED_ADAPTER);
        check_equal(
            mock_root->nodes[1].param_kind,
            CFLOW_NODE_PARAM_TYPED_ADAPTER);
        check_equal(local_root->nodes[1].fn_chain_count, (size_t)0u);
        check_equal(mock_root->nodes[1].fn_chain_count, (size_t)0u);
        check_true(cmeta_type_equal(
            local_root->nodes[1].input_type, &cflow_service_request_type));
        check_true(cmeta_type_equal(
            local_root->nodes[1].output_type, &cflow_service_response_type));

        check_true(cflow_eval_array(
            &local_graph, input, 3u, &local_result));
        check_true(cflow_eval_array(
            &mock_graph, input, 3u, &mock_result));
        check_equal(local_result.count, (size_t)3u);
        check_equal(mock_result.count, (size_t)3u);
        check_true(cmeta_type_equal(
            local_result.type, &cflow_service_response_type));
        check_true(cmeta_type_equal(
            mock_result.type, &cflow_service_response_type));
        check_equal(
            local_result.data, expected_local, sizeof(expected_local));
        check_equal(
            mock_result.data, expected_mock, sizeof(expected_mock));

        check_true(cflow_plan_compile_surface(
            &local_plan, &local_graph, NULL));
        check_true(cflow_plan_compile_surface(
            &mock_plan, &mock_graph, NULL));
        check_true(cflow_plan_eval_array(
            &local_plan, input, 3u, &local_compiled));
        check_true(cflow_plan_eval_array(
            &mock_plan, input, 3u, &mock_compiled));
        check_equal(
            local_compiled.data, expected_local, sizeof(expected_local));
        check_equal(
            mock_compiled.data, expected_mock, sizeof(expected_mock));

        cflow_result_destroy(&local_result);
        cflow_result_destroy(&mock_result);
        cflow_result_destroy(&local_compiled);
        cflow_result_destroy(&mock_compiled);
        cflow_plan_destroy(&local_plan);
        cflow_plan_destroy(&mock_plan);
        cflow_graph_destroy(&local_graph);
        cflow_graph_destroy(&mock_graph);
    }

    it("rejects malformed explicit typed adapters and mismatched contracts") {
        cflow_function_typed_adapter_projection projection = {0};
        cmeta_callable adapter =
            cflow_service_adapter(cflow_service_local_invoke);
        cmeta_function_abi_desc bad_abi =
            *FunctionAbi(cflow_projection_service);
        cmeta_type_desc zero_size = cflow_service_request_type;

        adapter.invoke = NULL;
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                adapter,
                &cflow_service_request_type,
                &cflow_service_response_type,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER);

        adapter = cflow_service_adapter(cflow_service_local_invoke);
        adapter.dispatch = CMETA_CALLABLE_DISPATCH_CANONICAL_RAW;
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                adapter,
                &cflow_service_request_type,
                &cflow_service_response_type,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER);

        adapter = cflow_service_adapter(cflow_service_local_invoke);
        adapter.meta.effects = CMETA_EFFECT_UNKNOWN;
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                adapter,
                &cflow_service_request_type,
                &cflow_service_response_type,
                &projection),
            CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH);

        adapter = cflow_service_adapter(cflow_service_local_invoke);
        bad_abi.return_carrier = CMETA_ABI_VOID;
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                &bad_abi,
                adapter,
                &cflow_service_request_type,
                &cflow_service_response_type,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ABI);

        zero_size.size = 0u;
        check_equal(
            cflow_function_typed_adapter_projection_admit(
                FunctionMeta(cflow_projection_service),
                FunctionAbi(cflow_projection_service),
                adapter,
                &zero_size,
                &cflow_service_response_type,
                &projection),
            CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH);
    }

    it("admits local and mock adapters without changing Graph topology") {
        cflow_function_projection local_projection = {0};
        cflow_function_projection mock_projection = {0};
        cflow_graph local_graph = {0};
        cflow_graph mock_graph = {0};
        cflow_plan local_plan = {0};
        cflow_plan mock_plan = {0};
        cflow_result local_result = {0};
        cflow_result mock_result = {0};
        cflow_result local_compiled = {0};
        cflow_result mock_compiled = {0};
        const cflow_subgraph *local_root;
        const cflow_subgraph *mock_root;
        const int input[] = {1, 2, 3};
        const int expected_local[] = {2, 3, 4};
        const int expected_mock[] = {101, 102, 103};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                CFLOW_OP_MAP,
                &local_projection),
            CFLOW_FUNCTION_PROJECTION_OK);

        /*
         * The mock backend has a different C target but the same executable
         * signature and semantic contract. The canonical operation identity
         * remains cflow_projection_local.
         */
        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_mock),
                CFLOW_OP_MAP,
                &mock_projection),
            CFLOW_FUNCTION_PROJECTION_OK);

        check_true(cflow_function_projection_valid(&local_projection));
        check_true(cflow_function_projection_valid(&mock_projection));
        check_true(cmeta_function_desc_equal(
            local_projection.function, mock_projection.function));
        check_false(cmeta_callable_same(
            local_projection.callable, mock_projection.callable));

        cflow_graph_init(&local_graph, &cmeta_type_int);
        cflow_graph_init(&mock_graph, &cmeta_type_int);
        check_true(cflow_graph_add_function_projection(
            &local_graph, &local_projection));
        check_true(cflow_graph_add_function_projection(
            &mock_graph, &mock_projection));

        /*
         * Graph owns the already-bound callable/type projection. Reflection
         * descriptors are control-plane admission inputs only.
         */
        local_projection = (cflow_function_projection){0};
        mock_projection = (cflow_function_projection){0};

        local_root = cflow_graph_subgraph(&local_graph, local_graph.root);
        mock_root = cflow_graph_subgraph(&mock_graph, mock_graph.root);
        check_not_null(local_root);
        check_not_null(mock_root);
        check_equal(local_root->node_count, mock_root->node_count);
        check_equal(local_root->node_count, (size_t)2);
        check_equal(local_root->nodes[1].op, CFLOW_OP_MAP);
        check_equal(mock_root->nodes[1].op, CFLOW_OP_MAP);
        check_true(cmeta_type_equal(
            local_root->nodes[1].input_type,
            mock_root->nodes[1].input_type));
        check_true(cmeta_type_equal(
            local_root->nodes[1].output_type,
            mock_root->nodes[1].output_type));

        check_true(cflow_eval_array(
            &local_graph, input, 3u, &local_result));
        check_true(cflow_eval_array(
            &mock_graph, input, 3u, &mock_result));
        check_int_result(&local_result, expected_local, 3u);
        check_int_result(&mock_result, expected_mock, 3u);

        check_true(cflow_plan_compile_surface(
            &local_plan, &local_graph, NULL));
        check_true(cflow_plan_compile_surface(
            &mock_plan, &mock_graph, NULL));
        check_true(cflow_plan_eval_array(
            &local_plan, input, 3u, &local_compiled));
        check_true(cflow_plan_eval_array(
            &mock_plan, input, 3u, &mock_compiled));
        check_int_result(&local_compiled, expected_local, 3u);
        check_int_result(&mock_compiled, expected_mock, 3u);

        cflow_result_destroy(&local_result);
        cflow_result_destroy(&mock_result);
        cflow_result_destroy(&local_compiled);
        cflow_result_destroy(&mock_compiled);
        cflow_plan_destroy(&local_plan);
        cflow_plan_destroy(&mock_plan);
        cflow_graph_destroy(&local_graph);
        cflow_graph_destroy(&mock_graph);
    }

    it("does not infer FILTER intent when MAP policy rejects a bool return") {
        cflow_function_projection projection = {0};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_positive),
                FunctionAbi(cflow_projection_positive),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_positive),
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER);
        check_false(cflow_function_projection_valid(&projection));
    }

    it("admits reflected FILTER predicates while preserving the element type") {
        cflow_function_projection local_projection = {0};
        cflow_function_projection mock_projection = {0};
        cflow_graph local_graph = {0};
        cflow_graph mock_graph = {0};
        cflow_plan local_plan = {0};
        cflow_plan mock_plan = {0};
        cflow_result local_result = {0};
        cflow_result mock_result = {0};
        cflow_result local_compiled = {0};
        cflow_result mock_compiled = {0};
        const int input[] = {-2, -1, 0, 1, 2, 3};
        const int expected_positive[] = {1, 2, 3};
        const int expected_even[] = {-2, 0, 2};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_positive),
                FunctionAbi(cflow_projection_positive),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_positive),
                CFLOW_OP_FILTER,
                &local_projection),
            CFLOW_FUNCTION_PROJECTION_OK);
        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_positive),
                FunctionAbi(cflow_projection_positive),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_even),
                CFLOW_OP_FILTER,
                &mock_projection),
            CFLOW_FUNCTION_PROJECTION_OK);

        check_true(cflow_function_projection_valid(&local_projection));
        check_true(cflow_function_projection_valid(&mock_projection));
        check_true(cmeta_type_equal(
            local_projection.function->return_type, &cmeta_type_bool));
        check_true(cmeta_type_equal(
            local_projection.input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(
            local_projection.output_type, &cmeta_type_int));
        check_true(cmeta_type_equal(
            mock_projection.output_type, &cmeta_type_int));

        cflow_graph_init(&local_graph, &cmeta_type_int);
        cflow_graph_init(&mock_graph, &cmeta_type_int);
        check_true(cflow_graph_add_function_projection(
            &local_graph, &local_projection));
        check_true(cflow_graph_add_function_projection(
            &mock_graph, &mock_projection));

        check_true(cflow_eval_array(
            &local_graph, input, 6u, &local_result));
        check_true(cflow_eval_array(
            &mock_graph, input, 6u, &mock_result));
        check_int_result(&local_result, expected_positive, 3u);
        check_int_result(&mock_result, expected_even, 3u);

        check_true(cflow_plan_compile_surface(
            &local_plan, &local_graph, NULL));
        check_true(cflow_plan_compile_surface(
            &mock_plan, &mock_graph, NULL));
        check_true(cflow_plan_eval_array(
            &local_plan, input, 6u, &local_compiled));
        check_true(cflow_plan_eval_array(
            &mock_plan, input, 6u, &mock_compiled));
        check_int_result(&local_compiled, expected_positive, 3u);
        check_int_result(&mock_compiled, expected_even, 3u);

        cflow_result_destroy(&local_result);
        cflow_result_destroy(&mock_result);
        cflow_result_destroy(&local_compiled);
        cflow_result_destroy(&mock_compiled);
        cflow_plan_destroy(&local_plan);
        cflow_plan_destroy(&mock_plan);
        cflow_graph_destroy(&local_graph);
        cflow_graph_destroy(&mock_graph);
    }

    it("admits reflected TRANSFORM within the generated signature policy") {
        cflow_function_projection projection = {0};
        cflow_graph graph = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        const int input[] = {1, 2, 3};
        const long expected[] = {1L, 2L, 3L};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_type_mismatch),
                FunctionAbi(cflow_projection_type_mismatch),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_type_mismatch),
                CFLOW_OP_TRANSFORM,
                &projection),
            CFLOW_FUNCTION_PROJECTION_OK);
        check_true(cflow_function_projection_valid(&projection));
        check_true(cmeta_type_equal(projection.input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(projection.output_type, &cmeta_type_long));

        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_graph_add_function_projection(&graph, &projection));
        check_true(cflow_plan_compile_surface(&plan, &graph, NULL));
        check_true(cflow_plan_eval_array(&plan, input, 3u, &result));
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&graph);
    }

    it("preserves effectful FunctionDesc semantics as a Graph barrier") {
        cflow_function_projection projection = {0};
        cflow_graph graph = {0};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_stateful),
                FunctionAbi(cflow_projection_stateful),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_stateful),
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_OK);

        check_equal(projection.callable.meta.effects,
                    (cmeta_effects)CMETA_EFFECT_STATEFUL);

        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cflow_graph_add_function_projection(&graph, &projection));
        check_true((cflow_graph_effects(&graph) & CMETA_EFFECT_STATEFUL) != 0u);
        cflow_graph_destroy(&graph);
    }

    it("admits reflected actions without inventing a Graph operator") {
        cflow_function_action_projection binary = {0};

        check_equal(
            cflow_function_action_projection_admit(
                FunctionMeta(cflow_projection_binary),
                FunctionAbi(cflow_projection_binary),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_binary),
                &binary),
            CFLOW_FUNCTION_PROJECTION_OK);
        check_true(cflow_function_action_projection_valid(&binary));
        check_equal(binary.function->param_count, (size_t)2);
    }

    it("rejects reflected action adapter mismatches") {
        cflow_function_action_projection projection = {0};

        check_equal(
            cflow_function_action_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_type_mismatch),
                &projection),
            CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH);

        check_equal(
            cflow_function_action_projection_admit(
                FunctionMeta(cflow_projection_stateful),
                FunctionAbi(cflow_projection_stateful),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                &projection),
            CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH);

        projection = (cflow_function_action_projection){
            sizeof(cflow_function_action_projection),
            FunctionMeta(cflow_projection_zero),
            FunctionAbi(cflow_projection_zero),
            {0}};
        check_false(cflow_function_action_projection_valid(&projection));
    }

    it("rejects shapes that are not unary IN value transforms") {
        cflow_function_projection projection = {0};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_binary),
                FunctionAbi(cflow_projection_binary),
                (cmeta_callable){0},
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_out),
                FunctionAbi(cflow_projection_out),
                (cmeta_callable){0},
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_zero),
                FunctionAbi(cflow_projection_zero),
                (cmeta_callable){0},
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                CFLOW_OP_FILTER,
                &projection),
            CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_SHAPE);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                CFLOW_OP_REDUCE,
                &projection),
            CFLOW_FUNCTION_PROJECTION_UNSUPPORTED_OPERATOR);
    }

    it("rejects adapter type and semantic contract mismatches") {
        cflow_function_projection projection = {0};

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_type_mismatch),
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_TYPE_MISMATCH);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_stateful),
                FunctionAbi(cflow_projection_stateful),
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_CONTRACT_MISMATCH);

        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                FunctionAbi(cflow_projection_local),
                (cmeta_callable){0},
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ADAPTER);
    }

    it("validates arbitrary projection values without assuming unary metadata") {
        cflow_function_projection malformed = {
            sizeof(cflow_function_projection),
            CFLOW_OP_MAP,
            FunctionMeta(cflow_projection_zero),
            FunctionAbi(cflow_projection_zero),
            {0},
            &cmeta_type_int,
            &cmeta_type_int
        };

        check_false(cflow_function_projection_valid(&malformed));
    }

    it("rejects inconsistent ABI sidecars") {
        cflow_function_projection projection = {0};
        cmeta_function_abi_desc bad =
            *FunctionAbi(cflow_projection_local);

        bad.return_carrier = CMETA_ABI_VOID;
        check_equal(
            cflow_function_projection_admit(
                FunctionMeta(cflow_projection_local),
                &bad,
                CFLOW_REFLECTED_CALLABLE(cflow_projection_local),
                CFLOW_OP_MAP,
                &projection),
            CFLOW_FUNCTION_PROJECTION_INVALID_ABI);

        check_equal(
            cflow_function_projection_status_string(
                CFLOW_FUNCTION_PROJECTION_INVALID_ABI),
            "invalid function ABI");
    }
}
