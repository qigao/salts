#ifndef CMETA_SIGNATURES_H
#define CMETA_SIGNATURES_H

#include <cmeta/pp.h>
#include <cmeta/types.h>

#define CMETA_U_ID_I(i, r) U_##i##_##r
#define CMETA_U_ID_E(i, r) CMETA_U_ID_I(i, r)
#define CMETA_U_ID(in, ret) CMETA_U_ID_E(CMETA_TYPE_TOKEN(in), CMETA_TYPE_TOKEN(ret))

#define CMETA_B_ID_I(a, b, r) B_##a##_##b##_##r
#define CMETA_B_ID_E(a, b, r) CMETA_B_ID_I(a, b, r)
#define CMETA_B_ID(a, b, ret) \
    CMETA_B_ID_E(CMETA_TYPE_TOKEN(a), CMETA_TYPE_TOKEN(b), CMETA_TYPE_TOKEN(ret))

#define CMETA_G_ID_I(i, o) G_##i##_##o
#define CMETA_G_ID_E(i, o) CMETA_G_ID_I(i, o)
#define CMETA_G_ID(in, out) CMETA_G_ID_E(CMETA_TYPE_TOKEN(in), CMETA_TYPE_TOKEN(out))

#define CMETA_SIG_NAME_I(id) CMETA_SIG_##id
#define CMETA_SIG_NAME_E(id) CMETA_SIG_NAME_I(id)
#define CMETA_SIG_NAME(id) CMETA_SIG_NAME_E(id)
#define CMETA_FN_TYPE_I(id) cmeta_fn_##id##_t
#define CMETA_FN_TYPE_E(id) CMETA_FN_TYPE_I(id)
#define CMETA_FN_TYPE(id) CMETA_FN_TYPE_E(id)
#define CMETA_CALL_MEMBER_I(id) call_##id
#define CMETA_CALL_MEMBER_E(id) CMETA_CALL_MEMBER_I(id)
#define CMETA_CALL_MEMBER(id) CMETA_CALL_MEMBER_E(id)
#define CMETA_MAKER_I(id) cmeta_make_##id
#define CMETA_MAKER_E(id) CMETA_MAKER_I(id)
#define CMETA_MAKER(id) CMETA_MAKER_E(id)

/* Unary full product R(T), N^2 over the callable universe only. */
#define CMETA_U_OUTER(in, M) \
    CMETA_PP_FOR_EACH_B(CMETA_U_INNER, (M, in), CMETA_CALLABLE_TYPE_LIST)
#define CMETA_U_INNER(ret, ctx) CMETA_U_INNER_E(ret, CMETA_PP_UNPAREN ctx)
#define CMETA_U_INNER_E(...) CMETA_U_INNER_I(__VA_ARGS__)
#define CMETA_U_INNER_I(ret, M, in) M(in, ret)
#define CMETA_GEN_UNARY_ALL(M) \
    CMETA_PP_FOR_EACH_A(CMETA_U_OUTER, M, CMETA_CALLABLE_TYPE_LIST)

/* Binary full product R(A,B), N^3 over the callable universe only. */
#define CMETA_B_OUTER_A(a, M) \
    CMETA_PP_FOR_EACH_B(CMETA_B_OUTER_B, (M, a), CMETA_CALLABLE_TYPE_LIST)
#define CMETA_B_OUTER_B(b, ctx) CMETA_B_OUTER_B_E(b, CMETA_PP_UNPAREN ctx)
#define CMETA_B_OUTER_B_E(...) CMETA_B_OUTER_B_I(__VA_ARGS__)
#define CMETA_B_OUTER_B_I(b, M, a) \
    CMETA_PP_FOR_EACH_C(CMETA_B_INNER, (M, a, b), CMETA_CALLABLE_TYPE_LIST)
#define CMETA_B_INNER(ret, ctx) CMETA_B_INNER_E(ret, CMETA_PP_UNPAREN ctx)
#define CMETA_B_INNER_E(...) CMETA_B_INNER_I(__VA_ARGS__)
#define CMETA_B_INNER_I(ret, M, a, b) M(a, b, ret)
#define CMETA_GEN_BINARY_ALL(M) \
    CMETA_PP_FOR_EACH_A(CMETA_B_OUTER_A, M, CMETA_CALLABLE_TYPE_LIST)

