#ifndef CMETA_PP_H
#define CMETA_PP_H

#if defined(__GNUC__) || defined(__clang__)
#define CMETA_UNUSED __attribute__((unused))
#else
#define CMETA_UNUSED
#endif

#define CMETA_INLINE static inline CMETA_UNUSED
#define CMETA_LOCAL static CMETA_UNUSED

#ifdef __cplusplus
#define CMETA_ALIGNOF(type) alignof(type)
#else
#define CMETA_ALIGNOF(type) _Alignof(type)
#endif

/* Small, ISO-C-preprocessor iteration toolkit.
 *
 * A/B/C are deliberately separate expansion families. The preprocessor
 * suppresses recursive expansion of the macro currently being expanded, so
 * distinct families let us form nested products from one tuple list.
 *
 * The tuple-list implementation supports up to 16 element types.
 */
#define CMETA_PP_CAT_I(a, b) a##b
#define CMETA_PP_CAT(a, b) CMETA_PP_CAT_I(a, b)
#define CMETA_PP_UNPAREN(...) __VA_ARGS__

#define CMETA_PP_NARG_I(_1,_2,_3,_4,_5,_6,_7,_8,_9,_10,_11,_12,_13,_14,_15,_16,N,...) N
#define CMETA_PP_NARG(...) CMETA_PP_NARG_I(__VA_ARGS__,16,15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0)

#define CMETA_PP_FEA_1(M,C,a) M(a,C)
#define CMETA_PP_FEA_2(M,C,a,...) M(a,C) CMETA_PP_FEA_1(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_3(M,C,a,...) M(a,C) CMETA_PP_FEA_2(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_4(M,C,a,...) M(a,C) CMETA_PP_FEA_3(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_5(M,C,a,...) M(a,C) CMETA_PP_FEA_4(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_6(M,C,a,...) M(a,C) CMETA_PP_FEA_5(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_7(M,C,a,...) M(a,C) CMETA_PP_FEA_6(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_8(M,C,a,...) M(a,C) CMETA_PP_FEA_7(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_9(M,C,a,...) M(a,C) CMETA_PP_FEA_8(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_10(M,C,a,...) M(a,C) CMETA_PP_FEA_9(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_11(M,C,a,...) M(a,C) CMETA_PP_FEA_10(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_12(M,C,a,...) M(a,C) CMETA_PP_FEA_11(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_13(M,C,a,...) M(a,C) CMETA_PP_FEA_12(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_14(M,C,a,...) M(a,C) CMETA_PP_FEA_13(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_15(M,C,a,...) M(a,C) CMETA_PP_FEA_14(M,C,__VA_ARGS__)
#define CMETA_PP_FEA_16(M,C,a,...) M(a,C) CMETA_PP_FEA_15(M,C,__VA_ARGS__)

#define CMETA_PP_FEB_1(M,C,a) M(a,C)
#define CMETA_PP_FEB_2(M,C,a,...) M(a,C) CMETA_PP_FEB_1(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_3(M,C,a,...) M(a,C) CMETA_PP_FEB_2(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_4(M,C,a,...) M(a,C) CMETA_PP_FEB_3(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_5(M,C,a,...) M(a,C) CMETA_PP_FEB_4(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_6(M,C,a,...) M(a,C) CMETA_PP_FEB_5(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_7(M,C,a,...) M(a,C) CMETA_PP_FEB_6(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_8(M,C,a,...) M(a,C) CMETA_PP_FEB_7(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_9(M,C,a,...) M(a,C) CMETA_PP_FEB_8(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_10(M,C,a,...) M(a,C) CMETA_PP_FEB_9(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_11(M,C,a,...) M(a,C) CMETA_PP_FEB_10(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_12(M,C,a,...) M(a,C) CMETA_PP_FEB_11(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_13(M,C,a,...) M(a,C) CMETA_PP_FEB_12(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_14(M,C,a,...) M(a,C) CMETA_PP_FEB_13(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_15(M,C,a,...) M(a,C) CMETA_PP_FEB_14(M,C,__VA_ARGS__)
#define CMETA_PP_FEB_16(M,C,a,...) M(a,C) CMETA_PP_FEB_15(M,C,__VA_ARGS__)

