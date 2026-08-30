#include <cflow/executor.h>
#include <cflow/scxml.h>
#include <cflow/statechart_instance.h>
#include <turbo_cmeta_data.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W3C_FIXTURE_PATH_CAPACITY 512u
#define W3C_MANIFEST_ROW_CAPACITY 256u
#define W3C_MANIFEST_BYTE_CAPACITY 131072u
#define W3C_MANIFEST_COLUMN_COUNT 9u
#define W3C_UPSTREAM_PREFIX "https://www.w3.org/Voice/2013/scxml-irp/"

enum {
    W3C_EXTERNAL_EVENT_CAPACITY = 2,
    W3C_INTERNAL_EVENT_CAPACITY = 8,
    /* Test 417 completes two regions, their parallel, its parent, and root. */
    W3C_COMPLETION_CAPACITY = 5,
    W3C_MICROSTEP_LIMIT = 32,
    W3C_UPSTREAM_TEST_DOCUMENT_COUNT = 202,
    W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT = 168,
    W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT = 34
};

typedef enum w3c_manifest_column {
    W3C_MANIFEST_ID = 0,
    W3C_MANIFEST_FIXTURE,
    W3C_MANIFEST_APPLICABILITY,
    W3C_MANIFEST_STATUS,
    W3C_MANIFEST_FEATURE,
    W3C_MANIFEST_UPSTREAM,
    W3C_MANIFEST_EXPECTED,
    W3C_MANIFEST_TRANSFORMATION,
    W3C_MANIFEST_RATIONALE
} w3c_manifest_column;

typedef struct w3c_run_result {
    cflow_machine_state_id current_state;
    cflow_machine_state_id pass_state;
    cflow_machine_state_id fail_state;
    bool done;
    bool errored;
} w3c_run_result;

typedef struct w3c_adapter_probe {
    size_t prepare_send_calls;
} w3c_adapter_probe;

typedef struct w3c_content_probe {
    size_t prepare_send_calls;
    size_t commits;
    size_t discards;
    cflow_scxml_content_kind kind;
    char bytes[32];
} w3c_content_probe;

Struct(w3c_cmeta_state,
    (tstr, invoke_id)
);

static bool w3c_cmeta_state_copy(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    memcpy(destination, source, sizeof(w3c_cmeta_state));
    return true;
}

static void w3c_cmeta_state_move(void *destination, void *source) {
    if (destination == NULL || source == NULL) return;
    memcpy(destination, source, sizeof(w3c_cmeta_state));
    memset(source, 0, sizeof(w3c_cmeta_state));
}

static void w3c_cmeta_state_destroy(void *value) {
    (void)value;
}

static const cmeta_type_identity w3c_cmeta_state_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.w3c.state");
static const cmeta_type_traits w3c_cmeta_state_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = w3c_cmeta_state_copy,
    .move_construct = w3c_cmeta_state_move,
    .destroy = w3c_cmeta_state_destroy};
static const cmeta_type_desc w3c_cmeta_state_type = {
    .name = "w3c_cmeta_state",
    .size = sizeof(w3c_cmeta_state),
    .align = _Alignof(w3c_cmeta_state),
    .kind = CMETA_T_OBJECT,
    .traits = &w3c_cmeta_state_traits,
    .identity = &w3c_cmeta_state_identity};
static const cmeta_data_buffer_shape w3c_owned_string_shape = {
    .ownership = CMETA_DATA_BUFFER_OWNED};
static const cmeta_data_desc w3c_owned_string_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.owned-string",
    .display_name = "W3C owned string",
    .kind = CMETA_DATA_STRING,
    .storage_type = &turbo_tstr_cmeta_type,
    .shape = &w3c_owned_string_shape,
    .buffer_ops = &turbo_tstr_cmeta_buffer_ops};
static const cmeta_data_field_desc w3c_cmeta_state_fields[] = {
    {"test.scxml.w3c.state.invoke-id", "invoke_id",
     offsetof(w3c_cmeta_state, invoke_id), &w3c_owned_string_desc}};
