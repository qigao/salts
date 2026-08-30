#include "cmeta_assign.h"
#include "cmeta_location.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCXML_ASSIGN_SINT64_UPPER_BOUND 9223372036854775808.0
#define SCXML_ASSIGN_UINT64_UPPER_BOUND 18446744073709551616.0

typedef struct cflow_scxml_cmeta_assign_program_impl {
    const cmeta_data_desc *destination;
    size_t destination_offset;
    size_t max_string_bytes;
    cflow_scxml_cmeta_expr_program expression;
} cflow_scxml_cmeta_assign_program_impl;

static cflow_scxml_cmeta_expr_status assign_report(
    cflow_scxml_cmeta_expr_diagnostic *diagnostic,
    cflow_scxml_cmeta_expr_status status, size_t byte_offset,
    const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        diagnostic->byte_offset = byte_offset;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

static bool assign_destination_accepts(
    const cmeta_data_desc *destination,
    cflow_scxml_cmeta_expr_value_kind source_kind) {
    switch (destination->kind) {
        case CMETA_DATA_BOOL:
            return source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_BOOL;
        case CMETA_DATA_SINT:
        case CMETA_DATA_UINT:
        case CMETA_DATA_FLOAT:
            return source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT ||
                   source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT ||
                   source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT;
        case CMETA_DATA_ENUM:
            return source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT ||
                   source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT;
        case CMETA_DATA_STRING:
            return source_kind == CFLOW_SCXML_CMETA_EXPR_VALUE_STRING;
        default:
            return false;
    }
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_compile(
    cflow_scxml_cmeta_assign_program *out,
    const char *location, size_t location_size,
    const char *expression, size_t expression_size,
    const cmeta_data_desc *root,
    cflow_scxml_cmeta_expr_resolve_state_fn resolve_state,
    void *resolve_user,
    const cflow_scxml_cmeta_expr_limits *limits_or_null,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cflow_scxml_cmeta_expr_limits limits =
        limits_or_null != NULL ? *limits_or_null
                               : cflow_scxml_cmeta_expr_default_limits();
    cflow_scxml_cmeta_assign_program_impl *impl = NULL;
    const cmeta_data_desc *destination = NULL;
    cflow_scxml_cmeta_expr_status status;
    cflow_scxml_cmeta_location destination_location = {0};
    if (out == NULL || out->impl != NULL || location == NULL ||
        location_size == 0u || expression == NULL || expression_size == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        resolve_state == NULL ||
        !cflow_scxml_cmeta_expr_limits_valid(&limits))
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta assignment compile arguments");
    if (location_size > limits.max_source_bytes)
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                             limits.max_source_bytes,
                             "CMeta assignment location byte limit exceeded");
    status = cflow_scxml_cmeta_location_compile(
        &destination_location, location, location_size, root,
        limits.max_path_depth, true, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    destination = destination_location.value;
    if ((destination->kind == CMETA_DATA_STRING &&
         (cmeta_data_buffer_ops_of(destination) == NULL ||
          cmeta_data_buffer_ops_of(destination)->ownership ==
              CMETA_DATA_BUFFER_CUSTOM)) ||
        (destination->kind == CMETA_DATA_ENUM &&
         cmeta_data_enum_ops_of(destination) == NULL))
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, 0u,
                             "CMeta assignment destination lacks a safe adapter");
    impl = (cflow_scxml_cmeta_assign_program_impl *)calloc(1u, sizeof(*impl));
    if (impl == NULL)
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED, 0u,
                             "CMeta assignment program allocation failed");
    status = cflow_scxml_cmeta_expr_compile_value(
        &impl->expression, expression, expression_size, root,
        resolve_state, resolve_user, &limits, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) {
        free(impl);
        return status;
    }
    if (!assign_destination_accepts(
            destination,
            cflow_scxml_cmeta_expr_program_value_kind(&impl->expression))) {
        cflow_scxml_cmeta_expr_program_destroy(&impl->expression);
        free(impl);
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, 0u,
                             "CMeta assignment source and destination types differ");
    }
    impl->destination = destination;
    impl->destination_offset = destination_location.offset;
    impl->max_string_bytes = limits.max_string_bytes;
    out->impl = impl;
    return assign_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
}

