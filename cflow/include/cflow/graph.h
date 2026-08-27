#ifndef CFLOW_GRAPH_H
#define CFLOW_GRAPH_H

#include <cflow/operators.h>
#include <cflow/meta.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CMETA_INVALID_ID UINT32_MAX

typedef uint32_t cflow_node_id;
typedef uint32_t cflow_edge_id;
typedef uint32_t cflow_subgraph_id;

typedef enum cflow_op {
    CFLOW_OP_SOURCE = 0,
    /* Structural relation nodes are IR primitives, not typed-callable operators. */
    CFLOW_OP_RELATION,
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) CFLOW_OP_##E,
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
    /* Positional operators are appended to preserve existing opcode values. */
    CFLOW_OP_TAKE,
    CFLOW_OP_SKIP,
    CFLOW_OP_COUNT
} cflow_op;

typedef struct cflow_slice_parameter {
    /* Public read-only IR metadata; mutate only through Graph builders. */
    bool present;
    size_t count;
} cflow_slice_parameter;

Enum(cflow_param_rule,
    (CFLOW_PARAM_NONE,     "none"),
    (CFLOW_PARAM_INPUT,    "input"),
    (CFLOW_PARAM_SUBGRAPH, "subgraph"),
    (CFLOW_PARAM_OUT_PTR,  "out_ptr"),
    (CFLOW_PARAM_CURSOR,   "cursor")
);

Enum(cflow_return_rule,
    (CFLOW_RETURN_BOOL,      "bool"),
    (CFLOW_RETURN_VALUE,     "value"),
    (CFLOW_RETURN_INPUT,     "input"),
    (CFLOW_RETURN_GENERATOR, "generator")
);

Enum(cflow_output_rule,
    (CFLOW_OUTPUT_SAME,     "same"),
    (CFLOW_OUTPUT_RETURN,   "return"),
    (CFLOW_OUTPUT_POINTEE1, "pointee1")
);

Enum(cflow_cardinality,
    (CMETA_CARD_ONE,    "one"),
    (CMETA_CARD_FILTER, "filter"),
    (CMETA_CARD_EXPAND, "expand"),
    (CMETA_CARD_REDUCE, "reduce")
);

Enum(cflow_subgraph_rule,
    (CFLOW_SUBGRAPH_NONE, "none"),
    (CFLOW_SUBGRAPH_1TO1, "1to1")
);

typedef struct cflow_op_schema {
    cflow_op op;
    const char *method;
    unsigned method_argc;
    int fn_arg;
    int subgraph_arg;
    unsigned fn_arity;
    cflow_param_rule params[3];
    cflow_return_rule ret;
    cflow_output_rule output;
    cflow_cardinality cardinality;
    cflow_subgraph_rule subgraph_rule;
    const char *semantic;
    cmeta_effects intrinsic_effects;
} cflow_op_schema;

typedef struct cflow_edge {
    cflow_node_id from;
    uint16_t from_port;
    cflow_node_id to;
    uint16_t to_port;
} cflow_edge;

Enum(cflow_relation_coordination,
    (CFLOW_REL_COORD_ALL,      "all"),
    (CFLOW_REL_COORD_ANY,      "any"),
    (CFLOW_REL_COORD_LATEST,   "latest"),
    (CFLOW_REL_COORD_SEQUENCE, "sequence")
);

Enum(cflow_relation_completion,
    (CFLOW_REL_COMPLETE_COORDINATOR,  "coordinator"),
    (CFLOW_REL_COMPLETE_FIRST_RESULT, "first_result"),
    (CFLOW_REL_COMPLETE_ALL_DONE,     "all_done")
);

Enum(cflow_relation_error,
    (CFLOW_REL_ERROR_FAIL_FAST, "fail_fast"),
    (CFLOW_REL_ERROR_IGNORE,    "ignore"),
    (CFLOW_REL_ERROR_TRY_NEXT,  "try_next")
);

Enum(cflow_relation_result,
    (CFLOW_REL_RESULT_FOLD,   "fold"),
    (CFLOW_REL_RESULT_SELECT, "select"),
    (CFLOW_REL_RESULT_INVOKE, "invoke")
);

