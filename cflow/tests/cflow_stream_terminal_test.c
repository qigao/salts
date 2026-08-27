#include <cflow/cflow.h>
#include "tinytest.h"

#include <string.h>

typedef struct terminal_range_owner {
    const int *values;
    size_t count;
    size_t resumes;
    size_t fail_on_resume;
} terminal_range_owner;

static size_t terminal_range_size(const void *object) {
    const terminal_range_owner *owner =
        (const terminal_range_owner *)object;
    return owner ? owner->count : 0u;
}

static cmeta_gen_status terminal_range_next(const void *object,
                                             cmeta_range_cursor *cursor,
                                             void *out_value) {
    terminal_range_owner *owner = (terminal_range_owner *)object;

    if (!owner || !cursor || !out_value) return CMETA_GEN_ERROR;
    ++owner->resumes;
    if (owner->fail_on_resume == owner->resumes) return CMETA_GEN_ERROR;
    if (cursor->index >= owner->count) return CMETA_GEN_DONE;
    memcpy(out_value, &owner->values[cursor->index], sizeof(int));
    ++cursor->index;
    return cursor->index == owner->count
        ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE;
}

static cmeta_range terminal_range(terminal_range_owner *owner) {
    return (cmeta_range){
        .object = owner,
        .element_type = &cmeta_type_int,
        .flags = CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED |
                 CMETA_RANGE_REUSABLE | CMETA_RANGE_CONSTRUCTS_VALUES,
        .size = terminal_range_size,
        .next = terminal_range_next,
        .version = 0u,
        .current_version = NULL
    };
}

typed(filter, value, bool, terminal_even, (int value)) {
    return value % 2 == 0;
}

typed(filter, value, bool, terminal_greater_than_five, (int value)) {
    return value > 5;
}

typed(map, value, long, terminal_to_long, (int value)) {
    return (long)value;
}

static bool terminal_failing_invoke(const cmeta_callable *self,
                                    void *out,
                                    const void *const *args) {
    (void)self;
    (void)out;
    (void)args;
    return false;
}

typedef struct terminal_visit_state {
    int sum;
    size_t calls;
    size_t fail_on_call;
} terminal_visit_state;

static bool terminal_sum(void *user,
                         const cmeta_type_desc *type,
                         const void *value) {
    terminal_visit_state *state = (terminal_visit_state *)user;

    if (!state || !value || !cmeta_type_equal(type, &cmeta_type_int))
        return false;
    ++state->calls;
    if (state->fail_on_call == state->calls) return false;
    state->sum += *(const int *)value;
    return true;
}

static void terminal_build_even_tail(cflow_stream *stream,
                                     terminal_range_owner *owner) {
    check_not_null(cflow_stream_from_range(stream, terminal_range(owner)));
    check_not_null(stream->filter(stream, terminal_even)->skip(stream, 1u));
}

