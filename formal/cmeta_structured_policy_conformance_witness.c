#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cflow/adapters.h>
#include <cflow/graph.h>
#include <cflow/lower.h>
#include <cflow/meta.h>
#include <cflow/plan.h>

typed_raw(flatMap, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          cmeta_gen_status, conf_policy_left_expand_i_l,
          (int x, long *out, size_t *cursor)) {
    if (!out || !cursor) return CMETA_GEN_ERROR;
    if (*cursor == 0u) {
        *out = (long)x;
        *cursor = 1u;
        return CMETA_GEN_VALUE;
    }
    if (*cursor == 1u) {
        *out = (long)x + 1L;
        *cursor = 2u;
        return CMETA_GEN_VALUE_AND_DONE;
    }
    return CMETA_GEN_DONE;
}

typed_raw(flatMap, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          cmeta_gen_status, conf_policy_right_expand_i_l,
          (int x, long *out, size_t *cursor)) {
    if (!out || !cursor) return CMETA_GEN_ERROR;
    if (*cursor == 0u) {
        *out = (long)x * 10L;
        *cursor = 1u;
        return CMETA_GEN_VALUE;
    }
    if (*cursor == 1u) {
        *out = (long)x * 10L + 100L;
        *cursor = 2u;
        return CMETA_GEN_VALUE_AND_DONE;
    }
    return CMETA_GEN_DONE;
}

typed_raw(flatMap, CMETA_EFFECT_MAY_FAIL,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS,
          cmeta_gen_status, conf_policy_fail_i_l,
          (int x, long *out, size_t *cursor)) {
    (void)x;
    (void)out;
    (void)cursor;
    return CMETA_GEN_ERROR;
}

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_policy_fallback_i_l, (int x)) {
    return (long)x * 10L;
}

typed_raw(reduce, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
              CMETA_PROP_ASSOCIATIVE | CMETA_PROP_NO_ALIAS,
          long, conf_policy_add_l, (long a, long b)) {
    return a + b;
}

static const char *type_token(const cmeta_type_desc *type) {
    if (cmeta_type_equal(type, &cmeta_type_int)) return "I";
    if (cmeta_type_equal(type, &cmeta_type_long)) return "L";
    return NULL;
}

static const char *coordination_name(cflow_relation_coordination value) {
    switch (value) {
        case CFLOW_REL_COORD_ALL: return "ALL";
        case CFLOW_REL_COORD_ANY: return "ANY";
        case CFLOW_REL_COORD_LATEST: return "LATEST";
        case CFLOW_REL_COORD_SEQUENCE: return "SEQUENCE";
    }
    return NULL;
}

static const char *completion_name(cflow_relation_completion value) {
    switch (value) {
        case CFLOW_REL_COMPLETE_COORDINATOR: return "COORDINATOR";
        case CFLOW_REL_COMPLETE_FIRST_RESULT: return "FIRST_RESULT";
        case CFLOW_REL_COMPLETE_ALL_DONE: return "ALL_DONE";
    }
    return NULL;
}

static const char *result_name(cflow_relation_result value) {
    switch (value) {
        case CFLOW_REL_RESULT_FOLD: return "FOLD";
        case CFLOW_REL_RESULT_SELECT: return "SELECT";
        case CFLOW_REL_RESULT_INVOKE: return "INVOKE";
    }
    return NULL;
}

