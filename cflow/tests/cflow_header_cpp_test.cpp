#include <cflow/cflow.h>
#include "tinytest.h"

#include <type_traits>

static_assert(std::is_standard_layout<cflow_run>::value,
              "cflow_run must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_find_result>::value,
              "find result must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_status_result>::value,
              "status result must remain C-compatible");
static_assert(std::is_standard_layout<cflow_collect_result>::value,
              "collect result must remain C-compatible");
static_assert(std::is_standard_layout<cflow_eval_options>::value,
              "evaluation options must remain C-compatible");
static_assert(std::is_standard_layout<cflow_set_state_ops>::value,
              "set backend interface must remain C-compatible");
static_assert(std::is_standard_layout<cflow_sequence_state_ops>::value,
              "sequence backend interface must remain C-compatible");
static_assert(std::is_standard_layout<cmeta_callable>::value,
              "cmeta_callable must remain a C-compatible value");
static_assert(std::is_standard_layout<cflow_event_type>::value,
              "cflow_event_type must remain a C-compatible schema row");
static_assert(std::is_standard_layout<cflow_mailbox>::value,
              "cflow_mailbox must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine>::value,
              "cflow_machine must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_hierarchy>::value,
              "machine hierarchy must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_hierarchy_instance>::value,
              "hierarchy instance must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_instance>::value,
              "cflow_machine_instance must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_instance_config>::value,
              "machine runtime config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_statechart>::value,
              "statechart must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_statechart_definition>::value,
              "statechart definition must remain C-compatible");
static_assert(std::is_standard_layout<cflow_statechart_instance>::value,
              "statechart runtime must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_statechart_instance_config>::value,
              "statechart runtime config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_actor>::value,
              "cflow_actor must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_actor_ref>::value,
              "cflow_actor_ref must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_actor_config>::value,
              "actor config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_statechart_actor_config>::value,
              "Statechart Actor config must remain C-compatible");
static_assert(
    std::is_standard_layout<cflow_statechart_actor_init_result>::value,
    "Statechart Actor init result must remain C-compatible");
static_assert(std::is_standard_layout<cflow_statechart_actor_stats>::value,
              "Statechart Actor stats must remain C-compatible");
static_assert(CFLOW_ACTOR_FAILED == 8,
              "existing Actor status values are ABI-visible");
static_assert(CFLOW_ACTOR_STATECHART_REJECTED == 9,
              "Statechart rejection must remain appended");
static_assert(CFLOW_OP_SOURCE == 0 && CFLOW_OP_RELATION == 1 &&
                   CFLOW_OP_FILTER == 2 && CFLOW_OP_ZIP == 7 &&
                   CFLOW_OP_TAKE == 8 && CFLOW_OP_SKIP == 9 &&
                   CFLOW_OP_DISTINCT == 10 && CFLOW_OP_SORTED == 11,
              "stateful opcodes must append without renumbering existing IR");
static_assert(std::is_standard_layout<cflow_io_actor>::value,
              "IO Actor must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_io_operation>::value,
              "IO operation must remain a C-compatible move token");
static_assert(std::is_standard_layout<cflow_io_actor_config>::value,
              "IO Actor config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_native_backend>::value,
              "native IO backend must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_io_native_operation>::value,
              "native IO operation must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_native_pipe_operation>::value,
              "native pipe operation must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_native_file_operation>::value,
              "native file operation must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_file>::value,
              "IO file facade must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_io_file_config>::value,
              "IO file config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_file_stats>::value,
              "IO file stats must remain C-compatible");
static_assert(std::is_standard_layout<cflow_timer_event_queue>::value,
              "timer Event queue must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_transition>::value,
              "cflow_machine_transition must remain a C-compatible row");
static_assert(std::is_standard_layout<turbo_readiness_registration>::value,
              "reactor registration must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_reactor_source_owner>::value,
              "reactor Source owner must remain a C-compatible handle");

using cflow_reactor_factory = int (*)(
    cflow_source *, cflow_reactor_source_owner *,
    turbo_readiness_registration *, turbo_readiness_events, const char *,
    const cmeta_type_desc *, cflow_read_fn, cflow_resource_close_fn, void *);
