#include <cflow/scxml.h>
#include <turbostl/typed.h>

#include "tinytest.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define FOREACH_TEST_MAX_STORAGE_BYTES (1024u * 1024u)

Struct(scxml_foreach_root,
    (TYPE(Vec, int), values),
    (int, item),
    (size_t, index),
    (int, outer_item),
    (size_t, outer_index),
    (int, total)
);

static bool foreach_root_copy(void *destination_, const void *source_) {
    scxml_foreach_root *destination = (scxml_foreach_root *)destination_;
    const scxml_foreach_root *source =
        (const scxml_foreach_root *)source_;
    size_t index;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    destination->values = VecOf(int);
    if (vec_init(&destination->values, source->values.element_limit) != STL_OK)
        return false;
    for (index = 0u; index < vec_size(&source->values); ++index) {
        const int *value = (const int *)vec_at_const(&source->values, index);
        if (value == NULL ||
            vec_push(&destination->values, value) != STL_OK) {
            vec_destroy(&destination->values);
            memset(destination, 0, sizeof(*destination));
            return false;
        }
    }
    destination->item = source->item;
    destination->index = source->index;
    destination->outer_item = source->outer_item;
    destination->outer_index = source->outer_index;
    destination->total = source->total;
    return true;
}

static void foreach_root_move(void *destination_, void *source_) {
    scxml_foreach_root *destination = (scxml_foreach_root *)destination_;
    scxml_foreach_root *source = (scxml_foreach_root *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void foreach_root_destroy(void *value_) {
    scxml_foreach_root *value = (scxml_foreach_root *)value_;
    if (value != NULL && value->values.initialized)
        vec_destroy(&value->values);
}

static const cmeta_type_traits foreach_root_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = foreach_root_copy,
    .move_construct = foreach_root_move,
    .destroy = foreach_root_destroy
};

static const cmeta_type_desc foreach_root_type = {
    .name = "scxml_foreach_root",
    .size = sizeof(scxml_foreach_root),
    .align = _Alignof(scxml_foreach_root),
    .kind = CMETA_T_OBJECT,
    .traits = &foreach_root_traits
};

static const cmeta_data_field_desc foreach_root_fields[] = {
    {"test.scxml.foreach.values", "values",
     offsetof(scxml_foreach_root, values), &cmeta_data_sequence},
    {"test.scxml.foreach.item", "item",
     offsetof(scxml_foreach_root, item), &cmeta_data_int},
    {"test.scxml.foreach.index", "index",
     offsetof(scxml_foreach_root, index), &cmeta_data_size},
    {"test.scxml.foreach.outer-item", "outer_item",
     offsetof(scxml_foreach_root, outer_item), &cmeta_data_int},
    {"test.scxml.foreach.outer-index", "outer_index",
     offsetof(scxml_foreach_root, outer_index), &cmeta_data_size},
    {"test.scxml.foreach.total", "total",
     offsetof(scxml_foreach_root, total), &cmeta_data_int}
};

static const cmeta_data_struct_shape foreach_root_shape = {
    .layout = StructMeta(scxml_foreach_root),
    .fields = foreach_root_fields,
    .field_count = sizeof(foreach_root_fields) /
                   sizeof(foreach_root_fields[0])
};

static const cmeta_data_desc foreach_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.foreach.root",
    .display_name = "SCXML foreach root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &foreach_root_type,
    .shape = &foreach_root_shape
};

typedef struct foreach_legacy_compile_options_v1 {
    uint32_t abi_version;
    size_t struct_size;
    const cmeta_data_desc *root;
    size_t max_source_bytes;
    size_t max_instructions;
    size_t max_operands;
    size_t max_expression_depth;
    size_t max_path_depth;
    size_t max_literal_bytes;
    size_t max_string_bytes;
} foreach_legacy_compile_options_v1;

_Static_assert(
    sizeof(foreach_legacy_compile_options_v1) ==
        offsetof(cflow_scxml_cmeta_compile_options_v1, max_iterations),
    "legacy CMeta compile options prefix changed");

static cflow_scxml_status compile_foreach(
    const char *source, cflow_scxml_program *program,
    cflow_scxml_diagnostic *diagnostic) {
    const cflow_scxml_cmeta_compile_options_v1 options =
        cflow_scxml_cmeta_default_compile_options(&foreach_root_data);
    return cflow_scxml_compile_cmeta(
        program, source, strlen(source), NULL, &options, diagnostic);
}

static cflow_scxml_status compile_foreach_with_options(
    const char *source, cflow_scxml_program *program,
    const cflow_scxml_cmeta_compile_options_v1 *options,
    cflow_scxml_diagnostic *diagnostic) {
    return cflow_scxml_compile_cmeta(
        program, source, strlen(source), NULL, options, diagnostic);
}

