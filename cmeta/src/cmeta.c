#include <cmeta/cmeta.h>

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define CMETA_STR_I(x) #x
#define CMETA_STR(x) CMETA_STR_I(x)

_Static_assert(CMETA_FLOAT_TRAITS_OBJECT_HASH_WIDTHS,
               "floating trait hashes require eight-bit bytes and matching copy widths");
_Static_assert(sizeof(float) == 4u,
               "floating trait hashes require a binary32-sized float");
_Static_assert(sizeof(double) == 8u,
               "floating trait hashes require a binary64-sized double");
_Static_assert(FLT_RADIX == 2,
               "floating trait hashes require a binary radix");
_Static_assert(FLT_MANT_DIG == 24 && FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128,
               "floating trait hashes require binary32 precision and exponent range");
_Static_assert(DBL_MANT_DIG == 53 && DBL_MIN_EXP == -1021 && DBL_MAX_EXP == 1024,
               "floating trait hashes require binary64 precision and exponent range");

#define CMETA_TRIVIAL_TRAIT_FLAGS \
    (CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE | \
     CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY | \
     CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY)

#define CMETA_DEFINE_TRIVIAL_TRAITS(prefix, type) \
    static bool prefix##_equal(const void *left, const void *right) { \
        return left != NULL && right != NULL && \
               *(const type *)left == *(const type *)right; \
    } \
    static uint64_t prefix##_hash(const void *value) { \
        return value == NULL ? 0u : (uint64_t)*(const type *)value; \
    } \
    static int prefix##_compare(const void *left, const void *right) { \
        type lhs; type rhs; \
        if (left == NULL || right == NULL) return 0; \
        lhs = *(const type *)left; rhs = *(const type *)right; \
        return lhs < rhs ? -1 : lhs > rhs; \
    } \
    static bool prefix##_copy_construct(void *destination, const void *source) { \
        if (destination == NULL || source == NULL) return false; \
        *(type *)destination = *(const type *)source; return true; \
    } \
    static void prefix##_move_construct(void *destination, void *source) { \
        if (destination != NULL && source != NULL) \
            *(type *)destination = *(type *)source; \
    } \
    static void prefix##_destroy(void *value) { (void)value; }

CMETA_DEFINE_TRIVIAL_TRAITS(cmeta_bool, _Bool)
CMETA_DEFINE_TRIVIAL_TRAITS(cmeta_int, int)
CMETA_DEFINE_TRIVIAL_TRAITS(cmeta_long, long)

const cmeta_type_traits cmeta_traits_bool = { CMETA_TRIVIAL_TRAIT_FLAGS,
    cmeta_bool_equal, cmeta_bool_hash, cmeta_bool_compare,
    cmeta_bool_copy_construct, cmeta_bool_move_construct, cmeta_bool_destroy };
const cmeta_type_traits cmeta_traits_int = { CMETA_TRIVIAL_TRAIT_FLAGS,
    cmeta_int_equal, cmeta_int_hash, cmeta_int_compare,
    cmeta_int_copy_construct, cmeta_int_move_construct, cmeta_int_destroy };
const cmeta_type_traits cmeta_traits_long = { CMETA_TRIVIAL_TRAIT_FLAGS,
    cmeta_long_equal, cmeta_long_hash, cmeta_long_compare,
    cmeta_long_copy_construct, cmeta_long_move_construct, cmeta_long_destroy };

