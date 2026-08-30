#include "cmeta_foreach.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static cflow_scxml_cmeta_expr_status foreach_report(
    cflow_scxml_cmeta_expr_diagnostic *diagnostic,
    cflow_scxml_cmeta_expr_status status, const char *message) {
    if (diagnostic != NULL) {
        memset(diagnostic, 0, sizeof(*diagnostic));
        diagnostic->status = status;
        if (message != NULL)
            (void)snprintf(diagnostic->message, sizeof(diagnostic->message),
                           "%s", message);
    }
    return status;
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_compile(
    cflow_scxml_cmeta_foreach_program *out,
    const char *array, size_t array_size,
    const char *item, size_t item_size,
    const char *index_or_null, size_t index_size,
    const cmeta_data_desc *root, size_t max_path_depth,
    size_t max_iterations,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    cflow_scxml_cmeta_foreach_program compiled = {0};
    const cmeta_type_desc *element_type;
    const cmeta_trait_flags lifecycle_traits =
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY;
    cflow_scxml_cmeta_expr_status status;
    if (out == NULL || out->sequence.root != NULL || array == NULL ||
        array_size == 0u || item == NULL || item_size == 0u ||
        (index_or_null == NULL) != (index_size == 0u) ||
        max_path_depth == 0u || max_iterations == 0u)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach compile arguments");
    status = cflow_scxml_cmeta_sequence_compile(
        &compiled.sequence, array, array_size, root, max_path_depth,
        diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    status = cflow_scxml_cmeta_location_compile(
        &compiled.item, item, item_size, root, max_path_depth, true,
        diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    element_type = compiled.sequence.element_type;
    if (!cmeta_type_equal(compiled.item.value->storage_type, element_type) ||
        compiled.item.storage_size != element_type->size)
        return foreach_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
            "CMeta foreach item requires the exact element type");
    if (cmeta_type_require_traits(
            element_type,
            CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY) ==
        CMETA_OK) {
        compiled.managed_item = false;
    } else if (cmeta_type_require_traits(element_type, lifecycle_traits) ==
               CMETA_OK) {
        compiled.managed_item = true;
    } else {
        return foreach_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
            "CMeta foreach element requires trivial storage or complete "
            "copy, move, and destroy traits");
    }
    if (index_or_null != NULL) {
        status = cflow_scxml_cmeta_location_compile(
            &compiled.index, index_or_null, index_size, root,
            max_path_depth, true, diagnostic);
        if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
        if (compiled.index.value->kind != CMETA_DATA_UINT ||
            !cmeta_type_equal(compiled.index.value->storage_type,
                              &cmeta_type_size) ||
            compiled.index.storage_size != sizeof(size_t))
            return foreach_report(
                diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
                "CMeta foreach index requires an exact size_t location");
        compiled.has_index = true;
    }
    compiled.max_iterations = max_iterations;
    *out = compiled;
    return foreach_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, NULL);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_open(
    const cflow_scxml_cmeta_foreach_program *program,
    void *staged_root, cmeta_range *out_range, size_t *out_length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    cmeta_range range = {0};
    size_t length = 0u;
    cflow_scxml_cmeta_expr_status status;
    if (program == NULL || program->sequence.root == NULL ||
        program->max_iterations == 0u || staged_root == NULL ||
        out_range == NULL || out_length == NULL)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach open arguments");
    status = cflow_scxml_cmeta_sequence_open(
        &program->sequence, staged_root, &range, &length, diagnostic);
    if (status != CFLOW_SCXML_CMETA_EXPR_OK) return status;
    if (program->managed_item &&
        (range.flags & CMETA_RANGE_CONSTRUCTS_VALUES) == 0u)
        return foreach_report(
            diagnostic, CFLOW_SCXML_CMETA_EXPR_TYPE_MISMATCH,
            "CMeta foreach managed Range must construct owned values");
    if (length > program->max_iterations)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_LIMIT_EXCEEDED,
                              "CMeta foreach iteration limit exceeded");
    *out_range = range;
    *out_length = length;
    return foreach_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, NULL);
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_value_init(
    const cflow_scxml_cmeta_foreach_program *program,
    cflow_scxml_cmeta_foreach_value *value,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    const cmeta_type_desc *type;
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;
    if (program == NULL || program->sequence.element_type == NULL ||
        value == NULL || value->allocation != NULL || value->storage != NULL ||
        value->live)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach scratch arguments");
    if (!program->managed_item)
        return foreach_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, NULL);

    type = program->sequence.element_type;
    if (type->size == 0u || type->align == 0u ||
        (type->align & (type->align - 1u)) != 0u ||
        type->size > SIZE_MAX - (type->align - 1u))
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach element storage is invalid");
    allocation_size = type->size + type->align - 1u;
    value->allocation = malloc(allocation_size);
    if (value->allocation == NULL)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                              "CMeta foreach scratch allocation failed");
    address = (uintptr_t)value->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        free(value->allocation);
        memset(value, 0, sizeof(*value));
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach scratch alignment overflowed");
    }
    aligned = (address + type->align - 1u) &
              ~((uintptr_t)type->align - 1u);
    value->storage = (void *)aligned;
    return foreach_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, NULL);
}

