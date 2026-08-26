#include <cflow/cflow.h>
#include "tinytest.h"

#include <type_traits>

static_assert(std::is_standard_layout<cflow_run>::value,
              "cflow_run must remain a C-compatible handle");
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
static_assert(std::is_standard_layout<cflow_actor>::value,
              "cflow_actor must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_actor_ref>::value,
              "cflow_actor_ref must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_actor_config>::value,
              "actor config must remain C-compatible");
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
static_assert(std::is_standard_layout<cflow_io_source_owner>::value,
              "IO Source owner must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_io_source_config>::value,
              "IO Source config must remain C-compatible");
static_assert(std::is_standard_layout<cflow_io_source_stats>::value,
              "IO Source stats must remain C-compatible");
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

using cflow_io_source_prepare_callback = cflow_io_source_prepare_status (*)(
    void *, cflow_io_operation *, const char **);
static_assert(std::is_same<cflow_io_source_prepare_fn,
                           cflow_io_source_prepare_callback>::value,
              "IO Source prepare callback must keep its exact C signature");
using cflow_io_source_encode_callback = cflow_read_status (*)(
    void *, cflow_io_request_id, cflow_io_lease_id, void *,
    const cflow_io_completion *, void *, const char **);
static_assert(std::is_same<cflow_io_source_encode_fn,
                           cflow_io_source_encode_callback>::value,
              "IO Source encode callback must keep its exact C signature");

suite("CFlow C++ public header") {
    it("exposes the aggregate API to C++ consumers") {
        cflow_run run = {};
        cflow_source source = {};
        cflow_graph graph = {};
        cflow_stream stream = {};
        cflow_subscription subscription = {};
        cflow_result result = {};
        const cflow_event_type event_schema[] = {{1u, &cmeta_type_int}};
        const int sent = 23;
        const cflow_event_view event = {1u, &cmeta_type_int, &sent};
        cflow_mailbox mailbox = {};
        cflow_machine machine = {};
        cflow_machine_hierarchy hierarchy = {};
        cflow_machine_instance instance = {};
        cflow_machine_instance_config machine_config = {};
        cflow_actor actor = {};
        cflow_actor_ref actor_ref = {};
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
        cflow_io_source_owner io_source_owner = {};
        cflow_io_source_config io_source_config = {};
        cflow_io_source_stats io_source_stats = {};
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
        cflow_stream_destroy(&stream);
        check_false(cflow_subscription_is_done(&subscription));
        cflow_result_destroy(&result);

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
        check_null(actor.impl);
        check_null(actor_ref.impl);
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
        check_null(io_source_owner.impl);
        check_null(io_source_config.name);
        check_null(io_source_config.prepare);
        check_null(io_source_config.encode);
        check_true(io_source_stats.actor.request_capacity == 0u);
        check_false(io_source_stats.source_live);
        (void)cflow_io_native_backend_file_operation_supported(
            native_backend_kind, native_file_kind);
        cflow_actor_ref_release(&actor_ref);
        cflow_actor_destroy(&actor);
        check_null(timer_events.impl);
        cflow_machine_instance_destroy(&instance);
    }
}
