#ifndef CMETA_DATA_REFLECT_H
#define CMETA_DATA_REFLECT_H

#include <cmeta/data_select.h>

/* Reflect an existing native record; this does not declare or own the record.
 * Rows are cmeta_field(native, member), or an explicit semantic override below.
 * IDs must be nonempty string literals. Descriptors must be immutable static
 * const objects and outlive every consumer. No registration or allocation runs.
 *
 * cmeta_reflect_data(User, "app.User",
 *     cmeta_field(int, id)
 *     cmeta_field(double, score)
 * );
 *
 * StructMeta(User) and cmeta_reflected_data(User) expose the generated views.
 * This declares one fixed-layout view per native type token, with 1..16 fields.
 * cmeta_reflect_data is a read-only projection and permits omitted fields.
 * cmeta_reflect_value explicitly attests complete, fieldwise value lifecycle.
 * C++ value reflection additionally requires trivial native lifecycle.
 * Explicit fields may supply a fourth argument: the canonical storage type
 * descriptor. It is required for storage outside CMETA_TYPEOF's builtin set.
 * Neither declaration grants object-field assignment or object ownership.
 */
#define CMETA_DATA_FIELD_3(native_, member_, descriptor_) \
    CMETA_DATA_FIELD_4(native_, member_, descriptor_, CMETA_TYPEOF(native_))
#define CMETA_DATA_FIELD_4(native_, member_, descriptor_, storage_) \
    , (native_, member_, descriptor_, storage_)
#define cmeta_data_field(...) \
    CMETA_PP_OVERLOAD(CMETA_DATA_FIELD_, __VA_ARGS__)(__VA_ARGS__)
#define CMETA_DATA_FIELD_ID_4(native_, member_, stable_id_, descriptor_) \
    CMETA_DATA_FIELD_ID_5(native_, member_, stable_id_, descriptor_, CMETA_TYPEOF(native_))
#define CMETA_DATA_FIELD_ID_5(native_, member_, stable_id_, descriptor_, storage_) \
    , (native_, member_, descriptor_, storage_, stable_id_)
#define cmeta_data_field_id(...) \
    CMETA_PP_OVERLOAD(CMETA_DATA_FIELD_ID_, __VA_ARGS__)(__VA_ARGS__)

#define cmeta_reflected_data(owner_) (&CMETA_PP_CAT(owner_, __data_meta))
#define cmeta_reflected_storage(owner_) (&CMETA_PP_CAT(owner_, __data_type))

#ifdef __cplusplus
#define CMETA_REFLECT_OWNER_PROOF_(owner_, mode_) \
    static_assert(std::is_standard_layout<owner_>::value, \
        "CMeta data reflection requires standard-layout storage"); \
    static_assert((mode_) == CMETA_DATA_REFLECTION_VIEW || \
        (std::is_trivial<owner_>::value && !std::is_union<owner_>::value), \
        "CMeta value reflection requires trivial non-union storage");
#define CMETA_REFLECT_QUALIFIER_PROOF_(native_, mode_) \
    static_assert(!std::is_volatile<native_>::value, \
        "CMeta reflection does not support volatile storage"); \
    static_assert((mode_) == CMETA_DATA_REFLECTION_VIEW || !std::is_const<native_>::value, \
        "CMeta value reflection requires writable fields");
/* Prove the builtin *type witness*, not the address of its extern
 * descriptor object. GCC/Clang are not required to treat a pointer to an
 * externally defined descriptor as an integral constant expression (and the
 * sanitizer Debug compilation correctly refuses to do so). This exact
 * schema replay matches the C11 _Generic builtin-type admission below. */
#define CMETA_REFLECT_BUILTIN_TYPE_MATCH_CPP_(builtin_, descriptor_) \
    || std::is_same<T, builtin_>::value
template <typename T>
struct cmeta_reflect_has_builtin_data_cpp
    : std::integral_constant<bool,
          false Replay(CMETA_BUILTIN_DATA_SCHEMA,
                       CMETA_REFLECT_BUILTIN_TYPE_MATCH_CPP_)> {};
#undef CMETA_REFLECT_BUILTIN_TYPE_MATCH_CPP_
#define CMETA_REFLECT_BUILTIN_PROOF_(native_) \
    static_assert(cmeta_reflect_has_builtin_data_cpp<native_>::value, \
        "CMeta data reflection requires an explicit data descriptor for this type");
