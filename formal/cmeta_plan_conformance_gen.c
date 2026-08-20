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

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          int, conf_map_i_i_plus_one, (int x)) {
    return x + 1;
}

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_map_i_l_twice, (int x)) {
    return (long)x * 2L;
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
    if (*cursor == 0u) {
        *out = (long)x;
        *cursor = 1u;
        return CMETA_GEN_VALUE;
    }
    if (*cursor == 1u) {
        *out = (long)x + 10L;
        *cursor = 2u;
        return CMETA_GEN_VALUE_AND_DONE;
    }
    return CMETA_GEN_DONE;
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
        printf(" }%s\n", i + 1u < impl->count ? "," : "");
    }
    printf("  ] } ::\n");
    return 1;
}

static int emit_integral_values(const cmeta_type_desc *type,
                                const void *data,
                                size_t count) {
    size_t i;
    if (count && !data) return 0;
    printf("[");
    for (i = 0; i < count; ++i) {
        long long value;
        if (cmeta_type_equal(type, &cmeta_type_int))
            value = (long long)((const int *)data)[i];
        else if (cmeta_type_equal(type, &cmeta_type_long))
            value = (long long)((const long *)data)[i];
        else
            return 0;
        if (i) printf(", ");
        printf("%lld", value);
    }
    printf("]");
    return 1;
}

typedef int (*build_witness_fn)(cflow_graph *graph);

static int build_filter(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_filter(graph, conf_keep_i.fn);
}

static int build_map(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_map(graph, conf_map_i_l.fn);
}

static int build_transform(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_transform(graph, conf_transform_i_l.fn);
}

static int build_fused_map(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_map(graph, conf_map_i_l.fn) &&
           cflow_graph_map(graph, conf_map_l_d.fn);
}

static int build_fused_runtime_map(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_map(graph, conf_map_i_i_plus_one.fn) &&
           cflow_graph_map(graph, conf_map_i_l_twice.fn);
}

static int build_flat_map(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_int);
    return cflow_graph_flatMap(graph, conf_flat_i_l.fn);
}

static int build_reduce(cflow_graph *graph) {
    cflow_graph_init(graph, &cmeta_type_long);
    return cflow_graph_reduce(graph, conf_reduce_l.fn);
}

static int compile_from_builder(cflow_plan *plan, build_witness_fn build) {
    cflow_graph graph = {0};
    int ok = build(&graph) && cflow_plan_compile_surface(plan, &graph, NULL);
    cflow_graph_destroy(&graph);
    return ok;
}

static int compile_filter(cflow_plan *plan) {
    return compile_from_builder(plan, build_filter);
}

static int compile_map(cflow_plan *plan) {
    return compile_from_builder(plan, build_map);
}

static int compile_transform(cflow_plan *plan) {
    return compile_from_builder(plan, build_transform);
}

static int compile_fused_map(cflow_plan *plan) {
    return compile_from_builder(plan, build_fused_map);
}

static int compile_fused_runtime_map(cflow_plan *plan) {
    return compile_from_builder(plan, build_fused_runtime_map);
}

static int compile_flat_map(cflow_plan *plan) {
    return compile_from_builder(plan, build_flat_map);
}

