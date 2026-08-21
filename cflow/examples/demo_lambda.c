#include <cflow/adapters.h>
#include <cflow/lower.h>
#include <cflow/opt.h>
#include <cflow/plan.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct affine_capture {
    double scale;
    double bias;
} affine_capture;

lambda1(map, value,
        long, scale_by, int, x, int, factor)
{
    return (long)x * (long)factor;
}

/* Meta-generated partial application: the binary implementation is declared
 * once; cmeta_bind(multiply, factor) produces a normal map callable value. */
cmeta_bindable(map, value,
               long, multiply, int, x, int, factor)
{
    return (long)x * (long)factor;
}

lambda1(map, value,
        double, affine, int, x, affine_capture, capture)
{
    return (double)x * capture.scale + capture.bias;
}

lambda2(reduce, value,
        long, add_bias, long, a, long, b, long, bias)
{
    return a + b + bias;
}

lambda2(zip, value,
        double, merge_bias, long, a, double, b, double, bias)
{
    return (double)a + b + bias;
}

lambda_gen(flatMap, value,
           expand_scaled, int, x, long, out, int, factor)
{
    if (*cursor == 0u) {
        *out = (long)x;
        *cursor = 1u;
        return CMETA_GEN_VALUE;
    }
    if (*cursor == 1u) {
        *out = (long)x * (long)factor;
        *cursor = 2u;
        return CMETA_GEN_VALUE_AND_DONE;
    }
    return CMETA_GEN_DONE;
}

static int expect_doubles(const cflow_result *r, const double *expected, size_t n) {
    if (!r || r->count != n || !cmeta_type_equal(r->type, &cmeta_type_double)) return 0;
    const double *v = (const double *)r->data;
    for (size_t i = 0; i < n; ++i)
        if (fabs(v[i] - expected[i]) > 1e-9) return 0;
    return 1;
}

int main(void) {
    const int input[] = {1, 2, 3};
    cflow_result out = {0};

    /* Named callable and closure have the same operator-specific value ABI. */
    cflow_stream s;
    if (!cflow_stream_init(&s, &cmeta_type_int)) return 1;
    int factor = 10;
    s.map(&s, scale_by(factor))->map(&s, half);
    factor = 99; /* capture was copied by value into the Graph */
    if (!cflow_eval_array(&s.graph, input, 3, &out)) return 2;
    const double e0[] = {5.0, 10.0, 15.0};
    if (!expect_doubles(&out, e0, 3)) return 3;
    cflow_result_destroy(&out);

    /* bind() is C Meta partial application, not a runtime Graph operator. */
    cflow_stream bound_stream;
    if (!cflow_stream_init(&bound_stream, &cmeta_type_int)) return 23;
    bound_stream.map(&bound_stream, cmeta_bind(multiply, 10))->map(&bound_stream, half);
    if (!cflow_eval_array(&bound_stream.graph, input, 3, &out)) return 24;
    if (!expect_doubles(&out, e0, 3)) return 25;
    if (cmeta_bindable_call(multiply)(7, 3) != 21L) return 26;
    cflow_result_destroy(&out);
    {
        cflow_plan bound_plan = {0};
        if (!cflow_plan_compile_surface(&bound_plan, &bound_stream.graph, NULL)) return 27;
        if (!cflow_plan_eval_array(&bound_plan, input, 3, &out)) return 28;
        if (!expect_doubles(&out, e0, 3)) return 29;
        cflow_result_destroy(&out);
        cflow_plan_destroy(&bound_plan);
    }
    cflow_stream_destroy(&bound_stream);

    /* Graph clone owns its own copy of inline captures. */
    cflow_graph clone = {0};
    clone.root = CMETA_INVALID_ID;
    if (!cflow_graph_clone(&clone, &s.graph)) return 4;
    cflow_stream_destroy(&s);
    if (!cflow_eval_array(&clone, input, 3, &out)) return 5;
    if (!expect_doubles(&out, e0, 3)) return 6;
    cflow_result_destroy(&out);
    cflow_graph_destroy(&clone);

    /* Optimizer fusion + compiled plan preserve captured bytes. */
    cflow_stream fused;
    if (!cflow_stream_init(&fused, &cmeta_type_int)) return 7;
    fused.map(&fused, scale_by(3))->map(&fused, half);
    cflow_graph norm = {0}, opt = {0};
    norm.root = opt.root = CMETA_INVALID_ID;
    if (!cflow_graph_normalize(&norm, &fused.graph)) return 8;
    if (!cflow_graph_optimize(&opt, &norm, (cflow_opt_options){CMETA_OPT_DEFAULT}, NULL)) return 9;
    const cflow_subgraph *root = cflow_graph_subgraph(&opt, opt.root);
    if (!root || root->node_count != 2u || root->nodes[root->tail].fn_chain_count != 2u) return 10;
    cflow_plan plan = {0};
    if (!cflow_plan_compile(&plan, &opt, NULL)) return 11;
    if (!cflow_plan_eval_array(&plan, input, 3, &out)) return 12;
    const double e1[] = {1.5, 3.0, 4.5};
    if (!expect_doubles(&out, e1, 3)) return 13;
    cflow_result_destroy(&out);
    cflow_plan_destroy(&plan);
    cflow_graph_destroy(&opt);
    cflow_graph_destroy(&norm);
    cflow_stream_destroy(&fused);

    /* Snapshot-imported subgraphs also own capture values. */
    cflow_stream left, right;
    if (!cflow_stream_init(&left, &cmeta_type_int) ||
        !cflow_stream_init(&right, &cmeta_type_int)) return 14;
    left.map(&left, square);
    right.map(&right, affine((affine_capture){ .scale = 0.5, .bias = 0.0 }));
    left.zip(&left, &right, merge_bias(1.0));
    cflow_stream_destroy(&right);
    if (!cflow_eval_array(&left.graph, input, 3, &out)) return 15;
    const double e2[] = {2.5, 6.0, 11.5};
    if (!expect_doubles(&out, e2, 3)) return 16;
    cflow_result_destroy(&out);
    cflow_stream_destroy(&left);

    /* Capturing binary and generator callbacks use the same callable value. */
    cflow_stream red;
    if (!cflow_stream_init(&red, &cmeta_type_int)) return 17;
    red.map(&red, scale_by(1))->reduce(&red, add_bias(1));
    if (!cflow_eval_array(&red.graph, input, 3, &out)) return 18;
    if (out.count != 1u || !cmeta_type_equal(out.type, &cmeta_type_long) ||
        ((long *)out.data)[0] != 8L) return 19;
    cflow_result_destroy(&out);
    cflow_stream_destroy(&red);

    cflow_stream gen;
    if (!cflow_stream_init(&gen, &cmeta_type_int)) return 20;
    gen.flatMap(&gen, expand_scaled(10));
    const int one[] = {2};
    if (!cflow_eval_array(&gen.graph, one, 1, &out)) return 21;
    if (out.count != 2u || !cmeta_type_equal(out.type, &cmeta_type_long) ||
        ((long *)out.data)[0] != 2L || ((long *)out.data)[1] != 20L) return 22;
    cflow_result_destroy(&out);
    cflow_stream_destroy(&gen);

    printf("first-class callable: named square/half and captured scale_by(10) share value ABI\n");
    printf("capture ownership: Graph clone + Subgraph snapshot + fused plan preserve inline capture\n");
    printf("capturing lambda2/lambda_gen: zip + reduce + flatMap PASS\n");
    printf("callable algebra: cmeta_bind(multiply, 10) partial application PASS\n");
    return 0;
}