static const cmeta_data_struct_shape w3c_cmeta_state_shape = {
    .layout = StructMeta(w3c_cmeta_state),
    .fields = w3c_cmeta_state_fields,
    .field_count = sizeof(w3c_cmeta_state_fields) /
                   sizeof(w3c_cmeta_state_fields[0])};
static const cmeta_data_desc w3c_cmeta_state_desc = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.w3c.state.schema",
    .display_name = "W3C CMeta state",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &w3c_cmeta_state_type,
    .shape = &w3c_cmeta_state_shape};

typedef struct w3c_invoke_probe {
    size_t starts;
    size_t commits;
    size_t discards;
    uint64_t token;
    char id[CFLOW_SCXML_EVENT_METADATA_CAPACITY + 1u];
} w3c_invoke_probe;

typedef struct w3c_manifest_row {
    char *columns[W3C_MANIFEST_COLUMN_COUNT];
} w3c_manifest_row;

typedef struct w3c_manifest_stats {
    size_t rows;
    size_t mandatory;
    size_t optional;
    size_t passed;
    size_t unsupported;
    size_t not_applicable;
} w3c_manifest_stats;

static bool split_manifest_row(char *line, w3c_manifest_row *out_row) {
    size_t column = 0u;
    char *cursor;
    if (line == NULL || out_row == NULL || line[0] == '\0') return false;
    memset(out_row, 0, sizeof(*out_row));
    out_row->columns[column++] = line;
    for (cursor = line; *cursor != '\0'; ++cursor) {
        if (*cursor != '\t') continue;
        if (column >= W3C_MANIFEST_COLUMN_COUNT) return false;
        *cursor = '\0';
        out_row->columns[column++] = cursor + 1;
    }
    if (column != W3C_MANIFEST_COLUMN_COUNT) return false;
    for (column = 0u; column < W3C_MANIFEST_COLUMN_COUNT; ++column) {
        if (out_row->columns[column][0] == '\0') return false;
    }
    return true;
}

static bool manifest_value_is(const char *value, const char *expected) {
    return value != NULL && expected != NULL && strcmp(value, expected) == 0;
}

static bool manifest_upstream_is_documented(const char *id,
                                            const char *upstream) {
    char expected[W3C_FIXTURE_PATH_CAPACITY];
    size_t numeric_length = 0u;
    int written;
    if (id == NULL || upstream == NULL) return false;
    while (id[numeric_length] >= '0' && id[numeric_length] <= '9') {
        ++numeric_length;
    }
    if (numeric_length == 0u) return false;
    written = snprintf(expected, sizeof(expected), "%s%.*s/test%s.",
                       W3C_UPSTREAM_PREFIX, (int)numeric_length, id, id);
    if (written < 0 || (size_t)written >= sizeof(expected) ||
        strncmp(upstream, expected, (size_t)written) != 0)
        return false;
    upstream += (size_t)written;
    return strcmp(upstream, "txml") == 0 || strcmp(upstream, "txt") == 0;
}

