#ifndef CFLOW_PLAN_INTERNAL_H
#define CFLOW_PLAN_INTERNAL_H

#include <cflow/plan.h>

typedef enum cflow_plan_opcode {
    CMETA_PLAN_FILTER,
    CMETA_PLAN_MAP,
    CMETA_PLAN_FLAT_MAP,
    CMETA_PLAN_REDUCE,
    CMETA_PLAN_TAKE,
    CMETA_PLAN_SKIP
} cflow_plan_opcode;

typedef struct cflow_plan_value_vec {
    unsigned char *data;
    size_t count;
    const cmeta_type_desc *type;
} cflow_plan_value_vec;

typedef struct cflow_plan_inst cflow_plan_inst;
typedef bool (*cflow_plan_step_fn)(const cflow_plan_inst *, cflow_plan_value_vec *);

typedef enum cflow_plan_unary_batch_mode {
    CFLOW_PLAN_BATCH_FILTER,
    CFLOW_PLAN_BATCH_MAP
} cflow_plan_unary_batch_mode;

typedef struct cflow_plan_call cflow_plan_call;
typedef bool (*cflow_plan_unary_batch_fn)(const cflow_plan_call *,
                                          cflow_plan_unary_batch_mode,
                                          const unsigned char *,
                                          size_t,
                                          unsigned char *,
                                          unsigned char *,
                                          size_t *);

struct cflow_plan_call {
    cmeta_callable fn;
    cmeta_callable_invoke_fn invoke;
    cflow_plan_unary_batch_fn raw_batch;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
};

struct cflow_plan_inst {
    cflow_plan_opcode opcode;
    cflow_plan_step_fn step;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    cflow_plan_call call;
    cflow_plan_call *fn_chain;
    size_t fn_chain_count;
    bool has_size_parameter;
    size_t size_parameter;
};

typedef struct cflow_plan_impl {
    cflow_plan_inst *code;
    size_t count;
    size_t terminal_reduce_index;
    bool parallel_reduce_supported;
    bool fused_value;
    size_t fused_filter_count;
    size_t fused_map_call_count;
} cflow_plan_impl;

typedef struct cflow_plan_eval_stats {
    bool fused_value_path;
    size_t allocation_calls;
    size_t allocated_bytes;
    size_t peak_live_bytes;
    size_t selection_bytes;
    size_t intermediate_bytes;
    size_t result_bytes;
    size_t staged_input_copy_bytes;
    size_t raw_batch_stage_calls;
    size_t adapter_item_calls;
} cflow_plan_eval_stats;

cflow_plan_step_fn cflow_plan_step_for_opcode(cflow_plan_opcode opcode);
cflow_plan_unary_batch_fn cflow_plan_unary_batch_for_signature(cmeta_sig sig);

bool cflow_plan_eval_array_profile(const cflow_plan *plan,
                                   const void *inputs,
                                   size_t input_count,
                                   cflow_result *out,
                                   cflow_plan_eval_stats *stats);

bool cflow_plan_eval_prefix_materialized(const cflow_plan *plan,
                                         const void *inputs,
                                         size_t input_count,
                                         size_t instruction_count,
                                         cflow_plan_value_vec *out);
void cflow_plan_value_vec_destroy(cflow_plan_value_vec *values);

#endif