static bool assign_sint_value(
    const cflow_scxml_cmeta_expr_value *source, int64_t *out) {
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT) {
        *out = source->data.sint;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT) {
        if (source->data.uint > INT64_MAX) return false;
        *out = (int64_t)source->data.uint;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT &&
        isfinite(source->data.number) &&
        trunc(source->data.number) == source->data.number &&
        source->data.number >= -SCXML_ASSIGN_SINT64_UPPER_BOUND &&
        source->data.number < SCXML_ASSIGN_SINT64_UPPER_BOUND) {
        *out = (int64_t)source->data.number;
        return true;
    }
    return false;
}

static bool assign_uint_value(
    const cflow_scxml_cmeta_expr_value *source, uint64_t *out) {
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT) {
        *out = source->data.uint;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT) {
        if (source->data.sint < 0) return false;
        *out = (uint64_t)source->data.sint;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT &&
        isfinite(source->data.number) &&
        trunc(source->data.number) == source->data.number &&
        source->data.number >= 0.0 &&
        source->data.number < SCXML_ASSIGN_UINT64_UPPER_BOUND) {
        *out = (uint64_t)source->data.number;
        return true;
    }
    return false;
}

static bool assign_store_sint(const cmeta_data_desc *destination,
                              void *object, int64_t value) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)destination->shape)->bits;
    switch (bits) {
        case 8: {
            const int8_t converted = (int8_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16: {
            const int16_t converted = (int16_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32: {
            const int32_t converted = (int32_t)value;
            if ((int64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool assign_store_uint(const cmeta_data_desc *destination,
                              void *object, uint64_t value) {
    const uint8_t bits =
        ((const cmeta_data_integer_shape *)destination->shape)->bits;
    switch (bits) {
        case 8: {
            const uint8_t converted = (uint8_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 16: {
            const uint16_t converted = (uint16_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 32: {
            const uint32_t converted = (uint32_t)value;
            if ((uint64_t)converted != value) return false;
            memcpy(object, &converted, sizeof(converted));
            return true;
        }
        case 64:
            memcpy(object, &value, sizeof(value));
            return true;
        default:
            return false;
    }
}

static bool assign_float_value(
    const cflow_scxml_cmeta_expr_value *source, double *out) {
    double value;
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_FLOAT) {
        *out = source->data.number;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_SINT) {
        value = (double)source->data.sint;
        if (value < -SCXML_ASSIGN_SINT64_UPPER_BOUND ||
            value >= SCXML_ASSIGN_SINT64_UPPER_BOUND ||
            (int64_t)value != source->data.sint)
            return false;
        *out = value;
        return true;
    }
    if (source->kind == CFLOW_SCXML_CMETA_EXPR_VALUE_UINT) {
        value = (double)source->data.uint;
        if (value >= SCXML_ASSIGN_UINT64_UPPER_BOUND ||
            (uint64_t)value != source->data.uint)
            return false;
        *out = value;
        return true;
    }
    return false;
}

static bool assign_store_float(const cmeta_data_desc *destination,
                               void *object, double value) {
    const uint8_t bits =
        ((const cmeta_data_float_shape *)destination->shape)->bits;
    if (bits == 32u) {
        const float converted = (float)value;
        if ((double)converted != value) return false;
        memcpy(object, &converted, sizeof(converted));
        return true;
    }
    if (bits == 64u) {
        memcpy(object, &value, sizeof(value));
        return true;
    }
    return false;
}

static cflow_scxml_cmeta_expr_status assign_string(
    const cflow_scxml_cmeta_assign_program_impl *impl, void *destination,
    const cflow_scxml_cmeta_expr_value *source,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cmeta_data_buffer_ops *ops =
        cmeta_data_buffer_ops_of(impl->destination);
    const unsigned char *bytes =
        (const unsigned char *)source->data.string.data;
    unsigned char *copy = NULL;
    cmeta_status status;
    if (source->data.string.size > impl->max_string_bytes)
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED, 0u,
                             "CMeta assignment string limit exceeded");
    if (ops->ownership == CMETA_DATA_BUFFER_OWNED &&
        source->data.string.size != 0u) {
        copy = (unsigned char *)malloc(source->data.string.size);
        if (copy == NULL)
            return assign_report(diagnostic,
                                 CFLOW_SCXML_CMETA_EXPR_ALLOCATION_FAILED, 0u,
                                 "CMeta assignment string copy failed");
        memcpy(copy, bytes, source->data.string.size);
        bytes = copy;
    }
    status = cmeta_data_buffer_restore_zero(impl->destination, destination);
    if (status == CMETA_OK)
        status = cmeta_data_buffer_assign(
            impl->destination, destination, bytes,
            source->data.string.size, impl->max_string_bytes);
    free(copy);
    return status == CMETA_OK
               ? assign_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL)
               : assign_report(diagnostic,
                               CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR, 0u,
                               "CMeta string assignment adapter failed");
}

static cflow_scxml_cmeta_expr_status assign_apply(
    const cflow_scxml_cmeta_assign_program *program,
    void *staged_root,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cflow_scxml_cmeta_assign_program_impl *impl =
        program != NULL
            ? (const cflow_scxml_cmeta_assign_program_impl *)program->impl
            : NULL;
    cflow_scxml_cmeta_expr_value source;
    cflow_scxml_cmeta_expr_status status;
    unsigned char *destination;
    int64_t sint;
    uint64_t uint;
    double number;
    if (impl == NULL || staged_root == NULL || is_active == NULL)
        return assign_report(diagnostic,
                             CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT, 0u,
                             "invalid CMeta assignment evaluation arguments");
    status = cflow_scxml_cmeta_expr_evaluate_value_with_system(
        &impl->expression, staged_root, is_active, active_user,
        system_values, &source, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    destination = (unsigned char *)staged_root + impl->destination_offset;
    switch (impl->destination->kind) {
        case CMETA_DATA_BOOL:
            memcpy(destination, &source.data.boolean, sizeof(bool));
            return assign_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, 0u,
                                 NULL);
        case CMETA_DATA_SINT:
            if (assign_sint_value(&source, &sint) &&
                assign_store_sint(impl->destination, destination, sint))
                return assign_report(diagnostic,
                                     CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_UINT:
            if (assign_uint_value(&source, &uint) &&
                assign_store_uint(impl->destination, destination, uint))
                return assign_report(diagnostic,
                                     CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_FLOAT:
            if (assign_float_value(&source, &number) &&
                assign_store_float(impl->destination, destination, number))
                return assign_report(diagnostic,
                                     CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
            break;
        case CMETA_DATA_ENUM:
            if (!assign_sint_value(&source, &sint)) break;
            if (cmeta_data_enum_restore_zero(impl->destination, destination) ==
                    CMETA_OK &&
                cmeta_data_enum_assign(impl->destination, destination, sint) ==
                    CMETA_OK)
                return assign_report(diagnostic,
                                     CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
            return assign_report(diagnostic,
                                 CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR, 0u,
                                 "CMeta enum assignment adapter failed");
        case CMETA_DATA_STRING:
            return assign_string(impl, destination, &source, diagnostic);
        default:
            break;
    }
    return assign_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, 0u,
                         "CMeta assignment conversion is not exact");
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_apply(
    const cflow_scxml_cmeta_assign_program *program,
    void *staged_root,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return assign_apply(program, staged_root, is_active, active_user, NULL,
                        diagnostic);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_assign_apply_with_system(
    const cflow_scxml_cmeta_assign_program *program,
    void *staged_root,
    cflow_scxml_cmeta_expr_is_active_fn is_active,
    void *active_user,
    const cflow_scxml_cmeta_expr_system_values *system_values,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    return assign_apply(program, staged_root, is_active, active_user,
                        system_values, diagnostic);
}

void cflow_scxml_cmeta_assign_program_destroy(
    cflow_scxml_cmeta_assign_program *program) {
    cflow_scxml_cmeta_assign_program_impl *impl;
    if (program == NULL || program->impl == NULL) return;
    impl = (cflow_scxml_cmeta_assign_program_impl *)program->impl;
    cflow_scxml_cmeta_expr_program_destroy(&impl->expression);
    free(impl);
    program->impl = NULL;
}
