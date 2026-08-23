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

/* Small probe layer used by tagged schema tokens such as TYPE(...). */
#define CMETA_PP_PROBE() ~, 1
#define CMETA_PP_SECOND(a, b, ...) b
#define CMETA_PP_IS_PROBE(...) CMETA_PP_SECOND(__VA_ARGS__, 0, 0)
#define CMETA_PP_PAREN_PROBE(...) CMETA_PP_PROBE()
#define CMETA_PP_IS_PAREN(x) CMETA_PP_IS_PROBE(CMETA_PP_PAREN_PROBE x)

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
    CMETA_PP_CAT(CMETA_PP_FEA_,CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)
#define CMETA_PP_FOR_EACH_B(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEB_,CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)
#define CMETA_PP_FOR_EACH_C(M,C,...) \
    CMETA_PP_CAT(CMETA_PP_FEC_,CMETA_PP_NARG(__VA_ARGS__))(M,C,__VA_ARGS__)

#define CMETA_SCHEMA_ROWS(M,C,...) \
    CMETA_PP_FOR_EACH_C(M,C,__VA_ARGS__)
#define CMETA_SCHEMA_APPLY(row,M) \
    CMETA_SCHEMA_APPLY_I(M, CMETA_PP_UNPAREN row)
#define CMETA_SCHEMA_APPLY_I(M,...) M(__VA_ARGS__)
#define CMETA_SCHEMA(M,...) \
    CMETA_SCHEMA_ROWS(CMETA_SCHEMA_APPLY, M, __VA_ARGS__)

#ifndef Schema
#define Schema(M, ...) CMETA_SCHEMA(M, __VA_ARGS__)
#endif

#ifndef Replay
#define Replay(M, C, ...) CMETA_SCHEMA_ROWS(M, C, __VA_ARGS__)
#endif

#endif /* CMETA_PP_H */