static cflow_statechart_instance_stats run_foreach(
    const cflow_scxml_program *program, const int *values,
    size_t value_count, int item, size_t index, int total) {
    scxml_foreach_root initial = {
        .values = VecOf(int), .item = item, .index = index, .total = total};
    cflow_executor executor = {0};
    cflow_scxml_session session = {0};
    cflow_statechart_instance_stats stats = {0};
    cflow_scxml_session_config config = {
        .program = program,
        .executor = &executor,
        .external_event_capacity = 2u,
        .internal_event_capacity = 4u,
        .completion_capacity = 2u,
        .microstep_limit = 32u,
        .max_storage_bytes = FOREACH_TEST_MAX_STORAGE_BYTES
    };
    cflow_scxml_cmeta_session_options_v1 data = {
        .abi_version = CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(cflow_scxml_cmeta_session_options_v1),
        .initial_state = &initial
    };
    size_t value_index;

    check_equal(vec_init(&initial.values, value_count), STL_OK);
    for (value_index = 0u; value_index < value_count; ++value_index)
        check_equal(vec_push(&initial.values, &values[value_index]), STL_OK);
    check_true(cflow_executor_serial_init(&executor));
    check_equal(cflow_scxml_session_init_cmeta(&session, &config, &data),
                CFLOW_STATECHART_RUNTIME_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_true(cflow_scxml_session_get_stats(&session, &stats));
    check_equal(cflow_scxml_session_destroy(&session),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&executor);
    vec_destroy(&initial.values);
    return stats;
}

spec("CFlow SCXML CMeta foreach") {
  it("assigns items and zero-based indexes in declared sequence order") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; item == 3 "
        "&amp;&amp; index == 2' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;
    cflow_scxml_status status;

    status = compile_foreach(source, &program, &diagnostic);
    info("diagnostic=%s", diagnostic.message);
    check_equal(status, CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("skips the body and preserves item and index for an empty sequence") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 5 &amp;&amp; item == 41 "
        "&amp;&amp; index == 43' target='done'/></state>"
        "<final id='done'/></scxml>";
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;
    cflow_scxml_status status;

    status = compile_foreach(source, &program, &diagnostic);
    info("diagnostic=%s", diagnostic.message);
    check_equal(status, CFLOW_SCXML_OK);
    stats = run_foreach(&program, NULL, 0u, 41, 43u, 5);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("executes nested foreach and conditional ranges within the admitted depth") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='outer_item' index='outer_index'>"
        "<foreach array='values' item='item' index='index'>"
        "<if cond='outer_item == 2 &amp;&amp; outer_index == 1 "
        "&amp;&amp; item == 3 &amp;&amp; index == 2'>"
        "<assign location='total' expr='99'/></if></foreach></foreach>"
        "</onentry><transition cond='total == 99' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("retains and raises events declared inside a foreach body") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item'><raise event='tick'/>"
        "</foreach></onentry><transition event='tick' target='done'/>"
        "</state><final id='done'/></scxml>";
    const int values[] = {1};
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 1u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("accepts the original v1 options prefix and applies the bounded default") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; item == 3' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    const cflow_scxml_cmeta_compile_options_v1 current =
        cflow_scxml_cmeta_default_compile_options(&foreach_root_data);
    foreach_legacy_compile_options_v1 legacy;
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    memcpy(&legacy, &current, sizeof(legacy));
    legacy.struct_size = sizeof(legacy);
    check_equal(compile_foreach_with_options(
                    source, &program,
                    (const cflow_scxml_cmeta_compile_options_v1 *)&legacy,
                    &diagnostic),
                CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 3u, 0, 0u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("raises error.execution and rolls back the whole block above the iteration limit") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item'/></foreach>"
        "</onentry><transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item == 9 &amp;&amp; index == 7' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    cflow_scxml_cmeta_compile_options_v1 options =
        cflow_scxml_cmeta_default_compile_options(&foreach_root_data);
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    options.max_iterations = 2u;
    check_equal(compile_foreach_with_options(
                    source, &program, &options, &diagnostic),
                CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 3u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);

    options.max_iterations = 0u;
    check_equal(compile_foreach_with_options(
                    source, &program, &options, &diagnostic),
                CFLOW_SCXML_INVALID_ARGUMENT);
    check_null(program.impl);
  }

  it("stops iteration and rolls back when a child assignment fails") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='4294967295'/></foreach>"
        "</onentry><transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item == 9 &amp;&amp; index == 7' "
        "target='done'/></state><final id='done'/></scxml>";
    const int values[] = {1};
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    check_equal(compile_foreach(source, &program, &diagnostic),
                CFLOW_SCXML_OK);
    stats = run_foreach(&program, values, 1u, 9, 7u, 0);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
  }

  it("rejects unresolved or mismatched foreach locations and empty bodies") {
    static const char *const invalid[] = {
        "<foreach item='item'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values'><assign location='total' expr='item'/></foreach>",
        "<foreach array='total' item='item'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='index'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='item' index='total'><assign location='total' expr='item'/></foreach>",
        "<foreach array='values' item='item'></foreach>"};
    size_t invalid_index;

    for (invalid_index = 0u;
         invalid_index < sizeof(invalid) / sizeof(invalid[0]);
         ++invalid_index) {
        char source[1024];
        cflow_scxml_program program = {0};
        cflow_scxml_diagnostic diagnostic = {0};
        cflow_scxml_status status;
        (void)snprintf(
            source, sizeof(source),
            "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
            "initial='work' datamodel='cmeta'><state id='work'><onentry>%s"
            "</onentry></state></scxml>", invalid[invalid_index]);
        status = compile_foreach(source, &program, &diagnostic);
        info("case=%zu diagnostic=%s", invalid_index, diagnostic.message);
        check_equal(status, CFLOW_SCXML_INVALID_STRUCTURE);
        check_null(program.impl);
    }
  }

  it("keeps CMeta foreach unavailable in finalize blocks") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'>"
        "<invoke id='child'><finalize><foreach array='values' item='item'>"
        "<assign location='total' expr='item'/></foreach></finalize>"
        "</invoke></state></scxml>";
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};

    check_equal(compile_foreach(source, &program, &diagnostic),
                CFLOW_SCXML_UNSUPPORTED_FEATURE);
    check_null(program.impl);
  }
}
