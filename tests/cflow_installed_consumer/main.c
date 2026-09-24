#include <cflow/adapters.h>
#include <cflow/function_projection.h>

#include <stddef.h>

FunctionDecl(value, int, installed_increment,
    (int, value, CMETA_PARAM_IN));

int installed_increment(int value) {
    return value + 1;
}

CFLOW_REFLECTED_ADAPTER(installed_increment);

int main(void) {
    cflow_function_projection projection = {0};
    cflow_graph graph = {0};
    cflow_result result = {0};
    const int input = 41;
    int status = 1;

    if (cflow_function_projection_admit(
            FunctionMeta(installed_increment),
            FunctionAbi(installed_increment),
            CFLOW_REFLECTED_CALLABLE(installed_increment),
            CFLOW_OP_MAP,
            &projection) != CFLOW_FUNCTION_PROJECTION_OK)
        goto cleanup;

    if (!cflow_function_projection_valid(&projection))
        goto cleanup;

    cflow_graph_init(&graph, &cmeta_type_int);
    if (graph.root == CMETA_INVALID_ID)
        goto cleanup;

    if (!cflow_graph_add_function_projection(&graph, &projection))
        goto cleanup;

    if (!cflow_eval_array(&graph, &input, 1u, &result))
        goto cleanup;

    if (result.count != 1u ||
        !cmeta_type_equal(result.type, &cmeta_type_int) ||
        result.data == NULL ||
        *(const int *)result.data != 42)
        goto cleanup;

    status = 0;

cleanup:
    cflow_result_destroy(&result);
    cflow_graph_destroy(&graph);
    return status;
}
