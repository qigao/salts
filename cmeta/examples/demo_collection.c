#include <cflow/adapters.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdio.h>

static int near(double a, double b) { return fabs(a - b) < 1e-9; }

int main(void) {
    int input[] = {1,2,3,4,5,6};
    cflow_stream s;
    if (!cflow_stream_init(&s, &cmeta_type_int)) return 1;
    s.filter(&s, even)->map(&s, square)->map(&s, half);
    if (!cflow_stream_ok(&s)) return 2;
    cflow_result out = {0};
    if (!cflow_eval_array(&s.graph, input, 6, &out)) return 3;
    if (out.count != 3 || !near(((double *)out.data)[0],2.0) ||
        !near(((double *)out.data)[1],8.0) || !near(((double *)out.data)[2],18.0)) return 4;
    printf("collection adapter: %.1f %.1f %.1f\n",
           ((double *)out.data)[0], ((double *)out.data)[1], ((double *)out.data)[2]);
    cflow_result_destroy(&out);

    cflow_stream r;
    int rr[] = {2,4};
    cflow_stream_init(&r, &cmeta_type_int);
    r.flatMap(&r, expand_long)->reduce(&r, add_long);
    if (!cflow_eval_array(&r.graph, rr, 2, &out)) return 5;
    if (out.count != 1 || ((long *)out.data)[0] != 66) return 6;
    printf("generator+reduce: %ld\n", ((long *)out.data)[0]);
    cflow_result_destroy(&out);

    cflow_stream left, right;
    cflow_stream_init(&left, &cmeta_type_int);
    cflow_stream_init(&right, &cmeta_type_int);
    left.map(&left, square);
    right.map(&right, as_double);
    left.zip(&left, &right, merge_long_double);
    int zz[] = {1,2,3};
    if (!cflow_eval_array(&left.graph, zz, 3, &out)) return 7;
    double *z = (double *)out.data;
    if (out.count != 3 || !near(z[0],2.25) || !near(z[1],6.25) || !near(z[2],12.25)) return 8;
    printf("zip: %.2f %.2f %.2f\n", z[0],z[1],z[2]);
    cflow_result_destroy(&out);
    cflow_stream_destroy(&left); cflow_stream_destroy(&right);
    cflow_stream_destroy(&r); cflow_stream_destroy(&s);
    return 0;
}
