#include <cflow/cflow.h>

#include "tinytest.h"

#include <stdlib.h>
#include <string.h>

typed(map, value, long, backend_as_long, (int value)) {
    return (long)value;
}

typed(reduce, associative, long, backend_add_pair, (long left, long right)) {
    return left + right;
}

typedef struct backend_range_owner {
    const int *values;
    size_t count;
    size_t pulls;
} backend_range_owner;

typedef struct backend_test_set {
    long values[8];
    size_t count;
    size_t limit;
    bool stores_long;
} backend_test_set;

typedef struct backend_test_sequence {
    long values[8];
    size_t count;
    size_t limit;
    bool stores_long;
} backend_test_sequence;

static size_t backend_opens;
static size_t backend_closes;
static size_t backend_sequence_opens;
static size_t backend_sequence_closes;
static size_t backend_sequence_sorts;

static cmeta_gen_status backend_range_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    backend_range_owner *owner = (backend_range_owner *)object;
    if (!owner || !cursor || !out_value) return CMETA_GEN_ERROR;
    if (cursor->index >= owner->count) return CMETA_GEN_DONE;
    *(int *)out_value = owner->values[cursor->index++];
    ++owner->pulls;
    return cursor->index == owner->count
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range backend_range(backend_range_owner *owner) {
    return (cmeta_range){
        .object = owner,
        .element_type = &cmeta_type_int,
        .flags = CMETA_RANGE_ORDERED | CMETA_RANGE_REUSABLE |
                 CMETA_RANGE_CONSTRUCTS_VALUES,
        .next = backend_range_next
    };
}

static cflow_status backend_set_open(
    void **out_state, const cmeta_type_desc *type, size_t limit) {
    backend_test_set *state;
    if (!out_state || *out_state ||
        (!cmeta_type_equal(type, &cmeta_type_int) &&
         !cmeta_type_equal(type, &cmeta_type_long)) ||
        limit == 0u || limit > 8u)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    state = (backend_test_set *)calloc(1u, sizeof(*state));
    if (!state) return CFLOW_STATUS_ALLOCATION_FAILED;
    state->limit = limit;
    state->stores_long = cmeta_type_equal(type, &cmeta_type_long);
    *out_state = state;
    ++backend_opens;
    return CFLOW_STATUS_OK;
}

static cflow_status backend_set_insert(
    void *state_ptr, const void *value, bool *inserted) {
    backend_test_set *state = (backend_test_set *)state_ptr;
    size_t index;
    long candidate;
    if (!state || !value || !inserted)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    candidate = state->stores_long
        ? *(const long *)value : (long)*(const int *)value;
    for (index = 0u; index < state->count; ++index) {
        if (state->values[index] == candidate) {
            *inserted = false;
            return CFLOW_STATUS_OK;
        }
    }
    if (state->count == state->limit)
        return CFLOW_STATUS_CAPACITY_EXCEEDED;
    state->values[state->count++] = candidate;
    *inserted = true;
    return CFLOW_STATUS_OK;
}

static void backend_set_close(void *state) {
    if (state) ++backend_closes;
    free(state);
}

static const cflow_set_state_ops backend_set_ops = {
    backend_set_open, backend_set_insert, backend_set_close
};

static cflow_status backend_sequence_open(
    void **out_state, const cmeta_type_desc *type, size_t limit) {
    backend_test_sequence *state;
    if (!out_state || *out_state ||
        (!cmeta_type_equal(type, &cmeta_type_int) &&
         !cmeta_type_equal(type, &cmeta_type_long)) ||
        limit == 0u || limit > 8u)
        return CFLOW_STATUS_INVALID_ARGUMENT;
    state = (backend_test_sequence *)calloc(1u, sizeof(*state));
    if (!state) return CFLOW_STATUS_ALLOCATION_FAILED;
    state->limit = limit;
    state->stores_long = cmeta_type_equal(type, &cmeta_type_long);
    *out_state = state;
    ++backend_sequence_opens;
    return CFLOW_STATUS_OK;
}

static cflow_status backend_sequence_append(
    void *state_ptr, const void *value) {
    backend_test_sequence *state = (backend_test_sequence *)state_ptr;
    if (!state || !value) return CFLOW_STATUS_INVALID_ARGUMENT;
    if (state->count == state->limit)
        return CFLOW_STATUS_CAPACITY_EXCEEDED;
    state->values[state->count++] = state->stores_long
        ? *(const long *)value : (long)*(const int *)value;
    return CFLOW_STATUS_OK;
}

