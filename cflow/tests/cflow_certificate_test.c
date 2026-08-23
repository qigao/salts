#include "tinytest.h"

#include <cflow/cflow.h>

#include <string.h>

typed(filter, value, bool, cflow_cert_even, (int value)) {
  return value % 2 == 0;
}

typed(map, value, long, cflow_cert_square, (int value)) {
  return (long)value * (long)value;
}

typed(reduce, associative, long, cflow_cert_add, (long left, long right)) {
  return left + right;
}

typed(reduce, associative, long, cflow_cert_other, (long left, long right)) {
  return left > right ? left : right;
}

typed(map, value, double, cflow_cert_as_double, (int value)) {
  return (double)value + 0.25;
}

typed(zip, value, double, cflow_cert_merge, (long left, double right)) {
  return (double)left + right;
}

typedef struct cflow_certificate_fixture {
  cflow_stream stream;
  cflow_graph normalized;
  cflow_graph optimized;
  cflow_plan plan;
  cflow_plan_certificate certificate;
} cflow_certificate_fixture;

static cflow_certificate_fixture cflow_certificate_state;

suite("CFlow Plan refinement certificate") {
  before_each() {
    cflow_certificate_fixture *state = &cflow_certificate_state;
    memset(state, 0, sizeof(*state));
    state->normalized.root = CMETA_INVALID_ID;
    state->optimized.root = CMETA_INVALID_ID;
    check_not_null(cflow_stream_init(&state->stream, &cmeta_type_int));
    check_not_null(state->stream.filter(&state->stream, cflow_cert_even));
    check_not_null(state->stream.map(&state->stream, cflow_cert_square));
    check_not_null(state->stream.reduce(&state->stream, cflow_cert_add));
    check_true(cflow_graph_normalize(&state->normalized, &state->stream.graph));
    check_true(cflow_graph_optimize(
        &state->optimized, &state->normalized,
        (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL));
    check_true(cflow_plan_compile(&state->plan, &state->optimized, NULL));
    check_true(cflow_plan_certificate_build(
        &state->certificate, &state->optimized, &state->plan,
        CFLOW_CERTIFIED_PATH_SEQUENTIAL));
  }

  after_each() {
    cflow_plan_certificate_destroy(&cflow_certificate_state.certificate);
    cflow_plan_destroy(&cflow_certificate_state.plan);
    cflow_graph_destroy(&cflow_certificate_state.optimized);
    cflow_graph_destroy(&cflow_certificate_state.normalized);
    cflow_stream_destroy(&cflow_certificate_state.stream);
  }

  it("checks sequential and ordered-parallel certificates") {
    cflow_certificate_fixture *state = &cflow_certificate_state;
    const char *error = "not cleared";

    check_true(cflow_plan_certificate_check(
        &state->certificate, &state->optimized, &state->plan, &error));
    check_null(error);
    cflow_plan_certificate_destroy(&state->certificate);
    check_true(cflow_plan_certificate_build(
        &state->certificate, &state->optimized, &state->plan,
        CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE));
    check_true(cflow_plan_certificate_check(
        &state->certificate, &state->optimized, &state->plan, &error));
    check_null(error);
  }

  it("rejects each tampered stable field and semantic row") {
    cflow_certificate_fixture *state = &cflow_certificate_state;
    cflow_plan_certificate *certificate = &state->certificate;
    cflow_plan_certificate_row *row = &certificate->rows[certificate->row_count - 1u];
    const char *error = NULL;
    uint32_t saved32;
    uint64_t saved64;
    size_t saved_count;
    const cmeta_type_desc *saved_type;
    cmeta_callable saved_callable;

    saved32 = certificate->version; certificate->version = 99u;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    check_not_null(error); certificate->version = saved32;
    saved64 = certificate->graph_fingerprint; ++certificate->graph_fingerprint;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    certificate->graph_fingerprint = saved64;
    saved32 = row->opcode; row->opcode = CFLOW_CERTIFIED_FILTER;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    row->opcode = saved32;
    saved_type = row->input_type; row->input_type = &cmeta_type_bool;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    row->input_type = saved_type;
    saved_callable = row->callable; row->callable = cflow_cert_other.fn;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    row->callable = saved_callable;
    saved32 = row->properties; row->properties &= ~CMETA_PROP_ASSOCIATIVE;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    row->properties = saved32;
    saved32 = certificate->path; certificate->path = 99u;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    certificate->path = saved32;
    saved32 = certificate->order; certificate->order = CFLOW_CERTIFICATE_ORDER_ENCOUNTER;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    certificate->order = saved32;
    saved_count = certificate->row_count; --certificate->row_count;
    check_false(cflow_plan_certificate_check(certificate, &state->optimized, &state->plan, &error));
    certificate->row_count = saved_count;
  }

  it("rejects a stale normalized Graph identity") {
    cflow_certificate_fixture *state = &cflow_certificate_state;
    cflow_graph stale = {0};
    const char *error = NULL;
    stale.root = CMETA_INVALID_ID;

    check_true(cflow_graph_clone(&stale, &state->optimized));
    check_false(cflow_plan_certificate_check(
        &state->certificate, &stale, &state->plan, &error));
    check_not_null(error);
    cflow_graph_destroy(&stale);
  }

  it("rejects Relation certificates without hiding Kernel execution") {
    const int input[] = {2, 3};
    const double expected[] = {6.25, 12.25};
    cflow_stream left = {0};
    cflow_stream right = {0};
    cflow_graph normalized = {0};
    cflow_graph optimized = {0};
    cflow_plan unsupported = {0};
    cflow_plan_certificate certificate = {0};
    cflow_result kernel = {0};
    normalized.root = optimized.root = CMETA_INVALID_ID;

    check_not_null(cflow_stream_init(&left, &cmeta_type_int));
    check_not_null(cflow_stream_init(&right, &cmeta_type_int));
    check_not_null(left.map(&left, cflow_cert_square));
    check_not_null(right.map(&right, cflow_cert_as_double));
    check_not_null(left.zip(&left, &right, cflow_cert_merge));
    check_true(cflow_graph_normalize(&normalized, &left.graph));
    check_true(cflow_graph_optimize(
        &optimized, &normalized,
        (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL));
    check_false(cflow_plan_compile(&unsupported, &optimized, NULL));
    check_false(cflow_plan_certificate_build(
        &certificate, &optimized, &unsupported,
        CFLOW_CERTIFIED_PATH_SEQUENTIAL));
    check_true(cflow_eval_array(&optimized, input, 2u, &kernel));
    check_equal(kernel.count, (size_t)2u);
    check_equal(kernel.data, expected, sizeof(expected));

    cflow_result_destroy(&kernel);
    cflow_plan_certificate_destroy(&certificate);
    cflow_plan_destroy(&unsupported);
    cflow_graph_destroy(&optimized);
    cflow_graph_destroy(&normalized);
    cflow_stream_destroy(&right);
    cflow_stream_destroy(&left);
  }
}