static const char *error_name(cflow_relation_error value) {
    switch (value) {
        case CFLOW_REL_ERROR_FAIL_FAST: return "FAIL_FAST";
        case CFLOW_REL_ERROR_IGNORE: return "IGNORE";
        case CFLOW_REL_ERROR_TRY_NEXT: return "TRY_NEXT";
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

static int build_latest_fold_graph(cflow_graph *graph) {
    cflow_graph left = {0};
    cflow_graph right = {0};
    const cflow_graph *branches[2];
    int ok = 0;

    cflow_graph_init(graph, &cmeta_type_int);
    cflow_graph_init(&left, &cmeta_type_int);
    cflow_graph_init(&right, &cmeta_type_int);
    if (graph->root == CMETA_INVALID_ID || left.root == CMETA_INVALID_ID ||
        right.root == CMETA_INVALID_ID)
        goto done;

    if (!cflow_graph_flatMap(&left, conf_policy_left_expand_i_l.fn) ||
        !cflow_graph_flatMap(&right, conf_policy_right_expand_i_l.fn))
        goto done;

    branches[0] = &left;
    branches[1] = &right;
    ok = cflow_graph_relation(graph, branches, 2u,
                              cflow_relation_latest_fold(), conf_policy_add_l.fn);

done:
    cflow_graph_destroy(&left);
    cflow_graph_destroy(&right);
    return ok;
}

static int build_fail_then_map_relation(cflow_graph *graph,
                                        cflow_relation_schema schema) {
    cflow_graph failing = {0};
    cflow_graph fallback = {0};
    const cflow_graph *branches[2];
    int ok = 0;

    cflow_graph_init(graph, &cmeta_type_int);
    cflow_graph_init(&failing, &cmeta_type_int);
    cflow_graph_init(&fallback, &cmeta_type_int);
    if (graph->root == CMETA_INVALID_ID || failing.root == CMETA_INVALID_ID ||
        fallback.root == CMETA_INVALID_ID)
        goto done;

    if (!cflow_graph_flatMap(&failing, conf_policy_fail_i_l.fn) ||
        !cflow_graph_map(&fallback, conf_policy_fallback_i_l.fn))
        goto done;

    branches[0] = &failing;
    branches[1] = &fallback;
    ok = cflow_graph_relation(graph, branches, 2u, schema, (cmeta_callable){0});

done:
    cflow_graph_destroy(&failing);
    cflow_graph_destroy(&fallback);
    return ok;
}

static int build_ignore_graph(cflow_graph *graph) {
    cflow_relation_schema schema = {
        CFLOW_REL_COORD_ANY,
        CFLOW_REL_COMPLETE_COORDINATOR,
        CFLOW_REL_RESULT_SELECT,
        CFLOW_REL_ERROR_IGNORE
    };
    return build_fail_then_map_relation(graph, schema);
}

static int build_try_next_graph(cflow_graph *graph) {
    return build_fail_then_map_relation(graph, cflow_relation_fallback());
}

typedef int (*build_policy_fn)(cflow_graph *graph);

static int emit_policy_witness(const char *name,
                               build_policy_fn build,
                               const int *inputs,
                               size_t input_count) {
    cflow_graph graph = {0};
    cflow_graph normalized = {0};
    cflow_plan plan = {0};
    cflow_result result = {0};
    const cflow_subgraph *root;
    const cflow_node *relation;
    const char *input_type;
    const char *output_type;
    const char *coord;
    const char *completion;
    const char *result_policy;
    const char *error_policy;
    const char *reducer = "";
    int plan_accepted;
    int ok = 0;

    normalized.root = CMETA_INVALID_ID;
    if (!build(&graph)) goto done;
    if (!cflow_graph_normalize(&normalized, &graph)) goto done;

    root = cflow_graph_subgraph(&normalized, normalized.root);
    relation = root ? cflow_subgraph_node(root, root->tail) : NULL;
    if (!root || !relation || relation->op != CFLOW_OP_RELATION ||
        !relation->has_relation || relation->subgraph_count != 2u)
        goto done;

    input_type = type_token(relation->input_type);
    output_type = type_token(relation->output_type);
    coord = coordination_name(relation->relation.coordination);
    completion = completion_name(relation->relation.completion);
    result_policy = result_name(relation->relation.result);
    error_policy = error_name(relation->relation.error);
    if (!input_type || !output_type || !coord || !completion ||
        !result_policy || !error_policy)
        goto done;
    if (relation->has_fn) {
        reducer = signature_id(relation->fn);
        if (!reducer) goto done;
    }

    if (!cflow_eval_array(&graph, inputs, input_count, &result)) goto done;
    if (!cmeta_type_equal(result.type, relation->output_type)) goto done;

    plan_accepted = cflow_plan_compile_surface(&plan, &graph, NULL) ? 1 : 0;
    if (plan_accepted) goto done;

    printf("  { name := \"%s\", inputType := \"%s\", outputType := \"%s\",\n",
           name, input_type, output_type);
    printf("    coordination := \"%s\", completion := \"%s\", result := \"%s\",\n",
           coord, completion, result_policy);
    printf("    error := \"%s\", branchCount := %zu, reducer := \"%s\", input := ",
           error_policy, relation->subgraph_count, reducer);
    if (!emit_integral_values(&cmeta_type_int, inputs, input_count)) goto done;
    printf(", count := %zu,\n    output := ", result.count);
    if (!emit_integral_values(result.type, result.data, result.count)) goto done;
    printf(", directPlanAccepted := false } ::\n");
    ok = 1;

done:
    cflow_result_destroy(&result);
    cflow_plan_destroy(&plan);
    cflow_graph_destroy(&normalized);
    cflow_graph_destroy(&graph);
    return ok;
}

int main(void) {
    static const int inputs[] = { -2, 0, 3 };

    puts("module");
    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_structured_policy_conformance_gen.c using the real CFlow relation policies. -/");
    puts("");
    puts("namespace CMeta.CStructuredPolicyGenerated");
    puts("");
    puts("structure PolicyWitness where");
    puts("  name : String");
    puts("  inputType : String");
    puts("  outputType : String");
    puts("  coordination : String");
    puts("  completion : String");
    puts("  result : String");
    puts("  error : String");
    puts("  branchCount : Nat");
    puts("  reducer : String");
    puts("  input : List Int");
    puts("  count : Nat");
    puts("  output : List Int");
    puts("  directPlanAccepted : Bool");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("def policyWitnesses : List PolicyWitness :=");
    if (!emit_policy_witness("relation_latest_fold_i_l",
                             build_latest_fold_graph, inputs, 3u) ||
        !emit_policy_witness("relation_any_ignore_i_l",
                             build_ignore_graph, inputs, 3u) ||
        !emit_policy_witness("relation_fallback_try_next_i_l",
                             build_try_next_graph, inputs, 3u))
        return EXIT_FAILURE;
    puts("  []");
    puts("");
    puts("end CMeta.CStructuredPolicyGenerated");
    return EXIT_SUCCESS;
}