typedef struct cflow_relation_schema {
    cflow_relation_coordination coordination;
    cflow_relation_completion completion;
    cflow_relation_result result;
    cflow_relation_error error;
} cflow_relation_schema;

typedef struct cflow_node {
    cflow_op op;
    cmeta_callable fn;
    bool has_fn;
    /* Optimizer-owned fused map chain. Empty on ordinary source/surface IR. */
    cmeta_callable *fn_chain;
    size_t fn_chain_count;
    bool has_relation;
    cflow_relation_schema relation;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    cflow_subgraph_id *subgraphs; /* graph-owned nested IR references */
    size_t subgraph_count;
    /* Immutable Graph metadata. Mutable position belongs to each Run. */
    cflow_slice_parameter slice;
} cflow_node;

typedef struct cflow_subgraph {
    cflow_node *nodes;
    size_t node_count;
    size_t node_capacity;
    cflow_edge *edges;
    size_t edge_count;
    size_t edge_capacity;
    cflow_node_id entry;
    cflow_node_id tail;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
} cflow_subgraph;

typedef struct cflow_graph {
    cflow_subgraph *subgraphs;
    size_t subgraph_count;
    size_t subgraph_capacity;
    cflow_subgraph_id root;
    /* Process-unique nonzero mutation token. Successful clone, initialization
     * and mutation replace it; destroy restores zero. */
    uint64_t version;
    const char *error;
} cflow_graph;

const cflow_op_schema *cflow_op_schema_get(cflow_op op);
const char *cflow_op_name(cflow_op op);
bool cflow_op_signature_allowed(cflow_op op, cmeta_sig sig);


void cflow_graph_init(cflow_graph *g, const cmeta_type_desc *source_type);
void cflow_graph_destroy(cflow_graph *g);
bool cflow_graph_clone(cflow_graph *dst, const cflow_graph *src);
bool cflow_graph_validate(const cflow_graph *g, const char **error);

/* Low-level structured IR builder. Stream is only a linear façade over these
 * primitives. Detached nodes may be created in any physical order, then wired
 * by explicit typed edges. */
cflow_subgraph_id cflow_graph_create_subgraph(cflow_graph *g,
                                               const cmeta_type_desc *source_type);
bool cflow_graph_create_node(cflow_graph *g,
                             cflow_subgraph_id subgraph,
                             cflow_op op,
                             cmeta_callable fn,
                             const cflow_subgraph_id *nested,
                             size_t nested_count,
                             cflow_node_id *out_node);
/**
 * Append one detached TAKE or SKIP node to `subgraph`.
 *
 * `input_type` is borrowed with the Graph and is also the output type. `count`
 * is immutable Graph metadata; execution position belongs to each Run.
 * Returns false for invalid arguments/opcodes, version exhaustion, or
 * allocation failure. On success `out_node` receives the new node id; callers
 * wire its data edges and select the subgraph exit explicitly.
 */
bool cflow_graph_create_slice_node(cflow_graph *g,
                                   cflow_subgraph_id subgraph,
                                   cflow_op op,
                                   const cmeta_type_desc *input_type,
                                   size_t count,
                                   cflow_node_id *out_node);
bool cflow_graph_create_relation_node(cflow_graph *g,
                                      cflow_subgraph_id subgraph,
                                      const cmeta_type_desc *input_type,
                                      const cflow_subgraph_id *branches,
                                      size_t branch_count,
                                      cflow_relation_schema schema,
                                      cmeta_callable reducer,
                                      cflow_node_id *out_node);
bool cflow_graph_connect(cflow_graph *g,
                         cflow_subgraph_id subgraph,
                         cflow_node_id from,
                         uint16_t from_port,
                         cflow_node_id to,
                         uint16_t to_port);
bool cflow_graph_set_subgraph_exit(cflow_graph *g,
                                   cflow_subgraph_id subgraph,
                                   cflow_node_id exit_node);

bool cflow_graph_add(cflow_graph *g, cflow_op op, cmeta_callable fn,
                     const cflow_graph *nested_graph);