#define CMETA_DEFINE_FLOAT_TRAITS(prefix, type, bits_type, zero, nan_hash) \
    static bool prefix##_equal(const void *left, const void *right) { \
        type lhs; type rhs; \
        if (left == NULL || right == NULL) return false; \
        lhs = *(const type *)left; rhs = *(const type *)right; \
        return (isnan(lhs) && isnan(rhs)) || lhs == rhs; \
    } \
    static uint64_t prefix##_hash(const void *value) { \
        type number; bits_type bits; \
        if (value == NULL) return 0u; \
        number = *(const type *)value; \
        if (isnan(number)) return (nan_hash); \
        if (number == (zero)) return 0u; \
        memcpy(&bits, &number, sizeof(bits)); return (uint64_t)bits; \
    } \
    static int prefix##_compare(const void *left, const void *right) { \
        type lhs; type rhs; \
        if (left == NULL || right == NULL) return 0; \
        lhs = *(const type *)left; rhs = *(const type *)right; \
        if (isnan(lhs)) return isnan(rhs) ? 0 : 1; \
        if (isnan(rhs)) return -1; \
        return lhs < rhs ? -1 : lhs > rhs; \
    } \
    static bool prefix##_copy_construct(void *destination, const void *source) { \
        if (destination == NULL || source == NULL) return false; \
        *(type *)destination = *(const type *)source; return true; \
    } \
    static void prefix##_move_construct(void *destination, void *source) { \
        if (destination != NULL && source != NULL) \
            *(type *)destination = *(type *)source; \
    } \
    static void prefix##_destroy(void *value) { (void)value; }

CMETA_DEFINE_FLOAT_TRAITS(cmeta_float, float, uint32_t, 0.0f,
                          UINT64_C(0x7fc00000))
CMETA_DEFINE_FLOAT_TRAITS(cmeta_double, double, uint64_t, 0.0,
                          UINT64_C(0x7ff8000000000000))

const cmeta_type_traits cmeta_traits_float = { CMETA_TRIVIAL_TRAIT_FLAGS,
    cmeta_float_equal, cmeta_float_hash, cmeta_float_compare,
    cmeta_float_copy_construct, cmeta_float_move_construct, cmeta_float_destroy };
const cmeta_type_traits cmeta_traits_double = { CMETA_TRIVIAL_TRAIT_FLAGS,
    cmeta_double_equal, cmeta_double_hash, cmeta_double_compare,
    cmeta_double_copy_construct, cmeta_double_move_construct, cmeta_double_destroy };

#undef CMETA_DEFINE_FLOAT_TRAITS
#undef CMETA_DEFINE_TRIVIAL_TRAITS
#undef CMETA_TRIVIAL_TRAIT_FLAGS

static const cmeta_type_identity cmeta_id_void =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.void");
static const cmeta_type_identity cmeta_id_bool =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.bool");
static const cmeta_type_identity cmeta_id_int =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.int");
static const cmeta_type_identity cmeta_id_long =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.long");
static const cmeta_type_identity cmeta_id_float =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.float");
static const cmeta_type_identity cmeta_id_double =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.double");
static const cmeta_type_identity cmeta_id_size =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.size");
static const cmeta_type_identity cmeta_id_gen_status =
    CMETA_TYPE_ID_ATOM_INIT("cmeta.gen_status");

static const cmeta_type_identity cmeta_id_bool_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_bool);
static const cmeta_type_identity cmeta_id_int_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_int);
static const cmeta_type_identity cmeta_id_long_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_long);
static const cmeta_type_identity cmeta_id_float_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_float);
static const cmeta_type_identity cmeta_id_double_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_double);
static const cmeta_type_identity cmeta_id_size_ptr =
    CMETA_TYPE_ID_POINTER_INIT(&cmeta_id_size);

#define CMETA_BUILTIN_ID_MARK_B CMETA_GENERIC_PROBE()
#define CMETA_BUILTIN_ID_MARK_I CMETA_GENERIC_PROBE()
#define CMETA_BUILTIN_ID_MARK_L CMETA_GENERIC_PROBE()
#define CMETA_BUILTIN_ID_MARK_F CMETA_GENERIC_PROBE()
#define CMETA_BUILTIN_ID_MARK_D CMETA_GENERIC_PROBE()
#define CMETA_BUILTIN_ID_MARK(tok) CMETA_PP_CAT(CMETA_BUILTIN_ID_MARK_, tok)

