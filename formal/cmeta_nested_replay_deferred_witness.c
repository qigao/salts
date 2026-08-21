#include <cmeta/pp.h>

#include <stdio.h>

#if !defined(__STDC_VERSION__) || __STDC_VERSION__ != 201112L
#error "nested replay deferred witness must compile as exact C11"
#endif

/* This proof path must not obtain nesting behavior from the current arity
 * counter or public one-or-more FOR_EACH adapter. */
#undef CMETA_PP_NARG
#define CMETA_PP_NARG(...) CMETA_NESTED_REPLAY_WITNESS_FORBIDS_NARG
#undef CMETA_PP_FOR_EACH
#define CMETA_PP_FOR_EACH(...) CMETA_NESTED_REPLAY_WITNESS_FORBIDS_FOR_EACH

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "nested replay check failed: %s\n", #expr); \
        return 1; \
    } \
} while (0)

#define ARRAY_LEN(values) (sizeof(values) / sizeof((values)[0]))

/* Fixed deferred-rescan machinery.  Its budget is a backend nesting-depth
 * budget; it is independent of the producer's element count. */
#define CMETA_PROOF_EMPTY()
#define CMETA_PROOF_DEFER(id) id CMETA_PROOF_EMPTY()
#define CMETA_PROOF_OBSTRUCT(...) \
    __VA_ARGS__ CMETA_PROOF_DEFER(CMETA_PROOF_EMPTY)()

#define CMETA_PROOF_EVAL0(...) __VA_ARGS__
#define CMETA_PROOF_EVAL1(...) \
    CMETA_PROOF_EVAL0(CMETA_PROOF_EVAL0(CMETA_PROOF_EVAL0(__VA_ARGS__)))
#define CMETA_PROOF_EVAL2(...) \
    CMETA_PROOF_EVAL1(CMETA_PROOF_EVAL1(CMETA_PROOF_EVAL1(__VA_ARGS__)))
#define CMETA_PROOF_EVAL3(...) \
    CMETA_PROOF_EVAL2(CMETA_PROOF_EVAL2(CMETA_PROOF_EVAL2(__VA_ARGS__)))
#define CMETA_PROOF_EVAL(...) CMETA_PROOF_EVAL3(__VA_ARGS__)

/* Producer identity is separate from arity. */
#define CMETA_PROOF_P(M) M(1) M(2)
#define CMETA_PROOF_Q(M) M(3) M(4) M(5)

/* One tiny indirect thunk is the auxiliary identity that lets P be produced
 * again only after the active outer P expansion has ended. */
#define CMETA_PROOF_P_INDIRECT() CMETA_PROOF_P
#define CMETA_PROOF_DEFER_P(M) \
    CMETA_PROOF_OBSTRUCT(CMETA_PROOF_P_INDIRECT) () (M)

#define CMETA_PROOF_COUNT(x) + 1

/* Different producer identities can nest directly: Q is not disabled while P
 * is expanding.  No expansion lane is part of this expression. */
#define CMETA_PROOF_DISTINCT_OUTER(x) + (0 CMETA_PROOF_Q(CMETA_PROOF_COUNT))

enum {
    cmeta_proof_distinct_count =
        0 Replay(CMETA_PROOF_P, CMETA_PROOF_DISTINCT_OUTER)
};

/* Same-producer nesting needs deferral, but still no arity family. */
#define CMETA_PROOF_LEVEL1(x) + (0 CMETA_PROOF_DEFER_P(CMETA_PROOF_COUNT))
#define CMETA_PROOF_LEVEL2(x) + (0 CMETA_PROOF_DEFER_P(CMETA_PROOF_LEVEL1))
#define CMETA_PROOF_LEVEL3(x) + (0 CMETA_PROOF_DEFER_P(CMETA_PROOF_LEVEL2))

enum {
    cmeta_proof_depth2_count =
        0 CMETA_PROOF_EVAL(Replay(CMETA_PROOF_P, CMETA_PROOF_LEVEL1)),
    cmeta_proof_depth3_count =
        0 CMETA_PROOF_EVAL(Replay(CMETA_PROOF_P, CMETA_PROOF_LEVEL2)),
    cmeta_proof_depth4_count =
        0 CMETA_PROOF_EVAL(Replay(CMETA_PROOF_P, CMETA_PROOF_LEVEL3)),

    /* This is the depth actually covered by the witness cases above. It is a
     * certified lower bound, not the preprocessor's claimed absolute maximum. */
    cmeta_proof_certified_same_producer_depth = 4
};

