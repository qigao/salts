#ifndef CMETA_STRUCT_H
#define CMETA_STRUCT_H

#include <cmeta/declared_type.h>
#include <cmeta/pp.h>
#include <cmeta/type_select.h>

#include <stddef.h>
#include <string.h>

typedef struct cmeta_field_desc {
    const char *name;
    const char *type_name;
    size_t offset;
    size_t size;
    size_t align;
    const cmeta_type_desc *type;
    const cmeta_declared_type *declared_type;
} cmeta_field_desc;

typedef struct cmeta_struct_desc {
    const char *name;
    size_t size;
    size_t align;
    const cmeta_field_desc *fields;
    size_t field_count;
} cmeta_struct_desc;

CMETA_INLINE const cmeta_field_desc *
cmeta_struct_field(const cmeta_struct_desc *desc, size_t index) {
    return desc && index < desc->field_count ? &desc->fields[index] : NULL;
}

CMETA_INLINE const cmeta_field_desc *
cmeta_struct_find_field(const cmeta_struct_desc *desc, const char *name) {
    size_t i;
    if (!desc || !name) return NULL;
    for (i = 0; i < desc->field_count; ++i)
        if (strcmp(desc->fields[i].name, name) == 0) return &desc->fields[i];
    return NULL;
}

#define CMETA_STRUCT_STORAGE_0(type) type
#define CMETA_STRUCT_STORAGE_1(spec) CMETA_TYPE_SPEC_STORAGE(spec)
#define CMETA_STRUCT_STORAGE_I(is_type, type) \
    CMETA_PP_CAT(CMETA_STRUCT_STORAGE_, is_type)(type)
#define CMETA_STRUCT_STORAGE(type) \
    CMETA_STRUCT_STORAGE_I(CMETA_TYPE_SPEC_IS(type), type)

#define CMETA_STRUCT_FIELD_DECL(type, name) CMETA_STRUCT_STORAGE(type) name;

#ifdef __cplusplus
#define CMETA_STRUCT_FIELD_SIZE(owner, name) \
    sizeof(static_cast<owner *>(nullptr)->name)
#define CMETA_STRUCT_TYPE_NULL nullptr
#else
#define CMETA_STRUCT_FIELD_SIZE(owner, name) sizeof(((owner *)0)->name)
#define CMETA_STRUCT_TYPE_NULL ((const cmeta_type_desc *)0)
#endif

#define CMETA_STRUCT_FIELD_ARGS_NAME_I(owner, name) owner##__##name##__type_args
#define CMETA_STRUCT_FIELD_ARGS_NAME(owner, name) \
    CMETA_STRUCT_FIELD_ARGS_NAME_I(owner, name)
#define CMETA_STRUCT_FIELD_DECLARED_NAME_I(owner, name) \
    owner##__##name##__declared_type
#define CMETA_STRUCT_FIELD_DECLARED_NAME(owner, name) \
    CMETA_STRUCT_FIELD_DECLARED_NAME_I(owner, name)

#define CMETA_STRUCT_DECLARED_ARG(arg, ignored) CMETA_TYPEOF(arg),

#define CMETA_STRUCT_FIELD_DECLARED(field, owner) \
    CMETA_STRUCT_FIELD_DECLARED_I(owner, CMETA_PP_UNPAREN field)
#define CMETA_STRUCT_FIELD_DECLARED_I(owner, ...) \
    CMETA_STRUCT_FIELD_DECLARED_II(owner, __VA_ARGS__)
#define CMETA_STRUCT_FIELD_DECLARED_II(owner, type, name) \
    CMETA_PP_CAT(CMETA_STRUCT_FIELD_DECLARED_, CMETA_TYPE_SPEC_IS(type))( \
        owner, type, name)
#define CMETA_STRUCT_FIELD_DECLARED_0(owner, type, name)
#define CMETA_STRUCT_FIELD_DECLARED_1(owner, spec, name) \
    CMETA_STRUCT_FIELD_DECLARED_TYPE(owner, name, CMETA_PP_UNPAREN spec)
#define CMETA_STRUCT_FIELD_DECLARED_TYPE(owner, name, ...) \
    CMETA_STRUCT_FIELD_DECLARED_TYPE_I(owner, name, __VA_ARGS__)
