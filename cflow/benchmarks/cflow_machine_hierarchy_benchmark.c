#include "tinytest.h"

#include <cflow/cflow.h>

#include <stdint.h>

enum {
    CFLOW_HIERARCHY_BENCH_BUILD_SAMPLES = 2000u,
    CFLOW_HIERARCHY_BENCH_RUN_SAMPLES = 200u,
    CFLOW_HIERARCHY_BENCH_EVENTS = 256u
};

static volatile uint64_t cflow_hierarchy_bench_sink;

static const cflow_machine_state flat_states[] = {
    {2u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
};
static const cflow_machine_hierarchy_state hierarchy_states[] = {
    {1u, 0u, 2u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE},
    {2u, 1u, 0u, &cmeta_type_int, CFLOW_MACHINE_STATE_ACTIVE}
};
static const cflow_event_type bench_events[] = {
    {10u, &cmeta_type_bool}
};
static const cflow_machine_transition flat_transitions[] = {
    {2u, 10u, 0u, 0u, 2u, 0u}
};
static const cflow_machine_transition hierarchy_transitions[] = {
    {1u, 10u, 0u, 0u, 2u, 0u}
};
static const cflow_machine_definition flat_definition = {
    flat_states, 1u, 2u, bench_events, 1u,
    NULL, 0u, NULL, 0u, flat_transitions, 1u
};
static const cflow_machine_hierarchy_definition hierarchy_definition = {
    hierarchy_states, 2u, 1u, bench_events, 1u,
    NULL, 0u, NULL, 0u, hierarchy_transitions, 1u
};

static void destroy_resumable(cflow_resumable *resumable) {
    if (resumable != NULL && resumable->ops != NULL)
        resumable->ops->destroy(resumable->state);
    if (resumable != NULL) *resumable = (cflow_resumable){0};
}

suite("CFlow hierarchical Machine Release benchmarks") {
    bench("separates normalization cost from steady-state execution") {
        bool flat_build_ok = false;
        bool hierarchy_build_ok = false;

        benchmark_batch("flat Machine build + destroy",
                        CFLOW_HIERARCHY_BENCH_BUILD_SAMPLES) {
            cflow_machine machine = {0};
            flat_build_ok = cflow_machine_build(
                &machine, &flat_definition) == CFLOW_MACHINE_OK;
            cflow_hierarchy_bench_sink += cflow_machine_transition_count(
                &machine);
            cflow_machine_destroy(&machine);
        }
        check_true(flat_build_ok);

        benchmark_batch("hierarchy normalize + flat build + destroy",
                        CFLOW_HIERARCHY_BENCH_BUILD_SAMPLES) {
            cflow_machine_hierarchy hierarchy = {0};
            hierarchy_build_ok = cflow_machine_hierarchy_build(
                &hierarchy, &hierarchy_definition) ==
                CFLOW_MACHINE_HIERARCHY_OK;
            cflow_hierarchy_bench_sink += cflow_machine_transition_count(
                cflow_machine_hierarchy_flat_machine(&hierarchy));
            cflow_machine_hierarchy_destroy(&hierarchy);
        }
        check_true(hierarchy_build_ok);
    }

    bench("compares equivalent flat and hierarchy-wrapper transitions") {
        cflow_machine machine = {0};
        cflow_machine_hierarchy hierarchy = {0};
        cflow_machine_instance flat_instance = {0};
        cflow_machine_hierarchy_instance hierarchy_instance = {0};
        cflow_executor flat_executor = {0};
        cflow_executor hierarchy_executor = {0};
        cflow_clock clock = {0};
        cflow_resumable flat_resumable = {0};
        cflow_resumable hierarchy_resumable = {0};
        cflow_publish_context context = {0};
        cflow_step step;
        const int initial_state = 0;
        const bool payload = true;
        const cflow_event_view event = {10u, &cmeta_type_bool, &payload};
        cflow_machine_instance_config flat_config;
        cflow_machine_hierarchy_instance_config hierarchy_config;
        cflow_machine_instance_stats flat_stats = {0};
        cflow_machine_hierarchy_instance_stats hierarchy_stats = {0};
        const uint64_t expected_transitions =
            (uint64_t)CFLOW_HIERARCHY_BENCH_RUN_SAMPLES *
            (uint64_t)CFLOW_HIERARCHY_BENCH_EVENTS;
        bool flat_run_ok = true;
        bool hierarchy_run_ok = true;
        size_t index;
        int output = 0;

        check_equal(cflow_machine_build(&machine, &flat_definition),
                    CFLOW_MACHINE_OK);
        check_equal(cflow_machine_hierarchy_build(
                        &hierarchy, &hierarchy_definition),
                    CFLOW_MACHINE_HIERARCHY_OK);
        check_true(cflow_executor_serial_init(&flat_executor));
        check_true(cflow_executor_serial_init(&hierarchy_executor));
        check_true(cflow_clock_virtual_init(&clock, (cflow_instant){0u}));
        flat_config = (cflow_machine_instance_config){
            &machine, &initial_state, &cmeta_type_int,
            NULL, 0u, NULL, 0u,
            CFLOW_HIERARCHY_BENCH_EVENTS, &flat_executor
        };
        check_equal(cflow_machine_instance_init(
                        &flat_instance, &flat_config),
                    CFLOW_MACHINE_INSTANCE_OK);
        hierarchy_config = (cflow_machine_hierarchy_instance_config){
            &hierarchy, &initial_state, &cmeta_type_int,
            NULL, 0u, NULL, 0u,
            CFLOW_HIERARCHY_BENCH_EVENTS, &hierarchy_executor,
            &clock, 1u
        };
        check_equal(cflow_machine_hierarchy_instance_init(
                        &hierarchy_instance, &hierarchy_config).status,
                    CFLOW_MACHINE_HIERARCHY_INSTANCE_OK);
        check_true(cflow_machine_instance_as_resumable(
            &flat_instance, &flat_resumable));
        check_true(cflow_machine_hierarchy_instance_as_resumable(
            &hierarchy_instance, &hierarchy_resumable));
        step = flat_resumable.ops->resume(
            flat_resumable.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);
        step = hierarchy_resumable.ops->resume(
            hierarchy_resumable.state, &context, &output);
        check_equal(step.kind, CFLOW_STEP_WAIT);

        benchmark_ops("flat Machine instance transitions",
                      CFLOW_HIERARCHY_BENCH_RUN_SAMPLES,
                      CFLOW_HIERARCHY_BENCH_EVENTS) {
            for (index = 0u; index < CFLOW_HIERARCHY_BENCH_EVENTS; ++index) {
                if (cflow_machine_instance_try_send(&flat_instance, &event) !=
                    CFLOW_MAILBOX_OK)
                    flat_run_ok = false;
            }
            if (!cflow_executor_wait_idle(&flat_executor)) flat_run_ok = false;
        }
        check_true(flat_run_ok);

        benchmark_ops("hierarchy wrapper normalized transitions",
                      CFLOW_HIERARCHY_BENCH_RUN_SAMPLES,
                      CFLOW_HIERARCHY_BENCH_EVENTS) {
            for (index = 0u; index < CFLOW_HIERARCHY_BENCH_EVENTS; ++index) {
                if (cflow_machine_hierarchy_instance_try_send(
                        &hierarchy_instance, &event) != CFLOW_MAILBOX_OK)
                    hierarchy_run_ok = false;
            }
            if (!cflow_executor_wait_idle(&hierarchy_executor))
                hierarchy_run_ok = false;
        }
        check_true(hierarchy_run_ok);
        check_true(cflow_machine_instance_get_stats(
            &flat_instance, &flat_stats));
        check_true(cflow_machine_hierarchy_instance_get_stats(
            &hierarchy_instance, &hierarchy_stats));
        check_equal(flat_stats.completed, expected_transitions);
        check_equal(flat_stats.failed, (uint64_t)0u);
        check_equal(hierarchy_stats.machine.completed, expected_transitions);
        check_equal(hierarchy_stats.machine.failed, (uint64_t)0u);

        cflow_hierarchy_bench_sink +=
            cflow_machine_instance_current_state(&flat_instance);
        cflow_hierarchy_bench_sink +=
            cflow_machine_hierarchy_instance_current_state(
                &hierarchy_instance);
        destroy_resumable(&hierarchy_resumable);
        destroy_resumable(&flat_resumable);
        cflow_machine_hierarchy_instance_destroy(&hierarchy_instance);
        cflow_machine_instance_destroy(&flat_instance);
        cflow_clock_destroy(&clock);
        cflow_executor_destroy(&hierarchy_executor);
        cflow_executor_destroy(&flat_executor);
        cflow_machine_hierarchy_destroy(&hierarchy);
        cflow_machine_destroy(&machine);
    }
}