static_assert(std::is_same<
                  decltype(&cflow_source_from_reactor_registration),
                  cflow_reactor_factory>::value,
              "reactor Source factory must keep its C linkage signature");
using cflow_reactor_owner_close = int (*)(cflow_reactor_source_owner *);
static_assert(std::is_same<decltype(&cflow_reactor_source_owner_close),
                           cflow_reactor_owner_close>::value,
              "reactor Source owner close must keep its C linkage signature");
using cflow_statechart_terminal_resumable_factory = bool (*)(
    cflow_statechart_instance *, cflow_resumable *);
static_assert(std::is_same<
                  decltype(&cflow_statechart_instance_as_terminal_resumable),
                  cflow_statechart_terminal_resumable_factory>::value,
              "Statechart Resumable adapter must keep its C signature");
using cflow_statechart_terminal_source_factory = bool (*)(
    cflow_statechart_instance *, cflow_source *);
static_assert(std::is_same<
                  decltype(&cflow_statechart_instance_as_terminal_source),
                  cflow_statechart_terminal_source_factory>::value,
              "Statechart Source adapter must keep its C signature");
using cflow_statechart_actor_factory = cflow_statechart_actor_init_result (*)(
    cflow_actor *, const cflow_statechart_actor_config *);
static_assert(std::is_same<decltype(&cflow_statechart_actor_init),
                           cflow_statechart_actor_factory>::value,
              "Statechart Actor initializer must keep its C signature");
using cflow_statechart_actor_stats_reader = bool (*)(
    const cflow_actor *, cflow_statechart_actor_stats *);
static_assert(std::is_same<decltype(&cflow_statechart_actor_get_stats),
                           cflow_statechart_actor_stats_reader>::value,
              "Statechart Actor stats must keep its C signature");
using cflow_stream_slice_function = cflow_stream *(*)(cflow_stream *, size_t);
static_assert(std::is_same<decltype(&cflow_stream_take),
                           cflow_stream_slice_function>::value,
              "Stream take must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_skip),
                            cflow_stream_slice_function>::value,
              "Stream skip must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_distinct),
                           cflow_stream_slice_function>::value,
              "Stream distinct must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_sorted),
                           cflow_stream_slice_function>::value,
              "Stream sorted must keep its C signature");
using cflow_stream_range_options_function = cflow_stream *(*) (
    cflow_stream *, cmeta_range, const cflow_eval_options *);
static_assert(std::is_same<
                  decltype(&cflow_stream_from_range_with_options),
                  cflow_stream_range_options_function>::value,
              "Stream backend injection must keep its C signature");
using cflow_stream_count_function = bool (*)(
    const cflow_stream *, size_t *, const char **);
static_assert(std::is_same<decltype(&cflow_stream_count),
                           cflow_stream_count_function>::value,
              "Stream count must keep its C signature");
using cflow_stream_match_function = bool (*)(
    const cflow_stream *, cflow_filter_callable, bool *, const char **);
static_assert(std::is_same<decltype(&cflow_stream_any_match),
                           cflow_stream_match_function>::value,
              "Stream any_match must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_all_match),
                           cflow_stream_match_function>::value,
              "Stream all_match must keep its C signature");
using cflow_stream_count_result_function = cflow_status_result (*)(
    const cflow_stream *, size_t *);
using cflow_eval_array_result_function = cflow_status_result (*)(
    const cflow_graph *, const void *, size_t, cflow_result *);
using cflow_eval_stream_result_function = cflow_status_result (*)(
    const cflow_stream *, cflow_result *);
using cflow_eval_stream_limit_result_function = cflow_status_result (*)(
    const cflow_stream *, size_t, cflow_result *);
using cflow_eval_collect_result_function = cflow_collect_result (*)(
    const cflow_stream *, cmeta_collector *, const char **);
using cflow_stream_match_result_function = cflow_status_result (*)(
    const cflow_stream *, cflow_filter_callable, bool *);
using cflow_stream_find_result_function = cflow_status_result (*)(
    const cflow_stream *, cflow_find_result *);