static bool validate_w3c_manifest_source(char *source,
                                         const char *fixture_directory,
                                         bool verify_fixtures,
                                         const char *documentation,
                                         w3c_manifest_stats *out_stats) {
    static const char header[] =
        "id\tfixture\tapplicability\tstatus\tfeature\tupstream\texpected\t"
        "transformation\trationale";
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char expected_fixture[64];
    char *cursor;
    size_t row_count = 0u;
    w3c_manifest_row rows[W3C_MANIFEST_ROW_CAPACITY];
    w3c_manifest_stats stats = {0};
    int written;
    if (source == NULL || fixture_directory == NULL || out_stats == NULL)
        return false;
    memset(out_stats, 0, sizeof(*out_stats));
    cursor = source;
    {
        char *newline = strchr(cursor, '\n');
        if (newline == NULL) goto cleanup;
        *newline = '\0';
        if (newline != cursor && newline[-1] == '\r') newline[-1] = '\0';
        if (strcmp(cursor, header) != 0) goto cleanup;
        cursor = newline + 1;
    }
    while (*cursor != '\0') {
        char *newline = strchr(cursor, '\n');
        char *line_end = newline != NULL ? newline : cursor + strlen(cursor);
        w3c_manifest_row *row;
        size_t previous;
        char *fixture_source = NULL;
        size_t fixture_size = 0u;
        if (line_end != cursor && line_end[-1] == '\r') line_end[-1] = '\0';
        if (newline != NULL) *newline = '\0';
        if (cursor[0] == '\0' || row_count == W3C_MANIFEST_ROW_CAPACITY)
            goto cleanup;
        row = &rows[row_count];
        if (!split_manifest_row(cursor, row)) goto cleanup;
        if ((!manifest_value_is(
                  row->columns[W3C_MANIFEST_APPLICABILITY], "MANDATORY") &&
             !manifest_value_is(
                 row->columns[W3C_MANIFEST_APPLICABILITY], "OPTIONAL")) ||
            (!manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS") &&
             !manifest_value_is(row->columns[W3C_MANIFEST_STATUS],
                                "UNSUPPORTED") &&
             !manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "N/A")) ||
            !manifest_upstream_is_documented(
                row->columns[W3C_MANIFEST_ID],
                row->columns[W3C_MANIFEST_UPSTREAM]))
            goto cleanup;
        for (previous = 0u; previous < row_count; ++previous) {
            if (strcmp(rows[previous].columns[W3C_MANIFEST_ID],
                       row->columns[W3C_MANIFEST_ID]) == 0 ||
                strcmp(rows[previous].columns[W3C_MANIFEST_FIXTURE],
                       row->columns[W3C_MANIFEST_FIXTURE]) == 0 ||
                strcmp(rows[previous].columns[W3C_MANIFEST_UPSTREAM],
                       row->columns[W3C_MANIFEST_UPSTREAM]) == 0)
                goto cleanup;
        }
        written = snprintf(expected_fixture, sizeof(expected_fixture),
                           "test%s.scxml",
                           row->columns[W3C_MANIFEST_ID]);
        if (written < 0 || (size_t)written >= sizeof(expected_fixture) ||
            strcmp(expected_fixture,
                   row->columns[W3C_MANIFEST_FIXTURE]) != 0)
            goto cleanup;
        if (manifest_value_is(row->columns[W3C_MANIFEST_APPLICABILITY],
                              "MANDATORY")) {
            ++stats.mandatory;
        } else {
            ++stats.optional;
        }
        if (manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS")) {
            if (!manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                   "TERMINAL_PASS") ||
                manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                  "NONE"))
                goto cleanup;
            if (documentation != NULL &&
                (strstr(documentation,
                        row->columns[W3C_MANIFEST_FIXTURE]) == NULL ||
                 strstr(documentation,
                        row->columns[W3C_MANIFEST_UPSTREAM]) == NULL))
                goto cleanup;
            ++stats.passed;
        } else if (manifest_value_is(
                       row->columns[W3C_MANIFEST_STATUS], "UNSUPPORTED")) {
            if (!manifest_value_is(
                    row->columns[W3C_MANIFEST_APPLICABILITY], "MANDATORY") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                   "NOT_RUN") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                   "NONE"))
                goto cleanup;
            ++stats.unsupported;
        } else {
            if (!manifest_value_is(
                    row->columns[W3C_MANIFEST_APPLICABILITY], "OPTIONAL") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_EXPECTED],
                                   "NOT_RUN") ||
                !manifest_value_is(row->columns[W3C_MANIFEST_TRANSFORMATION],
                                   "NONE"))
                goto cleanup;
            ++stats.not_applicable;
        }
        if (verify_fixtures &&
            manifest_value_is(row->columns[W3C_MANIFEST_STATUS], "PASS")) {
            written = snprintf(path, sizeof(path), "%s/%s",
                               fixture_directory,
                               row->columns[W3C_MANIFEST_FIXTURE]);
            if (written < 0 || (size_t)written >= sizeof(path)) goto cleanup;
            fixture_source = tt_read_file(path, &fixture_size);
            if (fixture_source == NULL || fixture_size == 0u) {
                free(fixture_source);
                goto cleanup;
            }
            free(fixture_source);
        }
        ++row_count;
        if (newline == NULL) break;
        cursor = newline + 1;
    }
    stats.rows = row_count;
    if (row_count == 0u) goto cleanup;
    *out_stats = stats;
    return true;