#define CMETA_BUILTIN_ATOM_B (&cmeta_id_bool)
#define CMETA_BUILTIN_ATOM_I (&cmeta_id_int)
#define CMETA_BUILTIN_ATOM_L (&cmeta_id_long)
#define CMETA_BUILTIN_ATOM_F (&cmeta_id_float)
#define CMETA_BUILTIN_ATOM_D (&cmeta_id_double)
#define CMETA_BUILTIN_ATOM_SELECT_1(tok) CMETA_PP_CAT(CMETA_BUILTIN_ATOM_, tok)
#define CMETA_BUILTIN_ATOM_SELECT_0(tok) NULL
#define CMETA_BUILTIN_ATOM_SELECT_I(flag, tok) \
    CMETA_PP_CAT(CMETA_BUILTIN_ATOM_SELECT_, flag)(tok)
#define CMETA_BUILTIN_ATOM_ID(tok) \
    CMETA_BUILTIN_ATOM_SELECT_I( \
        CMETA_GENERIC_IS_PROBE(CMETA_BUILTIN_ID_MARK(tok)), tok)

#define CMETA_BUILTIN_PTR_B (&cmeta_id_bool_ptr)
#define CMETA_BUILTIN_PTR_I (&cmeta_id_int_ptr)
#define CMETA_BUILTIN_PTR_L (&cmeta_id_long_ptr)
#define CMETA_BUILTIN_PTR_F (&cmeta_id_float_ptr)
#define CMETA_BUILTIN_PTR_D (&cmeta_id_double_ptr)
#define CMETA_BUILTIN_PTR_SELECT_1(tok) CMETA_PP_CAT(CMETA_BUILTIN_PTR_, tok)
#define CMETA_BUILTIN_PTR_SELECT_0(tok) NULL
#define CMETA_BUILTIN_PTR_SELECT_I(flag, tok) \
    CMETA_PP_CAT(CMETA_BUILTIN_PTR_SELECT_, flag)(tok)
#define CMETA_BUILTIN_PTR_ID(tok) \
    CMETA_BUILTIN_PTR_SELECT_I( \
        CMETA_GENERIC_IS_PROBE(CMETA_BUILTIN_ID_MARK(tok)), tok)

const cmeta_type_desc cmeta_type_void = {
    .name = "void", .size = 0, .align = 1, .kind = CMETA_T_VOID,
    .pointee = NULL, .traits = NULL, .identity = &cmeta_id_void
};
const cmeta_type_desc cmeta_type_size = {
    .name = "size_t", .size = sizeof(size_t), .align = _Alignof(size_t),
    .kind = CMETA_T_INTEGER, .pointee = NULL,
    .traits = NULL, .identity = &cmeta_id_size
};
const cmeta_type_desc cmeta_type_size_ptr = {
    .name = "size_t *", .size = sizeof(size_t *), .align = _Alignof(size_t *),
    .kind = CMETA_T_POINTER, .pointee = &cmeta_type_size,
    .traits = NULL, .identity = &cmeta_id_size_ptr
};
const cmeta_type_desc cmeta_type_gen_status = {
    .name = "cmeta_gen_status", .size = sizeof(cmeta_gen_status),
    .align = _Alignof(cmeta_gen_status), .kind = CMETA_T_INTEGER,
    .pointee = NULL, .traits = NULL, .identity = &cmeta_id_gen_status
};

#define CMETA_DEFINE_TYPE(row, ignored) \
    const cmeta_type_desc CMETA_TYPE_DESC(row) = { \
        .name = CMETA_STR(CMETA_TYPE_CTYPE(row)), \
        .size = sizeof(CMETA_TYPE_CTYPE(row)), \
        .align = _Alignof(CMETA_TYPE_CTYPE(row)), \
        .kind = CMETA_TYPE_KIND(row), \
        .pointee = NULL, \
        .traits = &CMETA_TYPE_TRAITS(row), \
        .identity = CMETA_BUILTIN_ATOM_ID(CMETA_TYPE_TOKEN(row)) \
    }; \
    const cmeta_type_desc CMETA_DESC_PTR(row) = { \
        .name = CMETA_STR(CMETA_TYPE_CTYPE(row)) " *", \
        .size = sizeof(CMETA_TYPE_CTYPE(row) *), \
        .align = _Alignof(CMETA_TYPE_CTYPE(row) *), \
        .kind = CMETA_T_POINTER, \
        .pointee = &CMETA_TYPE_DESC(row), \
        .traits = NULL, \
        .identity = CMETA_BUILTIN_PTR_ID(CMETA_TYPE_TOKEN(row)) \
    };
