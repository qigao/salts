#include <cflow/function_projection.h>
#include <cflow/adapters.h>
#include <cflow/effect.h>
#include <cflow/plan.h>

#include "tinytest.h"

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

FunctionDeclAsAbi(fallible, int, &cmeta_type_int, CMETA_ABI_SCALAR,
                  cflow_projection_out,
    (int *, output, CMETA_PARAM_OUT,
     &cmeta_type_int_ptr, CMETA_ABI_OBJECT_POINTER));

FunctionDecl(stateful, void, cflow_projection_void,
    (int, request, CMETA_PARAM_IN));

Function0Decl(value, int, cflow_projection_zero);

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
                FunctionMeta(cflow_projection_void),
                FunctionAbi(cflow_projection_void),
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