using cflow_stream_for_each_result_function = cflow_status_result (*)(
    const cflow_stream *, cflow_value_fn, void *);
static_assert(std::is_same<decltype(&cflow_stream_count_result),
                           cflow_stream_count_result_function>::value,
              "structured Stream count must keep its C signature");
static_assert(std::is_same<decltype(&cflow_eval_array_result),
                           cflow_eval_array_result_function>::value,
              "structured array evaluation must keep its C signature");
static_assert(std::is_same<decltype(&cflow_eval_stream_result),
                           cflow_eval_stream_result_function>::value,
              "structured Stream evaluation must keep its C signature");
static_assert(std::is_same<decltype(&cflow_eval_stream_limit_result),
                           cflow_eval_stream_limit_result_function>::value,
              "structured bounded Stream evaluation must keep its C signature");
static_assert(std::is_same<decltype(&cflow_eval_collect_result),
                           cflow_eval_collect_result_function>::value,
              "structured collection must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_any_match_result),
                           cflow_stream_match_result_function>::value,
              "structured Stream any_match must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_all_match_result),
                           cflow_stream_match_result_function>::value,
              "structured Stream all_match must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_find_first_result),
                           cflow_stream_find_result_function>::value,
              "structured Stream find_first must keep its C signature");
static_assert(std::is_same<decltype(&cflow_stream_for_each_result),
                           cflow_stream_for_each_result_function>::value,
              "structured Stream for_each must keep its C signature");