/* Strategy tracing uses singleton producer identities so one structural replay
 * node produces exactly one trace token.  The trace therefore records backend
 * expansion strategy rather than producer cardinality.
 *
 * Tag 1: direct producer invocation.
 * Tag 2: deferred + obstructed same-producer re-entry.
 */
enum {
    cmeta_proof_strategy_direct = 1,
    cmeta_proof_strategy_deferred_obstruct = 2
};

#define CMETA_PROOF_P_ONE(M) M(1)
#define CMETA_PROOF_Q_ONE(M) M(2)
#define CMETA_PROOF_P_ONE_INDIRECT() CMETA_PROOF_P_ONE
#define CMETA_PROOF_DEFER_P_ONE(M) \
    CMETA_PROOF_OBSTRUCT(CMETA_PROOF_P_ONE_INDIRECT) () (M)

#define CMETA_PROOF_TRACE_DIRECT_Q(x) cmeta_proof_strategy_direct,
#define CMETA_PROOF_TRACE_DISTINCT_P(x) \
    cmeta_proof_strategy_direct, \
    CMETA_PROOF_Q_ONE(CMETA_PROOF_TRACE_DIRECT_Q)

static const int cmeta_proof_distinct_strategy_trace[] = {
    Replay(CMETA_PROOF_P_ONE, CMETA_PROOF_TRACE_DISTINCT_P)
};

#define CMETA_PROOF_TRACE_DEFERRED_P(x) \
    cmeta_proof_strategy_deferred_obstruct,
#define CMETA_PROOF_TRACE_REENTRY_Q(x) \
    cmeta_proof_strategy_direct, \
    CMETA_PROOF_DEFER_P_ONE(CMETA_PROOF_TRACE_DEFERRED_P)
#define CMETA_PROOF_TRACE_REENTRY_P(x) \
    cmeta_proof_strategy_direct, \
    CMETA_PROOF_Q_ONE(CMETA_PROOF_TRACE_REENTRY_Q)

static const int cmeta_proof_reentry_strategy_trace[] = {
    CMETA_PROOF_EVAL(Replay(CMETA_PROOF_P_ONE, CMETA_PROOF_TRACE_REENTRY_P))
};

static void print_nat_list(const char *name, const int *values, size_t count) {
    size_t i;

    printf("def %s : List Nat := [", name);
    for (i = 0; i < count; ++i) {
        if (i != 0) {
            fputs(", ", stdout);
        }
        printf("%d", values[i]);
    }
    puts("]");
}

int main(void) {
    CHECK(cmeta_proof_distinct_count == 6);
    CHECK(cmeta_proof_depth2_count == 4);
    CHECK(cmeta_proof_depth3_count == 8);
    CHECK(cmeta_proof_depth4_count == 16);
    CHECK(cmeta_proof_certified_same_producer_depth == 4);

    CHECK(ARRAY_LEN(cmeta_proof_distinct_strategy_trace) == 2);
    CHECK(cmeta_proof_distinct_strategy_trace[0] == cmeta_proof_strategy_direct);
    CHECK(cmeta_proof_distinct_strategy_trace[1] == cmeta_proof_strategy_direct);

    CHECK(ARRAY_LEN(cmeta_proof_reentry_strategy_trace) == 3);
    CHECK(cmeta_proof_reentry_strategy_trace[0] == cmeta_proof_strategy_direct);
    CHECK(cmeta_proof_reentry_strategy_trace[1] == cmeta_proof_strategy_direct);
    CHECK(cmeta_proof_reentry_strategy_trace[2] ==
          cmeta_proof_strategy_deferred_obstruct);

    puts("namespace CMeta.NestedReplayGeneratedC");
    printf("def distinctCount : Nat := %d\n", cmeta_proof_distinct_count);
    printf("def depth2Count : Nat := %d\n", cmeta_proof_depth2_count);
    printf("def depth3Count : Nat := %d\n", cmeta_proof_depth3_count);
    printf("def depth4Count : Nat := %d\n", cmeta_proof_depth4_count);
    printf("def certifiedSameProducerDepth : Nat := %d\n",
           cmeta_proof_certified_same_producer_depth);
    print_nat_list("distinctStrategyTrace",
                   cmeta_proof_distinct_strategy_trace,
                   ARRAY_LEN(cmeta_proof_distinct_strategy_trace));
    print_nat_list("reentryStrategyTrace",
                   cmeta_proof_reentry_strategy_trace,
                   ARRAY_LEN(cmeta_proof_reentry_strategy_trace));
    puts("end CMeta.NestedReplayGeneratedC");
    return 0;
}
