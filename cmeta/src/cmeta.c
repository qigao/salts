#include <cmeta/cmeta.h>

#include <string.h>

#define CMETA_STR_I(x) #x
#define CMETA_STR(x) CMETA_STR_I(x)

static bool cmeta_int_equal(const void *left, const void *right) {
    return left != NULL && right != NULL &&
           *(const int *)left == *(const int *)right;
}

static uint64_t cmeta_int_hash(const void *value) {
    return value == NULL ? 0u : (uint64_t)*(const int *)value;
}

static int cmeta_int_compare(const void *left, const void *right) {
    int lhs;
    int rhs;

    if (left == NULL || right == NULL) return 0;
    lhs = *(const int *)left;
    rhs = *(const int *)right;
    return lhs < rhs ? -1 : lhs > rhs;
}

static bool cmeta_int_copy_construct(void *destination, const void *source) {
    if (destination == NULL || source == NULL) return false;
    *(int *)destination = *(const int *)source;
    return true;
}

static void cmeta_int_move_construct(void *destination, void *source) {
    if (destination != NULL && source != NULL)
        *(int *)destination = *(int *)source;
}

static void cmeta_int_destroy(void *value) {
    (void)value;
}

static const cmeta_type_traits cmeta_int_traits = {
    CMETA_TRAIT_EQUAL | CMETA_TRAIT_HASH | CMETA_TRAIT_COMPARE |
        CMETA_TRAIT_COPY | CMETA_TRAIT_MOVE | CMETA_TRAIT_DESTROY |
        CMETA_TRAIT_TRIVIAL_COPY | CMETA_TRAIT_TRIVIAL_DESTROY,
    cmeta_int_equal,
    cmeta_int_hash,
    cmeta_int_compare,
    cmeta_int_copy_construct,
    cmeta_int_move_construct,
    cmeta_int_destroy
};

#define CMETA_BUILTIN_TRAITS(type) \
    _Generic(((type *)0), int *: &cmeta_int_traits, default: NULL)

const cmeta_type_desc cmeta_type_void = {
    "void", 0, 1, CMETA_T_VOID, NULL, NULL
};
const cmeta_type_desc cmeta_type_size = {
    "size_t", sizeof(size_t), _Alignof(size_t), CMETA_T_INTEGER, NULL, NULL
};
const cmeta_type_desc cmeta_type_size_ptr = {
    "size_t *", sizeof(size_t *), _Alignof(size_t *), CMETA_T_POINTER,
    &cmeta_type_size, NULL
};
const cmeta_type_desc cmeta_type_gen_status = {
    "cmeta_gen_status", sizeof(cmeta_gen_status), _Alignof(cmeta_gen_status),
    CMETA_T_INTEGER, NULL, NULL
};

#define CMETA_DEFINE_TYPE(row, ignored) \
    const cmeta_type_desc CMETA_TYPE_DESC(row) = { \
        CMETA_STR(CMETA_TYPE_CTYPE(row)), sizeof(CMETA_TYPE_CTYPE(row)), \
        _Alignof(CMETA_TYPE_CTYPE(row)), CMETA_TYPE_KIND(row), NULL, \
        CMETA_BUILTIN_TRAITS(CMETA_TYPE_CTYPE(row)) \
    }; \
    const cmeta_type_desc CMETA_DESC_PTR(row) = { \
        CMETA_STR(CMETA_TYPE_CTYPE(row)) " *", sizeof(CMETA_TYPE_CTYPE(row) *), \
        _Alignof(CMETA_TYPE_CTYPE(row) *), CMETA_T_POINTER, &CMETA_TYPE_DESC(row), NULL \
    };
CMETA_PP_FOR_EACH_A(CMETA_DEFINE_TYPE, ~, CMETA_TYPE_LIST)
#undef CMETA_DEFINE_TYPE
#undef CMETA_BUILTIN_TRAITS

bool cmeta_type_equal(const cmeta_type_desc *a, const cmeta_type_desc *b) {
    if (a == b) return true;
    if (!a || !b) return false;
    if (a->kind != b->kind || a->size != b->size || a->align != b->align)
        return false;
    if (strcmp(a->name, b->name) != 0) return false;
    if (a->kind == CMETA_T_POINTER)
        return cmeta_type_equal(a->pointee, b->pointee);
    return true;
}

static const cmeta_type_desc *const cmeta_type_registry[] = {
#define CMETA_TYPE_REG_ITEM(row, ignored) &CMETA_TYPE_DESC(row),
    CMETA_PP_FOR_EACH_A(CMETA_TYPE_REG_ITEM, ~, CMETA_TYPE_LIST)
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

bool cmeta_fn_contract_valid(cmeta_fn fn) {
    const cmeta_sig_desc *sig = cmeta_fn_signature(fn);
    if (!sig || !cmeta_effects_valid(fn.effects) || !cmeta_properties_valid(fn.properties))
        return false;
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
    if (!args) return false;
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
    if (!input || !out || !cursor) return CMETA_GEN_ERROR;
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
