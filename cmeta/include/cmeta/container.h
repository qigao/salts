#ifndef CMETA_CONTAINER_H
#define CMETA_CONTAINER_H

/*
 * Typed container facade meta-schema.
 *
 * The raw container implementation remains ordinary hand-written C.  CMeta
 * generates only the repetitive typed facade. A typed container declaration is
 * a complete finite instantiation: wrapper methods, descriptors, and Range
 * adapters are header-local. Raw container algorithms remain ordinary compiled C.
 */

#include <cmeta/pp.h>
#include <cmeta/range.h>
#include <stdbool.h>
#include <stddef.h>

#define CMETA_CONTAINER_API_I(prefix, suffix) prefix##_##suffix
#define CMETA_CONTAINER_API(prefix, suffix) CMETA_CONTAINER_API_I(prefix, suffix)


/* -------------------------------------------------------------------------
 * Header-local Range capability generators
 * ------------------------------------------------------------------------- */

#define CMETA_CONTAINER1_INDEX_RANGE_DEFINE(name, type, prefix, flags, version_accessor) \
    CMETA_LOCAL const cmeta_type_desc name##_element_cmeta_type = { \
        CMETA_CONTAINER_STR(type), sizeof(type), _Alignof(type), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_type_desc name##_cmeta_type = { \
        CMETA_CONTAINER_STR(name), sizeof(name), _Alignof(name), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_INLINE size_t name##_cmeta_range_size(const void *object) { \
        const name *self = (const name *)object; \
        return self ? CMETA_CONTAINER_API(prefix, size)(&self->raw) : 0U; \
    } \
    CMETA_INLINE cmeta_gen_status name##_cmeta_range_next(const void *object, size_t *cursor, void *out_value) { \
        const name *self = (const name *)object; \
        const type *value; \
        size_t count; \
        if (!self || !cursor || !out_value) return CMETA_GEN_ERROR; \
        count = CMETA_CONTAINER_API(prefix, size)(&self->raw); \
        if (*cursor >= count) return CMETA_GEN_DONE; \
        value = (const type *)CMETA_CONTAINER_API(prefix, at_const)(&self->raw, *cursor); \
        if (!value) return CMETA_GEN_ERROR; \
        memcpy(out_value, value, sizeof(type)); \
        ++(*cursor); \
        return *cursor == count ? CMETA_GEN_VALUE_AND_DONE : CMETA_GEN_VALUE; \
    } \
    CMETA_INLINE cmeta_range name##_range(const name *self) { \
        cmeta_range range = { \
            self, CMETA_TYPEOF_OR(type, &name##_element_cmeta_type), (flags), \
            name##_cmeta_range_size, name##_cmeta_range_next, \
            cmeta_range_capture_version((version_accessor), self), (version_accessor) \
        }; \
        return range; \
    } \
    CMETA_INLINE cmeta_range name##_cmeta_erased_range(const void *object) { \
        return name##_range((const name *)object); \
    } \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc = { \
        CMETA_CONTAINER_STR(name), &name##_cmeta_type, \
        CMETA_TYPEOF_OR(type, &name##_element_cmeta_type), NULL, NULL, \
        name##_cmeta_erased_range, NULL, NULL, NULL \
    };

/* -------------------------------------------------------------------------
 * One-type facade
 * ------------------------------------------------------------------------- */
#define CMETA_C1_INLINE_DISPATCH(kind, pub, op, extra, ctx) \
    CMETA_C1_INLINE_DISPATCH_I(kind, pub, op, extra, CMETA_PP_UNPAREN ctx)
#define CMETA_C1_INLINE_DISPATCH_I(kind, pub, op, extra, ...) \
    CMETA_PP_CAT(CMETA_C1_INLINE_, kind)(pub, op, extra, __VA_ARGS__)

#define CMETA_CONTAINER1_DEFINE(name, type, raw_type, prefix, ok_code, aux, methods) \
    typedef struct name { cmeta_container_header cmeta; raw_type raw; } name; \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc; \
    methods(CMETA_C1_INLINE_DISPATCH, (name, type, raw_type, prefix, ok_code, aux))


#define CMETA_C1_INLINE_INIT_SIZE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self) { self->cmeta.descriptor = &name##_cmeta_container_desc; return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(type)); }

#define CMETA_C1_INLINE_FROM_ARRAY_SIZE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, const type *values, size_t count) { \
        self->cmeta.descriptor = &name##_cmeta_container_desc; \
        return CMETA_CONTAINER_API(prefix, op)(&self->raw, values, count, sizeof(type)); \
    }

