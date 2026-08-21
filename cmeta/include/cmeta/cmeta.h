#ifndef CMETA_H
#define CMETA_H

#include <cmeta/interface.h>
#include <cmeta/type_identity.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <cmeta/enum.h>
#include <cmeta/generic.h>
#include <cmeta/value.h>

#ifdef __cplusplus
extern "C" {
#endif

Enum(cmeta_type_kind,
    (CMETA_T_VOID,    "void"),
    (CMETA_T_BOOL,    "bool"),
    (CMETA_T_INTEGER, "integer"),
    (CMETA_T_FLOAT,   "float"),
    (CMETA_T_POINTER, "pointer"),
    (CMETA_T_OBJECT,  "object")
);

typedef struct cmeta_type_desc {
    const char *name;
    size_t size;
    size_t align;
    cmeta_type_kind kind;
    const struct cmeta_type_desc *pointee;
} cmeta_type_desc;

Enum(cmeta_gen_status,
    (CMETA_GEN_VALUE,          1, "value"),
    (CMETA_GEN_VALUE_AND_DONE, 2, "value_and_done"),
    (CMETA_GEN_DONE,           3, "done"),
    (CMETA_GEN_ERROR,          4, "error")
);

#include <cmeta/signatures.h>

extern const cmeta_type_desc cmeta_type_void;
extern const cmeta_type_desc cmeta_type_size;
extern const cmeta_type_desc cmeta_type_size_ptr;
extern const cmeta_type_desc cmeta_type_gen_status;

#define CMETA_DESC_PTR_I(desc) desc##_ptr
#define CMETA_DESC_PTR_E(desc) CMETA_DESC_PTR_I(desc)
#define CMETA_DESC_PTR(row) CMETA_DESC_PTR_E(CMETA_TYPE_DESC(row))
#define CMETA_DECLARE_TYPE(row, ignored) \
    extern const cmeta_type_desc CMETA_TYPE_DESC(row); \
    extern const cmeta_type_desc CMETA_DESC_PTR(row);
CMETA_PP_FOR_EACH_A(CMETA_DECLARE_TYPE, ~, CMETA_KNOWN_TYPE_LIST)
#undef CMETA_DECLARE_TYPE

bool cmeta_type_equal(const cmeta_type_desc *a, const cmeta_type_desc *b);
size_t cmeta_type_registry_count(void);
const cmeta_type_desc *cmeta_type_registry_at(size_t index);
const cmeta_type_desc *cmeta_type_find(const char *name);

#define CMETA_DECL_U(in, ret) \
    typedef CMETA_TYPE_CTYPE(ret) (*CMETA_FN_TYPE(CMETA_U_ID(in, ret)))(CMETA_TYPE_CTYPE(in));
#define CMETA_DECL_B(a, b, ret) \
    typedef CMETA_TYPE_CTYPE(ret) (*CMETA_FN_TYPE(CMETA_B_ID(a, b, ret)))( \
        CMETA_TYPE_CTYPE(a), CMETA_TYPE_CTYPE(b));
#define CMETA_DECL_G(in, out) \
    typedef cmeta_gen_status (*CMETA_FN_TYPE(CMETA_G_ID(in, out)))( \
        CMETA_TYPE_CTYPE(in), CMETA_TYPE_CTYPE(out) *, size_t *);
CMETA_ALL_SIGNATURES(CMETA_DECL_U, CMETA_DECL_B, CMETA_DECL_G)
#undef CMETA_DECL_U
#undef CMETA_DECL_B
#undef CMETA_DECL_G

typedef enum cmeta_sig {
    CMETA_SIG_INVALID = 0,
#define CMETA_ENUM_U(in, ret) CMETA_SIG_NAME(CMETA_U_ID(in, ret)),
#define CMETA_ENUM_B(a, b, ret) CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)),
#define CMETA_ENUM_G(in, out) CMETA_SIG_NAME(CMETA_G_ID(in, out)),
    CMETA_ALL_SIGNATURES(CMETA_ENUM_U, CMETA_ENUM_B, CMETA_ENUM_G)
#undef CMETA_ENUM_U
#undef CMETA_ENUM_B
#undef CMETA_ENUM_G
    CMETA_SIG_COUNT
} cmeta_sig;

typedef uint32_t cmeta_effects;

enum {
    /* PURE is the empty effect set. Other flags may be ORed together. */
    CMETA_EFFECT_PURE     = 0u,
    CMETA_EFFECT_STATEFUL = 1u << 0,
    CMETA_EFFECT_ASYNC    = 1u << 1,
    CMETA_EFFECT_IO       = 1u << 2,
    CMETA_EFFECT_MAY_FAIL = 1u << 3,
    /* UNKNOWN is conservative: optimizers must not assume purity. */
    CMETA_EFFECT_UNKNOWN  = 1u << 4,
    CMETA_EFFECT_MASK     = CMETA_EFFECT_STATEFUL | CMETA_EFFECT_ASYNC |
                            CMETA_EFFECT_IO | CMETA_EFFECT_MAY_FAIL |
                            CMETA_EFFECT_UNKNOWN
};

static inline bool cmeta_effects_are_pure(cmeta_effects e) { return e == CMETA_EFFECT_PURE; }
static inline bool cmeta_effects_valid(cmeta_effects e) { return (e & ~CMETA_EFFECT_MASK) == 0u; }

/* Positive semantic guarantees. Zero means no guarantee is known. Unlike
 * effects, these are not all closed under arbitrary composition: in
 * particular IDEMPOTENT is a unary endomorphism contract and must not be
 * blindly propagated to a whole Graph. */
typedef uint32_t cmeta_properties;
enum {
    CMETA_PROP_NONE          = 0u,
    CMETA_PROP_DETERMINISTIC = 1u << 0,
    CMETA_PROP_TOTAL         = 1u << 1,
    CMETA_PROP_IDEMPOTENT    = 1u << 2,
    CMETA_PROP_NO_ALIAS      = 1u << 3,
    /* Algebraic law for binary endomorphisms T(T,T)->T.  It is an
     * admission contract used by static analysis and future C-side rewrites. */
    CMETA_PROP_ASSOCIATIVE    = 1u << 4,
    CMETA_PROP_MASK          = CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL |
                               CMETA_PROP_IDEMPOTENT | CMETA_PROP_NO_ALIAS |
                               CMETA_PROP_ASSOCIATIVE
};
#define CMETA_PROP_STABLE (CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL)

static inline bool cmeta_properties_valid(cmeta_properties p) {
    return (p & ~CMETA_PROP_MASK) == 0u;
}
static inline bool cmeta_properties_include(cmeta_properties p, cmeta_properties required) {
    return (p & required) == required;
}

#include <cmeta/contract.h>

typedef union cmeta_raw_call {
#define CMETA_UNION_U(in, ret) \
    CMETA_FN_TYPE(CMETA_U_ID(in, ret)) CMETA_CALL_MEMBER(CMETA_U_ID(in, ret));
#define CMETA_UNION_B(a, b, ret) \
    CMETA_FN_TYPE(CMETA_B_ID(a, b, ret)) CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret));
