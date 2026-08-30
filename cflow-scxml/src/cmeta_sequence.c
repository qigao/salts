#include "cmeta_sequence.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static cflow_scxml_cmeta_expr_status sequence_report(
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

static bool sequence_decode_utf8(const char *data, size_t size,
                                 size_t *cursor, uint32_t *out_codepoint) {
    const size_t start = *cursor;
    const unsigned char lead = (unsigned char)data[start];
    size_t width;
    size_t index;
    uint32_t value;
    if (lead <= 0x7fu) {
        *out_codepoint = lead;
        *cursor = start + 1u;
        return true;
    }
    if (lead >= 0xc2u && lead <= 0xdfu) {
        width = 2u;
        value = lead & 0x1fu;
    } else if (lead >= 0xe0u && lead <= 0xefu) {
        width = 3u;
        value = lead & 0x0fu;
    } else if (lead >= 0xf0u && lead <= 0xf4u) {
        width = 4u;
        value = lead & 0x07u;
    } else {
        return false;
    }
    if (width > size - start) return false;
    for (index = 1u; index < width; ++index) {
        const unsigned char continuation = (unsigned char)data[start + index];
        if ((continuation & 0xc0u) != 0x80u) return false;
        value = (value << 6u) | (continuation & 0x3fu);
    }
    if ((width == 3u && value < 0x800u) ||
        (width == 4u && value < 0x10000u) ||
        (value >= 0xd800u && value <= 0xdfffu) || value > 0x10ffffu)
        return false;
    *out_codepoint = value;
    *cursor = start + width;
    return true;
}

static bool sequence_ncname_start(uint32_t codepoint) {
    return codepoint == '_' || (codepoint >= 'A' && codepoint <= 'Z') ||
           (codepoint >= 'a' && codepoint <= 'z') ||
           (codepoint >= 0xc0u && codepoint <= 0xd6u) ||
           (codepoint >= 0xd8u && codepoint <= 0xf6u) ||
           (codepoint >= 0xf8u && codepoint <= 0x2ffu) ||
           (codepoint >= 0x370u && codepoint <= 0x37du) ||
           (codepoint >= 0x37fu && codepoint <= 0x1fffu) ||
           (codepoint >= 0x200cu && codepoint <= 0x200du) ||
           (codepoint >= 0x2070u && codepoint <= 0x218fu) ||
           (codepoint >= 0x2c00u && codepoint <= 0x2fefu) ||
           (codepoint >= 0x3001u && codepoint <= 0xd7ffu) ||
           (codepoint >= 0xf900u && codepoint <= 0xfdcfu) ||
           (codepoint >= 0xfdf0u && codepoint <= 0xfffdu) ||
           (codepoint >= 0x10000u && codepoint <= 0xeffffu);
}

static bool sequence_ncname_continue(uint32_t codepoint) {
    return sequence_ncname_start(codepoint) || codepoint == '-' ||
           (codepoint >= '0' && codepoint <= '9') || codepoint == 0xb7u ||
           (codepoint >= 0x300u && codepoint <= 0x36fu) ||
           (codepoint >= 0x203fu && codepoint <= 0x2040u);
}

static bool sequence_path_valid(const char *path, size_t path_size,
                                size_t *out_error_offset) {
    size_t cursor = 0u;
    bool segment_start = true;
    while (cursor < path_size) {
        uint32_t codepoint;
        const size_t offset = cursor;
        if (path[cursor] == '.') {
            if (segment_start) {
                *out_error_offset = cursor;
                return false;
            }
            segment_start = true;
            ++cursor;
            continue;
        }
        if (!sequence_decode_utf8(path, path_size, &cursor, &codepoint) ||
            (segment_start ? !sequence_ncname_start(codepoint)
                           : !sequence_ncname_continue(codepoint))) {
            *out_error_offset = offset;
            return false;
        }
        segment_start = false;
    }
    if (segment_start) {
        *out_error_offset = path_size;
        return false;
    }
    return true;
}

static const cmeta_data_field_desc *sequence_find_data_field(
    const cmeta_data_struct_shape *shape, const char *name, size_t size) {
    size_t index;
    if (shape == NULL || name == NULL) return NULL;
    for (index = 0u; index < shape->field_count; ++index) {
        const cmeta_data_field_desc *field = &shape->fields[index];
        if (field->name != NULL && strlen(field->name) == size &&
            memcmp(field->name, name, size) == 0)
            return field;
    }
    return NULL;
}

