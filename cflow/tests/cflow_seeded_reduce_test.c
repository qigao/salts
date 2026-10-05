#include <cflow/cflow.h>
#include <cflow/certificate.h>
#include <cflow/opt.h>
#include "tinytest.h"

cmeta_function(reduce, value, long, cflow_seeded_subtract,
      (long accumulator, long value)) {
    return accumulator - value;
}

cmeta_function(reduce, associative, long, cflow_seeded_add,
      (long accumulator, long value)) {
    return accumulator + value;
}

static const cflow_node *seeded_tail(const cflow_graph *graph) {
    const cflow_subgraph *root =
        graph ? cflow_graph_subgraph(graph, graph->root) : NULL;
    return root ? cflow_subgraph_node(root, root->tail) : NULL;
}

static void check_long_result(const cflow_result *result, long expected) {
    check_not_null(result);
    check_equal(result->count, (size_t)1u);
    check_true(cmeta_type_equal(result->type, &cmeta_type_long));
    check_not_null(result->data);
    check_equal(((const long *)result->data)[0], expected);
}

suite("CFlow seeded reduce") {
    it("owns and preserves an inspectable typed seed") {
        cflow_graph surface = {0};
        cflow_graph clone = {0};
        cflow_graph normalized = {0};
        cflow_graph optimized = {0};
        const cflow_node *source_node;
        const cflow_node *clone_node;
        const cflow_node *normalized_node;
        const cflow_node *optimized_node;
        const char *error = NULL;
        long seed = 100L;

        surface.root = clone.root = normalized.root = optimized.root =
            CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_long);
        check_true(cflow_graph_reduce_seeded(
            &surface, cflow_seeded_subtract.fn, &seed));
        seed = 999L;

        check_true(cflow_graph_validate(&surface, &error));
        check_null(error);
        source_node = seeded_tail(&surface);
        check_not_null(source_node);
        check_equal(source_node->op, CFLOW_OP_REDUCE);
        check_equal(source_node->param_kind,
                    CFLOW_NODE_PARAM_REDUCE_SEED);
        check_not_null(cflow_node_reduce_seed(source_node));
        check_equal(*(const long *)cflow_node_reduce_seed(source_node), 100L);

        check_true(cflow_graph_clone(&clone, &surface));
        check_true(cflow_graph_structural_equal(&surface, &clone));
        clone_node = seeded_tail(&clone);
        check_not_null(clone_node);
        check_not_equal(cflow_node_reduce_seed(source_node),
                        cflow_node_reduce_seed(clone_node));
        check_equal(*(const long *)cflow_node_reduce_seed(clone_node), 100L);

        check_true(cflow_graph_normalize(&normalized, &surface));
        normalized_node = seeded_tail(&normalized);
        check_not_null(normalized_node);
        check_equal(normalized_node->param_kind,
                    CFLOW_NODE_PARAM_REDUCE_SEED);
        check_equal(*(const long *)cflow_node_reduce_seed(normalized_node),
                    100L);

        check_true(cflow_graph_optimize(
            &optimized, &normalized,
            (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL));
        optimized_node = seeded_tail(&optimized);
        check_not_null(optimized_node);
        check_equal(optimized_node->param_kind,
                    CFLOW_NODE_PARAM_REDUCE_SEED);
        check_equal(*(const long *)cflow_node_reduce_seed(optimized_node),
                    100L);

        cflow_graph_destroy(&optimized);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&clone);
        cflow_graph_destroy(&surface);
    }

    it("keeps ordered seed semantics in Graph and compiled Plan") {
        const long input[] = {1L, 2L, 3L};
        const long single[] = {7L};
        long seed = 100L;
        cflow_graph surface = {0};
        cflow_plan plan = {0};
        cflow_result graph_result = {0};
        cflow_result plan_result = {0};

        surface.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_long);
        check_true(cflow_graph_reduce_seeded(
            &surface, cflow_seeded_subtract.fn, &seed));
        check_true(cflow_plan_compile_surface(&plan, &surface, NULL));

        check_true(cflow_eval_array(
            &surface, input, sizeof(input) / sizeof(input[0]), &graph_result));
        check_true(cflow_plan_eval_array(
            &plan, input, sizeof(input) / sizeof(input[0]), &plan_result));
        check_long_result(&graph_result, 94L);
        check_long_result(&plan_result, 94L);
        cflow_result_destroy(&graph_result);
        cflow_result_destroy(&plan_result);

        check_true(cflow_eval_array(&surface, single, 1u, &graph_result));
        check_true(cflow_plan_eval_array(&plan, single, 1u, &plan_result));
        check_long_result(&graph_result, 93L);
        check_long_result(&plan_result, 93L);
        cflow_result_destroy(&graph_result);
        cflow_result_destroy(&plan_result);

        check_true(cflow_eval_array(&surface, NULL, 0u, &graph_result));
        check_true(cflow_plan_eval_array(&plan, NULL, 0u, &plan_result));
        check_long_result(&graph_result, 100L);
        check_long_result(&plan_result, 100L);
        cflow_result_destroy(&graph_result);
        cflow_result_destroy(&plan_result);

        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&surface);
    }

    it("does not infer parallel reassociation from a seeded associative reducer") {
        const long input[] = {1L, 2L, 3L};
        long seed = 10L;
        cflow_graph surface = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};

        surface.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_long);
        check_true(cflow_graph_reduce_seeded(
            &surface, cflow_seeded_add.fn, &seed));
        check_true(cflow_plan_compile_surface(&plan, &surface, NULL));
        check_false(cflow_plan_parallel_reduce_supported(&plan));
        check_true(cflow_plan_eval_array(
            &plan, input, sizeof(input) / sizeof(input[0]), &result));
        check_long_result(&result, 16L);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&surface);
    }

    it("records seeded reduce as semantic certificate metadata") {
        long seed = 5L;
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_graph other_surface = {0};
        cflow_graph other_normalized = {0};
        cflow_graph unseeded_surface = {0};
        cflow_graph unseeded_normalized = {0};
        cflow_plan plan = {0};
        cflow_plan other_plan = {0};
        cflow_plan unseeded_plan = {0};
        cflow_plan_certificate certificate = {0};
        cflow_plan_certificate mismatch = {0};
        cflow_plan_certificate unseeded_certificate = {0};
        const char *error = NULL;

        surface.root = normalized.root = other_surface.root =
            other_normalized.root = unseeded_surface.root =
            unseeded_normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cmeta_type_long);
        check_true(cflow_graph_reduce_seeded(
            &surface, cflow_seeded_subtract.fn, &seed));
        check_true(cflow_graph_normalize(&normalized, &surface));
        check_true(cflow_plan_compile(&plan, &normalized, NULL));
        check_true(cflow_plan_certificate_build(
            &certificate, &normalized, &plan,
            CFLOW_CERTIFIED_PATH_SEQUENTIAL));
        check_equal(certificate.row_count, (size_t)1u);
        check_equal(certificate.rows[0].opcode,
                    (uint32_t)CFLOW_CERTIFIED_REDUCE);
        check_true(cflow_plan_certificate_check(
            &certificate, &normalized, &plan, &error));
        check_null(error);

        {
            long other_seed = 9L;
            cflow_graph_init(&other_surface, &cmeta_type_long);
            check_true(cflow_graph_reduce_seeded(
                &other_surface, cflow_seeded_subtract.fn, &other_seed));
            check_true(cflow_graph_normalize(
                &other_normalized, &other_surface));
            check_true(cflow_plan_compile(
                &other_plan, &other_normalized, NULL));
            check_false(cflow_plan_certificate_build(
                &mismatch, &normalized, &other_plan,
                CFLOW_CERTIFIED_PATH_SEQUENTIAL));
        }

        cflow_graph_init(&unseeded_surface, &cmeta_type_long);
        check_true(cflow_graph_reduce(
            &unseeded_surface, cflow_seeded_subtract.fn));
        check_true(cflow_graph_normalize(
            &unseeded_normalized, &unseeded_surface));
        check_true(cflow_plan_compile(
            &unseeded_plan, &unseeded_normalized, NULL));
        check_true(cflow_plan_certificate_build(
            &unseeded_certificate, &unseeded_normalized, &unseeded_plan,
            CFLOW_CERTIFIED_PATH_SEQUENTIAL));
        check_not_equal(certificate.graph_fingerprint,
                        unseeded_certificate.graph_fingerprint);

        cflow_plan_certificate_destroy(&unseeded_certificate);
        cflow_plan_certificate_destroy(&mismatch);
        cflow_plan_certificate_destroy(&certificate);
        cflow_plan_destroy(&unseeded_plan);
        cflow_plan_destroy(&other_plan);
        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&unseeded_normalized);
        cflow_graph_destroy(&unseeded_surface);
        cflow_graph_destroy(&other_normalized);
        cflow_graph_destroy(&other_surface);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }
}