CMETA_PP_FOR_EACH_A(CMETA_DEFINE_TYPE, ~, CMETA_KNOWN_TYPE_LIST)
#undef CMETA_DEFINE_TYPE

const cmeta_type_identity *cmeta_type_identity_of(const cmeta_type_desc *desc) {
    return desc ? desc->identity : NULL;
}

bool cmeta_type_desc_valid(const cmeta_type_desc *desc) {
    if (!desc || !desc->name || desc->name[0] == '\0' || desc->align == 0u)
        return false;
    if (!desc->identity)
        return desc->kind != CMETA_T_POINTER || desc->pointee != NULL;
    if (!cmeta_type_identity_valid(desc->identity))
        return false;
    if (desc->kind == CMETA_T_POINTER) {
        if (!desc->pointee || !desc->pointee->identity)
            return false;
        if (desc->identity->form != CMETA_TYPE_POINTER)
            return false;
        return cmeta_type_identity_equal(desc->identity->base,
                                         desc->pointee->identity);
    }
    return desc->identity->form != CMETA_TYPE_POINTER;
}

bool cmeta_type_equal(const cmeta_type_desc *a, const cmeta_type_desc *b) {
    if (a == b) return a != NULL && cmeta_type_desc_valid(a);
    if (!a || !b || !cmeta_type_desc_valid(a) || !cmeta_type_desc_valid(b))
        return false;
    if (a->identity || b->identity) {
        if (!a->identity || !b->identity)
            return false;
        return cmeta_type_identity_equal(a->identity, b->identity);
    }
    if (a->kind != b->kind || a->size != b->size || a->align != b->align)
        return false;
    if (strcmp(a->name, b->name) != 0) return false;
    if (a->kind == CMETA_T_POINTER)
        return cmeta_type_equal(a->pointee, b->pointee);
    return true;
}

static const cmeta_type_desc *const cmeta_type_registry[] = {
#define CMETA_TYPE_REG_ITEM(row, ignored) &CMETA_TYPE_DESC(row),
    CMETA_PP_FOR_EACH_A(CMETA_TYPE_REG_ITEM, ~, CMETA_KNOWN_TYPE_LIST)
#undef CMETA_TYPE_REG_ITEM
};

size_t cmeta_type_registry_count(void) {
    return sizeof(cmeta_type_registry) / sizeof(cmeta_type_registry[0]);
}

const cmeta_type_desc *cmeta_type_registry_at(size_t index) {
    return index < cmeta_type_registry_count() ? cmeta_type_registry[index] : NULL;
}

const cmeta_type_desc *cmeta_type_find(const char *name) {
    size_t i;
    if (!name) return NULL;
    for (i = 0; i < cmeta_type_registry_count(); ++i)
        if (strcmp(cmeta_type_registry[i]->name, name) == 0) return cmeta_type_registry[i];
    return NULL;
}

static const cmeta_sig_desc sigs[CMETA_SIG_COUNT] = {
#define CMETA_DESC_U(in, ret) \
    [CMETA_SIG_NAME(CMETA_U_ID(in, ret))] = { \
        CMETA_SIG_NAME(CMETA_U_ID(in, ret)), \
        CMETA_STR(CMETA_TYPE_CTYPE(ret)) "(" CMETA_STR(CMETA_TYPE_CTYPE(in)) ")", \
        &CMETA_TYPE_DESC(ret), { &CMETA_TYPE_DESC(in), NULL, NULL }, \
        1, CMETA_FN_PROTOCOL_VALUE },
#define CMETA_DESC_B(a, b, ret) \
    [CMETA_SIG_NAME(CMETA_B_ID(a, b, ret))] = { \
        CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)), \
        CMETA_STR(CMETA_TYPE_CTYPE(ret)) "(" CMETA_STR(CMETA_TYPE_CTYPE(a)) "," \
            CMETA_STR(CMETA_TYPE_CTYPE(b)) ")", \
        &CMETA_TYPE_DESC(ret), { &CMETA_TYPE_DESC(a), &CMETA_TYPE_DESC(b), NULL }, \
        2, CMETA_FN_PROTOCOL_VALUE },
