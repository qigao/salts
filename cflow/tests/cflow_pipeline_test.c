#include <cflow/cflow.h>
#include <cflow/plan_internal.h>
#include "tinytest.h"

#include "cflow_test_ops.h"

typedef struct cflow_test_range_owner {
    uint64_t generation;
    int value;
} cflow_test_range_owner;

static uint64_t cflow_test_range_version(const void *object) {
    return ((const cflow_test_range_owner *)object)->generation;
}

static cmeta_gen_status cflow_test_range_next(const void *object,
                                               cmeta_range_cursor *cursor,
                                               void *out_value) {
    const cflow_test_range_owner *owner =
        (const cflow_test_range_owner *)object;
    if (cursor->index != 0u)
        return CMETA_GEN_DONE;
    *(int *)out_value = owner->value;
    ++cursor->index;
    return CMETA_GEN_VALUE;
}

static bool cflow_test_build_pipeline(cflow_stream *stream) {
    return cflow_stream_init(stream, &cmeta_type_int) &&
           stream->filter(stream, cflow_test_even) &&
           stream->map(stream, cflow_test_square) &&
           stream->map(stream, cflow_test_half);
}

cmeta_function(map, stateful, long, cflow_test_stateful_add_ten, (int value)) {
    return (long)value + 10L;
}

cmeta_function(map, value, int, cflow_test_increment_int, (int value)) {
    return value + 1;
}

static bool cflow_test_reject_raw_batch(
    const cflow_plan_call *call,
    cflow_plan_unary_batch_mode mode,
    const unsigned char *input,
    size_t input_count,
    unsigned char *selection,
    unsigned char *output,
    size_t *output_count) {
    (void)call;
    (void)mode;
    (void)input;
    (void)input_count;
    (void)selection;
    (void)output;
    if (output_count) *output_count = 0u;
    return false;
}

lambda1(map, value, long, cflow_test_captured_add,
        int, value, long, increment) {
    return (long)value + increment;
}

typedef struct cflow_test_borrowed_capture {
    int *increment;
} cflow_test_borrowed_capture;

lambda1(map, value, long, cflow_test_borrowed_ptr_add,
        int, value, cflow_test_borrowed_capture, capture) {
    return (long)value + (long)*capture.increment;
}

static void cflow_test_check_expected(const cflow_result *result) {
    const double *values;

    check_not_null(result);
    check_equal(result->count, (size_t)3);
    check_true(cmeta_type_equal(result->type, &cmeta_type_double));
    check_not_null(result->data);
    values = result->data;
    check_equal(values[0], 2.0);
    check_equal(values[1], 8.0);
    check_equal(values[2], 18.0);
}

