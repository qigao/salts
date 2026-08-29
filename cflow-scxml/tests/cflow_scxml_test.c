#include <cflow/scxml.h>
#include <cflow/executor.h>
#include <cflow/statechart_runtime.h>
#include <tlog.h>

#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_LOG_CAPTURE_CAPACITY 4u
#define SCXML_LOG_COMPONENT_CAPACITY 32u
#define SCXML_LOG_MESSAGE_CAPACITY 64u

typedef struct scxml_log_capture {
    size_t count;
    turbo_log_level_t levels[SCXML_LOG_CAPTURE_CAPACITY];
    char components[SCXML_LOG_CAPTURE_CAPACITY]
                   [SCXML_LOG_COMPONENT_CAPACITY];
    char messages[SCXML_LOG_CAPTURE_CAPACITY][SCXML_LOG_MESSAGE_CAPACITY];
} scxml_log_capture;

static cflow_scxml_status compile_status(
    const char *source, cflow_scxml_program *program,
    cflow_scxml_diagnostic *diagnostic);

static void capture_scxml_log(const turbo_log_entry_t *entry,
                              void *user_data) {
    scxml_log_capture *capture = (scxml_log_capture *)user_data;
    size_t index;
    size_t component_size;
    size_t message_size;
    if (entry == NULL || capture == NULL ||
        capture->count >= SCXML_LOG_CAPTURE_CAPACITY) {
        return;
    }
    index = capture->count++;
    capture->levels[index] = entry->level;
    component_size = entry->component != NULL ? strlen(entry->component) : 0u;
    if (component_size >= SCXML_LOG_COMPONENT_CAPACITY)
        component_size = SCXML_LOG_COMPONENT_CAPACITY - 1u;
    if (component_size != 0u)
        memcpy(capture->components[index], entry->component, component_size);
    capture->components[index][component_size] = '\0';
    message_size = entry->message_len;
    if (message_size >= SCXML_LOG_MESSAGE_CAPACITY)
        message_size = SCXML_LOG_MESSAGE_CAPACITY - 1u;
    if (message_size != 0u)
        memcpy(capture->messages[index], entry->message, message_size);
    capture->messages[index][message_size] = '\0';
}

static bool run_log_program(const char *source, bool install_logger,
                            scxml_log_capture *capture, bool *out_done,
                            bool *out_errored) {
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *bindings = NULL;
    size_t binding_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_config config = {0};
    cflow_statechart_instance_stats stats = {0};
    tlog_t *previous_logger = tlog_peek_default();
    tlog_t *logger = NULL;
    turbo_log_sink_t *sink = NULL;
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;

    if (source == NULL || capture == NULL || out_done == NULL ||
        out_errored == NULL) {
        return false;
    }
    memset(capture, 0, sizeof(*capture));
    if (install_logger) {
        const tlog_config_t log_config = {
            .min_level = TURBO_LOG_LEVEL_DEBUG, .buffer_size = 0u};
        logger = tlog_create(&log_config);
        if (logger == NULL) goto cleanup;
        sink = turbo_sink_callback_create(capture_scxml_log, capture);
        if (sink == NULL || tlog_add_sink(logger, sink) != 0) goto cleanup;
        sink = NULL;
        tlog_set_default(logger);
    } else {
        tlog_set_default(NULL);
    }
    if (compile_status(source, &program, &diagnostic) != CFLOW_SCXML_OK ||
        !cflow_scxml_program_runtime_bindings(
            &program, &bindings, &binding_count) ||
        !cflow_executor_serial_init(&executor)) {
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = cflow_scxml_program_statechart(&program),
        .initial_state = cflow_scxml_program_initial_state(&program),
        .executables = bindings,
        .executable_count = binding_count,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 2u,
        .microstep_limit = 16u,
        .executor = &executor};
    if (cflow_statechart_instance_init(&instance, &config) !=
        CFLOW_STATECHART_RUNTIME_OK) {
        goto cleanup;
    }
    instance_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_statechart_instance_get_stats(&instance, &stats)) {
        goto cleanup;
    }
    *out_done = stats.done;
    *out_errored = stats.errored;
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    if (logger != NULL) tlog_flush(logger);
    tlog_set_default(previous_logger);
    if (sink != NULL) turbo_sink_destroy(sink);
    if (logger != NULL) tlog_destroy(logger);
    cflow_scxml_program_destroy(&program);
    return succeeded;
}

static cflow_scxml_status compile_status(const char *source,
                                         cflow_scxml_program *program,
                                         cflow_scxml_diagnostic *diagnostic) {
    return cflow_scxml_compile(program, source, strlen(source), NULL,
                               diagnostic);
}

static const cflow_statechart_state *find_state(
    const cflow_scxml_program *program, const char *name) {
    const cflow_statechart *statechart =
        cflow_scxml_program_statechart(program);
    cflow_machine_state_id id = 0u;
    size_t index;

    if (!cflow_scxml_program_state_id(program, name, strlen(name), &id))
        return NULL;
    for (index = 0u; index < cflow_statechart_state_count(statechart); ++index) {
        const cflow_statechart_state *state =
            cflow_statechart_state_at(statechart, index);
        if (state != NULL && state->id == id) return state;
    }
    return NULL;
}

