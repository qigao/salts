#include <cflow/certificate.h>
#include <cflow/lower.h>
#include <cflow/plan_internal.h>

#include "dense_successor_index.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(CFLOW_PLAN_CERTIFICATE_V1 == 1u, "certificate version ABI");
_Static_assert(CFLOW_CERTIFIED_FILTER == 0, "certificate opcode ABI");
_Static_assert(CFLOW_CERTIFIED_MAP == 1, "certificate opcode ABI");
_Static_assert(CFLOW_CERTIFIED_FLAT_MAP == 2, "certificate opcode ABI");
_Static_assert(CFLOW_CERTIFIED_REDUCE == 3, "certificate opcode ABI");
_Static_assert(CFLOW_CERTIFIED_PATH_SEQUENTIAL == 0, "certificate path ABI");
_Static_assert(CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE == 1,
               "certificate path ABI");
_Static_assert(CFLOW_CERTIFICATE_ORDER_NOT_APPLICABLE == 0,
               "certificate order ABI");
_Static_assert(CFLOW_CERTIFICATE_ORDER_ENCOUNTER == 1,
               "certificate order ABI");

static uint64_t fingerprint_mix(uint64_t hash, uint64_t value) {
    hash ^= value;
    return hash * UINT64_C(1099511628211);
}

static bool certified_opcode(cflow_plan_opcode opcode, uint32_t *out) {
    if (!out) return false;
    switch (opcode) {
    case CMETA_PLAN_FILTER: *out = CFLOW_CERTIFIED_FILTER; return true;
    case CMETA_PLAN_MAP: *out = CFLOW_CERTIFIED_MAP; return true;
    case CMETA_PLAN_FLAT_MAP: *out = CFLOW_CERTIFIED_FLAT_MAP; return true;
    case CMETA_PLAN_REDUCE: *out = CFLOW_CERTIFIED_REDUCE; return true;
    }
    return false;
}

static bool graph_fingerprint(const cflow_graph *graph, uint64_t *out) {
    const cflow_subgraph *subgraph;
    cflow_dense_successor_index index = {0};
    uint64_t hash = UINT64_C(1469598103934665603);
    cflow_node_id id;
    size_t visited = 0u;

    if (!graph || !out || !cflow_graph_is_normalized(graph) ||
        graph->root >= graph->subgraph_count)
        return false;
    subgraph = &graph->subgraphs[graph->root];
    if (!subgraph->node_count || subgraph->entry >= subgraph->node_count ||
        cflow_dense_successor_index_build(&index, subgraph) !=
            CFLOW_DENSE_SUCCESSOR_INDEX_OK || index.has_fanout)
        goto fail;
    hash = fingerprint_mix(hash, subgraph->node_count);
    hash = fingerprint_mix(hash, subgraph->edge_count);
    id = subgraph->entry;
    for (;;) {
        const cflow_node *node;
        cflow_node_id successor;
        if (++visited > subgraph->node_count) goto fail;
        node = cflow_subgraph_node(subgraph, id);
        if (!node || node->op == CFLOW_OP_RELATION || node->subgraph_count)
            goto fail;
        hash = fingerprint_mix(hash, id);
        hash = fingerprint_mix(hash, (uint64_t)node->op);
        hash = fingerprint_mix(hash, node->has_fn ? node->fn.meta.effects : 0u);
        hash = fingerprint_mix(hash, node->has_fn ? node->fn.meta.properties : 0u);
        if (!cflow_dense_successor_index_successor(&index, id, &successor)) break;
        hash = fingerprint_mix(hash, successor);
        id = successor;
    }
    cflow_dense_successor_index_destroy(&index);
    *out = hash;
    return true;

fail:
    cflow_dense_successor_index_destroy(&index);
    return false;
}

void cflow_plan_certificate_destroy(cflow_plan_certificate *certificate) {
    if (!certificate) return;
    free(certificate->rows);
    memset(certificate, 0, sizeof(*certificate));
}

static bool count_rows(const cflow_plan_impl *impl, size_t *out) {
    size_t count = 0u;
    if (!impl || !out) return false;
    for (size_t pc = 0u; pc < impl->count; ++pc) {
        const size_t calls = impl->code[pc].opcode == CMETA_PLAN_MAP
            ? impl->code[pc].fn_chain_count : 1u;
        if (!calls || count > SIZE_MAX - calls) return false;
        count += calls;
    }
    *out = count;
    return true;
}

bool cflow_plan_certificate_build(cflow_plan_certificate *certificate,
                                  const cflow_graph *normalized_graph,
                                  const cflow_plan *plan,
                                  cflow_certified_path path) {
    const cflow_plan_impl *impl = plan ? (const cflow_plan_impl *)plan->impl : NULL;
    cflow_plan_certificate built = {0};
    size_t row_count;
    size_t row = 0u;

    if (!certificate || !normalized_graph || !impl ||
        !cflow_plan_graph_supported(normalized_graph) ||
        (path != CFLOW_CERTIFIED_PATH_SEQUENTIAL &&
         path != CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE) ||
        (path == CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE &&
         !cflow_plan_parallel_reduce_supported(plan)) ||
        !count_rows(impl, &row_count) ||
        row_count > SIZE_MAX / sizeof(*built.rows) ||
        impl->count > UINT32_MAX || !graph_fingerprint(
            normalized_graph, &built.graph_fingerprint))
        return false;
    built.rows = row_count
        ? (cflow_plan_certificate_row *)calloc(row_count, sizeof(*built.rows))
        : NULL;
    if (row_count && !built.rows) return false;
    built.version = CFLOW_PLAN_CERTIFICATE_V1;
    built.path = (uint32_t)path;
    built.order = path == CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE
        ? CFLOW_CERTIFICATE_ORDER_ENCOUNTER
        : CFLOW_CERTIFICATE_ORDER_NOT_APPLICABLE;
    built.required_capabilities =
        path == CFLOW_CERTIFIED_PATH_ORDERED_PARALLEL_REDUCE
            ? CMETA_EXEC_CAP_CONCURRENT : 0u;
    built.graph_version = normalized_graph->version;
    built.row_count = row_count;

    for (size_t pc = 0u; pc < impl->count; ++pc) {
        const cflow_plan_inst *inst = &impl->code[pc];
        const size_t calls = inst->opcode == CMETA_PLAN_MAP
            ? inst->fn_chain_count : 1u;
        uint32_t opcode;
        if (!certified_opcode(inst->opcode, &opcode)) goto fail;
        for (size_t call_index = 0u; call_index < calls; ++call_index) {
            const cflow_plan_call *call = inst->opcode == CMETA_PLAN_MAP
                ? &inst->fn_chain[call_index] : &inst->call;
            if (pc > UINT32_MAX || call_index > UINT32_MAX ||
                !cmeta_callable_contract_valid(call->fn))
                goto fail;
            built.rows[row++] = (cflow_plan_certificate_row){
                .opcode = opcode,
                .instruction_index = (uint32_t)pc,
                .callable_index = (uint32_t)call_index,
                .effects = call->fn.meta.effects,
                .properties = call->fn.meta.properties,
                .input_type = call->input_type ? call->input_type : inst->input_type,
                .output_type = call->output_type ? call->output_type : inst->output_type,
                .callable = call->fn
            };
        }
    }
    cflow_plan_certificate_destroy(certificate);
    *certificate = built;
    return true;

fail:
    cflow_plan_certificate_destroy(&built);
    return false;
}
