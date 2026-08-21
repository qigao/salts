#include <cflow/plan.h>
#include <cflow/lower.h>
#include <cflow/opt.h>
#include <cflow/stream.h>
#include "ops.h"

#include <stdio.h>
#include <string.h>

static bool same(const cflow_result *a, const cflow_result *b) {
    if (!a || !b || a->count != b->count || !cmeta_type_equal(a->type, b->type)) return false;
    return a->count == 0 || memcmp(a->data, b->data, a->count * a->type->size) == 0;
}

int main(void) {
    int input[] = {1,2,3,4,5,6};
    cflow_stream s;
    cflow_stream_init(&s, &cmeta_type_int);
    if (!s.filter(&s, even) || !s.map(&s, square) || !s.map(&s, half)) return 1;

    cflow_graph normalized = {0}, optimized = {0};
    normalized.root = optimized.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&normalized, &s.graph)) return 2;
    if (!cflow_graph_optimize(&optimized, &normalized, (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL)) return 3;

    cflow_plan plan = {0}; cflow_plan_compile_stats stats = {0};
    if (!cflow_plan_compile(&plan, &optimized, &stats)) {
        fprintf(stderr, "plan compile: %s\n", plan.error ? plan.error : "unknown"); return 4;
    }
    cflow_result interp = {0}, compiled = {0};
    if (!cflow_eval_array(&optimized, input, 6, &interp)) return 5;
    if (!cflow_plan_eval_array(&plan, input, 6, &compiled)) return 6;
    if (!same(&interp, &compiled)) return 7;
    if (stats.instructions != 2u || stats.map_callbacks != 2u) return 8; /* filter + fused map-chain */
    const double *p = compiled.data;
    if (compiled.count != 3u || p[0] != 2.0 || p[1] != 8.0 || p[2] != 18.0) return 9;
    printf("compiled plan: graph nodes=%zu -> instructions=%zu; fused map callbacks=%zu; trace 2 8 18\n",
           stats.graph_nodes, stats.instructions, stats.map_callbacks);
    cflow_result_destroy(&compiled); cflow_result_destroy(&interp); cflow_plan_destroy(&plan);
    cflow_graph_destroy(&optimized); cflow_graph_destroy(&normalized); cflow_stream_destroy(&s);

    cflow_stream f; cflow_stream_init(&f, &cmeta_type_int);
    if (!f.flatMap(&f, expand_long) || !f.reduce(&f, add_long)) return 10;
    int fr[] = {2,4};
    cflow_plan fp = {0}; cflow_result fi = {0}, fc = {0};
    if (!cflow_plan_compile_surface(&fp, &f.graph, NULL)) return 11;
    if (!cflow_eval_array(&f.graph, fr, 2, &fi) || !cflow_plan_eval_array(&fp, fr, 2, &fc) || !same(&fi, &fc)) return 12;
    if (fc.count != 1u || ((long *)fc.data)[0] != 66L) return 13;
    printf("compiled plan: flatMap generator + reduce -> 66\n");
    cflow_result_destroy(&fc); cflow_result_destroy(&fi); cflow_plan_destroy(&fp); cflow_stream_destroy(&f);

    cflow_stream left, right;
    cflow_stream_init(&left, &cmeta_type_int); cflow_stream_init(&right, &cmeta_type_int);
    left.map(&left, square); right.map(&right, as_double); left.zip(&left, &right, merge_long_double);
    cflow_graph zn = {0}, zo = {0}; zn.root = zo.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&zn, &left.graph) || !cflow_graph_optimize(&zo, &zn, (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL)) return 14;
    cflow_plan unsupported = {0};
    if (cflow_plan_graph_supported(&zo) || cflow_plan_compile(&unsupported, &zo, NULL)) return 15;
    printf("compiled plan boundary: structured RELATION rejected (no interpreter fallback)\n");
    cflow_plan_destroy(&unsupported); cflow_graph_destroy(&zo); cflow_graph_destroy(&zn);
    cflow_stream_destroy(&right); cflow_stream_destroy(&left);
    return 0;
}
