#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct scxml_foreach_managed_value {
    int observed;
    int *resource;
} scxml_foreach_managed_value;

#define CMETA_USER_TYPE_LIST                                                \
    , (O, scxml_foreach_managed_value,                                      \
       cmeta_type_scxml_foreach_managed_value, CMETA_T_OBJECT,              \
       cmeta_traits_scxml_foreach_managed_value)
#define CMETA_CALLABLE_TYPE_LIST CMETA_BUILTIN_TYPE_LIST

#include <cflow/scxml.h>
#include <turbostl/typed.h>

#include "tinytest.h"

#define MANAGED_FOREACH_MAX_STORAGE_BYTES (1024u * 1024u)

static size_t managed_foreach_live_resources;
static size_t managed_foreach_copy_count;
static size_t managed_foreach_move_count;

static scxml_foreach_managed_value managed_foreach_make(int value) {
    scxml_foreach_managed_value result = {0};
    result.resource = (int *)malloc(sizeof(*result.resource));
    if (result.resource != NULL) {
        result.observed = value;
        *result.resource = value;
        ++managed_foreach_live_resources;
    }
    return result;
}

static bool managed_foreach_copy(void *destination_, const void *source_) {
    scxml_foreach_managed_value *destination =
        (scxml_foreach_managed_value *)destination_;
    const scxml_foreach_managed_value *source =
        (const scxml_foreach_managed_value *)source_;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    if (source->resource == NULL) return true;
    *destination = managed_foreach_make(source->observed);
    if (destination->resource == NULL) return false;
    ++managed_foreach_copy_count;
    return true;
}

static void managed_foreach_move(void *destination_, void *source_) {
    scxml_foreach_managed_value *destination =
        (scxml_foreach_managed_value *)destination_;
    scxml_foreach_managed_value *source =
        (scxml_foreach_managed_value *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
    ++managed_foreach_move_count;
}

static void managed_foreach_destroy(void *value_) {
    scxml_foreach_managed_value *value =
        (scxml_foreach_managed_value *)value_;
    if (value == NULL) return;
    if (value->resource != NULL) {
        free(value->resource);
        --managed_foreach_live_resources;
    }
    memset(value, 0, sizeof(*value));
}

const cmeta_type_traits cmeta_traits_scxml_foreach_managed_value = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = managed_foreach_copy,
    .move_construct = managed_foreach_move,
    .destroy = managed_foreach_destroy
};

static const cmeta_type_identity managed_foreach_value_identity =
    CMETA_TYPE_ID_ATOM_INIT("test.scxml.foreach.managed.value");

const cmeta_type_desc cmeta_type_scxml_foreach_managed_value = {
    .name = "scxml_foreach_managed_value",
    .size = sizeof(scxml_foreach_managed_value),
    .align = _Alignof(scxml_foreach_managed_value),
    .kind = CMETA_T_OBJECT,
    .traits = &cmeta_traits_scxml_foreach_managed_value,
    .identity = &managed_foreach_value_identity
};

const cmeta_type_desc cmeta_type_scxml_foreach_managed_value_ptr = {
    .name = "scxml_foreach_managed_value *",
    .size = sizeof(scxml_foreach_managed_value *),
    .align = _Alignof(scxml_foreach_managed_value *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_scxml_foreach_managed_value
};

static const cmeta_field_desc managed_value_layout_fields[] = {
    {"observed", "int", offsetof(scxml_foreach_managed_value, observed),
     sizeof(int), _Alignof(int), &cmeta_type_int, NULL},
    {"resource", "int *", offsetof(scxml_foreach_managed_value, resource),
     sizeof(int *), _Alignof(int *), &cmeta_type_int_ptr, NULL}
};

static const cmeta_struct_desc managed_value_layout = {
    .name = "scxml_foreach_managed_value",
    .size = sizeof(scxml_foreach_managed_value),
    .align = _Alignof(scxml_foreach_managed_value),
    .fields = managed_value_layout_fields,
    .field_count = sizeof(managed_value_layout_fields) /
                   sizeof(managed_value_layout_fields[0])
};

static const cmeta_data_field_desc managed_value_fields[] = {
    {"test.scxml.foreach.managed.observed", "observed",
     offsetof(scxml_foreach_managed_value, observed), &cmeta_data_int}
};

static const cmeta_data_struct_shape managed_value_shape = {
    .layout = &managed_value_layout,
    .fields = managed_value_fields,
    .field_count = sizeof(managed_value_fields) /
                   sizeof(managed_value_fields[0])
};

static const cmeta_data_desc managed_value_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.foreach.managed",
    .display_name = "SCXML foreach managed value",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &cmeta_type_scxml_foreach_managed_value,
    .shape = &managed_value_shape
};