/* Balanced binary family: B(A,B), N^2 over the callable universe only. */
#define CMETA_BR_OUTER(a, M) \
    CMETA_PP_FOR_EACH_B(CMETA_BR_INNER, (M, a), CMETA_CALLABLE_TYPE_LIST)
#define CMETA_BR_INNER(b, ctx) CMETA_BR_INNER_E(b, CMETA_PP_UNPAREN ctx)
#define CMETA_BR_INNER_E(...) CMETA_BR_INNER_I(__VA_ARGS__)
#define CMETA_BR_INNER_I(b, M, a) M(a, b, b)
#define CMETA_GEN_BINARY_RETURN_RIGHT(M) \
    CMETA_PP_FOR_EACH_A(CMETA_BR_OUTER, M, CMETA_CALLABLE_TYPE_LIST)

/* Canonical resumable flatMap family:
 * cmeta_gen_status(IN, OUT *, size_t *cursor), N^2 over callable types. */
#define CMETA_G_OUTER(in, M) \
    CMETA_PP_FOR_EACH_B(CMETA_G_INNER, (M, in), CMETA_CALLABLE_TYPE_LIST)
#define CMETA_G_INNER(out, ctx) CMETA_G_INNER_E(out, CMETA_PP_UNPAREN ctx)
#define CMETA_G_INNER_E(...) CMETA_G_INNER_I(__VA_ARGS__)
#define CMETA_G_INNER_I(out, M, in) M(in, out)
#define CMETA_GEN_GENERATOR_ALL(M) \
    CMETA_PP_FOR_EACH_A(CMETA_G_OUTER, M, CMETA_CALLABLE_TYPE_LIST)

#include <cmeta/relations.h>
#define CMETA_UR_APPLY(rel, M) CMETA_UR_APPLY_E(M, CMETA_PP_UNPAREN rel)
#define CMETA_UR_APPLY_E(...) CMETA_UR_APPLY_I(__VA_ARGS__)
#define CMETA_UR_APPLY_I(M, in, ret) M(in, ret)
#define CMETA_GEN_UNARY_RELATIONS(M) \
    CMETA_PP_FOR_EACH_A(CMETA_UR_APPLY, M, CMETA_UNARY_RELATION_LIST)

#define CMETA_BR_APPLY(rel, M) CMETA_BR_APPLY_E(M, CMETA_PP_UNPAREN rel)
#define CMETA_BR_APPLY_E(...) CMETA_BR_APPLY_I(__VA_ARGS__)
#define CMETA_BR_APPLY_I(M, a, b, ret) M(a, b, ret)
#define CMETA_GEN_BINARY_RELATIONS(M) \
    CMETA_PP_FOR_EACH_A(CMETA_BR_APPLY, M, CMETA_BINARY_RELATION_LIST)

#define CMETA_GR_APPLY(rel, M) CMETA_GR_APPLY_E(M, CMETA_PP_UNPAREN rel)
#define CMETA_GR_APPLY_E(...) CMETA_GR_APPLY_I(__VA_ARGS__)
#define CMETA_GR_APPLY_I(M, in, out) M(in, out)
#define CMETA_GEN_GENERATOR_RELATIONS(M) \
    CMETA_PP_FOR_EACH_A(CMETA_GR_APPLY, M, CMETA_GENERATOR_RELATION_LIST)

#include <cmeta/policy.h>
#define CMETA_UNARY_SIGNATURES(M)     CMETA_POLICY_UNARY(M)
#define CMETA_BINARY_SIGNATURES(M)    CMETA_POLICY_BINARY(M)
#define CMETA_GENERATOR_SIGNATURES(M) CMETA_POLICY_GENERATOR(M)
#define CMETA_ALL_SIGNATURES(U, B, G) \
    CMETA_UNARY_SIGNATURES(U) \
    CMETA_BINARY_SIGNATURES(B) \
    CMETA_GENERATOR_SIGNATURES(G)

#endif