static cflow_status backend_sequence_sort(void *state_ptr) {
    backend_test_sequence *state = (backend_test_sequence *)state_ptr;
    size_t index;
    if (!state) return CFLOW_STATUS_INVALID_ARGUMENT;
    for (index = 1u; index < state->count; ++index) {
        long value = state->values[index];
        size_t position = index;
        while (position > 0u && state->values[position - 1u] > value) {
            state->values[position] = state->values[position - 1u];
            --position;
        }
        state->values[position] = value;
    }
    ++backend_sequence_sorts;
    return CFLOW_STATUS_OK;
}

static size_t backend_sequence_size(const void *object) {
    const backend_test_sequence *state =
        (const backend_test_sequence *)object;
    return state ? state->count : 0u;
}

static cmeta_gen_status backend_sequence_next(
    const void *object, cmeta_range_cursor *cursor, void *out_value) {
    const backend_test_sequence *state =
        (const backend_test_sequence *)object;
    if (!state || !cursor || !out_value) return CMETA_GEN_ERROR;
    if (cursor->index >= state->count) return CMETA_GEN_DONE;
    if (state->stores_long)
        *(long *)out_value = state->values[cursor->index++];
    else
        *(int *)out_value = (int)state->values[cursor->index++];
    return cursor->index == state->count
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range backend_sequence_range(const void *state_ptr) {
    const backend_test_sequence *state =
        (const backend_test_sequence *)state_ptr;
    return (cmeta_range){
        .object = state,
        .element_type = state && state->stores_long
            ? &cmeta_type_long : &cmeta_type_int,
        .flags = CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                 CMETA_RANGE_SORTED | CMETA_RANGE_REUSABLE |
                 CMETA_RANGE_CONSTRUCTS_VALUES,
        .size = backend_sequence_size,
        .next = backend_sequence_next
    };
}

static void backend_sequence_close(void *state) {
    if (state) ++backend_sequence_closes;
    free(state);
}

static const cflow_sequence_state_ops backend_sequence_ops = {
    backend_sequence_open,
    backend_sequence_append,
    backend_sequence_sort,
    backend_sequence_range,
    backend_sequence_close
};

suite("CFlow bounded state backends") {
    it("copies bounded sorted parameters through normalization") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_graph optimized = {0};
        const cflow_subgraph *subgraph;
        const cflow_node *node;

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_sorted(&surface, 4u));
        check_true(cflow_graph_normalize(&normalized, &surface));
        subgraph = cflow_graph_subgraph(&normalized, normalized.root);
        node = cflow_subgraph_node(subgraph, subgraph->tail);
        check_not_null(node);
        check_equal(node->op, CFLOW_OP_SORTED);
        check_equal(node->param_kind, CFLOW_NODE_PARAM_SORTED);
        check_equal(node->params.sorted.max_elements, (size_t)4u);
        check_false(cflow_plan_graph_supported(&normalized));
        check_true(cflow_graph_optimize(
            &optimized, &normalized, (cflow_opt_options){0u}, NULL));
        subgraph = cflow_graph_subgraph(&optimized, optimized.root);
        node = cflow_subgraph_node(subgraph, subgraph->tail);
        check_not_null(node);
        check_equal(node->op, CFLOW_OP_SORTED);
        check_equal(node->params.sorted.max_elements, (size_t)4u);

        cflow_graph_destroy(&optimized);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("rejects missing sorted backend before pulling the source") {
        const int values[] = {3, 1, 2};
        backend_range_owner owner = {values, 3u, 0u};
        cflow_stream stream = {0};
        size_t count = 99u;
        cflow_status_result result;

        check_not_null(cflow_stream_from_range(&stream, backend_range(&owner)));
        check_not_null(stream.sorted(&stream, 3u));
        result = cflow_stream_count_result(&stream, &count);
        check_equal(result.status, CFLOW_STATUS_UNSUPPORTED);
        check_equal(count, (size_t)0u);
        check_equal(owner.pulls, (size_t)0u);

        cflow_stream_destroy(&stream);
    }

    it("admits nested sorted state before pull and propagates options") {
        const int values[] = {2, 1};
        backend_range_owner owner = {values, 2u, 0u};
        const cflow_eval_options missing_sequence = {&backend_set_ops, NULL};
        const cflow_eval_options options = {
            &backend_set_ops, &backend_sequence_ops};
        cflow_stream left = {0};
        cflow_stream branch = {0};
        cflow_stream identity = {0};
        const cflow_graph *branches[2];
        size_t count = 99u;
        cflow_status_result result;

        check_not_null(cflow_stream_from_range_with_options(
            &left, backend_range(&owner), &missing_sequence));
        check_not_null(cflow_stream_init(&branch, &cmeta_type_int));
        check_not_null(branch.map(&branch, backend_as_long)
                                 ->sorted(&branch, 1u));
        check_not_null(cflow_stream_init(&identity, &cmeta_type_int));
        check_not_null(identity.map(&identity, backend_as_long));
        branches[0] = &branch.graph;
        branches[1] = &identity.graph;
        check_true(cflow_graph_relation(
            &left.graph, branches, 2u, cflow_relation_all_fold(),
            backend_add_pair.fn));
        result = cflow_stream_count_result(&left, &count);
        check_equal(result.status, CFLOW_STATUS_UNSUPPORTED);
        check_equal(owner.pulls, (size_t)0u);
        cflow_stream_destroy(&left);
        cflow_stream_destroy(&branch);
        cflow_stream_destroy(&identity);

        memset(&left, 0, sizeof(left));
        memset(&branch, 0, sizeof(branch));
        memset(&identity, 0, sizeof(identity));
        check_not_null(cflow_stream_from_range_with_options(
            &left, backend_range(&owner), &options));
        check_not_null(cflow_stream_init(&branch, &cmeta_type_int));
        check_not_null(branch.map(&branch, backend_as_long)
                                 ->sorted(&branch, 1u));
        check_not_null(cflow_stream_init(&identity, &cmeta_type_int));
        check_not_null(identity.map(&identity, backend_as_long));
        branches[0] = &branch.graph;
        branches[1] = &identity.graph;
        check_true(cflow_graph_relation(
            &left.graph, branches, 2u, cflow_relation_all_fold(),
            backend_add_pair.fn));
        result = cflow_stream_count_result(&left, &count);
        check_equal(result.status, CFLOW_STATUS_OK);
        check_equal(count, (size_t)2u);
        check_equal(owner.pulls, (size_t)2u);
        cflow_stream_destroy(&left);
        cflow_stream_destroy(&branch);
        cflow_stream_destroy(&identity);
    }

    it("sorts stably before downstream demand and isolates repeated runs") {
        const int values[] = {3, 1, 2, 1, 4};
        const int expected[] = {1, 1};
        backend_range_owner owner = {values, 5u, 0u};
        const cflow_eval_options options = {
            &backend_set_ops, &backend_sequence_ops};
        cflow_stream stream = {0};
        cflow_result output = {0};

        backend_sequence_opens = 0u;
        backend_sequence_closes = 0u;
        backend_sequence_sorts = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.sorted(&stream, 5u)->take(&stream, 2u));
        check_true(cflow_eval_stream_limit(&stream, 2u, &output));
        check_equal(output.count, (size_t)2u);
        check_equal(output.data, expected, sizeof(expected));
        check_equal(owner.pulls, (size_t)5u);
        cflow_result_destroy(&output);

        owner.pulls = 0u;
        check_true(cflow_eval_stream_limit(&stream, 2u, &output));
        check_equal(output.data, expected, sizeof(expected));
        check_equal(owner.pulls, (size_t)5u);
        cflow_result_destroy(&output);
        cflow_stream_destroy(&stream);
        check_equal(backend_sequence_opens, (size_t)2u);
        check_equal(backend_sequence_sorts, (size_t)2u);
        check_equal(backend_sequence_closes, (size_t)2u);
    }

    it("flushes consecutive sorted stages in dependency order") {
        const int values[] = {4, 2, 3, 1};
        const int expected[] = {1, 2, 3, 4};
        backend_range_owner owner = {values, 4u, 0u};
        const cflow_eval_options options = {
            &backend_set_ops, &backend_sequence_ops};
        cflow_stream stream = {0};
        cflow_result output = {0};

        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.sorted(&stream, 4u)->sorted(&stream, 4u));
        check_true(cflow_eval_stream_limit(&stream, 4u, &output));
        check_equal(output.count, (size_t)4u);
        check_equal(output.data, expected, sizeof(expected));

        cflow_result_destroy(&output);
        cflow_stream_destroy(&stream);
    }

    it("reports the sorted hard limit before emitting buffered values") {
        const int values[] = {3, 1, 2};
        backend_range_owner owner = {values, 3u, 0u};
        const cflow_eval_options options = {
            &backend_set_ops, &backend_sequence_ops};
        cflow_stream stream = {0};
        size_t count = 99u;
        cflow_status_result result;

        backend_sequence_opens = 0u;
        backend_sequence_closes = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.sorted(&stream, 2u));
        result = cflow_stream_count_result(&stream, &count);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(count, (size_t)0u);
        check_equal(owner.pulls, (size_t)3u);

        cflow_stream_destroy(&stream);
        check_equal(backend_sequence_opens, (size_t)1u);
        check_equal(backend_sequence_closes, (size_t)1u);
    }

    it("preserves sorted runtime failure through the byte result adapter") {
        const int values[] = {3, 1, 2};
        backend_range_owner owner = {values, 3u, 0u};
        const cflow_eval_options options = {
            &backend_set_ops, &backend_sequence_ops};
        cflow_stream stream = {0};
        cflow_result output = {(void *)values, 1u, &cmeta_type_int};
        cflow_status_result result;

        backend_sequence_opens = 0u;
        backend_sequence_closes = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.sorted(&stream, 2u));
        result = cflow_eval_stream_limit_result(&stream, 3u, &output);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_null(output.data);
        check_equal(output.count, (size_t)0u);
        check_null(output.type);
        check_equal(owner.pulls, (size_t)3u);

        cflow_stream_destroy(&stream);
        check_equal(backend_sequence_opens, (size_t)1u);
        check_equal(backend_sequence_closes, (size_t)1u);
    }

    it("copies bounded distinct parameters through normalization") {
        cflow_graph surface = {0};
        cflow_graph normalized = {0};
        cflow_graph optimized = {0};
        const cflow_subgraph *subgraph;
        const cflow_node *node;

        cflow_graph_init(&surface, &cmeta_type_int);
        check_true(cflow_graph_distinct(&surface, 4u));
        check_true(cflow_graph_normalize(&normalized, &surface));
        subgraph = cflow_graph_subgraph(&normalized, normalized.root);
        node = cflow_subgraph_node(subgraph, subgraph->tail);
        check_not_null(node);
        check_equal(node->op, CFLOW_OP_DISTINCT);
        check_equal(node->param_kind, CFLOW_NODE_PARAM_DISTINCT);
        check_equal(node->params.distinct.max_unique, (size_t)4u);
        check_false(cflow_plan_graph_supported(&normalized));
        check_true(cflow_graph_optimize(
            &optimized, &normalized, (cflow_opt_options){0u}, NULL));
        subgraph = cflow_graph_subgraph(&optimized, optimized.root);
        node = cflow_subgraph_node(subgraph, subgraph->tail);
        check_not_null(node);
        check_equal(node->op, CFLOW_OP_DISTINCT);
        check_equal(node->params.distinct.max_unique, (size_t)4u);

        cflow_graph_destroy(&optimized);
        cflow_graph_destroy(&normalized);
        cflow_graph_destroy(&surface);
    }

    it("rejects missing distinct backend before pulling the source") {
        const int values[] = {1, 2, 1};
        backend_range_owner owner = {values, 3u, 0u};
        cflow_stream stream = {0};
        size_t count = 99u;
        cflow_status_result result;

        check_not_null(cflow_stream_from_range(&stream, backend_range(&owner)));
        check_not_null(stream.distinct(&stream, 3u));
        result = cflow_stream_count_result(&stream, &count);
        check_equal(result.status, CFLOW_STATUS_UNSUPPORTED);
        check_equal(count, (size_t)0u);
        check_equal(owner.pulls, (size_t)0u);

        cflow_stream_destroy(&stream);
    }

    it("admits nested distinct state before pull and propagates options") {
        const int values[] = {1, 2};
        backend_range_owner owner = {values, 2u, 0u};
        const cflow_eval_options options = {&backend_set_ops, NULL};
        cflow_stream left = {0};
        cflow_stream branch = {0};
        cflow_stream identity = {0};
        const cflow_graph *branches[2];
        size_t count = 99u;
        cflow_status_result result;

        check_not_null(cflow_stream_from_range(&left, backend_range(&owner)));
        check_not_null(cflow_stream_init(&branch, &cmeta_type_int));
        check_not_null(branch.map(&branch, backend_as_long)
                                 ->distinct(&branch, 1u));
        check_not_null(cflow_stream_init(&identity, &cmeta_type_int));
        check_not_null(identity.map(&identity, backend_as_long));
        branches[0] = &branch.graph;
        branches[1] = &identity.graph;
        check_true(cflow_graph_relation(
            &left.graph, branches, 2u, cflow_relation_all_fold(),
            backend_add_pair.fn));
        result = cflow_stream_count_result(&left, &count);
        check_equal(result.status, CFLOW_STATUS_UNSUPPORTED);
        check_equal(owner.pulls, (size_t)0u);
        cflow_stream_destroy(&left);
        cflow_stream_destroy(&branch);
        cflow_stream_destroy(&identity);

        memset(&left, 0, sizeof(left));
        memset(&branch, 0, sizeof(branch));
        memset(&identity, 0, sizeof(identity));
        check_not_null(cflow_stream_from_range_with_options(
            &left, backend_range(&owner), &options));
        check_not_null(cflow_stream_init(&branch, &cmeta_type_int));
        check_not_null(branch.map(&branch, backend_as_long)
                                 ->distinct(&branch, 1u));
        check_not_null(cflow_stream_init(&identity, &cmeta_type_int));
        check_not_null(identity.map(&identity, backend_as_long));
        branches[0] = &branch.graph;
        branches[1] = &identity.graph;
        check_true(cflow_graph_relation(
            &left.graph, branches, 2u, cflow_relation_all_fold(),
            backend_add_pair.fn));
        result = cflow_stream_count_result(&left, &count);
        check_equal(result.status, CFLOW_STATUS_OK);
        check_equal(count, (size_t)2u);
        check_equal(owner.pulls, (size_t)2u);

        cflow_stream_destroy(&left);
        cflow_stream_destroy(&branch);
        cflow_stream_destroy(&identity);
    }

    it("keeps first occurrence order with an injected bounded set") {
        const int values[] = {3, 1, 3, 2, 1};
        const int expected[] = {3, 1, 2};
        backend_range_owner owner = {values, 5u, 0u};
        const cflow_eval_options options = {&backend_set_ops, NULL};
        cflow_stream stream = {0};
        cflow_result result = {0};

        backend_opens = 0u;
        backend_closes = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.distinct(&stream, 3u));
        check_true(cflow_eval_stream_limit(&stream, 3u, &result));
        check_equal(result.count, (size_t)3u);
        check_equal(result.data, expected, sizeof(expected));
        check_equal(owner.pulls, (size_t)5u);

        cflow_result_destroy(&result);
        owner.pulls = 0u;
        check_true(cflow_eval_stream_limit(&stream, 3u, &result));
        check_equal(result.count, (size_t)3u);
        cflow_result_destroy(&result);
        cflow_stream_destroy(&stream);
        check_equal(backend_opens, (size_t)2u);
        check_equal(backend_closes, (size_t)2u);
    }

    it("closes distinct state after downstream take cancels upstream") {
        const int values[] = {1, 1, 2, 3};
        backend_range_owner owner = {values, 4u, 0u};
        const cflow_eval_options options = {&backend_set_ops, NULL};
        cflow_stream stream = {0};
        size_t count = 0u;
        cflow_status_result result;

        backend_opens = 0u;
        backend_closes = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.distinct(&stream, 4u)->take(&stream, 2u));
        result = cflow_stream_count_result(&stream, &count);
        check_equal(result.status, CFLOW_STATUS_OK);
        check_equal(count, (size_t)2u);
        check_equal(owner.pulls, (size_t)3u);

        cflow_stream_destroy(&stream);
        check_equal(backend_opens, (size_t)1u);
        check_equal(backend_closes, (size_t)1u);
    }

    it("reports the distinct hard limit without emitting the new value") {
        const int values[] = {1, 1, 2, 3};
        backend_range_owner owner = {values, 4u, 0u};
        const cflow_eval_options options = {&backend_set_ops, NULL};
        cflow_stream stream = {0};
        size_t count = 99u;
        cflow_status_result result;

        backend_opens = 0u;
        backend_closes = 0u;
        check_not_null(cflow_stream_from_range_with_options(
            &stream, backend_range(&owner), &options));
        check_not_null(stream.distinct(&stream, 2u));
        result = cflow_stream_count_result(&stream, &count);
        check_equal(result.status, CFLOW_STATUS_CAPACITY_EXCEEDED);
        check_equal(count, (size_t)0u);
        check_equal(owner.pulls, (size_t)4u);

        cflow_stream_destroy(&stream);
        check_equal(backend_opens, (size_t)1u);
        check_equal(backend_closes, (size_t)1u);
    }
}