void cflow_scxml_cmeta_foreach_value_destroy(
    const cflow_scxml_cmeta_foreach_program *program,
    cflow_scxml_cmeta_foreach_value *value) {
    if (value == NULL) return;
    if (value->live && program != NULL &&
        program->sequence.element_type != NULL &&
        program->sequence.element_type->traits != NULL &&
        program->sequence.element_type->traits->destroy != NULL)
        program->sequence.element_type->traits->destroy(value->storage);
    free(value->allocation);
    memset(value, 0, sizeof(*value));
}

cflow_scxml_cmeta_expr_status cflow_scxml_cmeta_foreach_next(
    const cflow_scxml_cmeta_foreach_program *program,
    void *staged_root, const cmeta_range *range,
    cmeta_range_cursor *cursor, cflow_scxml_cmeta_foreach_value *value,
    size_t iteration, size_t length,
    cflow_scxml_cmeta_expr_diagnostic *diagnostic) {
    unsigned char *root = (unsigned char *)staged_root;
    const cmeta_type_desc *element_type;
    void *item;
    void *output;
    cmeta_gen_status status;
    if (program == NULL || program->sequence.root == NULL ||
        program->sequence.root->storage_type == NULL || staged_root == NULL ||
        range == NULL || cursor == NULL || value == NULL ||
        iteration >= length ||
        length > program->max_iterations ||
        program->item.offset > program->sequence.root->storage_type->size ||
        program->item.storage_size >
            program->sequence.root->storage_type->size - program->item.offset)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "invalid CMeta foreach iteration arguments");
    if (program->has_index &&
        (program->index.offset >
             program->sequence.root->storage_type->size ||
         program->index.storage_size >
             program->sequence.root->storage_type->size -
                 program->index.offset))
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach index location is invalid");

    element_type = program->sequence.element_type;
    item = root + program->item.offset;
    if (program->managed_item &&
        (cmeta_type_require_traits(
             element_type,
             CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY) !=
             CMETA_OK ||
         value->allocation == NULL || value->storage == NULL || value->live ||
         (range->flags & CMETA_RANGE_CONSTRUCTS_VALUES) == 0u))
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_INVALID_ARGUMENT,
                              "CMeta foreach managed scratch is invalid");
    output = program->managed_item ? value->storage : item;
    status = cmeta_range_next(range, cursor, output);
    if (status != CMETA_GEN_VALUE && status != CMETA_GEN_VALUE_AND_DONE)
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                              "CMeta foreach Range iteration failed");
    value->live = program->managed_item;
    if (status == CMETA_GEN_VALUE_AND_DONE && iteration + 1u != length) {
        cflow_scxml_cmeta_foreach_value_destroy(program, value);
        return foreach_report(diagnostic,
                              CFLOW_SCXML_CMETA_EXPR_EVALUATION_ERROR,
                              "CMeta foreach Range ended before its sized length");
    }
    if (program->managed_item) {
        element_type->traits->destroy(item);
        element_type->traits->move_construct(item, value->storage);
        element_type->traits->destroy(value->storage);
        value->live = false;
    }
    if (program->has_index) {
        memcpy(root + program->index.offset, &iteration, sizeof(iteration));
    }
    return foreach_report(diagnostic, CFLOW_SCXML_CMETA_EXPR_OK, NULL);
}