cleanup:
    memset(out_stats, 0, sizeof(*out_stats));
    return false;
}

static bool validate_w3c_manifest(w3c_manifest_stats *out_stats) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    char *documentation = NULL;
    size_t source_size = 0u;
    size_t documentation_size = 0u;
    bool valid = false;
    int written;
    if (out_stats == NULL) return false;
    memset(out_stats, 0, sizeof(*out_stats));
    written = snprintf(path, sizeof(path), "%s/manifest.tsv",
                       CFLOW_SCXML_W3C_FIXTURE_DIR);
    if (written < 0 || (size_t)written >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL || source_size == 0u ||
        source_size > W3C_MANIFEST_BYTE_CAPACITY)
        goto cleanup;
    written = snprintf(path, sizeof(path), "%s/README.md",
                       CFLOW_SCXML_W3C_FIXTURE_DIR);
    if (written < 0 || (size_t)written >= sizeof(path)) goto cleanup;
    documentation = tt_read_file(path, &documentation_size);
    if (documentation == NULL || documentation_size == 0u)
        goto cleanup;
    valid = validate_w3c_manifest_source(
        source, CFLOW_SCXML_W3C_FIXTURE_DIR, true, documentation, out_stats);
    if (!valid || out_stats->rows != W3C_UPSTREAM_TEST_DOCUMENT_COUNT ||
        out_stats->mandatory != W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT ||
        out_stats->optional != W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT) {
        valid = false;
        memset(out_stats, 0, sizeof(*out_stats));
    }

cleanup:
    free(documentation);
    free(source);
    return valid;
}

static cflow_scxml_adapter_status w3c_reject_send(
    void *user, const cflow_scxml_send_request *request,
    cflow_statechart_effect_ticket *out_ticket,
    const char **out_error) {
    w3c_adapter_probe *probe = (w3c_adapter_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL) {
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    }
    ++probe->prepare_send_calls;
    *out_error = "injected W3C send execution error";
    return CFLOW_SCXML_ADAPTER_ERROR_EXECUTION;
}

static void w3c_adapter_close(void *user) {
    (void)user;
}

static bool w3c_adapter_is_quiescent(void *user) {
    return user != NULL;
}

static void w3c_content_commit(void *user) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_content_discard(void *user) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static cflow_scxml_adapter_status w3c_capture_content_send(
    void *user, const cflow_scxml_send_request_v3 *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_content_probe *probe = (w3c_content_probe *)user;
    const cflow_scxml_content_view *content;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL ||
        request->payload.kind != CFLOW_SCXML_PAYLOAD_CONTENT)
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    content = &request->payload.content;
    if ((content->kind != CFLOW_SCXML_CONTENT_TEXT_UTF8 &&
         content->kind != CFLOW_SCXML_CONTENT_XML_UTF8) ||
        content->byte_count >= sizeof(probe->bytes) ||
        (content->byte_count != 0u && content->bytes == NULL))
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    if (content->byte_count != 0u)
        memcpy(probe->bytes, content->bytes, content->byte_count);
    probe->bytes[content->byte_count] = '\0';
    probe->kind = content->kind;
    ++probe->prepare_send_calls;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_content_commit, w3c_content_discard, probe};
    *out_error = NULL;
    return CFLOW_SCXML_ADAPTER_ACCEPTED;
}

static void w3c_invoke_commit(void *user) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe != NULL) ++probe->commits;
}

static void w3c_invoke_discard(void *user) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe != NULL) ++probe->discards;
}