#define CMETA_C1_INLINE_INIT_SIZE_COMPARE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self) { \
        self->cmeta.descriptor = &name##_cmeta_container_desc; \
        return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(type), aux, NULL); \
    }

#define CMETA_C1_INLINE_FROM_ARRAY_SIZE_COMPARE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, const type *values, size_t count) { \
        self->cmeta.descriptor = &name##_cmeta_container_desc; \
        return CMETA_CONTAINER_API(prefix, op)(&self->raw, values, count, sizeof(type), aux, NULL); \
    }

#define CMETA_C1_INLINE_INIT_KEY_HASH(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self) { \
        self->cmeta.descriptor = &name##_cmeta_container_desc; \
        return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(type), NULL, NULL, NULL); \
    }

#define CMETA_C1_INLINE_FROM_KEYS_HASH(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, const type *values, size_t count) { \
        self->cmeta.descriptor = &name##_cmeta_container_desc; \
        return CMETA_CONTAINER_API(prefix, op)(&self->raw, values, count, sizeof(type), NULL, NULL, NULL); \
    }

#define CMETA_C1_INLINE_DESTROY(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE void name##_##pub(name *self) { CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C1_INLINE_CLEAR(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE void name##_##pub(name *self) { CMETA_CONTAINER_API(prefix, op)(&self->raw); }

#define CMETA_C1_INLINE_RESERVE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, size_t capacity) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, capacity); }

#define CMETA_C1_INLINE_PUSH_VALUE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, type value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &value); }

#define CMETA_C1_INLINE_POP_BOOL(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE bool name##_##pub(name *self, type *out_value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, out_value) == (ok); }

#define CMETA_C1_INLINE_PTR_INDEX(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE type *name##_##pub(name *self, size_t index) { return (type *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }
#define CMETA_C1_INLINE_CONST_PTR_INDEX(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE const type *name##_##pub(const name *self, size_t index) { return (const type *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }

#define CMETA_C1_INLINE_PTR(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE type *name##_##pub(name *self) { return (type *)CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C1_INLINE_CONST_PTR(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE const type *name##_##pub(const name *self) { return (const type *)CMETA_CONTAINER_API(prefix, op)(&self->raw); }

#define CMETA_C1_INLINE_SIZE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE size_t name##_##pub(const name *self) { return CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C1_INLINE_BOOL(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE bool name##_##pub(const name *self) { return CMETA_CONTAINER_API(prefix, op)(&self->raw); }

#define CMETA_C1_INLINE_KEY_VALUE(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, type value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &value); }
#define CMETA_C1_INLINE_KEY_CONTAINS(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE bool name##_##pub(const name *self, type value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &value); }
#define CMETA_C1_INLINE_KEY_REMOVE_BOOL(pub, op, extra, name, type, raw_type, prefix, ok, aux) \
    CMETA_INLINE bool name##_##pub(name *self, type value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &value) == (ok); }

/* -------------------------------------------------------------------------
 * Two-type associative facade
 * ------------------------------------------------------------------------- */
#define CMETA_C2_INLINE_DISPATCH(kind, pub, op, extra, ctx) \
    CMETA_C2_INLINE_DISPATCH_I(kind, pub, op, extra, CMETA_PP_UNPAREN ctx)
#define CMETA_C2_INLINE_DISPATCH_I(kind, pub, op, extra, ...) \
    CMETA_PP_CAT(CMETA_C2_INLINE_, kind)(pub, op, extra, __VA_ARGS__)

#define CMETA_CONTAINER2_DEFINE(name, key_type, value_type, raw_type, prefix, ok_code, aux, methods) \
    typedef struct name##_entry { key_type key; value_type value; } name##_entry; \
    typedef struct name { cmeta_container_header cmeta; raw_type raw; } name; \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc; \
    methods(CMETA_C2_INLINE_DISPATCH, (name, key_type, value_type, raw_type, prefix, ok_code, aux))

