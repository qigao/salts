#ifndef CMETA_FUNCTION_H
#define CMETA_FUNCTION_H

#include <cmeta/cmeta.h>
#include <cmeta/abi.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t cmeta_param_flags;

enum {
    /* Zero preserves an explicit "direction not specified" state. */
    CMETA_PARAM_UNKNOWN = 0u,
    CMETA_PARAM_IN = 1u << 0,
    CMETA_PARAM_OUT = 1u << 1,
    CMETA_PARAM_INOUT = CMETA_PARAM_IN | CMETA_PARAM_OUT,
    CMETA_PARAM_NULLABLE = 1u << 2,
    CMETA_PARAM_BORROWED = 1u << 3,
    CMETA_PARAM_OWNED = 1u << 4,
    CMETA_PARAM_DIRECTION_MASK = CMETA_PARAM_IN | CMETA_PARAM_OUT,
    CMETA_PARAM_OWNERSHIP_MASK = CMETA_PARAM_BORROWED | CMETA_PARAM_OWNED,
    CMETA_PARAM_FLAG_MASK = CMETA_PARAM_DIRECTION_MASK | CMETA_PARAM_NULLABLE |
                            CMETA_PARAM_OWNERSHIP_MASK
};

typedef struct cmeta_param_desc {
    size_t size;
    const char *name;
    const cmeta_type_desc *type;
    cmeta_param_flags flags;
} cmeta_param_desc;

typedef struct cmeta_function_desc {
    size_t size;
    const char *name;
    const cmeta_type_desc *return_type;
    const cmeta_param_desc *params;
    size_t param_count;
    cmeta_effects effects;
    cmeta_properties properties;
} cmeta_function_desc;

typedef struct cmeta_function_abi_desc {
    size_t size;
    const cmeta_function_desc *function;
    cmeta_abi_carrier return_carrier;
    const cmeta_abi_carrier *param_carriers;
    size_t param_count;
} cmeta_function_abi_desc;

bool cmeta_param_desc_valid(const cmeta_param_desc *desc);
bool cmeta_function_desc_valid(const cmeta_function_desc *desc);
bool cmeta_function_abi_desc_valid(const cmeta_function_abi_desc *desc);

cmeta_abi_carrier
cmeta_function_param_abi(const cmeta_function_abi_desc *desc, size_t index);

static inline bool
cmeta_param_direction_known(const cmeta_param_desc *desc) {
    return desc != NULL &&
           (desc->flags & CMETA_PARAM_DIRECTION_MASK) != CMETA_PARAM_UNKNOWN;
}

const cmeta_param_desc *
cmeta_function_param(const cmeta_function_desc *desc, size_t index);

const cmeta_param_desc *
cmeta_function_find_param(const cmeta_function_desc *desc, const char *name);

#ifdef __cplusplus
}
#else

#include <cmeta/pp.h>
#include <cmeta/type_select.h>

/*
 * Function reflection is descriptive only. The declaration macros below emit
 * a normal C prototype plus immutable TU-local metadata and a static-inline
 * descriptor getter. They do not generate an erased ABI invocation path.
 *
 * Parameter rows are:
 *
 *   (type, name, flags)
 *   (type, name, flags, descriptor)
 *   (type, name, flags, descriptor, abi_carrier)
 *
 * The three-field form uses CMETA_TYPEOF(type) and carries scalar ABI metadata.
 * The four-field form remains the compatibility escape hatch for an explicit
 * descriptor and leaves ABI carrier unspecified.
 * The five-field form adds explicit ABI/FFI carrier metadata without changing
 * cmeta_param_desc/cmeta_function_desc binary layout.
 */

#define CMETA_FUNCTION_RETURN_TYPEOF(type) \
    _Generic((type *)0, \
        void *: &cmeta_type_void, \
        default: CMETA_TYPEOF(type))

