#ifndef CMETA_DATA_SELECT_H
#define CMETA_DATA_SELECT_H

#include <cmeta/data.h>
#include <cmeta/type_select.h>
#include <cmeta/compiler.h>
#include <stdint.h>

/* A schema owns type-to-Data selection only; each expression refers to the
 * provider's existing canonical descriptor. Do not list typedef aliases twice. */
#define CMETA_BUILTIN_DATA_SCHEMA(M) \
    Schema(M, (CMETA_BOOL_TYPE, &cmeta_data_bool), (int, &cmeta_data_int), \
        (long, &cmeta_data_long), (float, &cmeta_data_float), (double, &cmeta_data_double))

/* The pointer is a type witness: it is neither evaluated nor dereferenced.
 * Unregistered/volatile pointer types and duplicate compatible rows are errors.
 * Only the selected descriptor expression is evaluated, exactly once. */
#ifdef __cplusplus
#define CMETA_DATA_POINTER_ROW_(native, descriptor) \
    static_assert(std::is_same<decltype(descriptor), const cmeta_data_desc *>::value, \
        "CMeta data schema requires a const DataDesc pointer"); \
    static const cmeta_data_desc *select(native *) { return (descriptor); } \
    static const cmeta_data_desc *select(const native *) { return (descriptor); }
#define CMETA_DATA_POINTER_MATCH_(native, descriptor) \
    || std::is_same<witness, native *>::value || std::is_same<witness, const native *>::value
#define cmeta_data_of_in(pointer, schema) \
    ([]() -> const cmeta_data_desc * { \
        using witness = typename std::remove_cv<typename std::remove_reference<decltype(pointer)>::type>::type; \
        static_assert(false Replay(schema, CMETA_DATA_POINTER_MATCH_), \
            "CMeta data schema has no exact pointer type"); \
        struct selection { Replay(schema, CMETA_DATA_POINTER_ROW_) }; \
        return selection::select(static_cast<witness>(nullptr)); \
    }())
#else
#define CMETA_DATA_POINTER_ROW_(native, descriptor) \
    native *: _Generic((descriptor), const cmeta_data_desc *: (descriptor)), \
    const native *: _Generic((descriptor), const cmeta_data_desc *: (descriptor)),
#define CMETA_DATA_POINTER_MATCH_(native, descriptor) native *: 1, const native *: 1,
#define cmeta_data_of_in(pointer, schema) \
    (CMETA_CONST_REQUIRE(_Generic((pointer), Replay(schema, CMETA_DATA_POINTER_MATCH_) default: 0)), \
        _Generic((pointer), Replay(schema, CMETA_DATA_POINTER_ROW_) default: (const cmeta_data_desc *)0))
#endif
#define cmeta_data_of(pointer) cmeta_data_of_in(pointer, CMETA_BUILTIN_DATA_SCHEMA)

/*
 * Fixed-width typedefs may be aliases of int/long and therefore cannot be
 * portably added as duplicate _Generic/template specializations. Resolve them
 * by signedness + exact width through this canonical facade instead.
 */
const cmeta_data_desc *cmeta_data_integer_width(bool is_signed, uint8_t bits);

#ifdef __cplusplus
extern "C++" {

template <typename T>
inline constexpr const cmeta_data_desc *cmeta_dataof_cpp() noexcept {
    return nullptr;
}

#define CMETA_DATA_TYPE_CPP_(native, descriptor) \
    template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<native>() noexcept { \
        return (descriptor); }
Replay(CMETA_BUILTIN_DATA_SCHEMA, CMETA_DATA_TYPE_CPP_)
#undef CMETA_DATA_TYPE_CPP_

#define CMETA_DATAOF(type) (::cmeta_dataof_cpp<type>())
#define CMETA_DATAOF_OR(type, fallback_desc) \
    (::cmeta_dataof_cpp<type>() != nullptr ? \
         ::cmeta_dataof_cpp<type>() : (fallback_desc))

}

#else

#define CMETA_DATA_TYPE_ASSOC_(native, descriptor) native *: (descriptor),
#define CMETA_DATA_SELECT(type, fallback_desc) \
    _Generic((type *)0, Replay(CMETA_BUILTIN_DATA_SCHEMA, CMETA_DATA_TYPE_ASSOC_) \
        default: (fallback_desc))

#define CMETA_DATAOF(type) \
    CMETA_DATA_SELECT(type, (const cmeta_data_desc *)0)
#define CMETA_DATAOF_OR(type, fallback_desc) \
    CMETA_DATA_SELECT(type, (fallback_desc))

#endif

#endif /* CMETA_DATA_SELECT_H */
