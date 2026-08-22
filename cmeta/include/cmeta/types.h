#ifndef CMETA_TYPES_H
#define CMETA_TYPES_H

/*
 * Finite type universes for the CMeta system.
 *
 * Tuple fields:
 *   (TOKEN, C_TYPE, DESCRIPTOR_SYMBOL, KIND, TRAITS_SYMBOL)
 *
 * Known types participate in descriptors/reflection. Callable types are the
 * smaller finite set admitted to generated callable signature families. This
 * keeps reflected generic applications from expanding the callable Cartesian
 * product unless they are explicitly registered for callbacks.
 *
 * Compatibility:
 *   - projects defining only CMETA_TYPE_LIST retain the old behavior: that
 *     list is both the known and callable universe;
 *   - projects using CMETA_USER_TYPE_LIST retain builtins + user rows for both
 *     universes unless they explicitly override one of the new list macros.
 *
 * A shared configuration that changes callable signatures must be visible to
 * every translation unit because it changes cmeta_sig/cmeta_callable ABI.
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

#ifndef CMETA_KNOWN_TYPE_LIST
#  ifdef CMETA_TYPE_LIST
#    define CMETA_KNOWN_TYPE_LIST CMETA_TYPE_LIST
#  else
#    define CMETA_KNOWN_TYPE_LIST \
        CMETA_BUILTIN_TYPE_LIST CMETA_USER_TYPE_LIST
#  endif
#endif

#ifndef CMETA_CALLABLE_TYPE_LIST
#  ifdef CMETA_TYPE_LIST
#    define CMETA_CALLABLE_TYPE_LIST CMETA_TYPE_LIST
#  else
#    define CMETA_CALLABLE_TYPE_LIST CMETA_KNOWN_TYPE_LIST
#  endif
#endif

/* Deprecated compatibility spelling. Internal code should name the intended
 * universe explicitly. Keeping this alias preserves existing external config. */
#ifndef CMETA_TYPE_LIST
#define CMETA_TYPE_LIST CMETA_CALLABLE_TYPE_LIST
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
