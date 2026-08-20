#include <cflow/verify.h>
#include <cflow/stream.h>
#include "ops.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


lambda1(map, value,
        long, verify_scale_by, int, x, int, factor)
{
    return (long)x * (long)factor;
}

typedef enum vkind { V_INT, V_LONG, V_DOUBLE } vkind;
typedef struct value {
    vkind kind;
    union { int i; long l; double d; } u;
} value;

typedef struct refvec {
    value v[4096];
    size_t n;
    vkind kind;
} refvec;

static uint32_t rng_next(uint32_t *s) {
    uint32_t x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x ? x : 0x9e3779b9u;
    return *s;
}

static bool ref_filter_even(refvec *r) {
    size_t w = 0;
    for (size_t i = 0; i < r->n; ++i) if ((r->v[i].u.i % 2) == 0) r->v[w++] = r->v[i];
    r->n = w; return true;
}
static bool ref_clamp(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) if (r->v[i].u.i < 0) r->v[i].u.i = 0;
    return true;
}
static bool ref_square(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) { int x = r->v[i].u.i; r->v[i].kind = V_LONG; r->v[i].u.l = (long)x * x; }
    r->kind = V_LONG; return true;
}
static bool ref_twice(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) { int x = r->v[i].u.i; r->v[i].kind = V_LONG; r->v[i].u.l = (long)x * 2L; }
    r->kind = V_LONG; return true;
}
static bool ref_scale(refvec *r, int factor) {
    for (size_t i = 0; i < r->n; ++i) { int x = r->v[i].u.i; r->v[i].kind = V_LONG; r->v[i].u.l = (long)x * (long)factor; }
    r->kind = V_LONG; return true;
}
static bool ref_as_double(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) { int x = r->v[i].u.i; r->v[i].kind = V_DOUBLE; r->v[i].u.d = (double)x + 0.25; }
    r->kind = V_DOUBLE; return true;
}
static bool ref_expand(refvec *r) {
    if (r->n > 2048u) return false;
    for (size_t i = r->n; i-- > 0;) {
        int x = r->v[i].u.i;
        r->v[i * 2u].kind = V_LONG; r->v[i * 2u].u.l = (long)x;
        r->v[i * 2u + 1u].kind = V_LONG; r->v[i * 2u + 1u].u.l = (long)x * 10L;
    }
    r->n *= 2u; r->kind = V_LONG; return true;
}
static bool ref_half(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) { long x = r->v[i].u.l; r->v[i].kind = V_DOUBLE; r->v[i].u.d = (double)x / 2.0; }
    r->kind = V_DOUBLE; return true;
}
static bool ref_reduce(refvec *r) {
    if (!r->n) return true;
    long sum = 0;
    for (size_t i = 0; i < r->n; ++i) sum += r->v[i].u.l;
    r->v[0].kind = V_LONG; r->v[0].u.l = sum; r->n = 1; return true;
}
static bool ref_to_int(refvec *r) {
    for (size_t i = 0; i < r->n; ++i) { double x = r->v[i].u.d; r->v[i].kind = V_INT; r->v[i].u.i = (int)x; }
    r->kind = V_INT; return true;
}

static bool output_matches_ref(const cflow_result *out, const refvec *r) {
    if (!out || out->count != r->n) return false;
    if (r->kind == V_INT) {
        if (!cmeta_type_equal(out->type, &cmeta_type_int)) return false;
        const int *p = out->data;
        for (size_t i = 0; i < r->n; ++i) if (p[i] != r->v[i].u.i) return false;
    } else if (r->kind == V_LONG) {
        if (!cmeta_type_equal(out->type, &cmeta_type_long)) return false;
        const long *p = out->data;
        for (size_t i = 0; i < r->n; ++i) if (p[i] != r->v[i].u.l) return false;
    } else {
        if (!cmeta_type_equal(out->type, &cmeta_type_double)) return false;
        const double *p = out->data;
        for (size_t i = 0; i < r->n; ++i) if (fabs(p[i] - r->v[i].u.d) > 1e-12) return false;
    }
    return true;
}

