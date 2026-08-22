#ifndef CMETA_TYPES_H
#define CMETA_TYPES_H

/*
 * The finite element-type universe for the pure C11 meta system.
 *
 * Tuple fields:
 *   (TOKEN, C_TYPE, DESCRIPTOR_SYMBOL, KIND, TRAITS_SYMBOL)
 *
 * Every row automatically participates in every generated signature family:
 *   U(T)->R, B(T,U)->R, expand T->U, and cursor-generator T->U.
 *
 * A project may add types in a force-included/shared config header:
 *
 *   typedef struct Point { int x, y; } Point;
 *   #define CMETA_USER_TYPE_LIST \
 *       , (P, Point, cmeta_type_point, CMETA_T_OBJECT, cmeta_traits_point)
 *
 * The leading comma is intentional.  The same configuration must be visible
 * to every translation unit because it changes cmeta_sig and cmeta_callable.
 */
#ifdef __cplusplus
#define CMETA_BOOL_TYPE bool
#else
#define CMETA_BOOL_TYPE _Bool
#endif

#define CMETA_ROW_B (B, CMETA_BOOL_TYPE, cmeta_type_bool, CMETA_T_BOOL, cmeta_traits_bool)
#define CMETA_ROW_I (I, int,    cmeta_type_int,    CMETA_T_INTEGER, cmeta_traits_int)
#define CMETA_ROW_L (L, long,   cmeta_type_long,   CMETA_T_INTEGER, cmeta_traits_long)
#define CMETA_ROW_F (F, float,  cmeta_type_float,  CMETA_T_FLOAT,   cmeta_traits_float)
#define CMETA_ROW_D (D, double, cmeta_type_double, CMETA_T_FLOAT,   cmeta_traits_double)

#define CMETA_BUILTIN_TYPE_LIST \
    CMETA_ROW_B, CMETA_ROW_I, CMETA_ROW_L, CMETA_ROW_F, CMETA_ROW_D

#ifndef CMETA_USER_TYPE_LIST
#define CMETA_USER_TYPE_LIST
#endif

#ifndef CMETA_TYPE_LIST
#define CMETA_TYPE_LIST CMETA_BUILTIN_TYPE_LIST CMETA_USER_TYPE_LIST
#endif

#define CMETA_TYPE_TOKEN(row) CMETA_TYPE_TOKEN_I row
#define CMETA_TYPE_TOKEN_I(tok, ctype, desc, kind, traits) tok
#define CMETA_TYPE_CTYPE(row) CMETA_TYPE_CTYPE_I row
#define CMETA_TYPE_CTYPE_I(tok, ctype, desc, kind, traits) ctype
#define CMETA_TYPE_DESC(row) CMETA_TYPE_DESC_I row
#define CMETA_TYPE_DESC_I(tok, ctype, desc, kind, traits) desc
#define CMETA_TYPE_KIND(row) CMETA_TYPE_KIND_I row
#define CMETA_TYPE_KIND_I(tok, ctype, desc, kind, traits) kind
#define CMETA_TYPE_TRAITS(row) CMETA_TYPE_TRAITS_I row
#define CMETA_TYPE_TRAITS_I(tok, ctype, desc, kind, traits) traits

#endif
