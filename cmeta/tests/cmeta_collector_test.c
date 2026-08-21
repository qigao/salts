#include <cmeta/collector.h>
#include "tinytest.h"

#include <stdint.h>

typedef struct fake_collector_state {
    int *output;
    const cmeta_type_desc *expected_type;
    cmeta_status begin_status;
    cmeta_status accept_status;
    cmeta_status finish_status;
    size_t begin_count;
    size_t accept_count;
    size_t finish_count;
    size_t abort_count;
    size_t observed_limit;
} fake_collector_state;

static cmeta_status fake_collector_begin(void *context,
                                         const cmeta_type_desc *input,
                                         size_t limit) {
    fake_collector_state *state = (fake_collector_state *)context;

    ++state->begin_count;
    state->observed_limit = limit;
    if (!cmeta_type_equal(input, state->expected_type))
        return CMETA_TYPE_MISMATCH;
    return state->begin_status;
}

static cmeta_status fake_collector_accept(void *context, const void *value) {
    fake_collector_state *state = (fake_collector_state *)context;

    ++state->accept_count;
    if (state->accept_status != CMETA_OK)
        return state->accept_status;
    *state->output += *(const int *)value;
    return CMETA_OK;
}

static cmeta_status fake_collector_finish(void *context) {
    fake_collector_state *state = (fake_collector_state *)context;

    ++state->finish_count;
    return state->finish_status;
}

static void fake_collector_abort(void *context) {
    fake_collector_state *state = (fake_collector_state *)context;

    ++state->abort_count;
    *state->output = 0;
}

static const cmeta_collector_ops fake_collector_ops = {
    fake_collector_begin,
    fake_collector_accept,
    fake_collector_finish,
    fake_collector_abort
};

static cmeta_collector fake_collector(fake_collector_state *state,
                                      void *zero_output,
                                      const cmeta_type_desc *input,
                                      size_t limit) {
    return (cmeta_collector){
        .ops = &fake_collector_ops,
        .context = state,
        .zero_output = zero_output,
        .input_type = input,
        .limit = limit,
        .count = 0u,
        .state = CMETA_COLLECTOR_ZERO,
        .status = CMETA_OK
    };
}

spec("CMeta transactional collectors") {
    it("commits an empty begun collector") {
        int output = 0;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 0u);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_true(collector.state == CMETA_COLLECTOR_BEGUN);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_true(collector.state == CMETA_COLLECTOR_COMMITTED);
        check_equal(state.begin_count, (size_t)1u);
        check_equal(state.finish_count, (size_t)1u);
        check_equal(state.abort_count, (size_t)0u);
        cmeta_collector_abort(&collector);
        check_equal(state.abort_count, (size_t)0u);
    }

    it("aborts a full collector exactly once") {
        int output = 0;
        int one = 1;
        int two = 2;
        int three = 3;
        cmeta_type_desc equivalent_int = cmeta_type_int;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 2u);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &equivalent_int, &one),
                    CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &two),
                    CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &three),
                    CMETA_CAPACITY_EXCEEDED);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(collector.count, (size_t)2u);
        check_equal(state.accept_count, (size_t)2u);
        check_equal(state.abort_count, (size_t)1u);
        check_equal(output, 0);
        cmeta_collector_abort(&collector);
        check_equal(state.abort_count, (size_t)1u);
    }

    it("rejects an accepted value at zero capacity") {
        int output = 0;
        int value = 1;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 0u);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_CAPACITY_EXCEEDED);
        check_equal(state.accept_count, (size_t)0u);
        check_equal(state.abort_count, (size_t)1u);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
    }

    it("prevents SIZE_MAX count wrap before dispatch") {
        int output = 0;
        int value = 1;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, SIZE_MAX);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        collector.count = SIZE_MAX;
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_CAPACITY_EXCEEDED);
        check_equal(collector.count, SIZE_MAX);
        check_equal(state.accept_count, (size_t)0u);
        check_equal(state.abort_count, (size_t)1u);
    }

    it("aborts a type mismatch without invoking accept") {
        int output = 0;
        long value = 1;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 1u);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_long, &value),
                    CMETA_TYPE_MISMATCH);
        check_equal(state.accept_count, (size_t)0u);
        check_equal(state.abort_count, (size_t)1u);
        check_true(collector.state == CMETA_COLLECTOR_ABORTED);
    }

    it("rejects invalid transitions without adapter callbacks") {
        int output = 0;
        int value = 1;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 1u);

        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_INVALID_ARGUMENT);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);
        check_equal(state.accept_count, (size_t)0u);
        check_equal(state.finish_count, (size_t)0u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_begin(&collector), CMETA_INVALID_ARGUMENT);
        check_equal(state.begin_count, (size_t)1u);
        check_equal(cmeta_collector_finish(&collector), CMETA_OK);
        check_equal(cmeta_collector_finish(&collector), CMETA_INVALID_ARGUMENT);
        check_equal(state.finish_count, (size_t)1u);
    }

    it("aborts exactly once when begin or finish callbacks fail") {
        int output = 0;
        fake_collector_state begin_state = {
            .output = &output,
            .expected_type = &cmeta_type_int,
            .begin_status = CMETA_OUT_OF_MEMORY
        };
        cmeta_collector begin_collector =
            fake_collector(&begin_state, &output, &cmeta_type_int, 1u);
        fake_collector_state finish_state = {
            .output = &output,
            .expected_type = &cmeta_type_int,
            .finish_status = CMETA_CALLBACK_ERROR
        };
        cmeta_collector finish_collector =
            fake_collector(&finish_state, &output, &cmeta_type_int, 1u);

        check_equal(cmeta_collector_begin(&begin_collector), CMETA_OUT_OF_MEMORY);
        check_true(begin_collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(begin_state.abort_count, (size_t)1u);
        cmeta_collector_abort(&begin_collector);
        check_equal(begin_state.abort_count, (size_t)1u);

        check_equal(cmeta_collector_begin(&finish_collector), CMETA_OK);
        check_equal(cmeta_collector_finish(&finish_collector), CMETA_CALLBACK_ERROR);
        check_true(finish_collector.state == CMETA_COLLECTOR_ABORTED);
        check_equal(finish_state.abort_count, (size_t)1u);
    }

    it("maps unknown callback statuses before aborting") {
        int output = 0;
        int value = 1;
        fake_collector_state state = {
            .output = &output,
            .expected_type = &cmeta_type_int,
            .accept_status = (cmeta_status)99
        };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 1u);

        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, &cmeta_type_int, &value),
                    CMETA_CALLBACK_ERROR);
        check_equal(collector.status, CMETA_CALLBACK_ERROR);
        check_equal(state.abort_count, (size_t)1u);
    }

    it("fails fast for missing callbacks and null accept inputs") {
        int output = 0;
        fake_collector_state state = { .output = &output,
                                      .expected_type = &cmeta_type_int };
        cmeta_collector collector =
            fake_collector(&state, &output, &cmeta_type_int, 1u);
        cmeta_collector missing_ops = collector;

        missing_ops.ops = NULL;
        check_equal(cmeta_collector_begin(&missing_ops), CMETA_INVALID_ARGUMENT);
        check_equal(state.begin_count, (size_t)0u);
        check_equal(cmeta_collector_begin(&collector), CMETA_OK);
        check_equal(cmeta_collector_accept(&collector, NULL, NULL),
                    CMETA_INVALID_ARGUMENT);
        check_equal(state.accept_count, (size_t)0u);
        check_equal(state.abort_count, (size_t)1u);
    }
}