/** Append a linear TAKE node that forwards at most `limit` arriving values. */
bool cflow_graph_take(cflow_graph *g, size_t limit);
/** Append a linear SKIP node that drops the first `count` arriving values. */
bool cflow_graph_skip(cflow_graph *g, size_t count);

/* Data-driven structured relation. Every branch is snapshot-imported as a
 * Subgraph and receives the current value at the relation node. The relation
 * schema chooses coordination, completion and result semantics. For FOLD,
 * reducer must be a policy-enabled homogeneous T(T,T)->T callable. SELECT
 * requires homogeneous branch output type but no reducer. */
bool cflow_graph_relation(cflow_graph *g,
                          const cflow_graph *const *branches,
                          size_t branch_count,
                          cflow_relation_schema schema,
                          cmeta_callable reducer);

static inline cflow_relation_schema cflow_relation_all_fold(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_ALL,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_FOLD,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}
static inline cflow_relation_schema cflow_relation_fork_join_fold(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_ALL,
        CFLOW_REL_COMPLETE_ALL_DONE, CFLOW_REL_RESULT_FOLD,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}
static inline cflow_relation_schema cflow_relation_any_select(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_ANY,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}
static inline cflow_relation_schema cflow_relation_latest_fold(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_LATEST,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_FOLD,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}
static inline cflow_relation_schema cflow_relation_sequence_select(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_SEQUENCE,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}
static inline cflow_relation_schema cflow_relation_fallback(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_SEQUENCE,
        CFLOW_REL_COMPLETE_FIRST_RESULT, CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_TRY_NEXT };
    return schema;
}
static inline cflow_relation_schema cflow_relation_all_invoke(void) {
    const cflow_relation_schema schema = { CFLOW_REL_COORD_ALL,
        CFLOW_REL_COMPLETE_COORDINATOR, CFLOW_REL_RESULT_INVOKE,
        CFLOW_REL_ERROR_FAIL_FAST };
    return schema;
}



#define CMETA_GRAPH_PROTO_1(method) \
    bool cflow_graph_##method(cflow_graph *g, cmeta_callable fn);
#define CMETA_GRAPH_PROTO_2(method) \
    bool cflow_graph_##method(cflow_graph *g, const cflow_graph *other, cmeta_callable fn);
#define CMETA_GRAPH_PROTO_I(n, method) CMETA_GRAPH_PROTO_##n(method)
#define CMETA_GRAPH_PROTO(n, method) CMETA_GRAPH_PROTO_I(n, method)
#define CFLOW_OP_ROW(E, method, margc, fnarg, subgrapharg, farity, p0, p1, p2, ret, out, card, subgraphrule, semantic, intrinsic_effects) \
    CMETA_GRAPH_PROTO(margc, method)
Replay(CFlowOperators, CFLOW_OP_ROW)
#undef CFLOW_OP_ROW
#undef CMETA_GRAPH_PROTO
#undef CMETA_GRAPH_PROTO_I
#undef CMETA_GRAPH_PROTO_1
#undef CMETA_GRAPH_PROTO_2

const cflow_subgraph *cflow_graph_subgraph(const cflow_graph *g, cflow_subgraph_id id);
const cflow_node *cflow_subgraph_node(const cflow_subgraph *sg, cflow_node_id id);
const cflow_edge *cflow_subgraph_edge(const cflow_subgraph *sg, cflow_edge_id id);
size_t cflow_subgraph_out_degree(const cflow_subgraph *sg, cflow_node_id node);
bool cflow_subgraph_single_successor(const cflow_subgraph *sg,
                                     cflow_node_id node,
                                     cflow_node_id *successor);

const cmeta_type_desc *cflow_subgraph_source_type(const cflow_graph *g, cflow_subgraph_id id);
const cmeta_type_desc *cflow_subgraph_output_type(const cflow_graph *g, cflow_subgraph_id id);
bool cflow_subgraph_is_one_to_one(const cflow_graph *g, cflow_subgraph_id id);

const cmeta_type_desc *cflow_graph_source_type(const cflow_graph *g);
const cmeta_type_desc *cflow_graph_output_type(const cflow_graph *g);
bool cflow_graph_is_one_to_one(const cflow_graph *g);

#ifdef __cplusplus
}
#endif

#endif
