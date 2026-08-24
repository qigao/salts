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
static_assert(std::is_standard_layout<cflow_timer_event_queue>::value,
              "timer Event queue must remain a C-compatible handle");
static_assert(std::is_standard_layout<cflow_machine_transition>::value,
              "cflow_machine_transition must remain a C-compatible row");

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
        cflow_actor_ref_release(&actor_ref);
        cflow_actor_destroy(&actor);
        check_null(timer_events.impl);
        cflow_machine_instance_destroy(&instance);
    }
}
