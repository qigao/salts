#ifndef CMETA_COMPILER_H
#define CMETA_COMPILER_H

#if defined(__GNUC__) || defined(__clang__)
#define CMETA_UNUSED __attribute__((unused))
#else
#define CMETA_UNUSED
#endif
#define CMETA_INLINE static inline CMETA_UNUSED
#define CMETA_LOCAL static CMETA_UNUSED

#ifdef __cplusplus
#include <type_traits>
#define CMETA_ALIGNOF(type) alignof(type)
#define CMETA_STATIC_ASSERT(condition, message) static_assert((condition), message)
#define CMETA_TYPE_MATCHES(expression, type) \
    (std::is_same<decltype(expression), type>::value)
#define CMETA_TYPE_IS_VOID(type) (std::is_void<type>::value)
#define CMETA_TYPE_IS_VALUE(type_) \
    (std::is_same<typename std::decay<type_>::type,type_>::value)
#else
#define CMETA_ALIGNOF(type) _Alignof(type)
#define CMETA_STATIC_ASSERT(condition, message) _Static_assert((condition), message)
#define CMETA_TYPE_MATCHES(expression, type) \
    _Generic((expression), type: 1, default: 0)
#define CMETA_TYPE_IS_VOID(type) _Generic((type *)0, void *: 1, default: 0)
#define CMETA_TYPE_IS_VALUE(type) _Generic(((type *)0)[0], type: 1, default: 0)
#endif

/* Require a constant expression in both languages, including GNU VLA modes. */
#ifdef __cplusplus
#define CMETA_CONST_REQUIRE(condition) \
    (0 * static_cast<int>(sizeof(char[std::integral_constant<bool, (condition)>::value ? 1 : -1])))
#else
#define CMETA_CONST_REQUIRE(condition) \
    (0 * (int)sizeof(struct { unsigned cmeta_required : (condition) ? 1 : -1; }))
#endif

#if defined(__COUNTER__)
#define CMETA_COMPILER_COUNTER __COUNTER__
#endif

#endif