#define CMETA_DESC_G(in, out) \
    [CMETA_SIG_NAME(CMETA_G_ID(in, out))] = { \
        CMETA_SIG_NAME(CMETA_G_ID(in, out)), \
        "cmeta_gen_status(" CMETA_STR(CMETA_TYPE_CTYPE(in)) "," \
            CMETA_STR(CMETA_TYPE_CTYPE(out)) "*,size_t*)", \
        &cmeta_type_gen_status, \
        { &CMETA_TYPE_DESC(in), &CMETA_DESC_PTR(out), &cmeta_type_size_ptr }, \
        3, CMETA_FN_PROTOCOL_GENERATOR },
    CMETA_ALL_SIGNATURES(CMETA_DESC_U, CMETA_DESC_B, CMETA_DESC_G)
#undef CMETA_DESC_U
#undef CMETA_DESC_B
#undef CMETA_DESC_G
};

static const char *const cmeta_sig_symbols[CMETA_SIG_COUNT] = {
#define CMETA_SIG_SYMBOL_U(in, ret) \
    [CMETA_SIG_NAME(CMETA_U_ID(in, ret))] = CMETA_STR(CMETA_SIG_NAME(CMETA_U_ID(in, ret))),
#define CMETA_SIG_SYMBOL_B(a, b, ret) \
    [CMETA_SIG_NAME(CMETA_B_ID(a, b, ret))] = CMETA_STR(CMETA_SIG_NAME(CMETA_B_ID(a, b, ret))),
#define CMETA_SIG_SYMBOL_G(in, out) \
    [CMETA_SIG_NAME(CMETA_G_ID(in, out))] = CMETA_STR(CMETA_SIG_NAME(CMETA_G_ID(in, out))),
    CMETA_ALL_SIGNATURES(CMETA_SIG_SYMBOL_U, CMETA_SIG_SYMBOL_B, CMETA_SIG_SYMBOL_G)
#undef CMETA_SIG_SYMBOL_U
#undef CMETA_SIG_SYMBOL_B
#undef CMETA_SIG_SYMBOL_G
};

const cmeta_sig_desc *cmeta_fn_signature(cmeta_fn fn) {
    if (fn.sig <= CMETA_SIG_INVALID || fn.sig >= CMETA_SIG_COUNT) return NULL;
    return &sigs[fn.sig];
}

const char *cmeta_sig_to_string(cmeta_sig sig) {
    return (sig > CMETA_SIG_INVALID && sig < CMETA_SIG_COUNT) ? sigs[sig].spelling : NULL;
}

const char *cmeta_sig_to_symbol(cmeta_sig sig) {
    return (sig > CMETA_SIG_INVALID && sig < CMETA_SIG_COUNT) ? cmeta_sig_symbols[sig] : NULL;
}

static bool cmeta_fn_target_valid(cmeta_fn fn) {
    switch (fn.sig) {
#define CMETA_TARGET_U(in, ret) \
        case CMETA_SIG_NAME(CMETA_U_ID(in, ret)): \
            return fn.call.CMETA_CALL_MEMBER(CMETA_U_ID(in, ret)) != NULL;
#define CMETA_TARGET_B(a, b, ret) \
        case CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)): \
            return fn.call.CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret)) != NULL;
#define CMETA_TARGET_G(in, out) \
        case CMETA_SIG_NAME(CMETA_G_ID(in, out)): \
            return fn.call.CMETA_CALL_MEMBER(CMETA_G_ID(in, out)) != NULL;
        CMETA_ALL_SIGNATURES(CMETA_TARGET_U, CMETA_TARGET_B, CMETA_TARGET_G)