static bool randomized_pipeline(uint32_t seed) {
    uint32_t rng = seed ? seed : 1u;
    int input[8];
    size_t n = 1u + (rng_next(&rng) % 8u);
    refvec ref = { .n = n, .kind = V_INT };
    for (size_t i = 0; i < n; ++i) {
        input[i] = (int)(rng_next(&rng) % 31u) - 15;
        ref.v[i].kind = V_INT; ref.v[i].u.i = input[i];
    }

    cflow_stream s;
    cflow_stream_init(&s, &cmeta_type_int);
    unsigned steps = 1u + (rng_next(&rng) % 10u);
    for (unsigned step = 0; step < steps; ++step) {
        uint32_t choice = rng_next(&rng);
        if (ref.kind == V_INT) {
            switch (choice % 7u) {
                case 0: if (!s.filter(&s, even) || !ref_filter_even(&ref)) goto fail; break;
                case 1: if (!s.map(&s, clamp_nonnegative) || !ref_clamp(&ref)) goto fail; break;
                case 2: if (!s.map(&s, square) || !ref_square(&ref)) goto fail; break;
                case 3: if (!s.transform(&s, times_two_transform) || !ref_twice(&ref)) goto fail; break;
                case 4: if (!s.map(&s, as_double) || !ref_as_double(&ref)) goto fail; break;
                case 5: {
                    int factor = 1 + (int)(rng_next(&rng) % 5u);
                    if (!s.map(&s, verify_scale_by(factor)) || !ref_scale(&ref, factor)) goto fail;
                    break;
                }
                default: if (!s.flatMap(&s, expand_long) || !ref_expand(&ref)) goto fail; break;
            }
        } else if (ref.kind == V_LONG) {
            if ((choice & 1u) == 0u) {
                if (!s.map(&s, half) || !ref_half(&ref)) goto fail;
            } else {
                if (!s.reduce(&s, add_long) || !ref_reduce(&ref)) goto fail;
            }
        } else {
            if (!s.map(&s, to_int) || !ref_to_int(&ref)) goto fail;
        }
    }

    cflow_verify_report vr = {0};
    if (!cflow_verify_pipeline(&s.graph, input, n, &vr)) goto fail;
    if (!vr.compiled_plan_checked) goto fail;
    cflow_result out = {0};
    if (!cflow_eval_array(&s.graph, input, n, &out)) goto fail;
    bool ok = output_matches_ref(&out, &ref);
    cflow_result_destroy(&out);
    cflow_stream_destroy(&s);
    return ok;
fail:
    cflow_stream_destroy(&s);
    return false;
}

static bool verify_zip_and_relation(void) {
    int input[] = {1,2,3};
    cflow_stream left, right;
    cflow_stream_init(&left, &cmeta_type_int);
    cflow_stream_init(&right, &cmeta_type_int);
    left.map(&left, square);
    right.map(&right, as_double);
    if (!left.zip(&left, &right, merge_long_double)) return false;
    cflow_verify_report r = {0};
    if (!cflow_verify_pipeline(&left.graph, input, 3, &r)) { fprintf(stderr, "zip verify: %s\n", r.error ? r.error : "unknown"); return false; }

    cflow_stream b0, b1, b2;
    cflow_stream_init(&b0, &cmeta_type_int); b0.map(&b0, square);
    cflow_stream_init(&b1, &cmeta_type_int); b1.map(&b1, times_ten);
    cflow_stream_init(&b2, &cmeta_type_int); b2.map(&b2, plus_hundred);
    cflow_graph g; cflow_graph_init(&g, &cmeta_type_int);
    const cflow_graph *branches[] = { &b0.graph, &b1.graph, &b2.graph };
    bool ok = cflow_graph_relation(&g, branches, 3, cflow_relation_all_fold(), add_long.fn);
    if (ok && !cflow_verify_pipeline(&g, input, 3, &r)) { fprintf(stderr, "relation verify: %s\n", r.error ? r.error : "unknown"); ok = false; }
    cflow_graph_destroy(&g);
    cflow_stream_destroy(&b2); cflow_stream_destroy(&b1); cflow_stream_destroy(&b0);
    cflow_stream_destroy(&right); cflow_stream_destroy(&left);
    return ok;
}

int main(int argc, char **argv) {
    unsigned iterations = 2000u;
    if (argc > 1) iterations = (unsigned)strtoul(argv[1], NULL, 10);
    if (!iterations) iterations = 1u;

    if (!verify_zip_and_relation()) return 2;
    for (unsigned i = 1; i <= iterations; ++i) {
        if (!randomized_pipeline(0x9e3779b9u ^ (i * 2654435761u))) {
            fprintf(stderr, "C verification failed at seed %u\n", i);
            return 3;
        }
    }
    printf("C-only verification: %u randomized typed pipelines + ZIP/relation differential checks PASS\n", iterations);
    return 0;
}