#else
#define CMETA_REFLECT_OWNER_PROOF_(owner_, mode_)
#define CMETA_REFLECT_WRITABLE_CMETA_DATA_REFLECTION_VIEW(native_)
#define CMETA_REFLECT_WRITABLE_CMETA_DATA_REFLECTION_VALUE(native_) \
    _Static_assert(_Generic((native_ *)0, const native_ *: 0, default: 1), \
        "CMeta value reflection requires writable fields");
#define CMETA_REFLECT_QUALIFIER_PROOF_(native_, mode_) \
    _Static_assert(_Generic((native_ *)0, volatile native_ *: 0, default: 1), \
        "CMeta reflection does not support volatile storage"); \
    CMETA_PP_CAT(CMETA_REFLECT_WRITABLE_, mode_)(native_)
#define CMETA_REFLECT_BUILTIN_MATCH_(native_, descriptor_) native_ *: 1,
#define CMETA_REFLECT_BUILTIN_PROOF_(native_) \
    _Static_assert(_Generic((native_ *)0, \
        Replay(CMETA_BUILTIN_DATA_SCHEMA, CMETA_REFLECT_BUILTIN_MATCH_) default: 0), \
        "CMeta data reflection requires an explicit data descriptor for this type");
#endif

#define CMETA_REFLECT_ID_PROOF_(id_) \
    CMETA_STATIC_ASSERT(sizeof("" id_) > 1u, "CMeta data reflection requires a nonempty stable ID");
#define CMETA_REFLECT_FIELD_PROOF_(row_, context_) \
    CMETA_REFLECT_FIELD_PROOF_E_(CMETA_PP_TUPLE_GET_0(context_), \
        CMETA_PP_TUPLE_GET_1(context_), CMETA_PP_UNPAREN row_)
#define CMETA_REFLECT_FIELD_PROOF_E_(owner_, mode_, ...) \
    CMETA_PP_OVERLOAD(CMETA_REFLECT_FIELD_PROOF_, __VA_ARGS__)(owner_, mode_, __VA_ARGS__)
#define CMETA_REFLECT_FIELD_PROOF_2(owner_, mode_, native_, member_) \
    CMETA_REFLECT_BUILTIN_PROOF_(native_) \
    CMETA_REFLECT_FIELD_PROOF_4(owner_, mode_, native_, member_, \
        CMETA_DATAOF(native_), CMETA_TYPEOF(native_))