#undef CMETA_TARGET_U
#undef CMETA_TARGET_B
#undef CMETA_TARGET_G
        default:
            return false;
    }
}

bool cmeta_fn_contract_valid(cmeta_fn fn) {
    const cmeta_sig_desc *sig = cmeta_fn_signature(fn);
    if (!sig || !cmeta_effects_valid(fn.effects) || !cmeta_properties_valid(fn.properties))
        return false;
    if (!cmeta_fn_target_valid(fn)) return false;
    if ((fn.properties & CMETA_PROP_TOTAL) && (fn.effects & CMETA_EFFECT_MAY_FAIL))
        return false;
    if (fn.properties & CMETA_PROP_IDEMPOTENT) {
        if (sig->protocol != CMETA_FN_PROTOCOL_VALUE || sig->param_count != 1u ||
            !cmeta_type_equal(sig->params[0], sig->return_type))
            return false;
    }
    if (fn.properties & CMETA_PROP_ASSOCIATIVE) {
        if (sig->protocol != CMETA_FN_PROTOCOL_VALUE || sig->param_count != 2u ||
            !cmeta_type_equal(sig->params[0], sig->params[1]) ||
            !cmeta_type_equal(sig->params[0], sig->return_type))
            return false;
    }
    return true;
}

bool cmeta_fn_invoke(cmeta_fn fn, void *out, const void *const *args) {
    const cmeta_sig_desc *sig = cmeta_fn_signature(fn);
    if (!sig || sig->protocol != CMETA_FN_PROTOCOL_VALUE ||
        !cmeta_fn_target_valid(fn) || !args)
        return false;
    for (size_t i = 0; i < sig->param_count; ++i)
        if (!args[i]) return false;
    switch (fn.sig) {
#define CMETA_INVOKE_U(in, ret) \
        case CMETA_SIG_NAME(CMETA_U_ID(in, ret)): { \
            CMETA_TYPE_CTYPE(in) a0; CMETA_TYPE_CTYPE(ret) r; \
            memcpy(&a0, args[0], sizeof a0); \
            r = fn.call.CMETA_CALL_MEMBER(CMETA_U_ID(in, ret))(a0); \
            if (out) memcpy(out, &r, sizeof r); \
            return true; \
        }
#define CMETA_INVOKE_B(a, b, ret) \
        case CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)): { \
            CMETA_TYPE_CTYPE(a) a0; CMETA_TYPE_CTYPE(b) a1; CMETA_TYPE_CTYPE(ret) r; \
            memcpy(&a0, args[0], sizeof a0); memcpy(&a1, args[1], sizeof a1); \
            r = fn.call.CMETA_CALL_MEMBER(CMETA_B_ID(a, b, ret))(a0, a1); \
            if (out) memcpy(out, &r, sizeof r); \
            return true; \
        }
#define CMETA_INVOKE_G(in, out_type) \
        case CMETA_SIG_NAME(CMETA_G_ID(in, out_type)): return false;
        CMETA_ALL_SIGNATURES(CMETA_INVOKE_U, CMETA_INVOKE_B, CMETA_INVOKE_G)
#undef CMETA_INVOKE_U
#undef CMETA_INVOKE_B
#undef CMETA_INVOKE_G
        case CMETA_SIG_INVALID:
        case CMETA_SIG_COUNT:
            return false;
    }
    return false;
}