spec("CFlow Stream terminals") {
    it("evaluates common terminals over the current Graph output") {
        const int values[] = {1, 2, 3, 4, 5, 6};
        terminal_range_owner owner = {values, 6u, 0u, 0u};
        cflow_stream stream = {0};
        cflow_find_result found = {0};
        terminal_visit_state visit = {0};
        const char *error = NULL;
        size_t count = 99u;
        bool matched = false;

        terminal_build_even_tail(&stream, &owner);

        check_true(cflow_stream_count(&stream, &count, &error));
        check_equal(count, (size_t)2u);
        check_null(error);

        check_true(cflow_stream_any_match(
            &stream, terminal_greater_than_five, &matched, &error));
        check_true(matched);
        check_null(error);

        matched = false;
        check_true(cflow_stream_all_match(
            &stream, terminal_even, &matched, &error));
        check_true(matched);
        check_null(error);

        check_true(cflow_stream_find_first(&stream, &found, &error));
        check_true(cflow_find_result_has_value(&found));
        check_true(cmeta_type_equal(
            cflow_find_result_type(&found), &cmeta_type_int));
        check_equal(*(const int *)cflow_find_result_value(&found), 4);
        check_null(error);

        check_true(cflow_stream_for_each(
            &stream, terminal_sum, &visit, &error));
        check_equal(visit.calls, (size_t)2u);
        check_equal(visit.sum, 10);
        check_null(error);

        cflow_find_result_destroy(&found);
        check_false(cflow_find_result_has_value(&found));
        check_null(cflow_find_result_value(&found));
        cflow_stream_destroy(&stream);
    }

    it("defines empty input identities") {
        const int placeholder = 0;
        terminal_range_owner owner = {&placeholder, 0u, 0u, 0u};
        cflow_stream stream = {0};
        cflow_find_result found = {0};
        terminal_visit_state visit = {0};
        const char *error = NULL;
        size_t count = 9u;
        bool matched = true;

        check_not_null(cflow_stream_from_range(&stream, terminal_range(&owner)));
        check_true(cflow_stream_count(&stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_true(cflow_stream_any_match(
            &stream, terminal_even, &matched, &error));
        check_false(matched);
        check_true(cflow_stream_all_match(
            &stream, terminal_even, &matched, &error));
        check_true(matched);
        check_true(cflow_stream_find_first(&stream, &found, &error));
        check_false(cflow_find_result_has_value(&found));
        check_true(cflow_stream_for_each(
            &stream, terminal_sum, &visit, &error));
        check_equal(visit.calls, (size_t)0u);
        check_null(error);

        cflow_find_result_destroy(&found);
        cflow_stream_destroy(&stream);
    }

    it("short circuits matches and find before a later source error") {
        const int values[] = {2, 3};
        terminal_range_owner any_owner = {values, 2u, 0u, 2u};
        terminal_range_owner all_owner = {values, 2u, 0u, 2u};
        terminal_range_owner find_owner = {values, 2u, 0u, 2u};
        cflow_stream any_stream = {0};
        cflow_stream all_stream = {0};
        cflow_stream find_stream = {0};
        cflow_find_result found = {0};
        const char *error = NULL;
        bool matched = false;

        check_not_null(cflow_stream_from_range(
            &any_stream, terminal_range(&any_owner)));
        check_true(cflow_stream_any_match(
            &any_stream, terminal_even, &matched, &error));
        check_true(matched);
        check_equal(any_owner.resumes, (size_t)1u);
        check_null(error);

        check_not_null(cflow_stream_from_range(
            &all_stream, terminal_range(&all_owner)));
        check_true(cflow_stream_all_match(
            &all_stream, terminal_greater_than_five, &matched, &error));
        check_false(matched);
        check_equal(all_owner.resumes, (size_t)1u);
        check_null(error);

        check_not_null(cflow_stream_from_range(
            &find_stream, terminal_range(&find_owner)));
        check_true(cflow_stream_find_first(&find_stream, &found, &error));
        check_equal(*(const int *)cflow_find_result_value(&found), 2);
        check_equal(find_owner.resumes, (size_t)1u);
        check_null(error);

        cflow_find_result_destroy(&found);
        cflow_stream_destroy(&find_stream);
        cflow_stream_destroy(&all_stream);
        cflow_stream_destroy(&any_stream);
    }

    it("short circuits a source value that is also its terminal value") {
        const int value[] = {2};
        terminal_range_owner any_owner = {value, 1u, 0u, 0u};
        terminal_range_owner all_owner = {value, 1u, 0u, 0u};
        terminal_range_owner find_owner = {value, 1u, 0u, 0u};
        cflow_stream any_stream = {0};
        cflow_stream all_stream = {0};
        cflow_stream find_stream = {0};
        cflow_find_result found = {0};
        const char *error = NULL;
        bool matched = false;

        check_not_null(cflow_stream_from_range(
            &any_stream, terminal_range(&any_owner)));
        check_true(cflow_stream_any_match(
            &any_stream, terminal_even, &matched, &error));
        check_true(matched);

        check_not_null(cflow_stream_from_range(
            &all_stream, terminal_range(&all_owner)));
        check_true(cflow_stream_all_match(
            &all_stream, terminal_greater_than_five, &matched, &error));
        check_false(matched);

        check_not_null(cflow_stream_from_range(
            &find_stream, terminal_range(&find_owner)));
        check_true(cflow_stream_find_first(&find_stream, &found, &error));
        check_equal(*(const int *)cflow_find_result_value(&found), 2);
        check_equal(any_owner.resumes, (size_t)1u);
        check_equal(all_owner.resumes, (size_t)1u);
        check_equal(find_owner.resumes, (size_t)1u);
        check_null(error);

        cflow_find_result_destroy(&found);
        cflow_stream_destroy(&find_stream);
        cflow_stream_destroy(&all_stream);
        cflow_stream_destroy(&any_stream);
    }

    it("reports source predicate visitor and result-state failures") {
        const int values[] = {1, 2};
        terminal_range_owner count_owner = {values, 2u, 0u, 2u};
        terminal_range_owner predicate_owner = {values, 2u, 0u, 0u};
        terminal_range_owner visitor_owner = {values, 2u, 0u, 0u};
        cflow_stream count_stream = {0};
        cflow_stream predicate_stream = {0};
        cflow_stream visitor_stream = {0};
        cflow_find_result occupied = {(void *)values};
        terminal_visit_state visit = {0, 0u, 1u};
        cflow_filter_callable failing = terminal_even;
        cflow_filter_callable wrong = {.fn = terminal_to_long.fn};
        const char *error = NULL;
        size_t count = 7u;
        bool matched = true;

        check_not_null(cflow_stream_from_range(
            &count_stream, terminal_range(&count_owner)));
        check_false(cflow_stream_count(&count_stream, &count, &error));
        check_equal(count, (size_t)0u);
        check_equal(error, "range iteration failed");

        check_not_null(cflow_stream_from_range(
            &predicate_stream, terminal_range(&predicate_owner)));
        failing.fn.invoke = terminal_failing_invoke;
        check_false(cflow_stream_any_match(
            &predicate_stream, failing, &matched, &error));
        check_false(matched);
        check_equal(error, "stream predicate invocation failed");
        check_false(cflow_stream_all_match(
            &predicate_stream, wrong, &matched, &error));
        check_equal(error, "stream predicate type mismatch");
        check_equal(predicate_owner.resumes, (size_t)1u);

        check_not_null(cflow_stream_from_range(
            &visitor_stream, terminal_range(&visitor_owner)));
        check_false(cflow_stream_for_each(
            &visitor_stream, terminal_sum, &visit, &error));
        check_equal(error, "stream for_each callback failed");
        check_false(cflow_stream_find_first(
            &visitor_stream, &occupied, &error));
        check_equal(error, "find result is not empty");

        occupied.impl = NULL;
        cflow_stream_destroy(&visitor_stream);
        cflow_stream_destroy(&predicate_stream);
        cflow_stream_destroy(&count_stream);
    }

    it("returns stable structured status without changing legacy detail") {
        const int values[] = {1, 2};
        terminal_range_owner count_owner = {values, 2u, 0u, 2u};
        terminal_range_owner predicate_owner = {values, 2u, 0u, 0u};
        cflow_stream count_stream = {0};
        cflow_stream predicate_stream = {0};
        cflow_find_result found = {0};
        terminal_visit_state visit = {0};
        cflow_filter_callable wrong = {.fn = terminal_to_long.fn};
        cflow_status_result result;
        const char *legacy_error = NULL;
        size_t count = 9u;
        bool matched = true;

        check_not_null(cflow_stream_from_range(
            &count_stream, terminal_range(&count_owner)));
        result = cflow_stream_count_result(&count_stream, &count);
        check_equal(result.status, CFLOW_STATUS_EXECUTION_ERROR);
        check_false(cflow_status_result_is_ok(result));
        check_equal(cflow_status_result_message(result), "execution error");
        check_equal(count, (size_t)0u);

        count_owner.resumes = 0u;
        check_false(cflow_stream_count(
            &count_stream, &count, &legacy_error));
        check_equal(legacy_error, "range iteration failed");

        check_not_null(cflow_stream_from_range(
            &predicate_stream, terminal_range(&predicate_owner)));
        result = cflow_stream_all_match_result(
            &predicate_stream, wrong, &matched);
        check_equal(result.status, CFLOW_STATUS_TYPE_MISMATCH);
        check_equal(cflow_status_result_message(result), "type mismatch");
        check_false(matched);

        result = cflow_stream_find_first_result(&predicate_stream, NULL);
        check_equal(result.status, CFLOW_STATUS_INVALID_ARGUMENT);
        check_equal(cflow_status_result_message(result), "invalid argument");

        count = 99u;
        result = cflow_stream_count_result(&predicate_stream, &count);
        check_true(cflow_status_result_is_ok(result));
        check_equal(cflow_status_result_message(result), "ok");
        check_equal(count, (size_t)2u);

        result = cflow_stream_any_match_result(
            &predicate_stream, terminal_even, &matched);
        check_true(cflow_status_result_is_ok(result));
        check_true(matched);

        result = cflow_stream_find_first_result(&predicate_stream, &found);
        check_true(cflow_status_result_is_ok(result));
        check_equal(*(const int *)cflow_find_result_value(&found), 1);

        result = cflow_stream_for_each_result(
            &predicate_stream, terminal_sum, &visit);
        check_true(cflow_status_result_is_ok(result));
        check_equal(visit.calls, (size_t)2u);
        check_equal(visit.sum, 3);

        cflow_find_result_destroy(&found);
        cflow_stream_destroy(&predicate_stream);
        cflow_stream_destroy(&count_stream);
    }
}