#define CMETA_FUNCTION_ABI_SECOND_(a, b, ...) b
#define CMETA_FUNCTION_ABI_PROBE_() ~, 1
#define CMETA_FUNCTION_ABI_IS_PROBE_(...) \
    CMETA_FUNCTION_ABI_SECOND_(__VA_ARGS__, 0, 0)
#define CMETA_FUNCTION_ABI_VOID_MARK_void CMETA_FUNCTION_ABI_PROBE_()
#define CMETA_FUNCTION_RETURN_IS_VOID_(type) \
    CMETA_FUNCTION_ABI_IS_PROBE_( \
        CMETA_PP_CAT(CMETA_FUNCTION_ABI_VOID_MARK_, type))
#define CMETA_FUNCTION_RETURN_ABI_0 CMETA_ABI_SCALAR
#define CMETA_FUNCTION_RETURN_ABI_1 CMETA_ABI_VOID
#define CMETA_FUNCTION_RETURN_ABI(type) \
    CMETA_PP_CAT(CMETA_FUNCTION_RETURN_ABI_, \
                 CMETA_FUNCTION_RETURN_IS_VOID_(type))

#define CMETA_FUNCTION_COMMA_0
#define CMETA_FUNCTION_COMMA_1 ,
#define CMETA_FUNCTION_COMMA_2 ,
#define CMETA_FUNCTION_COMMA_3 ,
#define CMETA_FUNCTION_COMMA_4 ,
#define CMETA_FUNCTION_COMMA_5 ,
#define CMETA_FUNCTION_COMMA_6 ,
#define CMETA_FUNCTION_COMMA_7 ,
#define CMETA_FUNCTION_COMMA_8 ,
#define CMETA_FUNCTION_COMMA_9 ,
#define CMETA_FUNCTION_COMMA_10 ,
#define CMETA_FUNCTION_COMMA_11 ,
#define CMETA_FUNCTION_COMMA_12 ,
#define CMETA_FUNCTION_COMMA_13 ,
#define CMETA_FUNCTION_COMMA_14 ,
#define CMETA_FUNCTION_COMMA_15 ,

#define CMETA_FUNCTION_PARAM_DECL_3(type, name, flags) type name
#define CMETA_FUNCTION_PARAM_DECL_4(type, name, flags, descriptor) type name
#define CMETA_FUNCTION_PARAM_DECL_5(type, name, flags, descriptor, abi_carrier) \
    type name