cmeta_gen_status cmeta_fn_generate(cmeta_fn fn, const void *input,
                                   void *out, size_t *cursor) {
    const cmeta_sig_desc *sig = cmeta_fn_signature(fn);
    if (!sig || sig->protocol != CMETA_FN_PROTOCOL_GENERATOR ||
        !cmeta_fn_target_valid(fn) || !input || !out || !cursor)
        return CMETA_GEN_ERROR;
    switch (fn.sig) {
#define CMETA_GENERATE_U(in, ret) \
        case CMETA_SIG_NAME(CMETA_U_ID(in, ret)): return CMETA_GEN_ERROR;
#define CMETA_GENERATE_B(a, b, ret) \
        case CMETA_SIG_NAME(CMETA_B_ID(a, b, ret)): return CMETA_GEN_ERROR;
#define CMETA_GENERATE_G(in, out_type) \
        case CMETA_SIG_NAME(CMETA_G_ID(in, out_type)): { \
            CMETA_TYPE_CTYPE(in) a0; \
            memcpy(&a0, input, sizeof a0); \
            return fn.call.CMETA_CALL_MEMBER(CMETA_G_ID(in, out_type))( \
                a0, (CMETA_TYPE_CTYPE(out_type) *)out, cursor); \
        }
        CMETA_ALL_SIGNATURES(CMETA_GENERATE_U, CMETA_GENERATE_B, CMETA_GENERATE_G)
#undef CMETA_GENERATE_U
#undef CMETA_GENERATE_B
#undef CMETA_GENERATE_G
        case CMETA_SIG_INVALID:
        case CMETA_SIG_COUNT:
            return CMETA_GEN_ERROR;
    }
    return CMETA_GEN_ERROR;
}

bool cmeta_callable_bind(cmeta_callable in, cmeta_callable *out) {
    cmeta_fn meta;
    const cmeta_sig_desc *sig;
    if (!out) return false;
    meta = in.meta;
    if (meta.sig == CMETA_SIG_INVALID) {
        if (!in.resolve) return false;
        meta = in.resolve();
        /* The callable value is the ownership point for semantic contracts. */
        meta.effects = in.meta.effects;
        meta.properties = in.meta.properties;
    }
    if (!cmeta_fn_contract_valid(meta)) return false;
    sig = cmeta_fn_signature(meta);
    if (!sig) return false;
    if (sig->protocol == CMETA_FN_PROTOCOL_GENERATOR) {
        if (!in.generate) return false;
    } else if (!in.invoke) {
        return false;
    }
    if (in.capture_size > CMETA_CAPTURE_INLINE) return false;
    *out = in;
    out->meta = meta;
    out->resolve = NULL;
    return true;
}

const cmeta_sig_desc *cmeta_callable_signature(cmeta_callable fn) {
    cmeta_callable bound;
    if (!cmeta_callable_bind(fn, &bound)) return NULL;
    return cmeta_fn_signature(bound.meta);
}

bool cmeta_callable_contract_valid(cmeta_callable fn) {
    cmeta_callable bound;
    return cmeta_callable_bind(fn, &bound);
}

bool cmeta_callable_same(cmeta_callable a, cmeta_callable b) {
    cmeta_callable ba, bb;
    if (!cmeta_callable_bind(a, &ba) || !cmeta_callable_bind(b, &bb)) return false;
    if (ba.meta.sig != bb.meta.sig || ba.meta.effects != bb.meta.effects ||
        ba.meta.properties != bb.meta.properties || ba.invoke != bb.invoke ||
        ba.generate != bb.generate || ba.capture_size != bb.capture_size)
        return false;
    if (ba.capture_size && memcmp(ba.capture.bytes, bb.capture.bytes, ba.capture_size) != 0)
        return false;
    return true;
}

bool cmeta_callable_invoke(const cmeta_callable *fn, void *out, const void *const *args) {
    cmeta_callable bound;
    if (!fn) return false;
    if (fn->meta.sig != CMETA_SIG_INVALID)
        return fn->invoke ? fn->invoke(fn, out, args) : false;
    if (!cmeta_callable_bind(*fn, &bound) || !bound.invoke) return false;
    return bound.invoke(&bound, out, args);
}

cmeta_gen_status cmeta_callable_generate(const cmeta_callable *fn, const void *input,
                                         void *out, size_t *cursor) {
    cmeta_callable bound;
    if (!fn) return CMETA_GEN_ERROR;
    if (fn->meta.sig != CMETA_SIG_INVALID)
        return fn->generate ? fn->generate(fn, input, out, cursor) : CMETA_GEN_ERROR;
    if (!cmeta_callable_bind(*fn, &bound) || !bound.generate) return CMETA_GEN_ERROR;
    return bound.generate(&bound, input, out, cursor);
}
