#ifndef CMETA_META_CONTRACT_H
#define CMETA_META_CONTRACT_H

/*
 * User-facing semantic contracts.  These are compile-time aliases only;
 * Graph IR still stores the exact effect/property bitsets.
 *
 * Contract words deliberately describe programmer intent, not implementation
 * mechanism. UNKNOWN stays conservative and simply blocks stronger rewrites.
 */
#define CMETA_CONTRACT_unknown \
    (CMETA_EFFECT_UNKNOWN, CMETA_PROP_NONE)
#define CMETA_CONTRACT_value \
    (CMETA_EFFECT_PURE, CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_NO_ALIAS)
#define CMETA_CONTRACT_pure \
    (CMETA_EFFECT_PURE, CMETA_PROP_NONE)
#define CMETA_CONTRACT_idempotent \
    (CMETA_EFFECT_PURE, CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_IDEMPOTENT | CMETA_PROP_NO_ALIAS)
#define CMETA_CONTRACT_associative \
    (CMETA_EFFECT_PURE, CMETA_PROP_DETERMINISTIC | CMETA_PROP_TOTAL | CMETA_PROP_ASSOCIATIVE | CMETA_PROP_NO_ALIAS)
#define CMETA_CONTRACT_fallible \
    (CMETA_EFFECT_MAY_FAIL, CMETA_PROP_DETERMINISTIC | CMETA_PROP_NO_ALIAS)
#define CMETA_CONTRACT_io \
    (CMETA_EFFECT_IO | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE)
#define CMETA_CONTRACT_async \
    (CMETA_EFFECT_ASYNC | CMETA_EFFECT_MAY_FAIL, CMETA_PROP_NONE)
#define CMETA_CONTRACT_stateful \
    (CMETA_EFFECT_STATEFUL, CMETA_PROP_NONE)

#define CMETA_CONTRACT_I(name) CMETA_CONTRACT_##name
#define CMETA_CONTRACT(name) CMETA_CONTRACT_I(name)
#define CMETA_CONTRACT_EFFECTS_I(effects_, properties_) (effects_)
#define CMETA_CONTRACT_PROPERTIES_I(effects_, properties_) (properties_)
#define CMETA_CONTRACT_EFFECTS_II(tuple) CMETA_CONTRACT_EFFECTS_I tuple
#define CMETA_CONTRACT_PROPERTIES_II(tuple) CMETA_CONTRACT_PROPERTIES_I tuple
#define CMETA_CONTRACT_EFFECTS(name) CMETA_CONTRACT_EFFECTS_II(CMETA_CONTRACT(name))
#define CMETA_CONTRACT_PROPERTIES(name) CMETA_CONTRACT_PROPERTIES_II(CMETA_CONTRACT(name))

Enum(cmeta_contract_kind,
    (CMETA_CONTRACT_KIND_UNKNOWN,     "unknown"),
    (CMETA_CONTRACT_KIND_VALUE,       "value"),
    (CMETA_CONTRACT_KIND_PURE,        "pure"),
    (CMETA_CONTRACT_KIND_IDEMPOTENT,  "idempotent"),
    (CMETA_CONTRACT_KIND_ASSOCIATIVE, "associative"),
    (CMETA_CONTRACT_KIND_FALLIBLE,    "fallible"),
    (CMETA_CONTRACT_KIND_IO,          "io"),
    (CMETA_CONTRACT_KIND_ASYNC,       "async"),
    (CMETA_CONTRACT_KIND_STATEFUL,    "stateful")
);

typedef struct cmeta_contract_desc {
    cmeta_contract_kind kind;
    const char *name;
    cmeta_effects effects;
    cmeta_properties properties;
} cmeta_contract_desc;

static inline const cmeta_contract_desc *cmeta_contract_descriptor(cmeta_contract_kind kind) {
    static const cmeta_contract_desc table[] = {
        { CMETA_CONTRACT_KIND_UNKNOWN,     "unknown",     CMETA_CONTRACT_EFFECTS(unknown),     CMETA_CONTRACT_PROPERTIES(unknown) },
        { CMETA_CONTRACT_KIND_VALUE,       "value",       CMETA_CONTRACT_EFFECTS(value),       CMETA_CONTRACT_PROPERTIES(value) },
        { CMETA_CONTRACT_KIND_PURE,        "pure",        CMETA_CONTRACT_EFFECTS(pure),        CMETA_CONTRACT_PROPERTIES(pure) },
        { CMETA_CONTRACT_KIND_IDEMPOTENT,  "idempotent",  CMETA_CONTRACT_EFFECTS(idempotent),  CMETA_CONTRACT_PROPERTIES(idempotent) },
        { CMETA_CONTRACT_KIND_ASSOCIATIVE, "associative", CMETA_CONTRACT_EFFECTS(associative), CMETA_CONTRACT_PROPERTIES(associative) },
        { CMETA_CONTRACT_KIND_FALLIBLE,    "fallible",    CMETA_CONTRACT_EFFECTS(fallible),    CMETA_CONTRACT_PROPERTIES(fallible) },
        { CMETA_CONTRACT_KIND_IO,          "io",          CMETA_CONTRACT_EFFECTS(io),          CMETA_CONTRACT_PROPERTIES(io) },
        { CMETA_CONTRACT_KIND_ASYNC,       "async",       CMETA_CONTRACT_EFFECTS(async),       CMETA_CONTRACT_PROPERTIES(async) },
        { CMETA_CONTRACT_KIND_STATEFUL,    "stateful",    CMETA_CONTRACT_EFFECTS(stateful),    CMETA_CONTRACT_PROPERTIES(stateful) }
    };
    size_t n = sizeof(table) / sizeof(table[0]);
    size_t i;
    for (i = 0; i < n; ++i) if (table[i].kind == kind) return &table[i];
    return NULL;
}

static inline const cmeta_contract_desc *cmeta_contract_find(const char *name) {
    cmeta_contract_kind k;
    if (!cmeta_contract_kind_from_string(name, &k)) return NULL;
    return cmeta_contract_descriptor(k);
}

/* Advanced escape hatch: internal/framework code may still provide exact sets. */
#define CMETA_CONTRACT_RAW_EFFECTS(effects_, properties_) (effects_)
#define CMETA_CONTRACT_RAW_PROPERTIES(effects_, properties_) (properties_)

#endif