static bool run_condition_program(const char *source, const char *event_name,
                                  size_t *out_guard_count, bool *out_done,
                                  cflow_machine_state_id *out_current_state) {
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    const cflow_statechart_executable_binding *executables = NULL;
    const cflow_statechart_guard_binding *guards = NULL;
    size_t executable_count = 0u;
    size_t guard_count = 0u;
    cflow_executor executor = {0};
    cflow_statechart_instance instance = {0};
    cflow_statechart_instance_config config = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_event_view event = {0};
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;

    if (source == NULL || out_guard_count == NULL || out_done == NULL ||
        compile_status(source, &program, &diagnostic) != CFLOW_SCXML_OK ||
        !cflow_scxml_program_runtime_bindings(
            &program, &executables, &executable_count) ||
        !cflow_scxml_program_guard_bindings(
            &program, &guards, &guard_count) ||
        !cflow_executor_serial_init(&executor)) {
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_statechart_instance_config){
        .statechart = cflow_scxml_program_statechart(&program),
        .initial_state = cflow_scxml_program_initial_state(&program),
        .guards = guards,
        .guard_count = guard_count,
        .executables = executables,
        .executable_count = executable_count,
        .external_event_capacity = 2u,
        .internal_event_capacity = 2u,
        .completion_capacity = 4u,
        .microstep_limit = 32u,
        .executor = &executor};
    if (cflow_statechart_instance_init(&instance, &config) !=
        CFLOW_STATECHART_RUNTIME_OK) {
        goto cleanup;
    }
    instance_initialized = true;
    if (event_name != NULL) {
        if (!cflow_scxml_program_event(
                &program, event_name, strlen(event_name), &event) ||
            cflow_statechart_instance_try_send(&instance, &event) !=
                CFLOW_MAILBOX_OK ||
            !cflow_executor_wait_idle(&executor)) {
            goto cleanup;
        }
    }
    if (!cflow_statechart_instance_get_stats(&instance, &stats)) goto cleanup;
    *out_guard_count = guard_count;
    *out_done = stats.done;
    if (out_current_state != NULL) {
        *out_current_state =
            cflow_statechart_instance_current_state(&instance);
    }
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    cflow_scxml_program_destroy(&program);
    return succeeded;
}