#define CMETA_C2_INLINE_INIT_KV_HASH(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self) { self->cmeta.descriptor = &name##_cmeta_container_desc; return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(kt), sizeof(vt), NULL, NULL, NULL); }
#define CMETA_C2_INLINE_INIT_KV_COMPARE(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self) { self->cmeta.descriptor = &name##_cmeta_container_desc; return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(kt), sizeof(vt), aux, NULL); }
#define CMETA_C2_INLINE_INIT_WITH_ORDER_COMPARE(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, size_t min_degree) { self->cmeta.descriptor = &name##_cmeta_container_desc; return CMETA_CONTAINER_API(prefix, op)(&self->raw, sizeof(kt), sizeof(vt), aux, NULL, min_degree); }

#define CMETA_C2_INLINE_FROM_ENTRIES(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) \
    CMETA_INLINE int name##_##pub(name *self, const name##_entry *entries, size_t count) { \
        size_t i; int rc; \
        if (!self || (count > 0U && !entries)) return extra; \
        rc = name##_init(self); \
        if (rc != (ok)) return rc; \
        rc = CMETA_CONTAINER_API(prefix, reserve)(&self->raw, count); \
        if (rc != (ok)) { CMETA_CONTAINER_API(prefix, destroy)(&self->raw); return rc; } \
        for (i = 0; i < count; ++i) { \
            rc = CMETA_CONTAINER_API(prefix, put)(&self->raw, &entries[i].key, &entries[i].value); \
            if (rc != (ok)) { CMETA_CONTAINER_API(prefix, destroy)(&self->raw); return rc; } \
        } \
        return (ok); \
    }

#define CMETA_C2_INLINE_DESTROY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE void name##_##pub(name *self) { CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C2_INLINE_CLEAR(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE void name##_##pub(name *self) { CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C2_INLINE_RESERVE(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE int name##_##pub(name *self, size_t capacity) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, capacity); }
#define CMETA_C2_INLINE_PUT(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE int name##_##pub(name *self, kt key, vt value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key, &value); }
#define CMETA_C2_INLINE_GET(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE vt *name##_##pub(name *self, kt key) { return (vt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_GET_CONST(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE const vt *name##_##pub(const name *self, kt key) { return (const vt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_CONTAINS(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE bool name##_##pub(const name *self, kt key) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_REMOVE_STATUS_BOOL(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE bool name##_##pub(name *self, kt key, vt *out_value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key, out_value) == (ok); }
#define CMETA_C2_INLINE_REMOVE_BOOL(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE bool name##_##pub(name *self, kt key, vt *out_value) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key, out_value); }
#define CMETA_C2_INLINE_SIZE(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE size_t name##_##pub(const name *self) { return CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C2_INLINE_CAPACITY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE size_t name##_##pub(const name *self) { return CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C2_INLINE_EMPTY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE bool name##_##pub(const name *self) { return CMETA_CONTAINER_API(prefix, op)(&self->raw); }
#define CMETA_C2_INLINE_KEY_AT(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE kt *name##_##pub(name *self, size_t index) { return (kt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }
#define CMETA_C2_INLINE_KEY_AT_CONST(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE const kt *name##_##pub(const name *self, size_t index) { return (const kt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }
#define CMETA_C2_INLINE_VALUE_AT(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE vt *name##_##pub(name *self, size_t index) { return (vt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }
#define CMETA_C2_INLINE_VALUE_AT_CONST(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE const vt *name##_##pub(const name *self, size_t index) { return (const vt *)CMETA_CONTAINER_API(prefix, op)(&self->raw, index); }
#define CMETA_C2_INLINE_FIND_SLOT(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE size_t name##_##pub(name *self, kt key, bool *found) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key, found); }
#define CMETA_C2_INLINE_RAW_PTR_KEY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE extra *name##_##pub(name *self, kt key) { return (extra *)CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_RAW_CONST_PTR_KEY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE const extra *name##_##pub(const name *self, kt key) { return (const extra *)CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_COUNT_KEY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE size_t name##_##pub(const name *self, kt key) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }
#define CMETA_C2_INLINE_ERASE_KEY(pub, op, extra, name, kt, vt, raw_type, prefix, ok, aux) CMETA_INLINE size_t name##_##pub(name *self, kt key) { return CMETA_CONTAINER_API(prefix, op)(&self->raw, &key); }


/* -------------------------------------------------------------------------
 * Sparse one-type Range (set/hash-set style)
 * ------------------------------------------------------------------------- */