Struct(scxml_managed_foreach_root,
    (TYPE(Vec, scxml_foreach_managed_value), values),
    (scxml_foreach_managed_value, item),
    (size_t, index),
    (int, total)
);

static bool managed_root_copy(void *destination_, const void *source_) {
    scxml_managed_foreach_root *destination =
        (scxml_managed_foreach_root *)destination_;
    const scxml_managed_foreach_root *source =
        (const scxml_managed_foreach_root *)source_;
    size_t index;
    if (destination == NULL || source == NULL) return false;
    memset(destination, 0, sizeof(*destination));
    destination->values = VecOf(scxml_foreach_managed_value);
    if (vec_init(&destination->values, source->values.element_limit) != STL_OK)
        return false;
    for (index = 0u; index < vec_size(&source->values); ++index) {
        const scxml_foreach_managed_value *value =
            (const scxml_foreach_managed_value *)vec_at_const(
                &source->values, index);
        if (value == NULL || vec_push(&destination->values, value) != STL_OK)
            goto fail;
    }
    if (!managed_foreach_copy(&destination->item, &source->item)) goto fail;
    destination->index = source->index;
    destination->total = source->total;
    return true;

fail:
    vec_destroy(&destination->values);
    managed_foreach_destroy(&destination->item);
    memset(destination, 0, sizeof(*destination));
    return false;
}

static void managed_root_move(void *destination_, void *source_) {
    scxml_managed_foreach_root *destination =
        (scxml_managed_foreach_root *)destination_;
    scxml_managed_foreach_root *source =
        (scxml_managed_foreach_root *)source_;
    if (destination == NULL || source == NULL) return;
    *destination = *source;
    memset(source, 0, sizeof(*source));
}

static void managed_root_destroy(void *value_) {
    scxml_managed_foreach_root *value =
        (scxml_managed_foreach_root *)value_;
    if (value == NULL) return;
    if (value->values.initialized) vec_destroy(&value->values);
    managed_foreach_destroy(&value->item);
}

static const cmeta_type_traits managed_root_traits = {
    .flags = CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY,
    .copy_construct = managed_root_copy,
    .move_construct = managed_root_move,
    .destroy = managed_root_destroy
};

static const cmeta_type_desc managed_root_type = {
    .name = "scxml_managed_foreach_root",
    .size = sizeof(scxml_managed_foreach_root),
    .align = _Alignof(scxml_managed_foreach_root),
    .kind = CMETA_T_OBJECT,
    .traits = &managed_root_traits
};

static const cmeta_data_field_desc managed_root_fields[] = {
    {"test.scxml.foreach.managed.values", "values",
     offsetof(scxml_managed_foreach_root, values), &cmeta_data_sequence},
    {"test.scxml.foreach.managed.item", "item",
     offsetof(scxml_managed_foreach_root, item), &managed_value_data},
    {"test.scxml.foreach.managed.index", "index",
     offsetof(scxml_managed_foreach_root, index), &cmeta_data_size},
    {"test.scxml.foreach.managed.total", "total",
     offsetof(scxml_managed_foreach_root, total), &cmeta_data_int}
};

static const cmeta_data_struct_shape managed_root_shape = {
    .layout = StructMeta(scxml_managed_foreach_root),
    .fields = managed_root_fields,
    .field_count = sizeof(managed_root_fields) /
                   sizeof(managed_root_fields[0])
};

static const cmeta_data_desc managed_root_data = {
    .struct_size = sizeof(cmeta_data_desc),
    .abi_version = CMETA_DATA_DESC_ABI_VERSION,
    .stable_id = "test.scxml.foreach.managed.root",
    .display_name = "SCXML managed foreach root",
    .kind = CMETA_DATA_STRUCT,
    .storage_type = &managed_root_type,
    .shape = &managed_root_shape
};