#define CMETA_FUNCTION_PARAM_DECL_APPLY_I(...) \
    CMETA_PP_CAT(CMETA_FUNCTION_PARAM_DECL_, \
                 CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define CMETA_FUNCTION_PARAM_DECL_APPLY(row) \
    CMETA_FUNCTION_PARAM_DECL_APPLY_I row
#define CMETA_FUNCTION_PARAM_DECL(index, row, ignored) \
    CMETA_PP_CAT(CMETA_FUNCTION_COMMA_, index) \
    CMETA_FUNCTION_PARAM_DECL_APPLY(row)

#define CMETA_FUNCTION_PARAM_META_3(type, name, flags) \
    { sizeof(cmeta_param_desc), #name, CMETA_TYPEOF(type), \
      (cmeta_param_flags)(flags) },
#define CMETA_FUNCTION_PARAM_META_4(type, name, flags, descriptor) \
    { sizeof(cmeta_param_desc), #name, (descriptor), \
      (cmeta_param_flags)(flags) },
#define CMETA_FUNCTION_PARAM_META_5(type, name, flags, descriptor, abi_carrier) \
    { sizeof(cmeta_param_desc), #name, (descriptor), \
      (cmeta_param_flags)(flags) },
#define CMETA_FUNCTION_PARAM_META_APPLY_I(...) \
    CMETA_PP_CAT(CMETA_FUNCTION_PARAM_META_, \
                 CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define CMETA_FUNCTION_PARAM_META_APPLY(row) \
    CMETA_FUNCTION_PARAM_META_APPLY_I row
#define CMETA_FUNCTION_PARAM_META(row, ignored) \
    CMETA_FUNCTION_PARAM_META_APPLY(row)

#define CMETA_FUNCTION_PARAM_ABI_3(type, name, flags) CMETA_ABI_SCALAR
#define CMETA_FUNCTION_PARAM_ABI_4(type, name, flags, descriptor) \
    CMETA_ABI_UNSPECIFIED
#define CMETA_FUNCTION_PARAM_ABI_5(type, name, flags, descriptor, abi_carrier) \
    (abi_carrier)
#define CMETA_FUNCTION_PARAM_ABI_APPLY_I(...) \
    CMETA_PP_CAT(CMETA_FUNCTION_PARAM_ABI_, \
                 CMETA_PP_NARG(__VA_ARGS__))(__VA_ARGS__)
#define CMETA_FUNCTION_PARAM_ABI_APPLY(row) \
    CMETA_FUNCTION_PARAM_ABI_APPLY_I row
#define CMETA_FUNCTION_PARAM_ABI_ROW(row, ignored) \
    CMETA_FUNCTION_PARAM_ABI_APPLY(row),

/*
 * Optional declaration consumers may replay the exact FunctionDecl schema at
 * preprocessing time (for example to generate a test-only exact-ABI adapter).
 * The default is empty and CMeta never depends on those consumers.
 *
 * Extensions expand before the final declaration anchor, so they must emit
 * complete declarations/definitions of their own.
 */
#ifndef CMETA_FUNCTION_DECL_EXTENSION
#define CMETA_FUNCTION_DECL_EXTENSION(contract, return_type, return_desc, name, ...)
#endif

#ifndef CMETA_FUNCTION0_DECL_EXTENSION
#define CMETA_FUNCTION0_DECL_EXTENSION(contract, return_type, return_desc, name)
#endif

#ifndef CMETA_FUNCTION_DECL_ABI_EXTENSION
#define CMETA_FUNCTION_DECL_ABI_EXTENSION( \
    contract, return_type, return_desc, return_abi_carrier, name, ...)
#endif

#ifndef CMETA_FUNCTION0_DECL_ABI_EXTENSION
#define CMETA_FUNCTION0_DECL_ABI_EXTENSION( \
    contract, return_type, return_desc, return_abi_carrier, name)
#endif

#define CMETA_FUNCTION_DECL_AS_ABI( \
    contract, return_type, return_desc, return_abi_carrier, name, ...) \
    return_type name( \
        CMETA_PP_FOR_EACH_I(CMETA_FUNCTION_PARAM_DECL, ~, __VA_ARGS__)); \
    CMETA_LOCAL const cmeta_param_desc name##__function_params[] = { \
        CMETA_PP_FOR_EACH_A(CMETA_FUNCTION_PARAM_META, ~, __VA_ARGS__) \
    }; \
    CMETA_LOCAL const cmeta_function_desc name##__function_meta = { \
        sizeof(cmeta_function_desc), #name, (return_desc), \
        name##__function_params, CMETA_PP_NARG(__VA_ARGS__), \
        CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract) \
    }; \
    CMETA_LOCAL const cmeta_abi_carrier name##__function_param_abi[] = { \
        CMETA_PP_FOR_EACH_A(CMETA_FUNCTION_PARAM_ABI_ROW, ~, __VA_ARGS__) \
    }; \
    CMETA_LOCAL const cmeta_function_abi_desc name##__function_abi_meta = { \
        sizeof(cmeta_function_abi_desc), &name##__function_meta, \
        (return_abi_carrier), name##__function_param_abi, \
        CMETA_PP_NARG(__VA_ARGS__) \
    }; \
    CMETA_INLINE const cmeta_function_desc *name##_function(void) { \
        return &name##__function_meta; \
    } \
    CMETA_INLINE const cmeta_function_abi_desc *name##_function_abi(void) { \
        return &name##__function_abi_meta; \
    } \
    CMETA_FUNCTION_DECL_EXTENSION( \
        contract, return_type, return_desc, name, __VA_ARGS__) \
    CMETA_FUNCTION_DECL_ABI_EXTENSION( \
        contract, return_type, return_desc, return_abi_carrier, name, __VA_ARGS__) \
    typedef char name##__function_declaration_complete[1]

