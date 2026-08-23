#ifndef CFLOW_PLAN_INTERNAL_H
#define CFLOW_PLAN_INTERNAL_H

#include <cflow/plan.h>

typedef enum cflow_plan_opcode {
    CMETA_PLAN_FILTER,
    CMETA_PLAN_MAP,
    CMETA_PLAN_FLAT_MAP,
    CMETA_PLAN_REDUCE
} cflow_plan_opcode;

typedef struct cflow_plan_value_vec {
    unsigned char *data;
    size_t count;
    const cmeta_type_desc *type;
} cflow_plan_value_vec;

typedef struct cflow_plan_inst cflow_plan_inst;
typedef bool (*cflow_plan_step_fn)(const cflow_plan_inst *, cflow_plan_value_vec *);

typedef struct cflow_plan_call {
    cmeta_callable fn;
    cmeta_callable_invoke_fn invoke;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
} cflow_plan_call;

struct cflow_plan_inst {
    cflow_plan_opcode opcode;
    cflow_plan_step_fn step;
    const cmeta_type_desc *input_type;
    const cmeta_type_desc *output_type;
    cflow_plan_call call;
    cflow_plan_call *fn_chain;
    size_t fn_chain_count;
};

typedef struct cflow_plan_impl {
    cflow_plan_inst *code;
    size_t count;
} cflow_plan_impl;

cflow_plan_step_fn cflow_plan_step_for_opcode(cflow_plan_opcode opcode);

#endif
