#ifndef CFLOW_VALUE_STORAGE_H
#define CFLOW_VALUE_STORAGE_H

#include <cflow/graph.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct cflow_value_slot {
    void *allocation;
    void *storage;
    const cmeta_type_desc *type;
    bool live;
} cflow_value_slot;

static inline bool cflow_value_storage_type_supported(
    const cmeta_type_desc *type) {
    const cmeta_trait_flags required = CMETA_TRAIT_TRIVIAL_COPY |
                                       CMETA_TRAIT_TRIVIAL_DESTROY;

    return cmeta_type_require_traits(type, required) == CMETA_OK;
}

static inline bool cflow_value_lifecycle_type_supported(
    const cmeta_type_desc *type) {
    const cmeta_trait_flags required = CMETA_TRAIT_COPY |
                                       CMETA_TRAIT_MOVE |
                                       CMETA_TRAIT_DESTROY;

    return cmeta_type_require_traits(type, required) == CMETA_OK;
}

static inline bool cflow_value_type_supported(const cmeta_type_desc *type) {
    return cflow_value_storage_type_supported(type) ||
           cflow_value_lifecycle_type_supported(type);
}

static inline bool cflow_value_slot_init(cflow_value_slot *slot,
                                         const cmeta_type_desc *type) {
    size_t allocation_size;
    uintptr_t address;
    uintptr_t aligned;

    if (!slot || slot->allocation || slot->storage || slot->type ||
        slot->live || !cmeta_type_desc_valid(type) ||
        !cflow_value_type_supported(type) || type->size == 0u ||
        type->align == 0u || (type->align & (type->align - 1u)) != 0u ||
        type->size > SIZE_MAX - (type->align - 1u))
        return false;

    allocation_size = type->size + type->align - 1u;
    slot->allocation = malloc(allocation_size);
    if (!slot->allocation)
        return false;

    address = (uintptr_t)slot->allocation;
    if (address > UINTPTR_MAX - (type->align - 1u)) {
        free(slot->allocation);
        memset(slot, 0, sizeof(*slot));
        return false;
    }
    aligned = (address + type->align - 1u) &
              ~((uintptr_t)type->align - 1u);
    slot->storage = (void *)aligned;
    slot->type = type;
    return true;
}

static inline bool cflow_value_slot_copy(cflow_value_slot *slot,
                                         const void *source) {
    if (!slot || !slot->storage || !slot->type || slot->live || !source)
        return false;

    if (cflow_value_storage_type_supported(slot->type)) {
        memcpy(slot->storage, source, slot->type->size);
    } else if (!slot->type->traits->copy_construct(slot->storage, source)) {
        return false;
    }
    slot->live = true;
    return true;
}

static inline bool cflow_value_slot_move(cflow_value_slot *destination,
                                         cflow_value_slot *source) {
    if (!destination || !source || destination == source ||
        !destination->storage || !source->storage || !destination->type ||
        !source->type || destination->live || !source->live ||
        !cmeta_type_equal(destination->type, source->type))
        return false;

    if (cflow_value_storage_type_supported(source->type)) {
        memcpy(destination->storage, source->storage, source->type->size);
    } else {
        source->type->traits->move_construct(destination->storage,
                                             source->storage);
        source->type->traits->destroy(source->storage);
    }
    destination->live = true;
    source->live = false;
    return true;
}

static inline void cflow_value_slot_reset(cflow_value_slot *slot) {
    if (!slot || !slot->live)
        return;
    if (!cflow_value_storage_type_supported(slot->type))
        slot->type->traits->destroy(slot->storage);
    slot->live = false;
}

static inline void cflow_value_slot_destroy(cflow_value_slot *slot) {
    if (!slot)
        return;
    cflow_value_slot_reset(slot);
    free(slot->allocation);
    memset(slot, 0, sizeof(*slot));
}

static inline bool cflow_value_runtime_graph_supported(
    const cflow_graph *graph) {
    bool requires_lifecycle = false;
    size_t subgraph_index;

    if (!graph || (graph->subgraph_count != 0u && !graph->subgraphs))
        return false;
    for (subgraph_index = 0u; subgraph_index < graph->subgraph_count;
         ++subgraph_index) {
        const cflow_subgraph *subgraph = &graph->subgraphs[subgraph_index];
        size_t node_index;

        if (subgraph->node_count != 0u && !subgraph->nodes)
            return false;
        if (!cflow_value_type_supported(subgraph->input_type) ||
            !cflow_value_type_supported(subgraph->output_type))
            return false;
        requires_lifecycle = requires_lifecycle ||
            !cflow_value_storage_type_supported(subgraph->input_type) ||
            !cflow_value_storage_type_supported(subgraph->output_type);
        for (node_index = 0u; node_index < subgraph->node_count;
             ++node_index) {
            const cflow_node *node = &subgraph->nodes[node_index];

            if (!cflow_value_type_supported(node->input_type) ||
                !cflow_value_type_supported(node->output_type))
                return false;
            requires_lifecycle = requires_lifecycle ||
                !cflow_value_storage_type_supported(node->input_type) ||
                !cflow_value_storage_type_supported(node->output_type);
        }
    }
    if (!requires_lifecycle)
        return true;

    for (subgraph_index = 0u; subgraph_index < graph->subgraph_count;
         ++subgraph_index) {
        const cflow_subgraph *subgraph = &graph->subgraphs[subgraph_index];

        if (subgraph->node_count != 1u ||
            subgraph->nodes[0].op != CFLOW_OP_SOURCE)
            return false;
    }
    return true;
}

static inline bool cflow_value_storage_graph_supported(
    const cflow_graph *graph) {
    size_t subgraph_index;

    if (!graph || (graph->subgraph_count != 0u && !graph->subgraphs))
        return false;
    for (subgraph_index = 0u; subgraph_index < graph->subgraph_count;
         ++subgraph_index) {
        const cflow_subgraph *subgraph = &graph->subgraphs[subgraph_index];
        size_t node_index;

        if (subgraph->node_count != 0u && !subgraph->nodes)
            return false;
        if (!cflow_value_storage_type_supported(subgraph->input_type) ||
            !cflow_value_storage_type_supported(subgraph->output_type))
            return false;
        for (node_index = 0u; node_index < subgraph->node_count;
             ++node_index) {
            const cflow_node *node = &subgraph->nodes[node_index];

            if (!cflow_value_storage_type_supported(node->input_type) ||
                !cflow_value_storage_type_supported(node->output_type))
                return false;
        }
    }
    return true;
}

#endif