#define CMETA_STRUCT_FIELD_DECLARED_TYPE_I(owner, name, tag, kind, ...) \
    CMETA_LOCAL const cmeta_type_desc *const \
        CMETA_STRUCT_FIELD_ARGS_NAME(owner, name)[] = { \
            CMETA_PP_FOR_EACH_A(CMETA_STRUCT_DECLARED_ARG, ~, __VA_ARGS__) \
        }; \
    CMETA_LOCAL const cmeta_declared_type \
        CMETA_STRUCT_FIELD_DECLARED_NAME(owner, name) = { \
            CMETA_PP_CAT(CMETA_DECLARED_STORAGE_DESC_, kind), \
            CMETA_PP_CAT(CMETA_DECLARED_CONSTRUCTOR_, kind), \
            CMETA_STRUCT_FIELD_ARGS_NAME(owner, name), \
            CMETA_PP_NARG(__VA_ARGS__), \
            CMETA_PP_CAT(CMETA_DECLARED_CONSTRUCTION_, kind) \
        };

#define CMETA_STRUCT_FIELD_DESC(field, owner) \
    CMETA_STRUCT_FIELD_DESC_I(owner, CMETA_PP_UNPAREN field)
#define CMETA_STRUCT_FIELD_DESC_I(owner, ...) \
    CMETA_STRUCT_FIELD_DESC_II(owner, __VA_ARGS__)
#define CMETA_STRUCT_FIELD_DESC_II(owner, type, name) \
    CMETA_PP_CAT(CMETA_STRUCT_FIELD_DESC_, CMETA_TYPE_SPEC_IS(type))( \
        owner, type, name)
#define CMETA_STRUCT_FIELD_DESC_0(owner, type, name) \
    { #name, #type, offsetof(owner, name), CMETA_STRUCT_FIELD_SIZE(owner, name), \
      CMETA_ALIGNOF(type), CMETA_TYPEOF_OR(type, CMETA_STRUCT_TYPE_NULL), \
      NULL },
#define CMETA_STRUCT_FIELD_DESC_1(owner, spec, name) \
    { #name, #spec, offsetof(owner, name), CMETA_STRUCT_FIELD_SIZE(owner, name), \
      CMETA_ALIGNOF(CMETA_STRUCT_STORAGE(spec)), \
      CMETA_TYPE_SPEC_STORAGE_DESC(spec), \
      &CMETA_STRUCT_FIELD_DECLARED_NAME(owner, name) },

/* Single-declaration reflected struct.
 *
 *   Struct(Point,
 *       (int, x),
 *       (int, y)
 *   );
 *
 * A provider may also expose a generic type-position token:
 *
 *   Struct(Payload,
 *       (TYPE(Vec, int), values)
 *   );
 *
 * Field rows remain plain (type, name) tuples; no Field wrapper is needed.
 */
#define CMETA_STRUCT(type, ...) \
    typedef struct type { \
        Schema(CMETA_STRUCT_FIELD_DECL, __VA_ARGS__) \
    } type; \
    CMETA_SCHEMA_ROWS(CMETA_STRUCT_FIELD_DECLARED, type, __VA_ARGS__) \
    CMETA_LOCAL const cmeta_field_desc type##__struct_fields[] = { \
        CMETA_SCHEMA_ROWS(CMETA_STRUCT_FIELD_DESC, type, __VA_ARGS__) \
    }; \
    CMETA_LOCAL const cmeta_struct_desc type##__struct_meta = { \
        #type, sizeof(type), CMETA_ALIGNOF(type), type##__struct_fields, \
        sizeof(type##__struct_fields) / sizeof(type##__struct_fields[0]) \
    }; \
    CMETA_INLINE const cmeta_struct_desc *type##_meta(void) { \
        return &type##__struct_meta; \
    } \
    typedef char type##__struct_declaration_complete[1]

#ifndef Struct
#define Struct(type, ...) CMETA_STRUCT(type, __VA_ARGS__)
#endif

#ifndef StructMeta
#define StructMeta(type) (&type##__struct_meta)
#endif

#ifndef FieldCount
#define FieldCount(type) (StructMeta(type)->field_count)
#endif

#ifndef FieldMeta
#define FieldMeta(type, index) cmeta_struct_field(StructMeta(type), (index))
#endif

#ifndef FieldFind
#define FieldFind(type, name) cmeta_struct_find_field(StructMeta(type), (name))
#endif

#endif