#define CMETA_UNION_G(in, out) \
    CMETA_FN_TYPE(CMETA_G_ID(in, out)) CMETA_CALL_MEMBER(CMETA_G_ID(in, out));
    CMETA_ALL_SIGNATURES(CMETA_UNION_U, CMETA_UNION_B, CMETA_UNION_G)
#undef CMETA_UNION_U
#undef CMETA_UNION_B
#undef CMETA_UNION_G
} cmeta_raw_call;

/* Low-level signature descriptor used by the C11 type registry.  User-facing
 * Graph APIs use cmeta_callable below; this raw descriptor remains the typed
 * adapter substrate. */
typedef struct cmeta_fn {
    cmeta_sig sig;
    cmeta_raw_call call;
    cmeta_effects effects;
    cmeta_properties properties;
} cmeta_fn;

#define CMETA_CAPTURE_INLINE 32u

typedef union cmeta_capture_storage {
    max_align_t _align;
    unsigned char bytes[CMETA_CAPTURE_INLINE];
} cmeta_capture_storage;

typedef struct cmeta_callable cmeta_callable;
typedef cmeta_fn (*cmeta_callable_resolve_fn)(void);
typedef bool (*cmeta_callable_invoke_fn)(const cmeta_callable *self,
                                         void *out,
                                         const void *const *args);
typedef cmeta_gen_status (*cmeta_callable_generate_fn)(const cmeta_callable *self,
                                                       const void *input,
                                                       void *out,
                                                       size_t *cursor);

/* First-class immutable callable value.  Plain typed functions and capturing
 * C-meta lambdas share this exact representation.  capture is copied by value
 * with the Graph, Subgraph snapshots and compiled plans. */
struct cmeta_callable {
    cmeta_fn meta; /* sig may be resolved lazily before Graph insertion */
    cmeta_callable_resolve_fn resolve;
    cmeta_callable_invoke_fn invoke;
    cmeta_callable_generate_fn generate;
    size_t capture_size;
    cmeta_capture_storage capture;
};

#define CMETA_MAKE_U(in, ret) \
    static inline cmeta_fn CMETA_MAKER(CMETA_U_ID(in, ret))( \
        CMETA_FN_TYPE(CMETA_U_ID(in, ret)) fn) { \
        cmeta_fn x = { CMETA_SIG_NAME(CMETA_U_ID(in, ret)), \
            { .CMETA_CALL_MEMBER(CMETA_U_ID(in, ret)) = fn }, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE }; \
        return x; \
    }
#define CMETA_MAKE_B(a, b, ret) \
    static inline cmeta_fn CMETA_MAKER(CMETA_B_ID(a, b, ret))( \
        CMETA_FN_TYPE(CMETA_B_ID(a, b, ret)) fn) { \
        cmeta_fn x = { CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)), \
            { .CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret)) = fn }, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE }; \
        return x; \
    }
