#ifndef CMETA_COMPILER_H
#define CMETA_COMPILER_H

/* A missing compiler query reports unsupported; it never enables a substitute
 * implementation. Keep vendor probes here rather than in declaration DSLs. */
#ifdef __has_builtin
#define CMETA_HAS_BUILTIN(name) (__has_builtin(name) != 0)
#else
#define CMETA_HAS_BUILTIN(name) 0
#endif
#ifdef __has_attribute
#define CMETA_HAS_ATTRIBUTE(name) (__has_attribute(name) != 0)
#else
#define CMETA_HAS_ATTRIBUTE(name) 0
#endif
#ifdef __has_feature
#define CMETA_HAS_FEATURE(name) (__has_feature(name) != 0)
#else
#define CMETA_HAS_FEATURE(name) 0
#endif

#if CMETA_HAS_ATTRIBUTE(unused)
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

/* Both helpers require constant operands and contribute integer zero. Flags
 * are nonnegative integer bit sets; the mask is the caller's semantic policy. */
#define CMETA_LAYOUT_REQUIRE(condition) CMETA_CONST_REQUIRE(condition)
#define CMETA_FLAGS_REQUIRE(value, mask) \
    CMETA_CONST_REQUIRE((value) >= 0 && (((value) & (mask)) == (value)))

#if defined(__COUNTER__)
#define CMETA_HAS_COUNTER 1
#define CMETA_COMPILER_COUNTER __COUNTER__
#else
#define CMETA_HAS_COUNTER 0
#endif

#endif
