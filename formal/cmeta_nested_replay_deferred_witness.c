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
        0 CMETA_PROOF_EVAL(Replay(CMETA_PROOF_P, CMETA_PROOF_LEVEL3))
};

int main(void) {
    CHECK(cmeta_proof_distinct_count == 6);
    CHECK(cmeta_proof_depth2_count == 4);
    CHECK(cmeta_proof_depth3_count == 8);
    CHECK(cmeta_proof_depth4_count == 16);

    puts("cmeta nested replay deferred applicability: ok");
    return 0;
}