static int compile_reduce(cflow_plan *plan) {
    return compile_from_builder(plan, build_reduce);
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

static int execute_and_emit(const char *name,
                            compile_witness_fn compile,
                            const cmeta_type_desc *input_type,
                            const void *inputs,
                            size_t input_count) {
    cflow_plan plan = {0};
    cflow_result result = {0};
    const char *in_token;
    const char *out_token;
    int ok = compile(&plan);
    if (!ok) {
        fprintf(stderr, "failed to compile runtime witness %s: %s\n",
                name, plan.error ? plan.error : "unknown error");
        cflow_plan_destroy(&plan);
        return 0;
    }
    if (!cmeta_type_equal(plan.input_type, input_type)) {
        fprintf(stderr, "runtime witness %s has unexpected input type\n", name);
        cflow_plan_destroy(&plan);
        return 0;
    }
    if (!cflow_plan_eval_array(&plan, inputs, input_count, &result)) {
        fprintf(stderr, "failed to execute runtime witness %s\n", name);
        cflow_plan_destroy(&plan);
        return 0;
    }

    in_token = type_token(plan.input_type);
    out_token = type_token(result.type);
    if (!in_token || !out_token) {
        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        return 0;
    }

    printf("  { name := \"%s\", inputType := \"%s\", input := ", name, in_token);
    if (!emit_integral_values(plan.input_type, inputs, input_count)) {
        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        return 0;
    }
    printf(", outputType := \"%s\", count := %zu, output := ",
           out_token, result.count);
    if (!emit_integral_values(result.type, result.data, result.count)) {
        cflow_result_destroy(&result);
        cflow_plan_destroy(&plan);
        return 0;
    }
    printf(" } ::\n");

    cflow_result_destroy(&result);
    cflow_plan_destroy(&plan);
    return 1;
}

static int differential_and_emit(const char *name,
                                 build_witness_fn build,
                                 const cmeta_type_desc *input_type,
                                 const void *inputs,
                                 size_t input_count) {
    cflow_graph graph = {0};
    cflow_plan plan = {0};
    cflow_result reference = {0};
    cflow_result direct = {0};
    const char *input_token;
    const char *reference_token;
    const char *direct_token;
    int ok = build(&graph);

    if (!ok) {
        fprintf(stderr, "failed to build differential witness %s\n", name);
        cflow_graph_destroy(&graph);
        return 0;
    }
    if (!cmeta_type_equal(cflow_graph_source_type(&graph), input_type)) {
        fprintf(stderr, "differential witness %s has unexpected input type\n", name);
        cflow_graph_destroy(&graph);
        return 0;
    }
    if (!cflow_eval_array(&graph, inputs, input_count, &reference)) {
        fprintf(stderr, "reference runtime failed for differential witness %s\n", name);
        cflow_graph_destroy(&graph);
        return 0;
    }
    if (!cflow_plan_compile_surface(&plan, &graph, NULL)) {
        fprintf(stderr, "plan compile failed for differential witness %s: %s\n",
                name, plan.error ? plan.error : "unknown error");
        cflow_result_destroy(&reference);
        cflow_graph_destroy(&graph);
        cflow_plan_destroy(&plan);
        return 0;
    }
    if (!cflow_plan_eval_array(&plan, inputs, input_count, &direct)) {
        fprintf(stderr, "plan runtime failed for differential witness %s\n", name);
        cflow_result_destroy(&reference);
        cflow_graph_destroy(&graph);
        cflow_plan_destroy(&plan);
        return 0;
    }

    input_token = type_token(input_type);
    reference_token = type_token(reference.type);
    direct_token = type_token(direct.type);
    if (!input_token || !reference_token || !direct_token) {
        cflow_result_destroy(&direct);
        cflow_result_destroy(&reference);
        cflow_graph_destroy(&graph);
        cflow_plan_destroy(&plan);
        return 0;
    }

    printf("  { name := \"%s\", inputType := \"%s\", input := ",
           name, input_token);
    if (!emit_integral_values(input_type, inputs, input_count)) goto emit_fail;
    printf(", referenceOutputType := \"%s\", referenceCount := %zu, referenceOutput := ",
           reference_token, reference.count);
    if (!emit_integral_values(reference.type, reference.data, reference.count)) goto emit_fail;
    printf(", planOutputType := \"%s\", planCount := %zu, planOutput := ",
           direct_token, direct.count);
    if (!emit_integral_values(direct.type, direct.data, direct.count)) goto emit_fail;
    printf(" } ::\n");

    cflow_result_destroy(&direct);
    cflow_result_destroy(&reference);
    cflow_graph_destroy(&graph);
    cflow_plan_destroy(&plan);
    return 1;

emit_fail:
    cflow_result_destroy(&direct);
    cflow_result_destroy(&reference);
    cflow_graph_destroy(&graph);
    cflow_plan_destroy(&plan);
    return 0;
}

int main(void) {
    static const int int_inputs[] = { -2, 0, 3 };
    static const long reduce_inputs[] = { 2L, 3L, 5L };

    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_plan_conformance_gen.c using the real CFlow plan compiler/runtime. -/");
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
    puts("structure RuntimeWitness where");
    puts("  name : String");
    puts("  inputType : String");
    puts("  input : List Int");
    puts("  outputType : String");
    puts("  count : Nat");
    puts("  output : List Int");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("structure DifferentialWitness where");
    puts("  name : String");
    puts("  inputType : String");
    puts("  input : List Int");
    puts("  referenceOutputType : String");
    puts("  referenceCount : Nat");
    puts("  referenceOutput : List Int");
    puts("  planOutputType : String");
    puts("  planCount : Nat");
    puts("  planOutput : List Int");
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
    puts("def runtimeWitnesses : List RuntimeWitness :=");

    if (!execute_and_emit("filter_i", compile_filter,
                          &cmeta_type_int, int_inputs, 3u) ||
        !execute_and_emit("map_i_l", compile_map,
                          &cmeta_type_int, int_inputs, 3u) ||
        !execute_and_emit("transform_i_l", compile_transform,
                          &cmeta_type_int, int_inputs, 3u) ||
        !execute_and_emit("fused_map_i_i_l", compile_fused_runtime_map,
                          &cmeta_type_int, int_inputs, 3u) ||
        !execute_and_emit("flat_map_i_l", compile_flat_map,
                          &cmeta_type_int, int_inputs, 3u) ||
        !execute_and_emit("reduce_l", compile_reduce,
                          &cmeta_type_long, reduce_inputs, 3u) ||
        !execute_and_emit("reduce_l_empty", compile_reduce,
                          &cmeta_type_long, NULL, 0u))
        return EXIT_FAILURE;

    puts("  []");
    puts("");
    puts("def differentialWitnesses : List DifferentialWitness :=");

    if (!differential_and_emit("filter_i", build_filter,
                               &cmeta_type_int, int_inputs, 3u) ||
        !differential_and_emit("map_i_l", build_map,
                               &cmeta_type_int, int_inputs, 3u) ||
        !differential_and_emit("transform_i_l", build_transform,
                               &cmeta_type_int, int_inputs, 3u) ||
        !differential_and_emit("fused_map_i_i_l", build_fused_runtime_map,
                               &cmeta_type_int, int_inputs, 3u) ||
        !differential_and_emit("flat_map_i_l", build_flat_map,
                               &cmeta_type_int, int_inputs, 3u) ||
        !differential_and_emit("reduce_l", build_reduce,
                               &cmeta_type_long, reduce_inputs, 3u) ||
        !differential_and_emit("reduce_l_empty", build_reduce,
                               &cmeta_type_long, NULL, 0u))
        return EXIT_FAILURE;

    puts("  []");
    puts("");
    puts("end CMeta.CPlanGenerated");
    return EXIT_SUCCESS;
}