#define CMETA_MAKE_G(in, out) \
    static inline cmeta_fn CMETA_MAKER(CMETA_G_ID(in, out))( \
        CMETA_FN_TYPE(CMETA_G_ID(in, out)) fn) { \
        cmeta_fn x = { CMETA_SIG_NAME(CMETA_G_ID(in, out)), \
            { .CMETA_CALL_MEMBER(CMETA_G_ID(in, out)) = fn }, CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE }; \
        return x; \
    }
CMETA_ALL_SIGNATURES(CMETA_MAKE_U, CMETA_MAKE_B, CMETA_MAKE_G)
#undef CMETA_MAKE_U
#undef CMETA_MAKE_B
#undef CMETA_MAKE_G

static inline void cmeta_unsupported_signature(void) { }

#define CMETA_ASSOC_U(in, ret) \
    , CMETA_FN_TYPE(CMETA_U_ID(in, ret)): CMETA_MAKER(CMETA_U_ID(in, ret))
#define CMETA_ASSOC_B(a, b, ret) \
    , CMETA_FN_TYPE(CMETA_B_ID(a, b, ret)): CMETA_MAKER(CMETA_B_ID(a, b, ret))
#define CMETA_ASSOC_G(in, out) \
    , CMETA_FN_TYPE(CMETA_G_ID(in, out)): CMETA_MAKER(CMETA_G_ID(in, out))
#define CMETA_WRAP_TYPED_ANY(fn) \
    _Generic(&(fn), default: cmeta_unsupported_signature \
        CMETA_ALL_SIGNATURES(CMETA_ASSOC_U, CMETA_ASSOC_B, CMETA_ASSOC_G))(&(fn))

#define CMETA_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, cap_size) \
    { .meta = { .sig = CMETA_SIG_INVALID, .call = { 0 }, \
                .effects = (cmeta_effects)(effect_set), \
                .properties = (cmeta_properties)(property_set) }, \
      .resolve = (resolver_fn), .invoke = (invoke_fn), .generate = (generate_fn), \
      .capture_size = (cap_size), .capture = { .bytes = { 0 } } }

/* Raw first-class named callable for custom IR code. */
#define typed_any_raw(effect_set, property_set, ret, name, params) \
    static ret cmeta_typed_##name params; \
    static cmeta_fn cmeta_meta_##name(void) { \
        cmeta_fn x = CMETA_WRAP_TYPED_ANY(cmeta_typed_##name); \
        x.effects = (cmeta_effects)(effect_set); \
        x.properties = (cmeta_properties)(property_set); \
        return x; \
    } \
    static bool cmeta_invoke_##name(const cmeta_callable *self, void *out, const void *const *args) { \
        return self ? cmeta_fn_invoke(self->meta, out, args) : false; \
    } \
    static cmeta_gen_status cmeta_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        return self ? cmeta_fn_generate(self->meta, input, out, cursor) : CMETA_GEN_ERROR; \
    } \
    const cmeta_callable name = \
        CMETA_CALLABLE_INIT(effect_set, property_set, cmeta_meta_##name, \
                           cmeta_invoke_##name, cmeta_generate_##name, 0u); \
    static ret cmeta_typed_##name params
#define typed_any(contract, ret, name, params) \
    typed_any_raw(CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), ret, name, params)
#define typed_any_decl(name) extern const cmeta_callable name

Enum(cmeta_fn_protocol,
    (CMETA_FN_PROTOCOL_VALUE,     "value"),
    (CMETA_FN_PROTOCOL_GENERATOR, "generator")
);

typedef struct cmeta_sig_desc {
    cmeta_sig sig;
    const char *spelling;
    const cmeta_type_desc *return_type;
    const cmeta_type_desc *params[3];
    size_t param_count;
    cmeta_fn_protocol protocol;
} cmeta_sig_desc;

const cmeta_sig_desc *cmeta_fn_signature(cmeta_fn fn);
const char *cmeta_sig_to_string(cmeta_sig sig);
const char *cmeta_sig_to_symbol(cmeta_sig sig);
bool cmeta_fn_contract_valid(cmeta_fn fn);
bool cmeta_fn_invoke(cmeta_fn fn, void *out, const void *const *args);
cmeta_gen_status cmeta_fn_generate(cmeta_fn fn, const void *input,
                                   void *out, size_t *cursor);

/* First-class callable operations. Graph insertion resolves the logical
 * signature once; runtime invocation thereafter uses the bound erased adapter. */
bool cmeta_callable_bind(cmeta_callable in, cmeta_callable *out);
const cmeta_sig_desc *cmeta_callable_signature(cmeta_callable fn);
bool cmeta_callable_contract_valid(cmeta_callable fn);
bool cmeta_callable_same(cmeta_callable a, cmeta_callable b);
bool cmeta_callable_invoke(const cmeta_callable *fn, void *out, const void *const *args);
cmeta_gen_status cmeta_callable_generate(const cmeta_callable *fn, const void *input,
                                         void *out, size_t *cursor);

#ifdef __cplusplus
}
#endif
#endif