#define CMETA_PP_FEC_1(M,C,a) M(a,C)
#define CMETA_PP_FEC_2(M,C,a,...) M(a,C) CMETA_PP_FEC_1(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_3(M,C,a,...) M(a,C) CMETA_PP_FEC_2(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_4(M,C,a,...) M(a,C) CMETA_PP_FEC_3(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_5(M,C,a,...) M(a,C) CMETA_PP_FEC_4(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_6(M,C,a,...) M(a,C) CMETA_PP_FEC_5(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_7(M,C,a,...) M(a,C) CMETA_PP_FEC_6(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_8(M,C,a,...) M(a,C) CMETA_PP_FEC_7(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_9(M,C,a,...) M(a,C) CMETA_PP_FEC_8(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_10(M,C,a,...) M(a,C) CMETA_PP_FEC_9(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_11(M,C,a,...) M(a,C) CMETA_PP_FEC_10(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_12(M,C,a,...) M(a,C) CMETA_PP_FEC_11(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_13(M,C,a,...) M(a,C) CMETA_PP_FEC_12(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_14(M,C,a,...) M(a,C) CMETA_PP_FEC_13(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_15(M,C,a,...) M(a,C) CMETA_PP_FEC_14(M,C,__VA_ARGS__)
#define CMETA_PP_FEC_16(M,C,a,...) M(a,C) CMETA_PP_FEC_15(M,C,__VA_ARGS__)

#define CMETA_PP_FOR_EACH_A(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEA_, CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)
#define CMETA_PP_FOR_EACH_B(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEB_, CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)
#define CMETA_PP_FOR_EACH_C(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEC_, CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)

/* Public preprocessor kernel -------------------------------------------------
 *
 * These are the small reusable primitives that higher C Meta schemas build
 * on.  User-facing Enum/Struct/typed/bind declarations build on
 * these rather than growing ad-hoc REPEAT_1/REPEAT_2 families.
 *
 * M signatures:
 *   CMETA_PP_REPEAT(N, M, C)       -> M(index, C)
 *   CMETA_PP_FOR_EACH(M, C, ...)   -> M(item, C)
 *   CMETA_PP_FOR_EACH_I(M,C,...)   -> M(index, item, C)
 */
#define CMETA_PP_REPEAT_0(M,C)
#define CMETA_PP_REPEAT_1(M,C)  M(0,C)
#define CMETA_PP_REPEAT_2(M,C)  CMETA_PP_REPEAT_1(M,C)  M(1,C)
#define CMETA_PP_REPEAT_3(M,C)  CMETA_PP_REPEAT_2(M,C)  M(2,C)
#define CMETA_PP_REPEAT_4(M,C)  CMETA_PP_REPEAT_3(M,C)  M(3,C)
#define CMETA_PP_REPEAT_5(M,C)  CMETA_PP_REPEAT_4(M,C)  M(4,C)
#define CMETA_PP_REPEAT_6(M,C)  CMETA_PP_REPEAT_5(M,C)  M(5,C)
#define CMETA_PP_REPEAT_7(M,C)  CMETA_PP_REPEAT_6(M,C)  M(6,C)
#define CMETA_PP_REPEAT_8(M,C)  CMETA_PP_REPEAT_7(M,C)  M(7,C)
#define CMETA_PP_REPEAT_9(M,C)  CMETA_PP_REPEAT_8(M,C)  M(8,C)
#define CMETA_PP_REPEAT_10(M,C) CMETA_PP_REPEAT_9(M,C)  M(9,C)
#define CMETA_PP_REPEAT_11(M,C) CMETA_PP_REPEAT_10(M,C) M(10,C)
#define CMETA_PP_REPEAT_12(M,C) CMETA_PP_REPEAT_11(M,C) M(11,C)
#define CMETA_PP_REPEAT_13(M,C) CMETA_PP_REPEAT_12(M,C) M(12,C)
#define CMETA_PP_REPEAT_14(M,C) CMETA_PP_REPEAT_13(M,C) M(13,C)
#define CMETA_PP_REPEAT_15(M,C) CMETA_PP_REPEAT_14(M,C) M(14,C)
#define CMETA_PP_REPEAT_16(M,C) CMETA_PP_REPEAT_15(M,C) M(15,C)
#define CMETA_PP_REPEAT(N,M,C) \
    CMETA_PP_CAT(CMETA_PP_REPEAT_, N)(M,C)

