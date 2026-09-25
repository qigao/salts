#ifndef CMETA_DATA_SELECT_H
#define CMETA_DATA_SELECT_H

#include <cmeta/data.h>
#include <cmeta/type_select.h>

#ifdef __cplusplus
extern "C++" {

template <typename T>
inline constexpr const cmeta_data_desc *cmeta_dataof_cpp() noexcept {
    return nullptr;
}

template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<bool>() noexcept {
    return &cmeta_data_bool;
}
template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<int>() noexcept {
    return &cmeta_data_int;
}
template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<long>() noexcept {
    return &cmeta_data_long;
}
template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<float>() noexcept {
    return &cmeta_data_float;
}
template <> inline constexpr const cmeta_data_desc *cmeta_dataof_cpp<double>() noexcept {
    return &cmeta_data_double;
}

#define CMETA_DATAOF(type) (::cmeta_dataof_cpp<type>())
#define CMETA_DATAOF_OR(type, fallback_desc) \
    (::cmeta_dataof_cpp<type>() != nullptr ? \
         ::cmeta_dataof_cpp<type>() : (fallback_desc))

}

#else

#define CMETA_DATA_SELECT(type, fallback_desc) \
    _Generic((type *)0, \
        _Bool *: &cmeta_data_bool, \
        int *: &cmeta_data_int, \
        long *: &cmeta_data_long, \
        float *: &cmeta_data_float, \
        double *: &cmeta_data_double, \
        default: (fallback_desc))

#define CMETA_DATAOF(type) \
    CMETA_DATA_SELECT(type, (const cmeta_data_desc *)0)
#define CMETA_DATAOF_OR(type, fallback_desc) \
    CMETA_DATA_SELECT(type, (fallback_desc))

#endif

#endif /* CMETA_DATA_SELECT_H */
