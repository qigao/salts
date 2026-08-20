#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cflow/adapters.h>
#include <cflow/graph.h>
#include <cflow/lower.h>
#include <cflow/meta.h>
#include <cflow/plan.h>

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_rel_left_i_l, (int x)) {
    return (long)x;
}

typed_raw(map, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS,
          long, conf_rel_right_i_l, (int x)) {
    return (long)x * 10L;
}

typed_raw(reduce, CMETA_EFFECT_PURE,
          CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
              CMETA_PROP_ASSOCIATIVE | CMETA_PROP_NO_ALIAS,
          long, conf_rel_add_l, (long a, long b)) {
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

static int build_fold_graph(cflow_graph *graph) {
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

    if (!cflow_graph_map(&left, conf_rel_left_i_l.fn) ||
        !cflow_graph_map(&right, conf_rel_right_i_l.fn))
        goto done;

    branches[0] = &left;
    branches[1] = &right;
    ok = cflow_graph_relation(graph, branches, 2u,
                              cflow_relation_all_fold(), conf_rel_add_l.fn);

done:
    cflow_graph_destroy(&left);
    cflow_graph_destroy(&right);
    return ok;
}

static int emit_fold_witness(void) {
    static const int inputs[] = { -2, 0, 3 };
    cflow_graph graph = {0};
    cflow_graph normalized = {0};
    cflow_plan plan = {0};
    cflow_result result = {0};
    const cflow_subgraph *root;
    const cflow_node *relation;
    const char *input_type;
    const char *output_type;
    const char *reducer_sig;
    int plan_accepted;
    int ok = 0;

    normalized.root = CMETA_INVALID_ID;
    if (!build_fold_graph(&graph)) goto done;
    if (!cflow_graph_normalize(&normalized, &graph)) goto done;

    root = cflow_graph_subgraph(&normalized, normalized.root);
    relation = root ? cflow_subgraph_node(root, root->tail) : NULL;
    if (!root || !relation || relation->op != CFLOW_OP_RELATION ||
        !relation->has_relation || relation->relation.coordination != CFLOW_REL_COORD_ALL ||
        relation->relation.result != CFLOW_REL_RESULT_FOLD ||
        relation->subgraph_count != 2u || !relation->has_fn)
        goto done;

    input_type = type_token(relation->input_type);
    output_type = type_token(relation->output_type);
    reducer_sig = signature_id(relation->fn);
    if (!input_type || !output_type || !reducer_sig) goto done;

    if (!cflow_eval_array(&graph, inputs, 3u, &result)) goto done;
    if (!cmeta_type_equal(result.type, relation->output_type)) goto done;

    plan_accepted = cflow_plan_compile_surface(&plan, &graph, NULL) ? 1 : 0;
    if (plan_accepted) goto done;

    puts("  { name := \"relation_all_fold_i_l\",");
    printf("    inputType := \"%s\", outputType := \"%s\",\n",
           input_type, output_type);
    puts("    coordination := \"ALL\", result := \"FOLD\", branchCount := 2,");
    printf("    reducer := \"%s\", input := ", reducer_sig);
    if (!emit_integral_values(&cmeta_type_int, inputs, 3u)) goto done;
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
    puts("import Std");
    puts("");
    puts("/-! GENERATED by formal/cmeta_structured_conformance_gen.c using the real structured CFlow runtime. -/");
    puts("");
    puts("namespace CMeta.CStructuredGenerated");
    puts("");
    puts("structure RelationWitness where");
    puts("  name : String");
    puts("  inputType : String");
    puts("  outputType : String");
    puts("  coordination : String");
    puts("  result : String");
    puts("  branchCount : Nat");
    puts("  reducer : String");
    puts("  input : List Int");
    puts("  count : Nat");
    puts("  output : List Int");
    puts("  directPlanAccepted : Bool");
    puts("  deriving Repr, DecidableEq");
    puts("");
    puts("def relationWitnesses : List RelationWitness :=");
    if (!emit_fold_witness()) return EXIT_FAILURE;
    puts("  []");
    puts("");
    puts("end CMeta.CStructuredGenerated");
    return EXIT_SUCCESS;
}