#define CMETA_CONTAINER1_SLOT_RANGE_DEFINE(name, type, prefix, flags, version_accessor) \
    CMETA_LOCAL const cmeta_type_desc name##_element_cmeta_type = { \
        CMETA_CONTAINER_STR(type), sizeof(type), _Alignof(type), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_type_desc name##_cmeta_type = { \
        CMETA_CONTAINER_STR(name), sizeof(name), _Alignof(name), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_INLINE size_t name##_cmeta_range_size(const void *object) { \
        const name *self = (const name *)object; \
        return self ? CMETA_CONTAINER_API(prefix, size)(&self->raw) : 0U; \
    } \
    CMETA_INLINE cmeta_gen_status name##_cmeta_range_next(const void *object, size_t *cursor, void *out_value) { \
        const name *self = (const name *)object; \
        size_t limit; \
        if (!self || !cursor || !out_value) return CMETA_GEN_ERROR; \
        limit = CMETA_CONTAINER_API(prefix, capacity)(&self->raw); \
        while (*cursor < limit) { \
            const type *value = (const type *)CMETA_CONTAINER_API(prefix, key_at)(&self->raw, *cursor); \
            ++(*cursor); \
            if (value) { memcpy(out_value, value, sizeof(type)); return CMETA_GEN_VALUE; } \
        } \
        return CMETA_GEN_DONE; \
    } \
    CMETA_INLINE cmeta_range name##_range(const name *self) { \
        cmeta_range range = { \
            self, CMETA_TYPEOF_OR(type, &name##_element_cmeta_type), (flags), \
            name##_cmeta_range_size, name##_cmeta_range_next, \
            cmeta_range_capture_version((version_accessor), self), (version_accessor) \
        }; \
        return range; \
    } \
    CMETA_INLINE cmeta_range name##_cmeta_erased_range(const void *object) { \
        return name##_range((const name *)object); \
    } \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc = { \
        CMETA_CONTAINER_STR(name), &name##_cmeta_type, \
        CMETA_TYPEOF_OR(type, &name##_element_cmeta_type), NULL, NULL, \
        name##_cmeta_erased_range, NULL, NULL, NULL \
    };

/* -------------------------------------------------------------------------
 * Two-type associative Range views
 * ------------------------------------------------------------------------- */
#define CMETA_CONTAINER_STR_I(x) #x
#define CMETA_CONTAINER_STR(x) CMETA_CONTAINER_STR_I(x)


#define CMETA_CONTAINER2_RANGES_DEFINE(name, key_type, value_type, prefix, key_at_op, value_at_op, key_flags, value_flags, entry_flags, version_accessor) \
    CMETA_LOCAL const cmeta_type_desc name##_cmeta_type = { \
        CMETA_CONTAINER_STR(name), sizeof(name), _Alignof(name), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_type_desc name##_key_cmeta_type = { \
        CMETA_CONTAINER_STR(key_type), sizeof(key_type), _Alignof(key_type), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_type_desc name##_value_cmeta_type = { \
        CMETA_CONTAINER_STR(value_type), sizeof(value_type), _Alignof(value_type), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_type_desc name##_entry_cmeta_type = { \
        CMETA_CONTAINER_STR(name) "_entry", sizeof(name##_entry), _Alignof(name##_entry), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_INLINE size_t name##_cmeta_assoc_range_size(const void *object) { \
        const name *self = (const name *)object; \
        return self ? CMETA_CONTAINER_API(prefix, size)(&self->raw) : 0U; \
    } \
    CMETA_INLINE cmeta_gen_status name##_cmeta_keys_next(const void *object, size_t *cursor, void *out_value) { \
        const name *self = (const name *)object; \
        size_t limit; \
        if (!self || !cursor || !out_value) return CMETA_GEN_ERROR; \
        limit = CMETA_CONTAINER_API(prefix, capacity)(&self->raw); \
        while (*cursor < limit) { \
            const key_type *key = (const key_type *)CMETA_CONTAINER_API(prefix, key_at_op)(&self->raw, *cursor); \
            ++(*cursor); \
            if (key) { memcpy(out_value, key, sizeof(key_type)); return CMETA_GEN_VALUE; } \
        } \
        return CMETA_GEN_DONE; \
    } \
    CMETA_INLINE cmeta_gen_status name##_cmeta_values_next(const void *object, size_t *cursor, void *out_value) { \
        const name *self = (const name *)object; \
        size_t limit; \
        if (!self || !cursor || !out_value) return CMETA_GEN_ERROR; \
        limit = CMETA_CONTAINER_API(prefix, capacity)(&self->raw); \
        while (*cursor < limit) { \
            const value_type *value = (const value_type *)CMETA_CONTAINER_API(prefix, value_at_op)(&self->raw, *cursor); \
            ++(*cursor); \
            if (value) { memcpy(out_value, value, sizeof(value_type)); return CMETA_GEN_VALUE; } \
        } \
        return CMETA_GEN_DONE; \
    } \
    CMETA_INLINE cmeta_gen_status name##_cmeta_entries_next(const void *object, size_t *cursor, void *out_value) { \
        const name *self = (const name *)object; \
        size_t limit; \
        if (!self || !cursor || !out_value) return CMETA_GEN_ERROR; \
        limit = CMETA_CONTAINER_API(prefix, capacity)(&self->raw); \
        while (*cursor < limit) { \
            size_t slot = *cursor; \
            const key_type *key = (const key_type *)CMETA_CONTAINER_API(prefix, key_at_op)(&self->raw, slot); \
            const value_type *value = (const value_type *)CMETA_CONTAINER_API(prefix, value_at_op)(&self->raw, slot); \
            ++(*cursor); \
            if (key && value) { \
                name##_entry *entry = (name##_entry *)out_value; \
                memcpy(&entry->key, key, sizeof(key_type)); \
                memcpy(&entry->value, value, sizeof(value_type)); \
                return CMETA_GEN_VALUE; \
            } \
        } \
        return CMETA_GEN_DONE; \
    } \
    CMETA_INLINE cmeta_range name##_keys_range(const name *self) { \
        cmeta_range range = { self, CMETA_TYPEOF_OR(key_type, &name##_key_cmeta_type), (key_flags), \
            name##_cmeta_assoc_range_size, name##_cmeta_keys_next, \
            cmeta_range_capture_version((version_accessor), self), (version_accessor) }; \
        return range; \
    } \
    CMETA_INLINE cmeta_range name##_values_range(const name *self) { \
        cmeta_range range = { self, CMETA_TYPEOF_OR(value_type, &name##_value_cmeta_type), (value_flags), \
            name##_cmeta_assoc_range_size, name##_cmeta_values_next, \
            cmeta_range_capture_version((version_accessor), self), (version_accessor) }; \
        return range; \
    } \
    CMETA_INLINE cmeta_range name##_entries_range(const name *self) { \
        cmeta_range range = { self, &name##_entry_cmeta_type, (entry_flags), \
            name##_cmeta_assoc_range_size, name##_cmeta_entries_next, \
            cmeta_range_capture_version((version_accessor), self), (version_accessor) }; \
        return range; \
    } \
    CMETA_INLINE cmeta_range name##_cmeta_erased_keys_range(const void *object) { \
        return name##_keys_range((const name *)object); \
    } \
    CMETA_INLINE cmeta_range name##_cmeta_erased_values_range(const void *object) { \
        return name##_values_range((const name *)object); \
    } \
    CMETA_INLINE cmeta_range name##_cmeta_erased_entries_range(const void *object) { \
        return name##_entries_range((const name *)object); \
    } \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc = { \
        CMETA_CONTAINER_STR(name), &name##_cmeta_type, NULL, \
        CMETA_TYPEOF_OR(key_type, &name##_key_cmeta_type), \
        CMETA_TYPEOF_OR(value_type, &name##_value_cmeta_type), \
        NULL, name##_cmeta_erased_keys_range, name##_cmeta_erased_values_range, \
        name##_cmeta_erased_entries_range \
    };


/* Descriptor-only capability for typed containers that intentionally expose no Range view. */
#define CMETA_CONTAINER2_OPAQUE_DESCRIPTOR_DEFINE(name, key_type, value_type) \
    CMETA_LOCAL const cmeta_type_desc name##_cmeta_type = { \
        CMETA_CONTAINER_STR(name), sizeof(name), _Alignof(name), CMETA_T_OBJECT, NULL \
    }; \
    CMETA_LOCAL const cmeta_container_desc name##_cmeta_container_desc = { \
        CMETA_CONTAINER_STR(name), &name##_cmeta_type, NULL, NULL, NULL, \
        NULL, NULL, NULL, NULL \
    };

#endif /* CMETA_CONTAINER_H */
