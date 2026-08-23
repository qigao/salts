#ifndef CMETA_H
#define CMETA_H

#include <cmeta/interface.h>
#include <cmeta/status.h>
#include <cmeta/type_identity.h>
#include <cmeta/type_traits.h>

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
    const cmeta_type_traits *traits;
    const cmeta_type_identity *identity;
} cmeta_type_desc;

const cmeta_type_identity *cmeta_type_identity_of(const cmeta_type_desc *desc);
bool cmeta_type_desc_valid(const cmeta_type_desc *desc);
bool cmeta_type_equal(const cmeta_type_desc *a, const cmeta_type_desc *b);

static inline cmeta_status cmeta_type_require_traits(
    const cmeta_type_desc *type, cmeta_trait_flags required) {
    const cmeta_type_traits *traits;

    if (type == NULL || (required & CMETA_TRAIT_MASK) != required)
        return CMETA_INVALID_ARGUMENT;
    if (required == 0u)
        return CMETA_OK;

    traits = type->traits;
    if (traits == NULL || (traits->flags & required) != required)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_EQUAL) != 0u && traits->equal == NULL)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_HASH) != 0u && traits->hash == NULL)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_COMPARE) != 0u && traits->compare == NULL)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_COPY) != 0u && traits->copy_construct == NULL)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_MOVE) != 0u && traits->move_construct == NULL)
        return CMETA_TRAIT_MISSING;
    if ((required & CMETA_TRAIT_DESTROY) != 0u && traits->destroy == NULL)
        return CMETA_TRAIT_MISSING;
    return CMETA_OK;
}

Enum(cmeta_gen_status,
    (CMETA_GEN_VALUE,          1, "value"),
    (CMETA_GEN_VALUE_AND_DONE, 2, "value_and_done"),
    (CMETA_GEN_DONE,           3, "done"),
    (CMETA_GEN_ERROR,          4, "error"),
    (CMETA_GEN_MUTATED,        5, "mutated")
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
static inline bool cmeta_effects_valid(cmeta_effects e) {
    return (e & CMETA_EFFECT_MASK) == e;
}

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
    return (p & CMETA_PROP_MASK) == p;
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
#if defined(_MSC_VER)
    long double _align_long_double;
    long long _align_long_long;
    void *_align_pointer;
#else
    max_align_t _align;
#endif
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

typedef enum cmeta_callable_dispatch {
    CMETA_CALLABLE_DISPATCH_ADAPTER = 0,
    CMETA_CALLABLE_DISPATCH_CANONICAL_RAW = 1
} cmeta_callable_dispatch;

/* First-class immutable callable value. Plain typed functions and capturing
 * C-meta lambdas share this exact representation. dispatch states whether the
 * adapter or resolved raw target is authoritative. capture is copied by value
 * with the Graph, Subgraph snapshots and compiled plans. */
struct cmeta_callable {
    cmeta_fn meta; /* sig may be resolved lazily before Graph insertion */
    cmeta_callable_resolve_fn resolve;
    cmeta_callable_invoke_fn invoke;
    cmeta_callable_generate_fn generate;
    cmeta_callable_dispatch dispatch;
    size_t capture_size;
    cmeta_capture_storage capture;
};

#ifndef __cplusplus

/* Ordinary typed callables use a signature-specific adapter so their bound
 * metadata is validated once by Graph/Plan construction rather than decoded
 * again for every value. memcpy keeps erased arguments alignment-safe. */
#define CMETA_DETAIL_INVOKER_I(id) cmeta_detail_invoke_##id
#define CMETA_DETAIL_INVOKER_E(id) CMETA_DETAIL_INVOKER_I(id)
#define CMETA_DETAIL_INVOKER(id) CMETA_DETAIL_INVOKER_E(id)

static inline bool cmeta_detail_unsupported_invoke(
    const cmeta_callable *self, void *out, const void *const *args) {
    (void)self;
    (void)out;
    (void)args;
    return false;
}

#define CMETA_DEFINE_INVOKER_U(in, ret) \
    static inline bool CMETA_DETAIL_INVOKER(CMETA_U_ID(in, ret))( \
        const cmeta_callable *self, void *out, const void *const *args) { \
        CMETA_TYPE_CTYPE(in) a0; \
        CMETA_TYPE_CTYPE(ret) result; \
        if (!self || self->meta.sig != CMETA_SIG_NAME(CMETA_U_ID(in, ret)) || \
            !args || !args[0] || \
            !self->meta.call.CMETA_CALL_MEMBER(CMETA_U_ID(in, ret))) \
            return false; \
        memcpy(&a0, args[0], sizeof(a0)); \
        result = self->meta.call.CMETA_CALL_MEMBER(CMETA_U_ID(in, ret))(a0); \
        if (out) memcpy(out, &result, sizeof(result)); \
        return true; \
    }
#define CMETA_DEFINE_INVOKER_B(a, b, ret) \
    static inline bool CMETA_DETAIL_INVOKER(CMETA_B_ID(a, b, ret))( \
        const cmeta_callable *self, void *out, const void *const *args) { \
        CMETA_TYPE_CTYPE(a) a0; \
        CMETA_TYPE_CTYPE(b) a1; \
        CMETA_TYPE_CTYPE(ret) result; \
        if (!self || self->meta.sig != CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)) || \
            !args || !args[0] || !args[1] || \
            !self->meta.call.CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret))) \
            return false; \
        memcpy(&a0, args[0], sizeof(a0)); \
        memcpy(&a1, args[1], sizeof(a1)); \
        result = self->meta.call.CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret))(a0, a1); \
        if (out) memcpy(out, &result, sizeof(result)); \
        return true; \
    }