#define CMETA_PP_FEI_1(M,C,a) M(0,a,C)
/* Indexed foreach is implemented by tuple-packing the remaining list so the
 * public API stays compact.  The explicit arities below avoid recursive macro
 * tricks and remain predictable under strict ISO C preprocessors. */
#define CMETA_PP_FEI_2(M,C,a,b) M(0,a,C) M(1,b,C)
#define CMETA_PP_FEI_3(M,C,a,b,c) M(0,a,C) M(1,b,C) M(2,c,C)
#define CMETA_PP_FEI_4(M,C,a,b,c,d) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C)
#define CMETA_PP_FEI_5(M,C,a,b,c,d,e) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C)
#define CMETA_PP_FEI_6(M,C,a,b,c,d,e,f) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C)
#define CMETA_PP_FEI_7(M,C,a,b,c,d,e,f,g) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C)
#define CMETA_PP_FEI_8(M,C,a,b,c,d,e,f,g,h) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C)
#define CMETA_PP_FEI_9(M,C,a,b,c,d,e,f,g,h,i) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C)
#define CMETA_PP_FEI_10(M,C,a,b,c,d,e,f,g,h,i,j) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C)
#define CMETA_PP_FEI_11(M,C,a,b,c,d,e,f,g,h,i,j,k) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C)
#define CMETA_PP_FEI_12(M,C,a,b,c,d,e,f,g,h,i,j,k,l) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C) M(11,l,C)
#define CMETA_PP_FEI_13(M,C,a,b,c,d,e,f,g,h,i,j,k,l,m) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C) M(11,l,C) M(12,m,C)
#define CMETA_PP_FEI_14(M,C,a,b,c,d,e,f,g,h,i,j,k,l,m,n) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C) M(11,l,C) M(12,m,C) M(13,n,C)
#define CMETA_PP_FEI_15(M,C,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C) M(11,l,C) M(12,m,C) M(13,n,C) M(14,o,C)
#define CMETA_PP_FEI_16(M,C,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) M(0,a,C) M(1,b,C) M(2,c,C) M(3,d,C) M(4,e,C) M(5,f,C) M(6,g,C) M(7,h,C) M(8,i,C) M(9,j,C) M(10,k,C) M(11,l,C) M(12,m,C) M(13,n,C) M(14,o,C) M(15,p,C)

#define CMETA_PP_FOR_EACH(M,C,...) CMETA_PP_FOR_EACH_A(M,C,__VA_ARGS__)
#define CMETA_PP_FOR_EACH_I(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEI_, CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)

/* Schema replay -------------------------------------------------------------
 *
 * One tuple-row kernel serves every higher-level CMeta schema DSL.
 *
 *   Schema(M, (A, 1), (B, 2))
 *
 * unpacks each row and invokes M(A, 1), M(B, 2). A named schema is an ordinary
 * function-like macro and Replay(schema, M) is the canonical consumer form:
 *
 *   #define MySchema(M) Schema(M, (A, 1), (B, 2))
 *   Replay(MySchema, DECLARE)
 *
 * CMETA_SCHEMA_ROWS is the lower, context-preserving primitive for DSLs such
 * as Struct whose mapper needs the original packed row plus an owner token.
 */
#define CMETA_SCHEMA_ROWS(M,C,...) \
    CMETA_PP_FOR_EACH_C(M,C,__VA_ARGS__)