#define CMETA_REFLECT_FIELD_PROOF_4(owner_, mode_, native_, member_, descriptor_, storage_) \
    CMETA_STRUCT_FIELD_PROOF_I(owner_, native_, member_) \
    typedef native_ owner_##__data_native_##member_; \
    CMETA_REFLECT_QUALIFIER_PROOF_(owner_##__data_native_##member_, mode_) \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES((descriptor_), const cmeta_data_desc *), \
        "CMeta data reflection requires a const DataDesc pointer"); \
    CMETA_STATIC_ASSERT(CMETA_TYPE_MATCHES((storage_), const cmeta_type_desc *), \
        "CMeta data reflection requires a const storage descriptor pointer"); \
    enum { owner_##__data_field_##member_ = 0 };
#define CMETA_REFLECT_FIELD_PROOF_5(owner_, mode_, native_, member_, descriptor_, storage_, id_) \
    CMETA_REFLECT_FIELD_PROOF_4(owner_, mode_, native_, member_, descriptor_, storage_) \
    CMETA_REFLECT_ID_PROOF_(id_)

#define CMETA_REFLECT_LAYOUT_(row_, owner_) \
    CMETA_REFLECT_LAYOUT_E_(owner_, CMETA_PP_UNPAREN row_)
#define CMETA_REFLECT_LAYOUT_E_(owner_, ...) \
    CMETA_PP_OVERLOAD(CMETA_REFLECT_LAYOUT_, __VA_ARGS__)(owner_, __VA_ARGS__)
#define CMETA_REFLECT_LAYOUT_2(owner_, native_, member_) \
    CMETA_STRUCT_FIELD_DESC_0(owner_, native_, member_)
#define CMETA_REFLECT_LAYOUT_4(owner_, native_, member_, descriptor_, storage_) \
    { CMETA_PP_STRINGIFY(member_), CMETA_PP_STRINGIFY(native_), \
      offsetof(owner_, member_), sizeof(native_), CMETA_ALIGNOF(native_), (storage_), NULL },
#define CMETA_REFLECT_LAYOUT_5(owner_, native_, member_, descriptor_, storage_, id_) \
    CMETA_REFLECT_LAYOUT_4(owner_, native_, member_, descriptor_, storage_)

#define CMETA_REFLECT_FIELD_(row_, context_) \
    CMETA_REFLECT_FIELD_E_(CMETA_PP_TUPLE_GET_0(context_), \
        CMETA_PP_TUPLE_GET_1(context_), CMETA_PP_UNPAREN row_)
#define CMETA_REFLECT_FIELD_E_(owner_, prefix_, ...) \
    CMETA_PP_OVERLOAD(CMETA_REFLECT_FIELD_, __VA_ARGS__)(owner_, prefix_, __VA_ARGS__)
#define CMETA_REFLECT_FIELD_2(owner_, prefix_, native_, member_) \
    CMETA_REFLECT_FIELD_4(owner_, prefix_, native_, member_, CMETA_DATAOF(native_), CMETA_TYPEOF(native_))
#define CMETA_REFLECT_FIELD_4(owner_, prefix_, native_, member_, descriptor_, storage_) \
    CMETA_REFLECT_FIELD_5(owner_, prefix_, native_, member_, descriptor_, storage_, \
        prefix_ "." CMETA_PP_STRINGIFY(member_))
#define CMETA_REFLECT_FIELD_5(owner_, prefix_, native_, member_, descriptor_, storage_, id_) \
    { id_, CMETA_PP_STRINGIFY(member_), offsetof(owner_, member_), (descriptor_) },

#define CMETA_REFLECT_DATA_(owner_, id_, mode_, ...) \
    CMETA_REFLECT_OWNER_PROOF_(owner_, mode_) \
    CMETA_REFLECT_ID_PROOF_(id_) \
    CMETA_PP_FOR_EACH_A(CMETA_REFLECT_FIELD_PROOF_, (owner_, mode_), __VA_ARGS__) \
    CMETA_STRUCT_METADATA_DEFINE(owner_, \
        CMETA_PP_FOR_EACH_A(CMETA_REFLECT_LAYOUT_, owner_, __VA_ARGS__)); \
    CMETA_LOCAL const cmeta_type_identity owner_##__data_identity = \
        CMETA_TYPE_ID_ATOM_INIT(id_); \
    CMETA_LOCAL const cmeta_type_desc owner_##__data_type = { \
        CMETA_PP_STRINGIFY(owner_), sizeof(owner_), CMETA_ALIGNOF(owner_), \
        CMETA_T_OBJECT, NULL, NULL, &owner_##__data_identity \
    }; \
    CMETA_LOCAL const cmeta_data_field_desc owner_##__data_fields[] = { \
        CMETA_PP_FOR_EACH_A(CMETA_REFLECT_FIELD_, (owner_, id_), __VA_ARGS__) \
    }; \
    CMETA_LOCAL const cmeta_data_reflection_shape owner_##__data_shape = { \
        { StructMeta(owner_), owner_##__data_fields, \
          sizeof(owner_##__data_fields) / sizeof(owner_##__data_fields[0]) }, \
        sizeof(cmeta_data_reflection_shape), mode_ \
    }; \
    CMETA_LOCAL const cmeta_data_desc owner_##__data_meta = { \
        sizeof(cmeta_data_desc), CMETA_DATA_DESC_REFLECTION_ABI_VERSION, id_, \
        CMETA_PP_STRINGIFY(owner_), CMETA_DATA_STRUCT, &owner_##__data_type, \
        &owner_##__data_shape, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL \
    }

#define CMETA_REFLECT_DATA_PUBLIC_E_(...) CMETA_REFLECT_DATA_PUBLIC_I_(__VA_ARGS__)
#define CMETA_REFLECT_DATA_PUBLIC_I_(owner_, id_, mode_, sentinel_, ...) \
    CMETA_REFLECT_DATA_(owner_, id_, mode_, __VA_ARGS__)
#define cmeta_reflect_data(owner_, stable_id_, ...) \
    CMETA_REFLECT_DATA_PUBLIC_E_(owner_, stable_id_, CMETA_DATA_REFLECTION_VIEW, \
        cmeta_data_field_sentinel __VA_ARGS__)
#define cmeta_reflect_value(owner_, stable_id_, ...) \
    CMETA_REFLECT_DATA_PUBLIC_E_(owner_, stable_id_, CMETA_DATA_REFLECTION_VALUE, \
        cmeta_data_field_sentinel __VA_ARGS__)

#endif /* CMETA_DATA_REFLECT_H */