suite("SCXML Core to native CFlow Statechart compiler") {
    it("lowers supported structural elements and deterministic name maps") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='null' initial='idle'>\n"
            "  <state id='idle'>\n"
            "    <onentry/>\n"
            "    <transition event='go' target='work'/>\n"
            "    <onexit/>\n"
            "  </state>\n"
            "  <parallel id='work'>\n"
            "    <state id='left'>\n"
            "      <initial><transition target='left_ready'/></initial>\n"
            "      <state id='left_ready'/>\n"
            "    </state>\n"
            "    <state id='right' initial='right_ready'>\n"
            "      <state id='right_ready'/>\n"
            "      <final id='right_done'/>\n"
            "    </state>\n"
            "    <transition event='done.state.work' target='done'/>\n"
            "  </parallel>\n"
            "  <state id='memory'>\n"
            "    <history id='remember' type='deep'>\n"
            "      <transition target='memory_leaf'/>\n"
            "    </history>\n"
            "    <state id='memory_leaf'/>\n"
            "  </state>\n"
            "  <final id='done'/>\n"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_state *state;
        cflow_event_id go = 0u;
        cflow_event_view event = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_not_null(program.impl);
        statechart = cflow_scxml_program_statechart(&program);
        check_not_null(statechart);
        check_true(cmeta_type_equal(cflow_statechart_state_type(statechart),
                                    &cmeta_type_bool));

        state = find_state(&program, "idle");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_ATOMIC);
        state = find_state(&program, "work");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_PARALLEL);
        state = find_state(&program, "left");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_COMPOUND);
        state = find_state(&program, "done");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_FINAL);
        state = find_state(&program, "remember");
        check_not_null(state);
        check_equal(state->kind, CFLOW_STATECHART_HISTORY_DEEP);

        check_true(cflow_scxml_program_event_id(&program, "go", 2u, &go));
        check_not_equal(go, (cflow_event_id)0u);
        check_true(cflow_scxml_program_event(&program, "go", 2u, &event));
        check_equal(event.id, go);
        check_true(cmeta_type_equal(event.payload_type, &cmeta_type_bool));
        check_false(*(const bool *)event.payload);
        check_not_null(cflow_scxml_program_initial_state(&program));
        check_false(*(const bool *)cflow_scxml_program_initial_state(&program));

        cflow_scxml_program_destroy(&program);
        check_null(program.impl);
        cflow_scxml_program_destroy(&program);
    }

    it("admits omitted null datamodel and default initial child") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='first'/>\n"
            "  <final id='last'/>\n"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_not_null(find_state(&program, "first"));
        cflow_scxml_program_destroy(&program);
    }

    it("publishes an empty borrowed runtime binding view for structural programs") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='only'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        size_t binding_count = SIZE_MAX;

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_runtime_bindings(
            &program, &bindings, &binding_count));
        check_null(bindings);
        check_equal(binding_count, (size_t)0u);

        bindings =
            (const cflow_statechart_executable_binding *)(uintptr_t)1u;
        binding_count = SIZE_MAX;
        check_false(cflow_scxml_program_runtime_bindings(
            NULL, &bindings, &binding_count));
        check_true(bindings ==
                   (const cflow_statechart_executable_binding *)(uintptr_t)1u);
        check_equal(binding_count, SIZE_MAX);

        {
            const cflow_statechart_guard_binding *guard_bindings =
                (const cflow_statechart_guard_binding *)(uintptr_t)1u;
            size_t guard_count = SIZE_MAX;
            check_true(cflow_scxml_program_guard_bindings(
                &program, &guard_bindings, &guard_count));
            check_null(guard_bindings);
            check_equal(guard_count, (size_t)0u);

            guard_bindings =
                (const cflow_statechart_guard_binding *)(uintptr_t)1u;
            guard_count = SIZE_MAX;
            check_false(cflow_scxml_program_guard_bindings(
                NULL, &guard_bindings, &guard_count));
            check_true(
                guard_bindings ==
                (const cflow_statechart_guard_binding *)(uintptr_t)1u);
            check_equal(guard_count, SIZE_MAX);
        }

        cflow_scxml_program_destroy(&program);
    }

    it("lowers one null transition condition to a borrowed native guard") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'>"
            "<transition event='go' cond='  In ( active )  ' target='done'/>"
            "</state><final id='done'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart *statechart;
        const cflow_statechart_guard *guard;
        const cflow_statechart_transition *conditioned = NULL;
        const cflow_statechart_guard_binding *guard_bindings = NULL;
        size_t guard_count = 0u;
        size_t index;

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        statechart = cflow_scxml_program_statechart(&program);
        check_not_null(statechart);
        check_equal(cflow_statechart_guard_count(statechart), (size_t)1u);
        guard = cflow_statechart_guard_at(statechart, 0u);
        check_not_null(guard);
        for (index = 0u;
             index < cflow_statechart_transition_count(statechart); ++index) {
            const cflow_statechart_transition *candidate =
                cflow_statechart_transition_at(statechart, index);
            if (candidate != NULL && candidate->guard != 0u) {
                conditioned = candidate;
                break;
            }
        }
        check_not_null(conditioned);
        check_equal(conditioned->guard, guard->id);
        check_true(cflow_scxml_program_guard_bindings(
            &program, &guard_bindings, &guard_count));
        check_not_null(guard_bindings);
        check_equal(guard_count, (size_t)1u);
        check_equal(guard_bindings[0].id, guard->id);
        check_null(guard_bindings[0].fn);
        check_not_null(guard_bindings[0].contextual_fn);
        cflow_scxml_program_destroy(&program);
    }

    it("diagnoses invalid transition conditions at the cond attribute") {
        static const char malformed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond='ready' target='a'/></state>"
            "</scxml>";
        static const char quoted[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond=\"In('a')\" target='a'/>"
            "</state></scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><transition cond='In(missing)' target='a'/>"
            "</state></scxml>";
        static const char pseudo[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><history id='memory'>"
            "<transition target='leaf'/></history>"
            "<transition cond='In(memory)' target='leaf'/>"
            "<state id='leaf'/></state></scxml>";
        static const char initial_default[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<initial><transition cond='In(a)' target='a'/></initial>"
            "<state id='a'/></scxml>";
        static const char history_default[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><history id='memory'>"
            "<transition cond='In(a)' target='leaf'/></history>"
            "<state id='leaf'/></state></scxml>";
        const char *invalid[] = {
            malformed, quoted, unknown, pseudo, initial_default,
            history_default};
        const cflow_scxml_status expected[] = {
            CFLOW_SCXML_INVALID_STRUCTURE, CFLOW_SCXML_INVALID_STRUCTURE,
            CFLOW_SCXML_UNKNOWN_TARGET, CFLOW_SCXML_INVALID_STRUCTURE,
            CFLOW_SCXML_INVALID_STRUCTURE, CFLOW_SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_equal(
                diagnostic.location.byte_offset,
                (size_t)(strstr(invalid[index], "cond") - invalid[index]));
            check_null(program.impl);
        }
    }

    it("falls through a false child condition to a true ancestor condition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='parent'><state id='parent' initial='leaf'>"
            "<transition event='go' cond='In(parent)' target='done'/>"
            "<state id='leaf'><transition event='go' cond='In(other)' "
            "target='wrong'/></state></state><state id='other'/>"
            "<final id='done'/><final id='wrong'/></scxml>";
        size_t guard_count = 0u;
        cflow_machine_state_id done_id = 0u;
        cflow_machine_state_id current_state = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, "go", &guard_count, &done, &current_state));
        {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(source, &program, &diagnostic),
                        CFLOW_SCXML_OK);
            check_true(cflow_scxml_program_state_id(
                &program, "done", 4u, &done_id));
            cflow_scxml_program_destroy(&program);
        }
        check_equal(guard_count, (size_t)2u);
        check_true(done);
        check_equal(current_state, done_id);
    }

    it("selects the first document-ordered transition whose condition is true") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'>"
            "<transition event='go' cond='In(other)' target='wrong'/>"
            "<transition event='go' cond='In(start)' target='done'/>"
            "</state><state id='other'/><final id='wrong'/>"
            "<final id='done'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_machine_state_id done_id = 0u;
        cflow_machine_state_id current_state = 0u;
        size_t guard_count = 0u;
        bool done = false;

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_state_id(
            &program, "done", 4u, &done_id));
        cflow_scxml_program_destroy(&program);
        check_true(run_condition_program(
            source, "go", &guard_count, &done, &current_state));
        check_equal(guard_count, (size_t)2u);
        check_true(done);
        check_equal(current_state, done_id);
    }

    it("stabilizes a true eventless transition condition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'>"
            "<transition cond='In(start)' target='done'/></state>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, NULL, &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)1u);
        check_true(done);
    }

    it("selects a conditioned completion transition") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='parent'><state id='parent' initial='child_done'>"
            "<transition event='done.state.parent' cond='In(parent)' "
            "target='done'/><final id='child_done'/></state>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, NULL, &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)1u);
        check_true(done);
    }

    it("expands a parallel condition across multiple Event descriptors") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='both'><parallel id='both'>"
            "<transition event='go retry' cond='In(left)' target='done'/>"
            "<state id='left'/><state id='right'/></parallel>"
            "<final id='done'/></scxml>";
        size_t guard_count = 0u;
        bool done = false;

        check_true(run_condition_program(
            source, "retry", &guard_count, &done, NULL));
        check_equal(guard_count, (size_t)2u);
        check_true(done);
    }

    it("executes an onentry raise block through native runtime bindings") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'>"
            "<onentry><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/>"
            "</state>"
            "<final id='done'/>"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_runtime_bindings(
            &program, &bindings, &binding_count));
        check_not_null(bindings);
        check_equal(binding_count, (size_t)1u);
        check_equal(cflow_statechart_executable_count(
                        cflow_scxml_program_statechart(&program)),
                    (size_t)1u);
        check_equal(cflow_statechart_state_action_count(
                        cflow_scxml_program_statechart(&program)),
                    (size_t)1u);

        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = cflow_scxml_program_statechart(&program),
            .initial_state = cflow_scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 2u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        check_true(stats.done);
        check_equal(stats.actions, (uint64_t)1u);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
    }

    it("emits a label-only log and continues executable content") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'><onentry>"
            "<log label='entered'/><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, true, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)1u);
        check_equal(capture.levels[0], TURBO_LOG_LEVEL_DEBUG);
        check_equal(capture.components[0], "cflow.scxml");
        check_equal(capture.messages[0], "entered");
    }

    it("emits only the selected conditional log steps in document order") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<log label='before'/><if cond='In(active)'>"
            "<log label='chosen'/><else/><log label='wrong'/></if>"
            "<raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, true, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)2u);
        check_equal(capture.messages[0], "before");
        check_equal(capture.messages[1], "chosen");
    }

    it("treats a missing default logger as a successful log no-op") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='start'><state id='start'><onentry>"
            "<log/><raise event='advance'/></onentry>"
            "<transition event='advance' target='done'/></state>"
            "<final id='done'/></scxml>";
        scxml_log_capture capture = {0};
        bool done = false;
        bool errored = false;

        check_true(run_log_program(
            source, false, &capture, &done, &errored));
        check_true(done);
        check_false(errored);
        check_equal(capture.count, (size_t)0u);
    }

    it("marks only log-containing executable blocks with the IO effect") {
        static const char log_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log label='effect'/></onentry>"
            "</state></scxml>";
        static const char raise_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><raise event='next'/></onentry>"
            "</state></scxml>";
        const char *sources[] = {log_source, raise_source};
        const bool expected_io[] = {true, false};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            const cflow_statechart *statechart;
            const cflow_statechart_executable *executable;
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        CFLOW_SCXML_OK);
            statechart = cflow_scxml_program_statechart(&program);
            check_equal(cflow_statechart_executable_count(statechart),
                        (size_t)1u);
            executable = cflow_statechart_executable_at(statechart, 0u);
            check_not_null(executable);
            check_true((executable->effects & CMETA_EFFECT_STATEFUL) != 0u);
            check_true((executable->effects & CMETA_EFFECT_MAY_FAIL) != 0u);
            check_equal((executable->effects & CMETA_EFFECT_IO) != 0u,
                        expected_io[index]);
            cflow_scxml_program_destroy(&program);
        }
    }

    it("rejects unsupported log expressions at the owning attribute") {
        static const char expression_only[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log label='value' expr='1'/>"
            "</onentry></state></scxml>";
        static const char expression_first[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log expr='1' bad='x'/>"
            "</onentry></state></scxml>";
        static const char invalid_first[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log bad='x' expr='1'/>"
            "</onentry></state></scxml>";
        const char *sources[] = {
            expression_only, expression_first, invalid_first};
        const char *owners[] = {"expr=", "expr=", "bad="};
        const cflow_scxml_status expected[] = {
            CFLOW_SCXML_UNSUPPORTED_FEATURE,
            CFLOW_SCXML_UNSUPPORTED_FEATURE,
            CFLOW_SCXML_INVALID_STRUCTURE};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        expected[index]);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("rejects unknown log attributes and non-comment children") {
        static const char unknown_attribute[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log target='sink'/>"
            "</onentry></state></scxml>";
        static const char child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='active'><onentry><log><raise event='bad'/></log>"
            "</onentry></state></scxml>";
        const char *sources[] = {unknown_attribute, child};
        const char *owners[] = {"target=", "<raise"};
        size_t index;

        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        CFLOW_SCXML_INVALID_STRUCTURE);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("shares max_name_bytes across state names and retained log labels") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='s'><state id='s'><onentry><log label='x'/>"
            "</onentry></state></scxml>";
        cflow_scxml_limits limits = cflow_scxml_default_limits();
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        limits.max_name_bytes = 2u;
        check_equal(cflow_scxml_compile(&program, source, strlen(source),
                                        &limits, &diagnostic),
                    CFLOW_SCXML_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic.message, "max_name_bytes"));
        check_null(program.impl);
    }

    it("preserves exit transition entry and in-block raise order") {
        static const char source_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/raise_trace.scxml";
        static const char expected_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/raise_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(cflow_scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_runtime_bindings(
            &program, &bindings, &binding_count));
        check_equal(binding_count, (size_t)3u);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = cflow_scxml_program_statechart(&program),
            .initial_state = cflow_scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 4u,
            .completion_capacity = 2u,
            .microstep_limit = 32u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size = (size_t)snprintf(
            actual, sizeof(actual), "done %s\nactions %llu\n",
            stats.done ? "true" : "false",
            (unsigned long long)stats.actions);
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
        free(expected);
        free(source);
    }

    it("shares one transition block across every event descriptor") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'><transition event='go retry' target='next'>"
            "<raise event='hit'/></transition></state>"
            "<state id='next'><transition event='hit' target='done'/></state>"
            "<final id='done'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_equal(cflow_statechart_executable_count(
                        cflow_scxml_program_statechart(&program)),
                    (size_t)1u);
        check_equal(cflow_statechart_transition_action_count(
                        cflow_scxml_program_statechart(&program)),
                    (size_t)2u);
        cflow_scxml_program_destroy(&program);
    }

    it("executes raise blocks on initial and history default transitions") {
        static const char initial_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<initial><transition target='ready'>"
            "<raise event='initialized'/></transition></initial>"
            "<state id='ready'><transition event='initialized' "
            "target='done'/></state><final id='done'/></scxml>";
        static const char history_source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='parent'>"
            "<history id='memory'><transition target='ready'>"
            "<raise event='restored'/></transition></history>"
            "<state id='ready'>"
            "<transition event='restore' target='memory'/>"
            "<transition event='restored' target='done'/>"
            "</state><final id='done'/></state></scxml>";
        const char *sources[] = {initial_source, history_source};
        const char *trigger_names[] = {NULL, "restore"};
        size_t index;
        for (index = 0u; index < 2u; ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            const cflow_statechart_executable_binding *bindings = NULL;
            size_t binding_count = 0u;
            cflow_executor executor = {0};
            cflow_statechart_instance instance = {0};
            cflow_statechart_instance_config config = {0};
            cflow_statechart_instance_stats stats = {0};
            cflow_event_view trigger = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        CFLOW_SCXML_OK);
            check_true(cflow_scxml_program_runtime_bindings(
                &program, &bindings, &binding_count));
            check_equal(binding_count, (size_t)1u);
            check_true(cflow_executor_serial_init(&executor));
            config = (cflow_statechart_instance_config){
                .statechart = cflow_scxml_program_statechart(&program),
                .initial_state = cflow_scxml_program_initial_state(&program),
                .executables = bindings,
                .executable_count = binding_count,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .executor = &executor};
            check_equal(cflow_statechart_instance_init(&instance, &config),
                        CFLOW_STATECHART_RUNTIME_OK);
            if (trigger_names[index] != NULL) {
                check_true(cflow_scxml_program_event(
                    &program, trigger_names[index],
                    strlen(trigger_names[index]), &trigger));
                check_equal(cflow_statechart_instance_try_send(
                                &instance, &trigger),
                            CFLOW_MAILBOX_OK);
                check_true(cflow_executor_wait_idle(&executor));
            }
            check_true(cflow_statechart_instance_get_stats(&instance, &stats));
            check_true(stats.done);
            check_equal(cflow_statechart_instance_destroy(&instance),
                        CFLOW_STATECHART_RUNTIME_OK);
            cflow_executor_destroy(&executor);
            cflow_scxml_program_destroy(&program);
        }
    }

    it("rolls back a selected conditional branch when its Event queue is "
       "full") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='start'><transition event='go' target='next'>"
            "<if cond='In(start)'><raise event='wrong'/><else/>"
            "<raise event='first'/><raise event='second'/></if>"
            "</transition></state><state id='next'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        const cflow_statechart_state *start;
        cflow_machine_state_id states[2] = {0};
        size_t state_count = 0u;
        uint64_t version = 0u;

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_runtime_bindings(
            &program, &bindings, &binding_count));
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = cflow_scxml_program_statechart(&program),
            .initial_state = cflow_scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 1u,
            .completion_capacity = 2u,
            .microstep_limit = 16u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        start = find_state(&program, "start");
        check_not_null(start);
        check_true(cflow_scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        check_true(stats.errored);
        check_equal(stats.last_status,
                    CFLOW_STATECHART_RUNTIME_INTERNAL_QUEUE_FULL);
        check_equal(stats.internal_pending, (size_t)0u);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(version, UINT64_C(1));
        check_equal(states[state_count - 1u], start->id);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
    }

    it("executes the first matching conditional partition and nested blocks") {
        static const char first_true[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><raise event='hit'/>"
            "<elseif cond='In(active)'/><raise event='wrong'/></if>"
            "</onentry><transition event='hit' target='done'/>"
            "</state><state id='other'/><final id='done'/></scxml>";
        static const char else_match[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(other)'><raise event='wrong'/>"
            "<else/><raise event='hit'/></if></onentry>"
            "<transition event='hit' target='done'/></state>"
            "<state id='other'/><final id='done'/></scxml>";
        static const char no_match[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(other)'><raise event='wrong'/></if>"
            "</onentry></state><state id='other'/></scxml>";
        static const char nested[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><if cond='In(other)'>"
            "<raise event='wrong'/><else/><raise event='hit'/>"
            "</if></if></onentry><transition event='hit' target='done'/>"
            "</state><state id='other'/><final id='done'/></scxml>";
        static const char first_true_empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='active'><state id='active'><onentry>"
            "<if cond='In(active)'><elseif cond='In(other)'/>"
            "<raise event='wrong'/><else/><raise event='wrong'/></if>"
            "</onentry></state><state id='other'/></scxml>";
        const char *sources[] = {first_true, else_match, no_match, nested,
                                 first_true_empty};
        const bool expected_done[] = {true, true, false, true, false};
        size_t index;
        for (index = 0u; index < sizeof(sources) / sizeof(sources[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            const cflow_statechart_executable_binding *bindings = NULL;
            size_t binding_count = 0u;
            cflow_executor executor = {0};
            cflow_statechart_instance instance = {0};
            cflow_statechart_instance_config config = {0};
            cflow_statechart_instance_stats stats = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        CFLOW_SCXML_OK);
            check_true(cflow_scxml_program_runtime_bindings(&program, &bindings,
                                                            &binding_count));
            check_equal(binding_count, (size_t)1u);
            check_true(cflow_executor_serial_init(&executor));
            config = (cflow_statechart_instance_config){
                .statechart = cflow_scxml_program_statechart(&program),
                .initial_state = cflow_scxml_program_initial_state(&program),
                .executables = bindings,
                .executable_count = binding_count,
                .external_event_capacity = 2u,
                .internal_event_capacity = 2u,
                .completion_capacity = 2u,
                .microstep_limit = 16u,
                .executor = &executor};
            check_equal(cflow_statechart_instance_init(&instance, &config),
                        CFLOW_STATECHART_RUNTIME_OK);
            check_true(cflow_statechart_instance_get_stats(&instance, &stats));
            check_equal(stats.done, expected_done[index]);
            check_equal(stats.actions, UINT64_C(1));
            check_equal(cflow_statechart_instance_destroy(&instance),
                        CFLOW_STATECHART_RUNTIME_OK);
            cflow_executor_destroy(&executor);
            cflow_scxml_program_destroy(&program);
        }
    }

    it("observes action-time configuration in every executable phase") {
        static const char source_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/conditional_trace.scxml";
        static const char expected_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/conditional_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        const cflow_statechart_executable_binding *bindings = NULL;
        size_t binding_count = 0u;
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(cflow_scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_scxml_program_runtime_bindings(&program, &bindings,
                                                        &binding_count));
        check_equal(binding_count, (size_t)6u);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = cflow_scxml_program_statechart(&program),
            .initial_state = cflow_scxml_program_initial_state(&program),
            .executables = bindings,
            .executable_count = binding_count,
            .external_event_capacity = 2u,
            .internal_event_capacity = 8u,
            .completion_capacity = 4u,
            .microstep_limit = 32u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size = (size_t)snprintf(
            actual, sizeof(actual), "done %s\nactions %llu\n",
            stats.done ? "true" : "false", (unsigned long long)stats.actions);
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);
        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
        free(expected);
        free(source);
    }

    it("diagnoses null-model conditions and conditional marker structure") {
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if/></onentry></state></scxml>";
        static const char quoted[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond=\"In('a')\"/>"
            "</onentry></state></scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(missing)'/>"
            "</onentry></state></scxml>";
        static const char pseudo[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><history id='memory'>"
            "<transition target='leaf'/></history>"
            "<onentry><if cond='In(memory)'/></onentry>"
            "<state id='leaf'/></state></scxml>";
        static const char after_else[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(a)'><else/>"
            "<elseif cond='In(a)'/></if></onentry></state></scxml>";
        static const char marker_outside[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><else/></onentry></state></scxml>";
        static const char marker_child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='In(a)'>"
            "<else><raise event='bad'/></else></if>"
            "</onentry></state></scxml>";
        static const char whitespace[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><if cond='  In ( a )  '/>"
            "</onentry></state></scxml>";
        const char *invalid[] = {missing,     quoted,     unknown,
                                 pseudo,      after_else, marker_outside,
                                 marker_child};
        const cflow_scxml_status expected[] = {
            CFLOW_SCXML_INVALID_STRUCTURE, CFLOW_SCXML_INVALID_STRUCTURE,
            CFLOW_SCXML_UNKNOWN_TARGET,    CFLOW_SCXML_INVALID_STRUCTURE,
            CFLOW_SCXML_INVALID_STRUCTURE, CFLOW_SCXML_INVALID_STRUCTURE,
            CFLOW_SCXML_INVALID_STRUCTURE};
        size_t index;
        for (index = 0u; index < sizeof(invalid) / sizeof(invalid[0]);
             ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(invalid[index], &program, &diagnostic),
                        expected[index]);
            check_null(program.impl);
        }
        {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(whitespace, &program, &diagnostic),
                        CFLOW_SCXML_OK);
            cflow_scxml_program_destroy(&program);
        }
    }

    it("diagnoses invalid raise syntax at the owning token") {
        static const char missing[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise/></onentry></state></scxml>";
        static const char empty[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event=''/></onentry></state></scxml>";
        static const char whitespace[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='two words'/>"
            "</onentry></state></scxml>";
        static const char child[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='x'><raise event='y'/>"
            "</raise></onentry></state></scxml>";
        static const char attribute[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='a'><onentry><raise event='x' extra='bad'/>"
            "</onentry></state></scxml>";
        const char *sources[] = {missing, empty, whitespace, child, attribute};
        const char *owners[] = {"<raise/>", "event=", "event=", "<raise event='y'", "extra="};
        size_t index;
        for (index = 0u; index < 5u; ++index) {
            cflow_scxml_program program = {0};
            cflow_scxml_diagnostic diagnostic = {0};
            check_equal(compile_status(sources[index], &program, &diagnostic),
                        index == 4u ? CFLOW_SCXML_UNSUPPORTED_FEATURE
                                    : CFLOW_SCXML_INVALID_STRUCTURE);
            check_equal(diagnostic.location.byte_offset,
                        (size_t)(strstr(sources[index], owners[index]) -
                                 sources[index]));
            check_null(program.impl);
        }
    }

    it("reports namespace version and data-model admission precisely") {
        static const char wrong_namespace[] =
            "<scxml xmlns='urn:not-scxml' version='1.0'><state id='x'/></scxml>";
        static const char wrong_version[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='2.0'>"
            "<state id='x'/></scxml>";
        static const char wrong_model[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "datamodel='ecmascript'><state id='x'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(wrong_namespace, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_NAMESPACE);
        check_equal(diagnostic.location.byte_offset, (size_t)0u);
        check_null(program.impl);

        check_equal(compile_status(wrong_version, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_VERSION);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(wrong_version, "version") - wrong_version));

        check_equal(compile_status(wrong_model, &program, &diagnostic),
                    CFLOW_SCXML_UNSUPPORTED_DATAMODEL);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(wrong_model, "datamodel") - wrong_model));
    }

    it("rejects duplicate IDs and unknown targets at the owning attribute") {
        static const char duplicate[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='same'/>\n"
            "  <state id='same'/>\n"
            "</scxml>";
        static const char unknown[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><transition target='missing'/></state>\n"
            "</scxml>";
        static const char phase_order[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><transition target='missing'/></state>\n"
            "  <state id='a'/>\n"
            "</scxml>";
        static const char duplicate_document_order[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='z'/>\n"
            "  <state id='a'/>\n"
            "  <state id='z'/>\n"
            "  <state id='z'/>\n"
            "  <state id='a'/>\n"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(duplicate, &program, &diagnostic),
                    CFLOW_SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)3u);
        check_null(program.impl);

        check_equal(compile_status(unknown, &program, &diagnostic),
                    CFLOW_SCXML_UNKNOWN_TARGET);
        check_equal(diagnostic.location.line, (uint32_t)2u);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(unknown, "target") - unknown));

        check_equal(compile_status(phase_order, &program, &diagnostic),
                    CFLOW_SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)3u);

        check_equal(compile_status(duplicate_document_order, &program,
                                   &diagnostic),
                    CFLOW_SCXML_DUPLICATE_ID);
        check_equal(diagnostic.location.line, (uint32_t)4u);
    }

    it("requires state IDs to be XML NCNames") {
        static const char leading_digit[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='1bad'/></scxml>";
        static const char embedded_space[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='bad id'/></scxml>";
        static const char colon[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='bad:id'/></scxml>";
        static const char unicode_name[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='\xe7\x8a\xb6\xe6\x80\x81'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(leading_digit, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_STRUCTURE);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(leading_digit, "id=") - leading_digit));
        check_equal(compile_status(embedded_space, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_STRUCTURE);
        check_equal(compile_status(colon, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_STRUCTURE);
        check_equal(compile_status(unicode_name, &program, &diagnostic),
                    CFLOW_SCXML_OK);
        check_not_null(program.impl);
        cflow_scxml_program_destroy(&program);
    }

    it("fails fast for unsupported behavior instead of discarding it") {
        static const char executable[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><onentry><log expr='x'/></onentry></state>\n"
            "</scxml>";
        static const char multiple_targets[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='a'><transition target='a b'/></state>\n"
            "  <state id='b'/>\n"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(executable, &program, &diagnostic),
                    CFLOW_SCXML_UNSUPPORTED_FEATURE);
        check_equal(diagnostic.location.line, (uint32_t)2u);

        check_equal(compile_status(multiple_targets, &program, &diagnostic),
                    CFLOW_SCXML_UNSUPPORTED_FEATURE);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(multiple_targets, "target") -
                             multiple_targets));
    }

    it("rejects an unknown history type at its attribute") {
        static const char source[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>\n"
            "  <state id='parent' initial='leaf'>\n"
            "    <history id='memory' type='branch'>"
            "<transition target='leaf'/></history>\n"
            "    <state id='leaf'/>\n"
            "  </state>\n"
            "</scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};

        check_equal(compile_status(source, &program, &diagnostic),
                    CFLOW_SCXML_INVALID_STRUCTURE);
        check_equal(diagnostic.location.byte_offset,
                    (size_t)(strstr(source, "type") - source));
        check_null(program.impl);
    }

    it("leaves output empty when XML syntax or configured limits fail") {
        static const char malformed[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='x'></scxml>";
        static const char valid[] =
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0'>"
            "<state id='xy'/></scxml>";
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_scxml_limits limits = cflow_scxml_default_limits();

        check_equal(compile_status(malformed, &program, &diagnostic),
                    CFLOW_SCXML_XML_ERROR);
        check_null(program.impl);

        limits.max_states = 1u;
        check_equal(cflow_scxml_compile(&program, valid, strlen(valid), &limits,
                                        &diagnostic),
                    CFLOW_SCXML_LIMIT_EXCEEDED);
        check_null(program.impl);

        limits = cflow_scxml_default_limits();
        limits.max_name_bytes = 1u;
        check_equal(cflow_scxml_compile(&program, valid, strlen(valid), &limits,
                                        &diagnostic),
                    CFLOW_SCXML_LIMIT_EXCEEDED);
        check_not_null(strstr(diagnostic.message, "max_name_bytes"));
        check_null(program.impl);
    }

    it("executes an independent SCXML fixture as the expected native trace") {
        static const char source_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/core_trace.scxml";
        static const char expected_path[] =
            CFLOW_SCXML_FIXTURE_DIR "/core_trace.expected";
        char *source;
        char *expected;
        size_t source_size = 0u;
        size_t expected_size = 0u;
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_executor executor = {0};
        cflow_statechart_instance instance = {0};
        cflow_statechart_instance_config config = {0};
        cflow_statechart_instance_stats stats = {0};
        cflow_event_view go = {0};
        cflow_machine_state_id states[2] = {0u, 0u};
        size_t state_count = 0u;
        uint64_t version = 0u;
        char actual[64];
        size_t actual_size;

        source = tt_read_file(source_path, &source_size);
        expected = tt_read_file(expected_path, &expected_size);
        check_not_null(source);
        check_not_null(expected);
        check_equal(cflow_scxml_compile(&program, source, source_size, NULL,
                                        &diagnostic),
                    CFLOW_SCXML_OK);
        check_true(cflow_executor_serial_init(&executor));
        config = (cflow_statechart_instance_config){
            .statechart = cflow_scxml_program_statechart(&program),
            .initial_state = cflow_scxml_program_initial_state(&program),
            .external_event_capacity = 4u,
            .internal_event_capacity = 4u,
            .completion_capacity = 4u,
            .microstep_limit = 64u,
            .executor = &executor};
        check_equal(cflow_statechart_instance_init(&instance, &config),
                    CFLOW_STATECHART_RUNTIME_OK);
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(state_count, (size_t)2u);
        actual_size = (size_t)snprintf(actual, sizeof(actual),
                                       "v%llu %u %u\n",
                                       (unsigned long long)version,
                                       (unsigned int)states[0],
                                       (unsigned int)states[1]);

        check_true(cflow_scxml_program_event(&program, "go", 2u, &go));
        check_equal(cflow_statechart_instance_try_send(&instance, &go),
                    CFLOW_MAILBOX_OK);
        check_true(cflow_executor_wait_idle(&executor));
        check_equal(cflow_statechart_instance_copy_configuration(
                        &instance, states, 2u, &state_count, &version),
                    CFLOW_STATECHART_SNAPSHOT_OK);
        check_equal(state_count, (size_t)2u);
        actual_size += (size_t)snprintf(
            actual + actual_size, sizeof(actual) - actual_size,
            "v%llu %u %u\n",
            (unsigned long long)version,
            (unsigned int)states[0], (unsigned int)states[1]);
        check_true(cflow_statechart_instance_get_stats(&instance, &stats));
        actual_size += (size_t)snprintf(
            actual + actual_size, sizeof(actual) - actual_size,
            "done %s\n", stats.done ? "true" : "false");
        check_equal(actual_size, expected_size);
        check_equal(actual, expected);

        check_equal(cflow_statechart_instance_destroy(&instance),
                    CFLOW_STATECHART_RUNTIME_OK);
        cflow_executor_destroy(&executor);
        cflow_scxml_program_destroy(&program);
        free(expected);
        free(source);
    }
}