suite("CFlow C++ public header") {
    it("exposes the aggregate API to C++ consumers") {
        cflow_run run = {};
        cflow_source source = {};
        cflow_graph graph = {};
        cflow_stream stream = {};
        cflow_subscription subscription = {};
        cflow_result result = {};
        cflow_find_result find_result = {};
        cflow_status_result status_result = {CFLOW_STATUS_OK};
        const cflow_event_type event_schema[] = {{1u, &cmeta_type_int}};
        const int sent = 23;
        const cflow_event_view event = {1u, &cmeta_type_int, &sent};
        cflow_mailbox mailbox = {};
        cflow_machine machine = {};
        cflow_machine_hierarchy hierarchy = {};
        cflow_machine_instance instance = {};
        cflow_machine_instance_config machine_config = {};
        cflow_statechart statechart = {};
        cflow_statechart_definition statechart_definition = {};
        cflow_statechart_instance statechart_instance = {};
        cflow_resumable statechart_terminal_resumable = {};
        cflow_source statechart_terminal_source = {};
        cflow_statechart_instance_config statechart_config = {};
        cflow_statechart_instance_stats statechart_stats = {};
        cflow_actor actor = {};
        cflow_actor_ref actor_ref = {};
        cflow_statechart_actor_config statechart_actor_config = {};
        cflow_statechart_actor_init_result statechart_actor_init = {};
        cflow_statechart_actor_stats statechart_actor_stats = {};
        cflow_io_actor io_actor = {};
        cflow_io_operation io_operation = {};
        cflow_io_native_backend native_backend = {};
        cflow_io_native_operation native_operation = {};
        cflow_io_native_pipe_operation native_pipe_operation = {};
        cflow_io_native_file_operation native_file_operation = {};
        cflow_io_file io_file = {};
        cflow_io_file_config io_file_config = {};
        cflow_io_file_stats io_file_stats = {};
        cflow_io_file_submit_result io_file_submit = {};
        cflow_io_native_backend_kind native_backend_kind = CFLOW_IO_NATIVE_POLL;
        cflow_io_native_pipe_operation_kind native_pipe_kind =
            CFLOW_IO_NATIVE_PIPE_READ;
        cflow_io_native_file_operation_kind native_file_kind =
            CFLOW_IO_NATIVE_FILE_READ_AT;
        cflow_io_backend_ops native_pipe_ops =
            cflow_io_native_backend_pipe_actor_ops();
        cflow_io_backend_ops native_file_ops =
            cflow_io_native_backend_file_actor_ops();
        cflow_timer_event_queue timer_events = {};
        cflow_event_id event_id = 0u;
        const cmeta_type_desc *event_type = nullptr;
        int received = 0;

        check_null(run.impl);
        check_false(cflow_source_valid(&source));
        cflow_graph_init(&graph, &cmeta_type_int);
        check_true(cmeta_type_equal(
            cflow_graph_source_type(&graph), &cmeta_type_int));
        check_true(cflow_graph_structural_equal(&graph, &graph));
        cflow_graph_destroy(&graph);

        check_true(cflow_stream_init(&stream, &cmeta_type_int) == &stream);
        check_true(stream.skip(&stream, 1u)->take(&stream, 2u) == &stream);
        cflow_stream_destroy(&stream);
        check_false(cflow_subscription_is_done(&subscription));
        cflow_result_destroy(&result);
        check_false(cflow_find_result_has_value(&find_result));
        cflow_find_result_destroy(&find_result);
        check_true(cflow_status_result_is_ok(status_result));
        check_true(cflow_status_result_message(status_result) ==
                   cflow_status_string(CFLOW_STATUS_OK));

        check_true(cflow_mailbox_init(&mailbox, event_schema, 1u, 1u) ==
                   CFLOW_MAILBOX_OK);
        check_true(cflow_mailbox_try_send(&mailbox, &event) ==
                   CFLOW_MAILBOX_OK);
        check_true(cflow_mailbox_try_receive(
                       &mailbox, &event_id, &event_type,
                       &received, sizeof(received)) == CFLOW_MAILBOX_OK);
        check_true(event_id == static_cast<cflow_event_id>(1u));
        check_true(event_type == &cmeta_type_int);
        check_true(received == sent);
        cflow_mailbox_destroy(&mailbox);
        check_null(machine.impl);
        cflow_machine_destroy(&machine);
        check_null(hierarchy.impl);
        cflow_machine_hierarchy_destroy(&hierarchy);
        check_null(instance.impl);
        check_null(machine_config.machine);
        check_null(statechart.impl);
        check_null(statechart_definition.state_type);
        check_null(statechart_instance.impl);
        check_null(statechart_terminal_resumable.ops);
        check_false(cflow_source_valid(&statechart_terminal_source));
        check_null(statechart_config.statechart);
        check_true(statechart_stats.last_status ==
                   CFLOW_STATECHART_RUNTIME_OK);
        cflow_statechart_destroy(&statechart);
        check_null(actor.impl);
        check_null(actor_ref.impl);
        check_null(statechart_actor_config.statechart.statechart);
        check_true(statechart_actor_init.status == CFLOW_ACTOR_OK);
        check_true(statechart_actor_stats.state == CFLOW_ACTOR_STATE_START);
        check_null(io_actor.impl);
        check_null(io_operation.user);
        check_null(native_backend.impl);
        check_true(native_backend_kind == CFLOW_IO_NATIVE_POLL);
        check_true(native_operation.kind == CFLOW_IO_NATIVE_TCP_RECV);
        check_true(native_pipe_operation.kind == CFLOW_IO_NATIVE_PIPE_READ);
        check_true(native_pipe_kind == CFLOW_IO_NATIVE_PIPE_READ);
        check_not_null(native_pipe_ops.submit);
        (void)cflow_io_native_backend_pipe_supported(native_backend_kind);
        check_true(native_file_operation.kind == CFLOW_IO_NATIVE_FILE_READ_AT);
        check_true(native_file_kind == CFLOW_IO_NATIVE_FILE_READ_AT);
        check_not_null(native_file_ops.submit);
        check_null(io_file.impl);
        check_true(io_file_config.open_flags == 0u);
        check_true(io_file_stats.operation_slots_in_use == 0u);
        check_true(io_file_submit.status == CFLOW_IO_FILE_SUBMIT_ACCEPTED);
        (void)cflow_io_native_backend_file_operation_supported(
            native_backend_kind, native_file_kind);
        cflow_actor_ref_release(&actor_ref);
        cflow_actor_destroy(&actor);
        check_null(timer_events.impl);
        cflow_machine_instance_destroy(&instance);
    }
}