static const cmeta_field_desc *sequence_find_layout_field(
    const cmeta_struct_desc *layout, const char *name, size_t size) {
    size_t index;
    if (layout == NULL || name == NULL) return NULL;
    for (index = 0u; index < layout->field_count; ++index) {
        const cmeta_field_desc *field = &layout->fields[index];
        if (field->name != NULL && strlen(field->name) == size &&
            memcmp(field->name, name, size) == 0)
            return field;
    }
    return NULL;
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_sequence_compile(
    cflow_scxml_cmeta_sequence_program *out,
    const char *location, size_t location_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cmeta_data_desc *current = root;
    const cmeta_data_field_desc *selected_data = NULL;
    const cmeta_field_desc *selected_layout = NULL;
    size_t absolute_offset = 0u;
    size_t segment_start = 0u;
    size_t depth = 0u;
    size_t index;
    size_t lexical_error = 0u;
    cflow_scxml_cmeta_sequence_program compiled = {0};
    if (out == NULL || out->root != NULL || location == NULL ||
        location_size == 0u || max_path_depth == 0u ||
        !cmeta_data_desc_valid(root) || root->kind != CMETA_DATA_STRUCT ||
        root->storage_type == NULL)
        return sequence_report(diagnostic,
                               CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT, 0u,
                               "invalid CMeta sequence compile arguments");
    if (!sequence_path_valid(location, location_size, &lexical_error))
        return sequence_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_SYNTAX_ERROR, lexical_error,
            "CMeta sequence location is not a dotted NCName path");
    for (index = 0u; index <= location_size; ++index) {
        const bool at_end = index == location_size;
        const cmeta_data_struct_shape *shape;
        size_t field_size;
        if (!at_end && location[index] != '.') continue;
        if (++depth > max_path_depth)
            return sequence_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED, index,
                "CMeta sequence path depth limit exceeded");
        if (!cmeta_data_desc_valid(current) ||
            current->kind != CMETA_DATA_STRUCT || current->shape == NULL ||
            current->storage_type == NULL)
            return sequence_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                segment_start,
                "CMeta sequence location traverses a non-struct value");
        shape = (const cmeta_data_struct_shape *)current->shape;
        field_size = index - segment_start;
        selected_data = sequence_find_data_field(
            shape, location + segment_start, field_size);
        selected_layout = sequence_find_layout_field(
            shape->layout, location + segment_start, field_size);
        if (selected_data == NULL || selected_layout == NULL ||
            selected_data->offset != selected_layout->offset ||
            !cmeta_data_desc_valid(selected_data->value) ||
            selected_layout->type == NULL ||
            !cmeta_type_desc_valid(selected_layout->type) ||
            selected_layout->size != selected_layout->type->size ||
            selected_layout->offset > current->storage_type->size ||
            selected_layout->size >
                current->storage_type->size - selected_layout->offset ||
            absolute_offset > SIZE_MAX - selected_layout->offset)
            return sequence_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                segment_start, "CMeta sequence location is unresolved");
        absolute_offset += selected_layout->offset;
        if (absolute_offset > root->storage_type->size ||
            selected_layout->size >
                root->storage_type->size - absolute_offset)
            return sequence_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                segment_start,
                "CMeta sequence location exceeds root storage");
        current = selected_data->value;
        if (at_end) break;
        if (current->storage_type == NULL ||
            !cmeta_type_equal(current->storage_type,
                              selected_layout->type))
            return sequence_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_UNKNOWN_LOCATION,
                segment_start,
                "CMeta sequence location traverses abstract storage");
        segment_start = index + 1u;
    }
    if (selected_data == NULL || selected_layout == NULL ||
        current->kind != CMETA_DATA_SEQUENCE ||
        current->storage_type != NULL || current->shape != NULL ||
        !cmeta_declared_type_valid(selected_layout->declared_type) ||
        selected_layout->declared_type->arity != 1u ||
        !cmeta_type_equal(selected_layout->declared_type->storage_type,
                          selected_layout->type))
        return sequence_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, segment_start,
            "CMeta sequence location is not a declared unary sequence");
    compiled.root = root;
    compiled.container_type = selected_layout->type;
    compiled.element_type = cmeta_declared_type_argument(
        selected_layout->declared_type, 0u);
    compiled.offset = absolute_offset;
    compiled.storage_size = selected_layout->size;
    if (!cmeta_type_desc_valid(compiled.element_type))
        return sequence_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, segment_start,
            "CMeta sequence element type is invalid");
    *out = compiled;
    return sequence_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_sequence_open(
    const cflow_scxml_cmeta_sequence_program *program,
    const void *root_object, cmeta_range *out_range, size_t *out_length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const unsigned char *root_bytes = (const unsigned char *)root_object;
    const void *container;
    const cmeta_data_desc *semantic;
    const cmeta_type_desc *runtime_element;
    cmeta_range range;
    size_t length;
    const cmeta_range_flags required =
        CMETA_RANGE_SIZED | CMETA_RANGE_ORDERED;
    if (program == NULL || program->root == NULL ||
        program->root->storage_type == NULL ||
        program->container_type == NULL || program->element_type == NULL ||
        root_object == NULL || out_range == NULL || out_length == NULL ||
        program->offset > program->root->storage_type->size ||
        program->storage_size >
            program->root->storage_type->size - program->offset ||
        program->storage_size != program->container_type->size)
        return sequence_report(diagnostic,
                               CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT, 0u,
                               "invalid CMeta sequence open arguments");
    container = root_bytes + program->offset;
    semantic = cmeta_container_data(container);
    if (!cmeta_data_desc_valid(semantic) ||
        semantic->kind != CMETA_DATA_SEQUENCE ||
        !cmeta_container_type_application_valid(container) ||
        cmeta_container_type_arity(container) != 1u) {
        return sequence_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, 0u,
            "CMeta sequence runtime container contract mismatched");
    }
    runtime_element = cmeta_container_type_argument(container, 0u);
    if (!cmeta_type_equal(runtime_element, program->element_type) ||
        !cmeta_container_range_view(
            container, CMETA_CONTAINER_VIEW_DEFAULT, &range) ||
        !cmeta_type_equal(range.element_type, program->element_type) ||
        (range.flags & required) != required || range.size == NULL) {
        return sequence_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH, 0u,
            "CMeta sequence Range contract mismatched");
    }
    length = cmeta_range_size(&range);
    *out_range = range;
    *out_length = length;
    return sequence_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, 0u, NULL);
}
