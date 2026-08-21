#include <cmeta/cmeta.h>

#include <string.h>

#define CMETA_STR_I(x) #x
#define CMETA_STR(x) CMETA_STR_I(x)

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

/* Builtin identity is selected from the finite CMeta row token schema, not
 * from descriptor display names. Unknown/project row tokens remain legacy. */
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
    .name = "void",
    .size = 0,
    .align = 1,
    .kind = CMETA_T_VOID,
    .pointee = NULL,
    .identity = &cmeta_id_void
};
const cmeta_type_desc cmeta_type_size = {
    .name = "size_t",
    .size = sizeof(size_t),
    .align = _Alignof(size_t),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .identity = &cmeta_id_size
};
const cmeta_type_desc cmeta_type_size_ptr = {
    .name = "size_t *",
    .size = sizeof(size_t *),
    .align = _Alignof(size_t *),
    .kind = CMETA_T_POINTER,
    .pointee = &cmeta_type_size,
    .identity = &cmeta_id_size_ptr
};
const cmeta_type_desc cmeta_type_gen_status = {
    .name = "cmeta_gen_status",
    .size = sizeof(cmeta_gen_status),
    .align = _Alignof(cmeta_gen_status),
    .kind = CMETA_T_INTEGER,
    .pointee = NULL,
    .identity = &cmeta_id_gen_status
};

#define CMETA_DEFINE_TYPE(row, ignored) \
    const cmeta_type_desc CMETA_TYPE_DESC(row) = { \
        .name = CMETA_STR(CMETA_TYPE_CTYPE(row)), \
        .size = sizeof(CMETA_TYPE_CTYPE(row)), \
        .align = _Alignof(CMETA_TYPE_CTYPE(row)), \
        .kind = CMETA_TYPE_KIND(row), \
        .pointee = NULL, \
        .identity = CMETA_BUILTIN_ATOM_ID(CMETA_TYPE_TOKEN(row)) \
    }; \
    const cmeta_type_desc CMETA_DESC_PTR(row) = { \
        .name = CMETA_STR(CMETA_TYPE_CTYPE(row)) " *", \
        .size = sizeof(CMETA_TYPE_CTYPE(row) *), \
        .align = _Alignof(CMETA_TYPE_CTYPE(row) *), \
        .kind = CMETA_T_POINTER, \
        .pointee = &CMETA_TYPE_DESC(row), \
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

    if (!desc->identity) {
        return desc->kind != CMETA_T_POINTER || desc->pointee != NULL;
    }

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
    if (a == b) return a != NULL;
    if (!a || !b) return false;

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