static cflow_scxml_adapter_status w3c_capture_invoke_start(
    void *user, const cflow_scxml_invoke_start_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    w3c_invoke_probe *probe = (w3c_invoke_probe *)user;
    if (probe == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL || request->id == NULL || request->id_size == 0u ||
        request->id_size >= sizeof(probe->id))
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    memcpy(probe->id, request->id, request->id_size);
    probe->id[request->id_size] = '\0';
    probe->token = request->token;
    ++probe->starts;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_commit, w3c_invoke_discard, probe};
    *out_error = NULL;
    return CFLOW_SCXML_ADAPTER_ACCEPTED;
}

static cflow_scxml_adapter_status w3c_accept_invoke_cancel(
    void *user, const cflow_scxml_invoke_cancel_request *request,
    cflow_statechart_effect_ticket *out_ticket, const char **out_error) {
    if (user == NULL || request == NULL || out_ticket == NULL ||
        out_error == NULL)
        return CFLOW_SCXML_ADAPTER_INVALID_CONTRACT;
    *out_ticket = (cflow_statechart_effect_ticket){
        w3c_invoke_commit, w3c_invoke_discard, user};
    *out_error = NULL;
    return CFLOW_SCXML_ADAPTER_ACCEPTED;
}

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
    cflow_statechart_instance_status runtime_status;

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
    if (!cflow_scxml_program_instance_bindings(
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
    if (runtime_status != CFLOW_STATECHART_INSTANCE_OK) {
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

static bool run_w3c_content_fixture(const char *fixture_name,
                                    const char *expected_content) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    cflow_scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_machine_state_id pass_state = 0u;
    w3c_content_probe probe = {0};
    const cflow_scxml_event_io_adapter_v3 event_io = {
        .abi_version = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V3,
        .struct_size = sizeof(event_io),
        .capabilities = CFLOW_SCXML_EVENT_IO_CAP_SEND |
            CFLOW_SCXML_EVENT_IO_CAP_CONTENT_V3,
        .prepare_send = w3c_capture_content_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const cflow_scxml_session_adapters_v3 adapters = {
        .abi_version = CFLOW_SCXML_SESSION_ADAPTERS_ABI_V3,
        .struct_size = sizeof(adapters),
        .event_io = &event_io,
        .event_io_user = &probe};
    cflow_scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL || expected_content == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         CFLOW_SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (cflow_scxml_compile(
            &program, source, source_size, NULL, &diagnostic) !=
            CFLOW_SCXML_OK ||
        !cflow_scxml_program_state_id(
            &program, "pass", sizeof("pass") - 1u, &pass_state) ||
        !cflow_executor_serial_init(&executor))
        goto cleanup;
    executor_initialized = true;
    config = (cflow_scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u};
    if (cflow_scxml_session_init_v3(&session, &config, &adapters) !=
        CFLOW_STATECHART_INSTANCE_OK)
        goto cleanup;
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_scxml_session_get_stats(&session, &stats))
        goto cleanup;
    succeeded = stats.done && !stats.errored &&
        probe.prepare_send_calls == 1u && probe.commits == 1u &&
        probe.discards == 0u &&
        probe.kind == CFLOW_SCXML_CONTENT_TEXT_UTF8 &&
        strcmp(probe.bytes, expected_content) == 0;

cleanup:
    if (session_initialized &&
        cflow_scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    cflow_scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_invoke_idlocation_fixture(const char *fixture_name,
                                              const char *expected_id) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    cflow_scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_machine_state_id pass_state = 0u;
    w3c_invoke_probe probe = {0};
    const cflow_scxml_invoke_adapter_v1 invoke = {
        .abi_version = CFLOW_SCXML_INVOKE_ADAPTER_ABI_V1,
        .struct_size = sizeof(invoke),
        .capabilities = CFLOW_SCXML_INVOKE_CAP_START |
            CFLOW_SCXML_INVOKE_CAP_CANCEL,
        .prepare_start = w3c_capture_invoke_start,
        .prepare_cancel = w3c_accept_invoke_cancel,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    const w3c_cmeta_state initial = {0};
    const cflow_scxml_cmeta_session_options_v1 data = {
        .abi_version = CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(data),
        .initial_state = &initial};
    const cflow_scxml_cmeta_compile_options_v1 compile_options =
        cflow_scxml_cmeta_default_compile_options(&w3c_cmeta_state_desc);
    cflow_scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL) return false;
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         CFLOW_SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    source = tt_read_file(path, &source_size);
    if (source == NULL) goto cleanup;
    if (cflow_scxml_compile_cmeta(
            &program, source, source_size, NULL, &compile_options,
            &diagnostic) != CFLOW_SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_scxml_program_state_id(
            &program, "pass", sizeof("pass") - 1u, &pass_state)) {
        info("fixture=%s pass state missing", fixture_name);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 2u,
        .adapter_internal_event_capacity = 2u,
        .invocation_capacity = 1u,
        .invoke = &invoke,
        .invoke_user = &probe};
    {
        const cflow_statechart_instance_status init_status =
            cflow_scxml_session_init_cmeta(&session, &config, &data);
        if (init_status != CFLOW_STATECHART_INSTANCE_OK) {
            info("fixture=%s session init status=%d error=%s", fixture_name,
                 (int)init_status, cflow_scxml_session_error(&session));
            goto cleanup;
        }
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor)) {
        info("fixture=%s initial wait failed", fixture_name);
        goto cleanup;
    }
    if (probe.starts != 1u || probe.commits != 1u ||
        probe.discards != 0u || probe.token == 0u || probe.id[0] == '\0' ||
        (expected_id != NULL && strcmp(probe.id, expected_id) != 0)) {
        info("fixture=%s starts=%zu commits=%zu discards=%zu token=%llu id=%s",
             fixture_name, probe.starts, probe.commits, probe.discards,
             (unsigned long long)probe.token, probe.id);
        goto cleanup;
    }
    if (cflow_scxml_session_report_invoke_done(&session, probe.token) !=
        CFLOW_MAILBOX_OK) {
        info("fixture=%s done report rejected", fixture_name);
        goto cleanup;
    }
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_scxml_session_get_stats(&session, &stats)) {
        info("fixture=%s final wait or stats failed", fixture_name);
        goto cleanup;
    }
    succeeded = stats.done && !stats.errored;
    if (!succeeded)
        info("fixture=%s done=%d errored=%d error=%s", fixture_name,
             stats.done ? 1 : 0, stats.errored ? 1 : 0,
             cflow_scxml_session_error(&session));

cleanup:
    if (session_initialized &&
        cflow_scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK)
        succeeded = false;
    if (executor_initialized) cflow_executor_destroy(&executor);
    cflow_scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static bool run_w3c_adapter_error_fixture(
    const char *fixture_name, cflow_statechart_instance_stats *out_stats,
    size_t *out_prepare_send_calls) {
    char path[W3C_FIXTURE_PATH_CAPACITY];
    char *source = NULL;
    size_t source_size = 0u;
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_executor executor = {0};
    cflow_scxml_session session = {0};
    w3c_adapter_probe probe = {0};
    cflow_scxml_event_io_adapter_v1 adapter = {
        .abi_version = CFLOW_SCXML_EVENT_IO_ADAPTER_ABI_V1,
        .struct_size = sizeof(cflow_scxml_event_io_adapter_v1),
        .capabilities = CFLOW_SCXML_EVENT_IO_CAP_SEND,
        .prepare_send = w3c_reject_send,
        .close = w3c_adapter_close,
        .is_quiescent = w3c_adapter_is_quiescent};
    cflow_scxml_session_config config = {0};
    bool executor_initialized = false;
    bool session_initialized = false;
    bool succeeded = false;
    int path_size;

    if (fixture_name == NULL || out_stats == NULL ||
        out_prepare_send_calls == NULL) {
        return false;
    }
    path_size = snprintf(path, sizeof(path), "%s/%s",
                         CFLOW_SCXML_W3C_FIXTURE_DIR, fixture_name);
    if (path_size < 0 || (size_t)path_size >= sizeof(path)) return false;
    memset(out_stats, 0, sizeof(*out_stats));
    *out_prepare_send_calls = 0u;
    source = tt_read_file(path, &source_size);
    if (source == NULL) {
        info("fixture=%s read failed", fixture_name);
        goto cleanup;
    }
    if (cflow_scxml_compile(
            &program, source, source_size, NULL, &diagnostic) !=
        CFLOW_SCXML_OK) {
        info("fixture=%s compile diagnostic=%s", fixture_name,
             diagnostic.message);
        goto cleanup;
    }
    if (!cflow_executor_serial_init(&executor)) {
        info("fixture=%s executor initialization failed", fixture_name);
        goto cleanup;
    }
    executor_initialized = true;
    config = (cflow_scxml_session_config){
        .program = &program,
        .executor = &executor,
        .external_event_capacity = W3C_EXTERNAL_EVENT_CAPACITY,
        .internal_event_capacity = W3C_INTERNAL_EVENT_CAPACITY,
        .completion_capacity = W3C_COMPLETION_CAPACITY,
        .microstep_limit = W3C_MICROSTEP_LIMIT,
        .effect_capacity = 1u,
        .adapter_internal_event_capacity = 1u,
        .event_io = &adapter,
        .adapter_user = &probe};
    if (cflow_scxml_session_init(&session, &config) !=
        CFLOW_STATECHART_INSTANCE_OK) {
        info("fixture=%s session initialization failed", fixture_name);
        goto cleanup;
    }
    session_initialized = true;
    if (!cflow_executor_wait_idle(&executor) ||
        !cflow_scxml_session_get_stats(&session, out_stats)) {
        info("fixture=%s executor wait or stats failed", fixture_name);
        goto cleanup;
    }
    *out_prepare_send_calls = probe.prepare_send_calls;
    succeeded = true;

cleanup:
    if (session_initialized) {
        if (!out_stats->done) {
            cflow_scxml_session_cancel(&session);
            (void)cflow_executor_wait_idle(&executor);
        }
        if (cflow_scxml_session_destroy(&session) !=
            CFLOW_STATECHART_INSTANCE_OK) {
            succeeded = false;
        }
    }
    if (executor_initialized) cflow_executor_destroy(&executor);
    cflow_scxml_program_destroy(&program);
    free(source);
    return succeeded;
}

static void check_w3c_adapter_error_fixture(const char *fixture_name) {
    cflow_statechart_instance_stats stats = {0};
    size_t prepare_send_calls = 0u;

    check_true(run_w3c_adapter_error_fixture(
        fixture_name, &stats, &prepare_send_calls));
    check_true(stats.done);
    check_false(stats.errored);
    check_equal(prepare_send_calls, (size_t)1u);
}

suite("SCXML W3C-derived conformance regression corpus") {
    it("validates the complete strict upstream inventory") {
        w3c_manifest_stats stats = {0};
        check_true(validate_w3c_manifest(&stats));
        check_equal(stats.rows,
                    (size_t)W3C_UPSTREAM_TEST_DOCUMENT_COUNT);
        check_equal(stats.mandatory,
                    (size_t)W3C_UPSTREAM_MANDATORY_DOCUMENT_COUNT);
        check_equal(stats.optional,
                    (size_t)W3C_UPSTREAM_OPTIONAL_DOCUMENT_COUNT);
        check_equal(stats.passed, (size_t)36u);
        check_equal(stats.unsupported, (size_t)132u);
        check_equal(stats.not_applicable, (size_t)34u);
    }

    it("rejects duplicate inventory IDs") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\tone\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\ttwo\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, CFLOW_SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects PASS rows whose local fixture is missing") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "999999\ttest999999.scxml\tMANDATORY\tPASS\tstate\t"
            W3C_UPSTREAM_PREFIX
            "999999/test999999.txml\tTERMINAL_PASS\tlocal rewrite\twitness\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, CFLOW_SCXML_W3C_FIXTURE_DIR, true, NULL, &stats));
    }

    it("rejects malformed inventory rows") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            W3C_UPSTREAM_PREFIX "144/test144.txml\tNOT_RUN\tNONE\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, CFLOW_SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects inventory sources outside the documented W3C origin") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tUNSUPPORTED\traise\t"
            "https://example.invalid/144/test144.txml\tNOT_RUN\tNONE\t"
            "undocumented\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, CFLOW_SCXML_W3C_FIXTURE_DIR, false, NULL, &stats));
    }

    it("rejects PASS rows missing synchronized provenance documentation") {
        char source[] =
            "id\tfixture\tapplicability\tstatus\tfeature\tupstream\t"
            "expected\ttransformation\trationale\n"
            "144\ttest144.scxml\tMANDATORY\tPASS\traise\t"
            W3C_UPSTREAM_PREFIX
            "144/test144.txml\tTERMINAL_PASS\tlocal rewrite\twitness\n";
        w3c_manifest_stats stats = {0};
        check_false(validate_w3c_manifest_source(
            source, CFLOW_SCXML_W3C_FIXTURE_DIR, false,
            "test144.scxml without its upstream URL", &stats));
    }

    it("test 144 preserves raised internal event order") {
        check_w3c_fixture("test144.scxml");
    }

    it("test 147 executes only the first true conditional partition") {
        check_w3c_fixture("test147.scxml");
    }

    it("test 148 executes else when every condition is false") {
        check_w3c_fixture("test148.scxml");
    }

    it("test 149 skips conditional content without a matching partition") {
        check_w3c_fixture("test149.scxml");
    }

    it("test 158 executes one content block in document order") {
        check_w3c_fixture("test158.scxml");
    }

    it("test 159 aborts the remainder of a failing content block") {
        check_w3c_adapter_error_fixture("test159.scxml");
    }

    it("test 179 delivers evaluated content bytes without alteration") {
        check_true(run_w3c_content_fixture("test179.scxml", "123"));
    }

    it("test 223 binds an automatically generated invoke ID") {
        check_true(run_w3c_invoke_idlocation_fixture(
            "test223.scxml", NULL));
    }

    it("test 224 generates invoke IDs in stateid.platformid form") {
        check_true(run_w3c_invoke_idlocation_fixture(
            "test224.scxml", "s0.1"));
    }

    it("test 355 selects the first root child when initial is omitted") {
        check_w3c_fixture("test355.scxml");
    }

    it("test 375 executes onentry handlers in document order") {
        check_w3c_fixture("test375.scxml");
    }

    it("test 376 keeps onentry handlers as separate executable blocks") {
        check_w3c_adapter_error_fixture("test376.scxml");
    }

    it("test 377 executes onexit handlers in document order") {
        check_w3c_fixture("test377.scxml");
    }

    it("test 378 keeps onexit handlers as separate executable blocks") {
        check_w3c_adapter_error_fixture("test378.scxml");
    }

    it("test 387 enters the declared default history configuration") {
        check_w3c_fixture("test387.scxml");
    }

    it("test 399 applies unions prefixes boundaries and wildcards") {
        check_w3c_fixture("test399.scxml");
    }

    it("test 579 orders initial and default history content") {
        check_w3c_fixture("test579.scxml");
    }

    it("test 580 keeps history pseudo states out of the configuration") {
        check_w3c_fixture("test580.scxml");
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

    it("test 407 executes onexit content when a state exits") {
        check_w3c_fixture("test407.scxml");
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

    it("test 421 drains unmatched internal events before an enabled one") {
        check_w3c_fixture("test421.scxml");
    }

    it("test 503 gives targetless transitions an empty exit set") {
        check_w3c_fixture("test503.scxml");
    }

    it("test 504 exits every active descendant of the external LCCA") {
        check_w3c_fixture("test504.scxml");
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

    it("test 576 enters both non-default root initial targets") {
        check_w3c_fixture("test576.scxml");
    }
}