#define CMETA_SCHEMA_APPLY_I(M, ...) M(__VA_ARGS__)
#define CMETA_SCHEMA_APPLY(row, M) \
    CMETA_SCHEMA_APPLY_I(M, CMETA_PP_UNPAREN row)
#define CMETA_SCHEMA(M, ...) \
    CMETA_SCHEMA_ROWS(CMETA_SCHEMA_APPLY, M, __VA_ARGS__)

#ifndef Schema
#define Schema(M, ...) CMETA_SCHEMA(M, __VA_ARGS__)
#endif

#ifndef Replay
#define Replay(schema, M) schema(M)
#endif

/* Operator source rows may be either the legacy normalized 15-field row or a
 * structured 7-field row. Both forms invoke the consumer with the same flat
 * 15-field ABI. Replay(schema, M) remains unchanged. */
#define CMETA_OPERATOR_ROW_APPLY(row, M) \
    CMETA_OPERATOR_ROW_APPLY_E(M, CMETA_PP_UNPAREN row)
#define CMETA_OPERATOR_ROW_APPLY_E(M, ...) \
    CMETA_OPERATOR_ROW_APPLY_I(M, CMETA_PP_NARG(__VA_ARGS__), __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_I(M, n, ...) \
    CMETA_OPERATOR_ROW_APPLY_II(M, n, __VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_II(M, n, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_ROW_APPLY_, n)(M, __VA_ARGS__)

#define CMETA_OPERATOR_ROW_APPLY_15(M, ...) M(__VA_ARGS__)

#define CMETA_OPERATOR_CALL_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_CALL_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_CALL_ARGS_call(margc, fnarg, childarg) \
    margc, fnarg, childarg

#define CMETA_OPERATOR_FN_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FN_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FN_ARGS_fn(arity, p0, p1, p2, ret) \
    arity, p0, p1, p2, ret

#define CMETA_OPERATOR_FLOW_ARGS(tag, ...) \
    CMETA_PP_CAT(CMETA_OPERATOR_FLOW_ARGS_, tag)(__VA_ARGS__)
#define CMETA_OPERATOR_FLOW_ARGS_flow(out, card, childrule) \
    out, card, childrule

#define CMETA_OPERATOR_SEMANTIC_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_SEMANTIC_ARG_, tag)(value)
#define CMETA_OPERATOR_SEMANTIC_ARG_semantic(value) value

#define CMETA_OPERATOR_EFFECT_ARG(tag, value) \
    CMETA_PP_CAT(CMETA_OPERATOR_EFFECT_ARG_, tag)(value)
#define CMETA_OPERATOR_EFFECT_ARG_effect(value) value

/* Expansion trampoline: subrow extractors intentionally emit commas, so they
 * must expand before the fixed 15-argument consumer signature is reparsed. */
#define CMETA_OPERATOR_ROW_APPLY_7(M, E, method, callrow, fnrow, flowrow, semanticrow, effectrow) \
    CMETA_OPERATOR_ROW_APPLY_STRUCTURED(M, E, method, \
        CMETA_OPERATOR_CALL_ARGS callrow, \
        CMETA_OPERATOR_FN_ARGS fnrow, \
        CMETA_OPERATOR_FLOW_ARGS flowrow, \
        CMETA_OPERATOR_SEMANTIC_ARG semanticrow, \
        CMETA_OPERATOR_EFFECT_ARG effectrow)
#define CMETA_OPERATOR_ROW_APPLY_STRUCTURED(...) \
    CMETA_OPERATOR_ROW_APPLY_STRUCTURED_I(__VA_ARGS__)
#define CMETA_OPERATOR_ROW_APPLY_STRUCTURED_I(M, E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect) \
    M(E, method, margc, fnarg, childarg, farity, p0, p1, p2, ret, out, card, childrule, semantic, effect)

/* Semantic compatibility alias. Operators owns source normalization; Schema
 * remains the generic tuple-row replay primitive. */
#ifndef Operators
#define Operators(M, ...) \
    CMETA_SCHEMA_ROWS(CMETA_OPERATOR_ROW_APPLY, M, __VA_ARGS__)
#endif

#endif
