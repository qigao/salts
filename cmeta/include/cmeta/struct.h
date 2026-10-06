#ifndef CMETA_STRUCT_H
#define CMETA_STRUCT_H

#include <cmeta/declared_type.h>
#include <cmeta/pp.h>
#include <cmeta/type_select.h>

#include <stddef.h>
#include <string.h>

/* Inspect native member type at compile time, including array extent and
 * qualifiers. No object evaluation or reflection lookup is involved. */
#ifdef __cplusplus
#include <type_traits>
#define cmeta_require_field(owner_, member_, type_) \
    static_assert(std::is_same<decltype(static_cast<owner_ *>(nullptr)->member_), type_>::value, \
                  "CMeta required field type mismatch")
#else
#define cmeta_require_field(owner_, member_, type_) \
    _Static_assert(_Generic(&((owner_ *)0)->member_, type_ *: 1, default: 0), \
                   "CMeta required field type mismatch")
#endif

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

/*
 * Explicit marker for a reflected logical field whose native storage is not
 * reachable as object + offset. Such fields require a provider-backed object
 * access path; fixed-layout reflection must fail closed instead of inventing
 * an offset.
 */
#define CMETA_FIELD_DYNAMIC_OFFSET ((size_t)-1)

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
    { CMETA_PP_STRINGIFY(name), CMETA_PP_STRINGIFY(type), \
      offsetof(owner, name), CMETA_STRUCT_FIELD_SIZE(owner, name), \
      CMETA_ALIGNOF(type), CMETA_TYPEOF_OR(type, CMETA_STRUCT_TYPE_NULL), \
      NULL },
#define CMETA_STRUCT_FIELD_DESC_1(owner, spec, name) \
    { CMETA_PP_STRINGIFY(name), CMETA_PP_STRINGIFY(spec), \
      offsetof(owner, name), CMETA_STRUCT_FIELD_SIZE(owner, name), \
      CMETA_ALIGNOF(CMETA_STRUCT_STORAGE(spec)), \
      CMETA_TYPE_SPEC_STORAGE_DESC(spec), \
      &CMETA_STRUCT_FIELD_DECLARED_NAME(owner, name) },

/* Single-declaration reflected struct.
 *
 * The low-level CMETA_STRUCT replay kernel consumes comma-separated
 * (type, name) tuples. The public cmeta_struct/cmeta_field surface below
 * deliberately presents a declaration-like stream without field separators:
 *
 *   cmeta_struct(Point,
 *       cmeta_field(int, x)
 *       cmeta_field(int, y)
 *   );
 *
 * cmeta_field expands to a leading comma plus the existing tuple row. The
 * cmeta_struct trampoline injects and discards one sentinel argument, so the
 * established Schema/Replay implementation remains the single metadata/layout
 * generator. No source parser or lowering phase is involved.
 *
 * A provider may also expose a generic type-position token:
 *
 *   cmeta_struct(Payload,
 *       cmeta_field(TYPE(Vec, int), values)
 *   );
 */
#define CMETA_STRUCT(...) CMETA_STRUCT_I(__VA_ARGS__)
#define CMETA_STRUCT_I(type, ...) \
    typedef struct type { \
        Schema(CMETA_STRUCT_FIELD_DECL, __VA_ARGS__) \
    } type; \
    CMETA_SCHEMA_ROWS(CMETA_STRUCT_FIELD_DECLARED, type, __VA_ARGS__) \
    CMETA_LOCAL const cmeta_field_desc type##__struct_fields[] = { \
        CMETA_SCHEMA_ROWS(CMETA_STRUCT_FIELD_DESC, type, __VA_ARGS__) \
    }; \
    CMETA_LOCAL const cmeta_struct_desc type##__struct_meta = { \
        CMETA_PP_STRINGIFY(type), sizeof(type), CMETA_ALIGNOF(type), type##__struct_fields, \
        sizeof(type##__struct_fields) / sizeof(type##__struct_fields[0]) \
    }; \
    CMETA_INLINE const cmeta_struct_desc *type##_meta(void) { \
        return &type##__struct_meta; \
    } \
    typedef char type##__struct_declaration_complete[1]