suite("CFlow pipeline") {
    it("accepts normalized plans whose values provide lifecycle callbacks") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_plan plan = {0};

        normalized.root = CMETA_INVALID_ID;
        cflow_graph_init(&surface, &cflow_test_owned_value_type);
        check_true(cflow_graph_normalize(&normalized, &surface));

        check_true(cflow_plan_graph_supported(&normalized));
        check_true(cflow_plan_compile(&plan, &normalized, NULL));
        check_not_null(plan.impl);
        check_null(plan.error);
        check_false(cflow_plan_parallel_reduce_supported(&plan));

        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("reports a mutated borrowed range owner") {
        cflow_test_range_owner owner = {7u, 42};
        cmeta_range range = {
            &owner, &cmeta_type_int, CMETA_RANGE_NONE, NULL,
            cflow_test_range_next, owner.generation, cflow_test_range_version
        };
        cflow_publisher source = {0};
        cflow_step step;
        int output = 0;

        check_true(cflow_publisher_from_range(&source, range));
        step = cflow_publisher_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_VALUE);
        check_equal(output, 42);
        ++owner.generation;
        step = cflow_publisher_resume(&source, NULL, &output);
        check_true(step.kind == CFLOW_STEP_ERROR);
        check_equal(step.error, "range owner mutated");
        cflow_publisher_destroy(&source);
    }

    it("evaluates a typed fluent pipeline") {
        cflow_stream stream = {0};
        cflow_result result = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_stream_ok(&stream));
        check_true(cflow_eval_array(&stream.graph, input, 6u, &result));
        cflow_test_check_expected(&result);

        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
    }

    it("keeps compiled-plan output equal to interpreter output") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_plan_compile_stats stats = {0};
        cflow_result interpreted = {0};
        cflow_result compiled = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, &stats));
        check_null(plan.error);
        check_true(cflow_eval_array(&stream.graph, input, 6u, &interpreted));
        check_true(cflow_plan_eval_array(&plan, input, 6u, &compiled));
        check_true(cflow_result_equal(&interpreted, &compiled));
        check_equal(stats.instructions, (size_t)2);
        check_equal(stats.inference_queries, (size_t)2);
        check_equal(stats.map_callbacks, (size_t)2);
        cflow_test_check_expected(&compiled);

        cflow_result_destroy(&compiled);
        cflow_result_destroy(&interpreted);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("keeps phase results independent of flat edge storage order") {
        cflow_stream stream = {0};
        cflow_graph normalized = {0};
        cflow_graph optimized = {0};
        cflow_plan plan = {0};
        cflow_opt_stats opt_stats = {0};
        cflow_plan_compile_stats plan_stats = {0};
        cflow_result normalized_result = {0};
        cflow_result optimized_result = {0};
        cflow_result compiled_result = {0};
        cflow_subgraph *surface_root;
        const char *validation = NULL;
        const int input[] = {1, 2, 3, 4, 5, 6};

        normalized.root = CMETA_INVALID_ID;
        optimized.root = CMETA_INVALID_ID;
        check_true(cflow_test_build_pipeline(&stream));
        surface_root = &stream.graph.subgraphs[stream.graph.root];
        check_equal(surface_root->edge_count, (size_t)3u);
        for (size_t left = 0u, right = surface_root->edge_count - 1u; left < right;
             ++left, --right) {
            cflow_edge edge = surface_root->edges[left];
            surface_root->edges[left] = surface_root->edges[right];
            surface_root->edges[right] = edge;
        }

        check_true(cflow_graph_validate(&stream.graph, &validation));
        check_null(validation);
        check_true(cflow_graph_normalize(&normalized, &stream.graph));
        check_true(cflow_graph_optimize(&optimized, &normalized,
                                        (cflow_opt_options){CMETA_OPT_DEFAULT}, &opt_stats));
        check_equal(opt_stats.nodes_before, (size_t)4u);
        check_equal(opt_stats.nodes_after, (size_t)3u);
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, &plan_stats));
        check_equal(plan_stats.graph_nodes, (size_t)3u);
        check_equal(plan_stats.instructions, (size_t)2u);
        check_true(cflow_eval_array(&normalized, input, 6u, &normalized_result));
        check_true(cflow_eval_array(&optimized, input, 6u, &optimized_result));
        check_true(cflow_plan_eval_array(&plan, input, 6u, &compiled_result));
        check_true(cflow_result_equal(&normalized_result, &optimized_result));
        check_true(cflow_result_equal(&normalized_result, &compiled_result));
        cflow_test_check_expected(&compiled_result);

        cflow_result_destroy(&compiled_result);
        cflow_result_destroy(&optimized_result);
        cflow_result_destroy(&normalized_result);
        cflow_plan_destroy(&plan);
        cflow_graph_destroy(&optimized);
        cflow_graph_destroy(&normalized);
        cflow_stream_destroy(&stream);
    }

    it("reuses one bounded workspace across fused raw batches") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_plan_batch_workspace workspace = {0};
        cflow_plan_batch_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int first[] = {1, 2, 3, 4, 5, 6};
        const double first_expected[] = {2.0, 8.0, 18.0};
        const int second[] = {2, 4};
        const double second_expected[] = {2.0, 8.0};
        const size_t max_value_size = sizeof(long) > sizeof(double)
                                          ? sizeof(long)
                                          : sizeof(double);
        const size_t expected_workspace_bytes =
            1u + 2u * 6u * max_value_size;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_batch_workspace_supported(&plan));
        check_true(cflow_plan_batch_workspace_init(&workspace, &plan, 6u));
        check_equal(cflow_plan_batch_workspace_capacity(&workspace), (size_t)6u);
        check_equal(cflow_plan_batch_workspace_bytes(&workspace),
                    expected_workspace_bytes);

        check_true(cflow_plan_eval_array_workspace_profile(
            &plan, first, 6u, &workspace, &result, &stats));
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(stats.allocated_bytes, (size_t)0u);
        check_equal(stats.raw_batch_stage_calls, (size_t)3u);
        check_equal(stats.adapter_item_calls, (size_t)0u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, (size_t)3u * sizeof(long));
        check_equal(stats.result_bytes, sizeof(first_expected));
        check_equal(stats.peak_live_bytes, expected_workspace_bytes);
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_not_null(result.data);
        check_equal(memcmp(result.data, first_expected, sizeof(first_expected)), 0);

        memset(&result, 0, sizeof(result));
        memset(&stats, 0, sizeof(stats));
        check_true(cflow_plan_eval_array_workspace_profile(
            &plan, second, 2u, &workspace, &result, &stats));
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(stats.allocated_bytes, (size_t)0u);
        check_equal(stats.raw_batch_stage_calls, (size_t)3u);
        check_equal(stats.adapter_item_calls, (size_t)0u);
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_equal(memcmp(result.data, second_expected, sizeof(second_expected)), 0);

        cflow_plan_batch_workspace_destroy(&workspace);
        check_equal(cflow_plan_batch_workspace_capacity(&workspace), (size_t)0u);
        check_equal(cflow_plan_batch_workspace_bytes(&workspace), (size_t)0u);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("keeps a fused workspace reusable after capacity rejection") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_plan_batch_workspace workspace = {0};
        cflow_plan_batch_result result = {(const void *)1, 99u, &cmeta_type_int};
        const int too_many[] = {1, 2, 3, 4};
        const int valid[] = {2, 4};
        const double expected[] = {2.0, 8.0};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_batch_workspace_init(&workspace, &plan, 3u));

        check_false(cflow_plan_eval_array_workspace(
            &plan, too_many, 4u, &workspace, &result));
        check_null(result.data);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);

        check_true(cflow_plan_eval_array_workspace(
            &plan, valid, 2u, &workspace, &result));
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_equal(memcmp(result.data, expected, sizeof(expected)), 0);

        cflow_plan_batch_workspace_destroy(&workspace);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("rejects fused workspaces for adapter and materialized plans") {
        cflow_stream captured_stream = {0};
        cflow_stream stateful_stream = {0};
        cflow_plan captured_plan = {0};
        cflow_plan stateful_plan = {0};
        cflow_plan_batch_workspace workspace = {0};
        cflow_map_callable captured = cflow_test_captured_add(10L);

        check_not_null(cflow_stream_init(&captured_stream, &cmeta_type_int));
        check_not_null(captured_stream.map(&captured_stream, captured));
        check_true(cflow_plan_compile_surface(
            &captured_plan, &captured_stream.graph, NULL));
        check_false(cflow_plan_batch_workspace_supported(&captured_plan));
        check_false(cflow_plan_batch_workspace_init(
            &workspace, &captured_plan, 8u));
        check_null(workspace.impl);

        check_not_null(cflow_stream_init(&stateful_stream, &cmeta_type_int));
        check_not_null(stateful_stream.map(
            &stateful_stream, cflow_test_stateful_add_ten));
        check_true(cflow_plan_compile_surface(
            &stateful_plan, &stateful_stream.graph, NULL));
        check_false(cflow_plan_batch_workspace_supported(&stateful_plan));
        check_false(cflow_plan_batch_workspace_init(
            &workspace, &stateful_plan, 8u));
        check_null(workspace.impl);

        cflow_plan_destroy(&stateful_plan);
        cflow_plan_destroy(&captured_plan);
        cflow_stream_destroy(&stateful_stream);
        cflow_stream_destroy(&captured_stream);
    }

    it("reports exact bounded resources for a fused value plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};
        const size_t expected_intermediate = 3u * sizeof(long);
        const size_t expected_result = 3u * sizeof(double);
        const size_t expected_total = 1u + expected_intermediate + expected_result;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 6u, &result, &stats));
        cflow_test_check_expected(&result);
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)3u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, expected_intermediate);
        check_equal(stats.result_bytes, expected_result);
        check_equal(stats.allocated_bytes, expected_total);
        check_equal(stats.peak_live_bytes, expected_intermediate + expected_result);
        check_equal(stats.staged_input_copy_bytes, (size_t)0u);
        check_equal(stats.raw_batch_stage_calls, (size_t)3u);
        check_equal(stats.adapter_item_calls, (size_t)0u);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("reuses dead fused map slots across long value chains") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {5, 6, 7, 8};
        const size_t value_bytes = sizeof(input);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(
            &plan, input, sizeof(input) / sizeof(input[0]), &result, &stats));

        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)2u);
        check_equal(stats.allocated_bytes, (size_t)2u * value_bytes);
        check_equal(stats.peak_live_bytes, (size_t)2u * value_bytes);
        check_equal(stats.selection_bytes, (size_t)0u);
        check_equal(stats.intermediate_bytes, (size_t)3u * value_bytes);
        check_equal(stats.result_bytes, value_bytes);
        check_equal(stats.staged_input_copy_bytes, (size_t)0u);
        check_equal(stats.raw_batch_stage_calls, (size_t)4u);
        check_equal(stats.adapter_item_calls, (size_t)0u);
        check_equal(result.count, sizeof(expected) / sizeof(expected[0]));
        check_true(cmeta_type_equal(result.type, &cmeta_type_int));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("cleans reused fused map slots when a later raw batch fails") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        cflow_plan_impl *impl;
        cflow_plan_call *failing_call = NULL;
        cflow_plan_unary_batch_fn original_raw_batch = NULL;
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {5, 6, 7, 8};
        size_t map_call_index = 0u;

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_not_null(stream.map(&stream, cflow_test_increment_int));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));

        impl = (cflow_plan_impl *)plan.impl;
        check_not_null(impl);
        for (size_t pc = 0u; impl && pc < impl->count; ++pc) {
            cflow_plan_inst *inst = &impl->code[pc];
            for (size_t k = 0u; k < inst->fn_chain_count; ++k) {
                if (map_call_index == 2u)
                    failing_call = &inst->fn_chain[k];
                ++map_call_index;
            }
        }
        check_equal(map_call_index, (size_t)4u);
        check_not_null(failing_call);
        original_raw_batch = failing_call ? failing_call->raw_batch : NULL;
        check_not_null(original_raw_batch);

        if (failing_call)
            failing_call->raw_batch = cflow_test_reject_raw_batch;
        check_false(cflow_plan_eval_array_profile(
            &plan, input, sizeof(input) / sizeof(input[0]), &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.raw_batch_stage_calls, (size_t)2u);
        check_equal(result.count, (size_t)0u);
        check_null(result.data);
        check_null(result.type);

        if (failing_call)
            failing_call->raw_batch = original_raw_batch;
        memset(&stats, 0, sizeof(stats));
        check_true(cflow_plan_eval_array_profile(
            &plan, input, sizeof(input) / sizeof(input[0]), &result, &stats));
        check_equal(result.count, sizeof(expected) / sizeof(expected[0]));
        check_true(cmeta_type_equal(result.type, &cmeta_type_int));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("owns capture bytes but borrows transitive pointer identity across clone") {
        cflow_stream stream = {0};
        cflow_graph clone = {0};
        cflow_result result = {0};
        cflow_test_borrowed_capture original_capture = {0};
        cflow_test_borrowed_capture cloned_capture = {0};
        int external_increment = 10;
        const int input[] = {1, 2};
        const long expected[] = {21L, 22L};
        cflow_map_callable callable =
            cflow_test_borrowed_ptr_add(
                (cflow_test_borrowed_capture){&external_increment});
        const cflow_node *source_node;
        const cflow_node *clone_node;

        clone.root = CMETA_INVALID_ID;
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, callable));
        check_true(cflow_graph_clone(&clone, &stream.graph));

        source_node = cflow_subgraph_node(
            cflow_graph_subgraph(&stream.graph, stream.graph.root), 1u);
        clone_node = cflow_subgraph_node(
            cflow_graph_subgraph(&clone, clone.root), 1u);
        check_not_null(source_node);
        check_not_null(clone_node);
        check_equal(source_node->fn.capture_size,
                    sizeof(cflow_test_borrowed_capture));
        check_equal(clone_node->fn.capture_size,
                    sizeof(cflow_test_borrowed_capture));
        check_true(&source_node->fn.capture != &clone_node->fn.capture);

        memcpy(&original_capture, source_node->fn.capture.bytes,
               sizeof(original_capture));
        memcpy(&cloned_capture, clone_node->fn.capture.bytes,
               sizeof(cloned_capture));
        check_true(original_capture.increment == &external_increment);
        check_true(cloned_capture.increment == &external_increment);

        external_increment = 20;
        cflow_stream_destroy(&stream);
        check_equal(external_increment, 20);

        check_true(cflow_eval_array(&clone, input, 2u, &result));
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_graph_destroy(&clone);
        check_equal(external_increment, 20);
    }

    it("keeps capturing maps on the adapter path") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        cflow_map_callable callable = cflow_test_captured_add(10L);
        const int input[] = {1, 2, 3};
        const long expected[] = {11L, 12L, 13L};
        const void *args[] = {&input[0]};
        long direct = 0L;
        bool eval_ok;

        check_true(cmeta_callable_invoke(&callable.fn, &direct, args));
        check_equal(direct, 11L);
        check_false(cmeta_callable_can_dispatch_canonical_raw(callable.fn));
        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, callable));
        check_true(cflow_stream_ok(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cmeta_callable_invoke(
            &((const cflow_plan_impl *)plan.impl)->code[0].fn_chain[0].fn,
            &direct, args));
        check_equal(direct, 11L);
        check_null(((const cflow_plan_impl *)plan.impl)->code[0].fn_chain[0].raw_batch);
        eval_ok = cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats);
        check_equal(stats.adapter_item_calls, (size_t)3u);
        check_true(eval_ok);
        check_equal(result.count, (size_t)3u);
        check_equal(((const long *)result.data)[0], 11L);
        check_equal(((const long *)result.data)[1], 12L);
        check_equal(((const long *)result.data)[2], 13L);
        check_equal(result.data, expected, sizeof(expected));
        check_equal(stats.raw_batch_stage_calls, (size_t)0u);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("retains the materialized path for a stateful map") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3};
        const long expected[] = {11L, 12L, 13L};

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_stateful_add_ten));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_false(stats.fused_value_path);
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_long));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("returns a typed null result without allocations for empty fused input") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, NULL, 0u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(stats.allocated_bytes, (size_t)0u);
        check_equal(stats.peak_live_bytes, (size_t)0u);
        check_equal(result.count, (size_t)0u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_null(result.data);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("fails a fused transaction before allocation for a missing input buffer") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        int sentinel = 0;
        cflow_result result = {&sentinel, 1u, &cmeta_type_int};
        cflow_plan_eval_stats stats = {0};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_false(cflow_plan_eval_array_profile(&plan, NULL, 1u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)0u);
        check_equal(result.count, (size_t)0u);
        check_null(result.type);
        check_null(result.data);

        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("allocates no result storage when a fused filter rejects every input") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 3, 5};
        const size_t expected_auxiliary = 1u;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)1u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, (size_t)0u);
        check_equal(stats.result_bytes, (size_t)0u);
        check_equal(stats.allocated_bytes, expected_auxiliary);
        check_equal(stats.peak_live_bytes, expected_auxiliary);
        check_equal(result.count, (size_t)0u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_null(result.data);

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("uses exact intermediate and result buffers for a map-only plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3};
        const double expected[] = {0.5, 2.0, 4.5};
        const size_t expected_intermediate = 3u * sizeof(long);
        const size_t expected_result = sizeof(expected);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.map(&stream, cflow_test_square));
        check_not_null(stream.map(&stream, cflow_test_half));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 3u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)2u);
        check_equal(stats.selection_bytes, (size_t)0u);
        check_equal(stats.intermediate_bytes, expected_intermediate);
        check_equal(stats.result_bytes, expected_result);
        check_equal(stats.allocated_bytes, expected_intermediate + expected_result);
        check_equal(stats.peak_live_bytes, expected_intermediate + expected_result);
        check_equal(result.count, (size_t)3u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_double));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("copies only selected values for a filter-only plan") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        cflow_result result = {0};
        cflow_plan_eval_stats stats = {0};
        const int input[] = {1, 2, 3, 4};
        const int expected[] = {2, 4};
        const size_t expected_total = 1u + sizeof(expected);

        check_not_null(cflow_stream_init(&stream, &cmeta_type_int));
        check_not_null(stream.filter(&stream, cflow_test_even));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        check_true(cflow_plan_eval_array_profile(&plan, input, 4u, &result, &stats));
        check_true(stats.fused_value_path);
        check_equal(stats.allocation_calls, (size_t)2u);
        check_equal(stats.selection_bytes, (size_t)1u);
        check_equal(stats.intermediate_bytes, (size_t)0u);
        check_equal(stats.result_bytes, sizeof(expected));
        check_equal(stats.allocated_bytes, expected_total);
        check_equal(stats.peak_live_bytes, expected_total);
        check_equal(result.count, (size_t)2u);
        check_true(cmeta_type_equal(result.type, &cmeta_type_int));
        check_equal(result.data, expected, sizeof(expected));

        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("predecodes immutable Filter and Map callback records once") {
        cflow_stream stream = {0};
        cflow_plan plan = {0};
        const cflow_plan_impl *impl;
        const cflow_plan_inst *filter;
        const cflow_plan_inst *map;

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_plan_compile_surface(&plan, &stream.graph, NULL));
        impl = (const cflow_plan_impl *)plan.impl;
        check_not_null(impl);
        check_equal(impl->count, (size_t)2u);

        filter = &impl->code[0];
        check_equal(filter->opcode, CMETA_PLAN_FILTER);
        check_not_null(filter->call.invoke);
        check_not_null(filter->call.raw_batch);
        check_true(cmeta_type_equal(filter->call.input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(filter->call.output_type, &cmeta_type_bool));

        map = &impl->code[1];
        check_equal(map->opcode, CMETA_PLAN_MAP);
        check_equal(map->fn_chain_count, (size_t)2u);
        check_not_null(map->fn_chain[0].invoke);
        check_not_null(map->fn_chain[0].raw_batch);
        check_true(cmeta_type_equal(map->fn_chain[0].input_type, &cmeta_type_int));
        check_true(cmeta_type_equal(map->fn_chain[0].output_type, &cmeta_type_long));
        check_not_null(map->fn_chain[1].invoke);
        check_not_null(map->fn_chain[1].raw_batch);
        check_true(cmeta_type_equal(map->fn_chain[1].input_type, &cmeta_type_long));
        check_true(cmeta_type_equal(map->fn_chain[1].output_type, &cmeta_type_double));

        cflow_plan_destroy(&plan);
        cflow_stream_destroy(&stream);
    }

    it("compares result values with their CMeta equality traits") {
        float positive_zero = 0.0f;
        float negative_zero = -0.0f;
        const cflow_result left = {
            .data = &positive_zero, .count = 1u, .type = &cmeta_type_float
        };
        const cflow_result right = {
            .data = &negative_zero, .count = 1u, .type = &cmeta_type_float
        };

        check_true(cflow_result_equal(&left, &right));
    }

    it("verifies normalization optimization and execution parity") {
        cflow_stream stream = {0};
        cflow_verify_report report = {0};
        const int input[] = {1, 2, 3, 4, 5, 6};

        check_true(cflow_test_build_pipeline(&stream));
        check_true(cflow_verify_pipeline(&stream.graph, input, 6u, &report));
        check_null(report.error);
        check_equal(report.input_count, (size_t)6);
        check_equal(report.output_count, (size_t)3);
        check_true(report.compiled_plan_checked);
        check_equal(report.compiled_instructions, (size_t)2);

        cflow_stream_destroy(&stream);
    }

    it("rejects missing stream initialization arguments") {
        cflow_stream stream = {0};

        check_null(cflow_stream_init(NULL, &cmeta_type_int));
        check_null(cflow_stream_init(&stream, NULL));
    }
}
