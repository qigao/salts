#ifndef CMETA_VALUE_H
#define CMETA_VALUE_H

#include <cmeta/generic.h>
#include <stdbool.h>
#include <stddef.h>

/* Built-in CMeta value-kind registrations. */
#define CMETA_GENERIC_KIND_Pair CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Tuple CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Option CMETA_GENERIC_PROBE()
#define CMETA_GENERIC_KIND_Result CMETA_GENERIC_PROBE()

/* Pair ------------------------------------------------------------------- */
#define CMETA_TYPED_Pair(name, first_type, second_type) \
    typedef struct name { \
        first_type first; \
        second_type second; \
    } name; \
    enum { name##_arity = 2 }

#define PairMake(type_name, first_value, second_value) \
    ((type_name){ .first = (first_value), .second = (second_value) })

/* Tuple ------------------------------------------------------------------
 * Tuple fields deliberately use v0..v15 rather than _0.._15: leading
 * underscore identifiers at file scope are reserved by ISO C.
 */
#define CMETA_TUPLE_FIELD_0(t)  t v0;
#define CMETA_TUPLE_FIELD_1(t)  t v1;
#define CMETA_TUPLE_FIELD_2(t)  t v2;
#define CMETA_TUPLE_FIELD_3(t)  t v3;
#define CMETA_TUPLE_FIELD_4(t)  t v4;
#define CMETA_TUPLE_FIELD_5(t)  t v5;
#define CMETA_TUPLE_FIELD_6(t)  t v6;
#define CMETA_TUPLE_FIELD_7(t)  t v7;
#define CMETA_TUPLE_FIELD_8(t)  t v8;
#define CMETA_TUPLE_FIELD_9(t)  t v9;
#define CMETA_TUPLE_FIELD_10(t) t v10;
#define CMETA_TUPLE_FIELD_11(t) t v11;
#define CMETA_TUPLE_FIELD_12(t) t v12;
#define CMETA_TUPLE_FIELD_13(t) t v13;
#define CMETA_TUPLE_FIELD_14(t) t v14;
#define CMETA_TUPLE_FIELD_15(t) t v15;

#define CMETA_TUPLE_2(name,a,b) \
    typedef struct name { a v0; b v1; } name; enum { name##_arity = 2 }
#define CMETA_TUPLE_3(name,a,b,c) \
    typedef struct name { a v0; b v1; c v2; } name; enum { name##_arity = 3 }
#define CMETA_TUPLE_4(name,a,b,c,d) \
    typedef struct name { a v0; b v1; c v2; d v3; } name; enum { name##_arity = 4 }
#define CMETA_TUPLE_5(name,a,b,c,d,e) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; } name; enum { name##_arity = 5 }
#define CMETA_TUPLE_6(name,a,b,c,d,e,f) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; } name; enum { name##_arity = 6 }
#define CMETA_TUPLE_7(name,a,b,c,d,e,f,g) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; } name; enum { name##_arity = 7 }
#define CMETA_TUPLE_8(name,a,b,c,d,e,f,g,h) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; } name; enum { name##_arity = 8 }
#define CMETA_TUPLE_9(name,a,b,c,d,e,f,g,h,i) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; } name; enum { name##_arity = 9 }
#define CMETA_TUPLE_10(name,a,b,c,d,e,f,g,h,i,j) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; } name; enum { name##_arity = 10 }
#define CMETA_TUPLE_11(name,a,b,c,d,e,f,g,h,i,j,k) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; } name; enum { name##_arity = 11 }
#define CMETA_TUPLE_12(name,a,b,c,d,e,f,g,h,i,j,k,l) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; l v11; } name; enum { name##_arity = 12 }
#define CMETA_TUPLE_13(name,a,b,c,d,e,f,g,h,i,j,k,l,m) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; l v11; m v12; } name; enum { name##_arity = 13 }
#define CMETA_TUPLE_14(name,a,b,c,d,e,f,g,h,i,j,k,l,m,n) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; l v11; m v12; n v13; } name; enum { name##_arity = 14 }
#define CMETA_TUPLE_15(name,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; l v11; m v12; n v13; o v14; } name; enum { name##_arity = 15 }
#define CMETA_TUPLE_16(name,a,b,c,d,e,f,g,h,i,j,k,l,m,n,o,p) \
    typedef struct name { a v0; b v1; c v2; d v3; e v4; f v5; g v6; h v7; i v8; j v9; k v10; l v11; m v12; n v13; o v14; p v15; } name; enum { name##_arity = 16 }

#define CMETA_TYPED_Tuple(name, ...) \
    CMETA_PP_CAT(CMETA_TUPLE_, CMETA_PP_NARG(__VA_ARGS__))(name, __VA_ARGS__)

/* Positional construction remains explicit and ordinary C: 
 *   Point3 p = { .v0 = 1.0, .v1 = 2.0, .v2 = 3.0 };
 */
#define TupleArity(type_name) ((size_t)type_name##_arity)

/* Option ----------------------------------------------------------------- */
#define CMETA_TYPED_Option(name, value_type) \
    typedef struct name { \
        bool has_value; \
        value_type value; \
    } name

#define OptionSome(type_name, value_expr) \
    ((type_name){ .has_value = true, .value = (value_expr) })
#define OptionNone(type_name) ((type_name){ .has_value = false })
#define OptionHas(option_expr) ((option_expr).has_value)

/* Result ----------------------------------------------------------------- */
#define CMETA_TYPED_Result(name, value_type, error_type) \
    typedef struct name { \
        bool ok; \
        union { value_type value; error_type error; } data; \
    } name

#define ResultOk(type_name, value_expr) \
    ((type_name){ .ok = true, .data.value = (value_expr) })
#define ResultErr(type_name, error_expr) \
    ((type_name){ .ok = false, .data.error = (error_expr) })
#define ResultIsOk(result_expr) ((result_expr).ok)
#define ResultIsErr(result_expr) (!(result_expr).ok)

#endif /* CMETA_VALUE_H */