static cflow_statechart_instance_stats run_managed_foreach(
    const cflow_scxml_program *program, const int *values,
    size_t value_count) {
    scxml_managed_foreach_root initial = {
        .values = VecOf(scxml_foreach_managed_value),
        .item = {0}, .index = 0u, .total = 0
    };
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
        .max_storage_bytes = MANAGED_FOREACH_MAX_STORAGE_BYTES
    };
    cflow_scxml_cmeta_session_options_v1 data = {
        .abi_version = CFLOW_SCXML_CMETA_SESSION_OPTIONS_ABI_V1,
        .struct_size = sizeof(cflow_scxml_cmeta_session_options_v1),
        .initial_state = &initial
    };
    size_t index;

    initial.item = managed_foreach_make(41);
    check_not_null(initial.item.resource);
    check_equal(vec_init(&initial.values, value_count), STL_OK);
    for (index = 0u; index < value_count; ++index) {
        scxml_foreach_managed_value value = managed_foreach_make(values[index]);
        check_not_null(value.resource);
        check_equal(vec_push(&initial.values, &value), STL_OK);
        managed_foreach_destroy(&value);
    }
    check_true(cflow_executor_serial_init(&executor));
    check_equal(cflow_scxml_session_init_cmeta(&session, &config, &data),
                CFLOW_STATECHART_RUNTIME_OK);
    check_true(cflow_executor_wait_idle(&executor));
    check_true(cflow_scxml_session_get_stats(&session, &stats));
    check_equal(cflow_scxml_session_destroy(&session),
                CFLOW_STATECHART_RUNTIME_OK);
    cflow_executor_destroy(&executor);
    managed_root_destroy(&initial);
    return stats;
}

spec("CFlow SCXML CMeta managed foreach") {
  it("moves independently owned Range values into the staged item") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='item.observed'/></foreach>"
        "</onentry><transition cond='total == 3 &amp;&amp; "
        "item.observed == 3 &amp;&amp; index == 2' target='done'/></state>"
        "<final id='done'/></scxml>";
    const int values[] = {1, 2, 3};
    const cflow_scxml_cmeta_compile_options_v1 options =
        cflow_scxml_cmeta_default_compile_options(&managed_root_data);
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    managed_foreach_live_resources = 0u;
    managed_foreach_copy_count = 0u;
    managed_foreach_move_count = 0u;
    {
        const cflow_scxml_status status = cflow_scxml_compile_cmeta(
            &program, source, strlen(source), NULL, &options, &diagnostic);
        info("diagnostic=%s", diagnostic.message);
        check_equal(status, CFLOW_SCXML_OK);
    }
    stats = run_managed_foreach(&program, values, 3u);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
    check_true(managed_foreach_copy_count >= 3u);
    check_true(managed_foreach_move_count >= 3u);
    check_equal(managed_foreach_live_resources, (size_t)0u);
  }

  it("releases managed values and rolls back after a child error") {
    static const char source[] =
        "<scxml xmlns='http://www.w3.org/2005/07/scxml' version='1.0' "
        "initial='work' datamodel='cmeta'><state id='work'><onentry>"
        "<foreach array='values' item='item' index='index'>"
        "<assign location='total' expr='4294967295'/></foreach>"
        "</onentry><transition event='error.execution' "
        "cond='total == 0 &amp;&amp; item.observed == 41 &amp;&amp; "
        "index == 0' target='done'/></state><final id='done'/></scxml>";
    const int values[] = {7};
    const cflow_scxml_cmeta_compile_options_v1 options =
        cflow_scxml_cmeta_default_compile_options(&managed_root_data);
    cflow_scxml_program program = {0};
    cflow_scxml_diagnostic diagnostic = {0};
    cflow_statechart_instance_stats stats;

    managed_foreach_live_resources = 0u;
    managed_foreach_copy_count = 0u;
    managed_foreach_move_count = 0u;
    check_equal(cflow_scxml_compile_cmeta(
                    &program, source, strlen(source), NULL, &options,
                    &diagnostic), CFLOW_SCXML_OK);
    stats = run_managed_foreach(&program, values, 1u);
    check_true(stats.done);
    check_false(stats.errored);
    cflow_scxml_program_destroy(&program);
    check_true(managed_foreach_move_count >= 1u);
    check_equal(managed_foreach_live_resources, (size_t)0u);
  }
}
