#ifndef CMETA_TYPE_SELECT_H
#define CMETA_TYPE_SELECT_H

#include <cmeta/cmeta.h>
#include <cmeta/pp.h>
#include <cmeta/types.h>

#ifdef __cplusplus
extern "C++" {

template <typename T>
inline constexpr const cmeta_type_desc *cmeta_typeof_cpp() noexcept {
    return nullptr;
}

#define CMETA_CPP_TYPEOF_SPECIALIZE(row, ignored) \
    template <> \
    inline constexpr const cmeta_type_desc * \
    cmeta_typeof_cpp<CMETA_TYPE_CTYPE(row)>() noexcept { \
        return &CMETA_TYPE_DESC(row); \
    }
CMETA_PP_FOR_EACH_A(CMETA_CPP_TYPEOF_SPECIALIZE, ~, CMETA_KNOWN_TYPE_LIST)
#undef CMETA_CPP_TYPEOF_SPECIALIZE

#define CMETA_TYPEOF(type) (::cmeta_typeof_cpp<type>())
#define CMETA_TYPEOF_OR(type, fallback_desc) \
    (::cmeta_typeof_cpp<type>() != nullptr ? \
         ::cmeta_typeof_cpp<type>() : (fallback_desc))

}

#else

#define CMETA_TYPE_SELECT_ASSOC(row, ignored) \
    CMETA_TYPE_CTYPE(row) *: &CMETA_TYPE_DESC(row),
#define CMETA_TYPE_SELECT(type, fallback_desc) \
    _Generic((type *)0, \
        CMETA_PP_FOR_EACH_B(CMETA_TYPE_SELECT_ASSOC, ~, CMETA_KNOWN_TYPE_LIST) \
        default: (fallback_desc))
#define CMETA_TYPEOF(type) \
    CMETA_TYPE_SELECT(type, (const cmeta_type_desc *)0)
#define CMETA_TYPEOF_OR(type, fallback_desc) \
    CMETA_TYPE_SELECT(type, (fallback_desc))

#endif

#endif /* CMETA_TYPE_SELECT_H */
