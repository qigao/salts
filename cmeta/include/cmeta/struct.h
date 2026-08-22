#ifndef CMETA_STRUCT_H
#define CMETA_STRUCT_H

#include <cmeta/pp.h>

#include <stddef.h>
#include <string.h>

typedef struct cmeta_field_desc {
    const char *name;
    const char *type_name;
    size_t offset;
    size_t size;
    size_t align;
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

#define CMETA_STRUCT_FIELD_DECL(type, name) type name;

#define CMETA_STRUCT_FIELD_DESC(field, owner) CMETA_STRUCT_FIELD_DESC_I(owner, CMETA_PP_UNPAREN field)
#define CMETA_STRUCT_FIELD_DESC_I(owner, ...) CMETA_STRUCT_FIELD_DESC_II(owner, __VA_ARGS__)
#define CMETA_STRUCT_FIELD_DESC_II(owner, type, name) \
    { #name, #type, offsetof(owner, name), sizeof(((owner *)0)->name), CMETA_ALIGNOF(type) },

/* Single-declaration reflected struct.
 *
 *   Struct(Point,
 *       (int, x),
 *       (int, y)
 *   );
 *
 * Field rows are plain (type, name) tuples; no Field wrapper is needed.
 */
#define CMETA_STRUCT(type, ...) \
    typedef struct type { \
        Schema(CMETA_STRUCT_FIELD_DECL, __VA_ARGS__) \
    } type; \
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