/* Public declaration-like field stream.
 *
 * The leading comma is intentional. During cmeta_struct expansion:
 *
 *   sentinel cmeta_field(int, x) cmeta_field(long, y)
 *
 * becomes:
 *
 *   sentinel, (int, x), (long, y)
 *
 * and the trampoline drops sentinel before invoking CMETA_STRUCT.
 */
#ifndef cmeta_field
#define cmeta_field(type_, name_) , (type_, name_)
#endif

#define CMETA_STRUCT_PUBLIC_EXPAND_(...) CMETA_STRUCT_PUBLIC_DISPATCH_(__VA_ARGS__)
#define CMETA_STRUCT_PUBLIC_DISPATCH_(type_, sentinel_, ...) \
    CMETA_STRUCT(type_, __VA_ARGS__)

#ifndef cmeta_struct
#define cmeta_struct(type_, ...) \
    CMETA_STRUCT_PUBLIC_EXPAND_(type_, cmeta_struct_field_sentinel __VA_ARGS__)
#endif

/* Legacy framework spelling keeps the low-level tuple replay contract for
 * internal schemas. New application code should use cmeta_struct/cmeta_field.
 */
#ifndef Struct
#define Struct(type, ...) CMETA_STRUCT(type, __VA_ARGS__)
#endif

#ifndef StructMeta
#define StructMeta(type) (&CMETA_PP_CAT(type,__struct_meta))
#endif

/* Linux-style intrusive owner projection with ordinary C11 type checking.
 *
 *   cmeta_intrusive(Task, ready_node, ListNode);
 *
 * generates:
 *   Task *Task_from_ready_node(ListNode *);
 *   const Task *Task_from_ready_node_const(const ListNode *);
 *
 * The member type is explicit so strict C11 can validate it with _Generic
 * without relying on GNU typeof/statement expressions.
 */
#define CMETA_INTRUSIVE_FROM_NAME_I_(owner_, member_) owner_##_from_##member_
#define CMETA_INTRUSIVE_FROM_NAME_(owner_, member_) \
    CMETA_INTRUSIVE_FROM_NAME_I_(owner_, member_)
#define CMETA_INTRUSIVE_FROM_CONST_NAME_I_(owner_, member_) \
    owner_##_from_##member_##_const
#define CMETA_INTRUSIVE_FROM_CONST_NAME_(owner_, member_) \
    CMETA_INTRUSIVE_FROM_CONST_NAME_I_(owner_, member_)

#define CMETA_INTRUSIVE(owner_, member_, member_type_)                         \
    _Static_assert(                                                           \
        _Generic(&((owner_ *)0)->member_, member_type_ *: 1, default: 0),     \
        "CMeta intrusive member type mismatch");                             \
    CMETA_INLINE owner_ *CMETA_INTRUSIVE_FROM_NAME_(owner_, member_)(         \
        member_type_ *member_ptr_) {                                          \
        return member_ptr_ == NULL                                            \
                   ? NULL                                                     \
                   : (owner_ *)((unsigned char *)member_ptr_ -                 \
                                offsetof(owner_, member_));                    \
    }                                                                         \
    CMETA_INLINE const owner_ *                                               \
    CMETA_INTRUSIVE_FROM_CONST_NAME_(owner_, member_)(                        \
        const member_type_ *member_ptr_) {                                    \
        return member_ptr_ == NULL                                            \
                   ? NULL                                                     \
                   : (const owner_ *)((const unsigned char *)member_ptr_ -     \
                                      offsetof(owner_, member_));              \
    }

#ifndef cmeta_intrusive
#define cmeta_intrusive(owner_, member_, member_type_) \
    CMETA_INTRUSIVE(owner_, member_, member_type_)
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
