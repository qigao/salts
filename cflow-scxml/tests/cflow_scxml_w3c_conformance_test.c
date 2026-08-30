#include <cflow/executor.h>
#include <cflow/scxml.h>
#include <cflow/statechart_runtime.h>

#include "tinytest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W3C_FIXTURE_PATH_CAPACITY 512u

enum {
    W3C_EXTERNAL_EVENT_CAPACITY = 2,
    W3C_INTERNAL_EVENT_CAPACITY = 8,
    /* Test 417 completes two regions, their parallel, its parent, and root. */
    W3C_COMPLETION_CAPACITY = 5,
    W3C_MICROSTEP_LIMIT = 32
};

typedef struct w3c_run_result {
    cflow_machine_state_id current_state;
    cflow_machine_state_id pass_state;
    cflow_machine_state_id fail_state;
    bool done;
    bool errored;
} w3c_run_result;

static bool run_w3c_fixture(const char *fixture_name,
                            w3c_run_result *out_result) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
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
    bool executor_initialized = false;
    bool instance_initialized = false;
    bool succeeded = false;
    int path_size;
    cflow_scxml_status compile_status;
    cflow_statechart_runtime_status runtime_status;

    if (fixture_name == NULL || out_result == NULL) {
        return false;
    }
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         CFLOW_SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    memset(out_result, 0, sizeof(*out_result));
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    compile_status =
        cflow_scxml_compile(&program, source, source_size, NULL, &diagnostic);
    if (compile_status != CFLOW_SCXML_OK) {
        info("fixture=%s compile_status=%d diagnostic=%s", fixture_name,
             (int)compile_status, diagnostic.message);
        goto cleanup;
    }
    if (!cflow_scxml_program_state_id(&program, "pass", 4u,
                                      &out_result->pass_state) ||
        !cflow_scxml_program_state_id(&program, "fail", 4u,
                                      &out_result->fail_state)) {
        info("fixture=%s result states missing", fixture_name);
        goto cleanup;
    }
    if (!cflow_scxml_program_runtime_bindings(
            &program, &executables, &executable_count) ||
        !cflow_scxml_program_guard_bindings(&program, &guards, &guard_count)) {
        info("fixture=%s runtime bindings missing", fixture_name);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
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
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .executor = &executor};
    runtime_status = cflow_statechart_instance_init(&instance, &config);
    if (runtime_status != CFLOW_STATECHART_RUNTIME_OK) {
        info("fixture=%s runtime_status=%d", fixture_name,
             (int)runtime_status);
        goto cleanup;
    }
    instance_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_statechart_instance_get_stats(&instance, &stats)) {
        info("fixture=%s executor wait or stats failed", fixture_name);
        goto cleanup;
    }
    out_result->current_state =
        cflow_statechart_instance_current_state(&instance);
    out_result->done = stats.done;
    out_result->errored = stats.errored;
    succeeded = true;

cleanup:
    if (instance_initialized)
        (void)cflow_statechart_instance_destroy(&instance);
    if (executor_initialized) cflow_executor_destroy(&executor);
    cflow_scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static void check_w3c_fixture(const char *fixture_name) {
    w3c_run_result result = {0};

    check_true(run_w3c_fixture(fixture_name, &result));
    check_true(result.done);
    check_false(result.errored);
    check_equal(result.current_state, result.pass_state);
    check_not_equal(result.current_state, result.fail_state);
}

suite("SCXML W3C-derived conformance regression corpus") {
    it("test 144 preserves raised internal event order") {
        check_w3c_fixture("test144.scxml");
    }

    it("test 355 selects the first root child when initial is omitted") {
        check_w3c_fixture("test355.scxml");
    }

    it("test 375 executes onentry handlers in document order") {
        check_w3c_fixture("test375.scxml");
    }

    it("test 377 executes onexit handlers in document order") {
        check_w3c_fixture("test377.scxml");
    }

    it("test 403a applies source priority, document order, and guards") {
        check_w3c_fixture("test403a.scxml");
    }

    it("test 404 executes exits in exit order before transition content") {
        check_w3c_fixture("test404.scxml");
    }

    it("test 405 executes selected transition content in document order") {
        check_w3c_fixture("test405.scxml");
    }

    it("test 406 executes transition content before entry-order actions") {
        check_w3c_fixture("test406.scxml");
    }

    it("test 409 removes exited descendants before ancestor onexit") {
        check_w3c_fixture("test409.scxml");
    }

    it("test 411 adds a state before its own onentry") {
        check_w3c_fixture("test411.scxml");
    }

    it("test 412 orders parent entry, initial transition, then child entry") {
        check_w3c_fixture("test412.scxml");
    }

    it("test 416 raises compound state completion") {
        check_w3c_fixture("test416.scxml");
    }

    it("test 417 raises parallel state completion") {
        check_w3c_fixture("test417.scxml");
    }

    it("test 419 selects eventless transitions before queued events") {
        check_w3c_fixture("test419.scxml");
    }

    it("test 503 gives targetless transitions an empty exit set") {
        check_w3c_fixture("test503.scxml");
    }

    it("test 505 retains a compound source for an internal descendant target") {
        check_w3c_fixture("test505.scxml");
    }

    it("test 506 treats a non-descendant internal target as external") {
        check_w3c_fixture("test506.scxml");
    }

    it("test 533 treats an internal transition from parallel as external") {
        check_w3c_fixture("test533.scxml");
    }
}
