#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cflow/graph.h>
#include <cflow/meta.h>
#include <cflow/plan.h>
#include <cflow/plan_internal.h>

typed_raw(filter, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          _Bool, conf_keep_i, (int x)) {
    return x != 0;
}

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_map_i_l, (int x)) {
    return (long)x;
}

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          double, conf_map_l_d, (long x)) {
    return (double)x;
}

typed_raw(transform, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_transform_i_l, (int x)) {
    return (long)x;
}

typed_raw(flatMap, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          cmeta_gen_status, conf_flat_i_l,
          (int x, long *out, size_t *cursor)) {
    if (!out || !cursor) return CMETA_GEN_ERROR;
    if (*cursor != 0u) return CMETA_GEN_DONE;
    *out = (long)x;
    *cursor = 1u;
    return CMETA_GEN_VALUE_AND_DONE;
}

typed_raw(reduce, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
              CMETA_PROP_ASSOCIATIVE | CMETA_PROP_NO_ALIAS,
          long, conf_reduce_l, (long a, long b)) {
    return a + b;
}

static const char *type_token(const cmeta_type_desc *type) {
    if (cmeta_type_equal(type, &cmeta_type_bool)) return "B";
    if (cmeta_type_equal(type, &cmeta_type_int)) return "I";
    if (cmeta_type_equal(type, &cmeta_type_long)) return "L";
    if (cmeta_type_equal(type, &cmeta_type_float)) return "F";
    if (cmeta_type_equal(type, &cmeta_type_double)) return "D";
    return NULL;
}

static const char *opcode_name(cflow_plan_opcode opcode) {
    switch (opcode) {
        case CMETA_PLAN_FILTER: return "FILTER";
        case CMETA_PLAN_MAP: return "MAP";
        case CMETA_PLAN_FLAT_MAP: return "FLAT_MAP";
        case CMETA_PLAN_REDUCE: return "REDUCE";
    }
    return NULL;
}

static const char *signature_id(cmeta_callable fn) {
    const cmeta_sig_desc *sig = cmeta_callable_signature(fn);
    const char *symbol;
    static const char prefix[] = "CMETA_SIG_";
    if (!sig) return NULL;
    symbol = cmeta_sig_to_symbol(sig->sig);
    if (!symbol || strncmp(symbol, prefix, sizeof(prefix) - 1u) != 0) return NULL;
    return symbol + sizeof(prefix) - 1u;
}

static int emit_callbacks(const cflow_plan_inst *inst) {
    size_t i;
    printf("[");
    if (inst->opcode == CMETA_PLAN_MAP) {
        if (!inst->fn_chain || !inst->fn_chain_count) return 0;
        for (i = 0; i < inst->fn_chain_count; ++i) {
            const char *id = signature_id(inst->fn_chain[i]);
            if (!id) return 0;
            if (i) printf(", ");
            printf("\"%s\"", id);
        }
    } else {
        const char *id = signature_id(inst->fn);
        if (!id) return 0;
        printf("\"%s\"", id);
    }
    printf("]");
    return 1;
}

static int emit_plan(const char *name, const cflow_plan *plan) {
    const cflow_plan_impl *impl = plan ? (const cflow_plan_impl *)plan->impl : NULL;
    const char *input = plan ? type_token(plan->input_type) : NULL;
    const char *output = plan ? type_token(plan->output_type) : NULL;
    size_t i;
    if (!impl || !input || !output) return 0;

    printf("  { name := \"%s\", input := \"%s\", output := \"%s\", code := [\n",
           name, input, output);
    for (i = 0; i < impl->count; ++i) {
        const cflow_plan_inst *inst = &impl->code[i];
        const char *opcode = opcode_name(inst->opcode);
        const char *inst_input = type_token(inst->input_type);
        const char *inst_output = type_token(inst->output_type);
        if (!opcode || !inst_input || !inst_output) return 0;
        printf("    { opcode := \"%s\", input := \"%s\", output := \"%s\", callbacks := ",
               opcode, inst_input, inst_output);
        if (!emit_callbacks(inst)) return 0;
        printf(" },\n");
    }
    printf("  ] } ::\n");
    return 1;
}

static int compile_filter(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_int);
    ok = cflow_graph_filter(&graph, conf_keep_i.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_map(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_int);
    ok = cflow_graph_map(&graph, conf_map_i_l.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_transform(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_int);
    ok = cflow_graph_transform(&graph, conf_transform_i_l.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_fused_map(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_int);
    ok = cflow_graph_map(&graph, conf_map_i_l.fn) &&
         cflow_graph_map(&graph, conf_map_l_d.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_flat_map(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_int);
    ok = cflow_graph_flatMap(&graph, conf_flat_i_l.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_reduce(cflow_plan *plan) {
    cflow_graph graph = {0};
    int ok;
    cflow_graph_init(&graph, &cmeta_type_long);
    ok = cflow_graph_reduce(&graph, conf_reduce_l.fn) &&
         cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

typedef int (*compile_witness_fn)(cflow_plan *plan);

static int compile_and_emit(const char *name, compile_witness_fn compile) {
    cflow_plan plan = {0};
    int ok = compile(&plan);
    if (!ok) {
        fprintf(stderr, "failed to compile witness %s: %s\n",
                name, plan.error ? plan.error : "unknown error");
        cflow_plan_destroy(&plan);
        return 0;
    }
    ok = emit_plan(name, &plan);
    cflow_plan_destroy(&plan);
    if (!ok) fprintf(stderr, "failed to emit witness %s\n", name);
    return ok;
}

int main(void) {
    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_plan_conformance_gen.c using the real CFlow plan compiler. -/");
    puts("");
    puts("namespace CMeta.CPlanGenerated");
    puts("");
    puts("structure PlanInstRow where");
    puts("  opcode : String");
    puts("  input : String");
    puts("  output : String");
    puts("  callbacks : List String");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("structure PlanWitness where");
    puts("  name : String");
    puts("  input : String");
    puts("  output : String");
    puts("  code : List PlanInstRow");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("def witnesses : List PlanWitness :=");

    if (!compile_and_emit("filter_i", compile_filter) ||
        !compile_and_emit("map_i_l", compile_map) ||
        !compile_and_emit("transform_i_l", compile_transform) ||
        !compile_and_emit("fused_map_i_l_d", compile_fused_map) ||
        !compile_and_emit("flat_map_i_l", compile_flat_map) ||
        !compile_and_emit("reduce_l", compile_reduce))
        return EXIT_FAILURE;

    puts("  []");
    puts("");
    puts("end CMeta.CPlanGenerated");
    return EXIT_SUCCESS;
}