#define CMETA_DEFINE_INVOKER_G(in, out_type) \
    static inline bool CMETA_DETAIL_INVOKER(CMETA_G_ID(in, out_type))( \
        const cmeta_callable *self, void *out, const void *const *args) { \
        (void)self; \
        (void)out; \
        (void)args; \
        return false; \
    }
CMETA_ALL_SIGNATURES(CMETA_DEFINE_INVOKER_U, CMETA_DEFINE_INVOKER_B,
                     CMETA_DEFINE_INVOKER_G)
#undef CMETA_DEFINE_INVOKER_U
#undef CMETA_DEFINE_INVOKER_B
#undef CMETA_DEFINE_INVOKER_G

#define CMETA_INVOKER_ASSOC_U(in, ret) \
    , CMETA_FN_TYPE(CMETA_U_ID(in, ret)): CMETA_DETAIL_INVOKER(CMETA_U_ID(in, ret))
#define CMETA_INVOKER_ASSOC_B(a, b, ret) \
    , CMETA_FN_TYPE(CMETA_B_ID(a, b, ret)): CMETA_DETAIL_INVOKER(CMETA_B_ID(a, b, ret))
#define CMETA_INVOKER_ASSOC_G(in, out_type) \
    , CMETA_FN_TYPE(CMETA_G_ID(in, out_type)): CMETA_DETAIL_INVOKER(CMETA_G_ID(in, out_type))
#define CMETA_TYPED_INVOKER_ANY(fn) \
    _Generic(&(fn), default: cmeta_detail_unsupported_invoke \
        CMETA_ALL_SIGNATURES(CMETA_INVOKER_ASSOC_U, CMETA_INVOKER_ASSOC_B, \
                             CMETA_INVOKER_ASSOC_G))

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

#define CMETA_DETAIL_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, dispatch_kind, cap_size) \
    { .meta = { .sig = CMETA_SIG_INVALID, .call = { 0 }, \
                .effects = (cmeta_effects)(effect_set), \
                .properties = (cmeta_properties)(property_set) }, \
      .resolve = (resolver_fn), .invoke = (invoke_fn), .generate = (generate_fn), \
      .dispatch = (dispatch_kind), \
      .capture_size = (cap_size), .capture = { .bytes = { 0 } } }

#define CMETA_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, cap_size) \
    CMETA_DETAIL_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, \
                               CMETA_CALLABLE_DISPATCH_ADAPTER, cap_size)

#define CMETA_CANONICAL_RAW_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, cap_size) \
    CMETA_DETAIL_CALLABLE_INIT(effect_set, property_set, resolver_fn, invoke_fn, generate_fn, \
                               CMETA_CALLABLE_DISPATCH_CANONICAL_RAW, cap_size)

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
        return CMETA_TYPED_INVOKER_ANY(cmeta_typed_##name)(self, out, args); \
    } \
    static cmeta_gen_status cmeta_generate_##name(const cmeta_callable *self, const void *input, void *out, size_t *cursor) { \
        return self ? cmeta_fn_generate(self->meta, input, out, cursor) : CMETA_GEN_ERROR; \
    } \
    const cmeta_callable name = \
        CMETA_CANONICAL_RAW_CALLABLE_INIT(effect_set, property_set, cmeta_meta_##name, \
                                         cmeta_invoke_##name, cmeta_generate_##name, 0u); \
    static ret cmeta_typed_##name params
#define typed_any(contract, ret, name, params) \
    typed_any_raw(CMETA_CONTRACT_EFFECTS(contract), CMETA_CONTRACT_PROPERTIES(contract), ret, name, params)

#endif /* !__cplusplus */

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
 * signature once. Runtime consumers use the bound adapter unless the validated
 * dispatch contract explicitly permits canonical raw execution. */
bool cmeta_callable_bind(cmeta_callable in, cmeta_callable *out);
const cmeta_sig_desc *cmeta_callable_signature(cmeta_callable fn);
bool cmeta_callable_contract_valid(cmeta_callable fn);
bool cmeta_callable_can_dispatch_canonical_raw(cmeta_callable fn);
bool cmeta_callable_same(cmeta_callable a, cmeta_callable b);
bool cmeta_callable_invoke(const cmeta_callable *fn, void *out, const void *const *args);
cmeta_gen_status cmeta_callable_generate(const cmeta_callable *fn, const void *input,
                                         void *out, size_t *cursor);

#ifdef __cplusplus
}
#endif
#endif