#define CMETA_FUNCTION_DECL_AS(contract, return_type, return_desc, name, ...) \
    CMETA_FUNCTION_DECL_AS_ABI( \
        contract, return_type, return_desc, CMETA_ABI_UNSPECIFIED, \
        name, __VA_ARGS__)

#define CMETA_FUNCTION_DECL(contract, return_type, name, ...) \
    CMETA_FUNCTION_DECL_AS_ABI( \
        contract, return_type, CMETA_FUNCTION_RETURN_TYPEOF(return_type), \
        CMETA_FUNCTION_RETURN_ABI(return_type), name, __VA_ARGS__)

#define CMETA_FUNCTION0_DECL_AS_ABI( \
    contract, return_type, return_desc, return_abi_carrier, name) \
    return_type name(void); \
    CMETA_LOCAL const cmeta_function_desc name##__function_meta = { \
        sizeof(cmeta_function_desc), #name, (return_desc), NULL, 0u, \
        CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract) \
    }; \
    CMETA_LOCAL const cmeta_function_abi_desc name##__function_abi_meta = { \
        sizeof(cmeta_function_abi_desc), &name##__function_meta, \
        (return_abi_carrier), NULL, 0u \
    }; \
    CMETA_INLINE const cmeta_function_desc *name##_function(void) { \
        return &name##__function_meta; \
    } \
    CMETA_INLINE const cmeta_function_abi_desc *name##_function_abi(void) { \
        return &name##__function_abi_meta; \
    } \
    CMETA_FUNCTION0_DECL_EXTENSION(contract, return_type, return_desc, name) \
    CMETA_FUNCTION0_DECL_ABI_EXTENSION( \
        contract, return_type, return_desc, return_abi_carrier, name) \
    typedef char name##__function_declaration_complete[1]

#define CMETA_FUNCTION0_DECL_AS(contract, return_type, return_desc, name) \
    CMETA_FUNCTION0_DECL_AS_ABI( \
        contract, return_type, return_desc, CMETA_ABI_UNSPECIFIED, name)

#define CMETA_FUNCTION0_DECL(contract, return_type, name) \
    CMETA_FUNCTION0_DECL_AS_ABI( \
        contract, return_type, CMETA_FUNCTION_RETURN_TYPEOF(return_type), \
        CMETA_FUNCTION_RETURN_ABI(return_type), name)

#define CMETA_FUNCTION_META(name) (name##_function())
#define CMETA_FUNCTION_ABI(name) (name##_function_abi())

#ifndef FunctionDecl
#define FunctionDecl(...) CMETA_FUNCTION_DECL(__VA_ARGS__)
#endif

#ifndef FunctionDeclAs
#define FunctionDeclAs(...) CMETA_FUNCTION_DECL_AS(__VA_ARGS__)
#endif

#ifndef FunctionDeclAsAbi
#define FunctionDeclAsAbi(...) CMETA_FUNCTION_DECL_AS_ABI(__VA_ARGS__)
#endif

#ifndef Function0Decl
#define Function0Decl(...) CMETA_FUNCTION0_DECL(__VA_ARGS__)
#endif

#ifndef Function0DeclAs
#define Function0DeclAs(...) CMETA_FUNCTION0_DECL_AS(__VA_ARGS__)
#endif

#ifndef Function0DeclAsAbi
#define Function0DeclAsAbi(...) CMETA_FUNCTION0_DECL_AS_ABI(__VA_ARGS__)
#endif

#ifndef FunctionMeta
#define FunctionMeta(name) CMETA_FUNCTION_META(name)
#endif

#ifndef FunctionAbi
#define FunctionAbi(name) CMETA_FUNCTION_ABI(name)
#endif

#endif /* !__cplusplus */

#endif /* CMETA_FUNCTION_H */
