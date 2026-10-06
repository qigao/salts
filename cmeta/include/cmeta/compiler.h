#ifndef CMETA_COMPILER_H
#define CMETA_COMPILER_H

#include <stddef.h>

/* Preserve the public inline-storage ABI, including MSVC's alignment members. */
#if defined(_MSC_VER)
#define CMETA_MAX_ALIGN_MEMBERS \
    long double _align_long_double; \
    long long _align_long_long; \
    void *_align_pointer;
#else
#define CMETA_MAX_ALIGN_MEMBERS max_align_t _align;
#endif

/* MSVC's C atomic lock-free query is not an integer constant expression.
 * Native slots still require their independent size/alignment layout proof. */
#if !defined(_MSC_VER) || defined(__clang__)
#define CMETA_HAS_ATOMIC_POINTER_LOCK_FREE_CONSTANT 1
#else
#define CMETA_HAS_ATOMIC_POINTER_LOCK_FREE_CONSTANT 0
#endif

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

/* Admit standard zero-argument variadics only. Older modes deliberately keep
 * the explicit-count PP contract even when a vendor accepts an extension. */
#if defined(_MSVC_TRADITIONAL) && _MSVC_TRADITIONAL
#define CMETA_HAS_VA_OPT 0
#elif (defined(__cplusplus) && __cplusplus >= 202002L) || \
      (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L) || \
      (defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L)
#define CMETA_HAS_VA_OPT 1
#else
#define CMETA_HAS_VA_OPT 0
#endif

#if CMETA_HAS_ATTRIBUTE(unused)
#define CMETA_UNUSED __attribute__((unused))
#else
#define CMETA_UNUSED
#endif
#define CMETA_INLINE static inline CMETA_UNUSED
#define CMETA_LOCAL static CMETA_UNUSED

/* This attribute supplies lexical lowering, not resource ownership semantics.
 * Unsupported backends must use an explicit structured scope contract. */
#if CMETA_HAS_ATTRIBUTE(cleanup)
#define CMETA_HAS_CLEANUP 1
#define CMETA_ATTR_CLEANUP(function) __attribute__((cleanup(function)))
#else
#define CMETA_HAS_CLEANUP 0
#endif

/* Section storage is an opt-in backend, not a promise of discovery semantics.
 * ELF requires retain, since used alone does not survive linker GC. */
#define CMETA_SECTION_NONE 0
#define CMETA_SECTION_COFF 1
#define CMETA_SECTION_ELF 2
#define CMETA_SECTION_MACHO 3
#if defined(_WIN32) && defined(_MSC_VER)
#define CMETA_SECTION_BACKEND CMETA_SECTION_COFF
#define CMETA_ATTR_SECTION(name) __pragma(section(name,read,write)) __declspec(allocate(name))
#define CMETA_ATTR_ALIGNED(size) __declspec(align(size))
#if defined(_M_IX86)
#define CMETA_COFF_RETAIN(symbol) __pragma(comment(linker,"/include:_" symbol))
#else
#define CMETA_COFF_RETAIN(symbol) __pragma(comment(linker,"/include:" symbol))
#endif
#elif defined(__ELF__) && CMETA_HAS_ATTRIBUTE(section) && CMETA_HAS_ATTRIBUTE(retain) && \
    CMETA_HAS_ATTRIBUTE(used) && CMETA_HAS_ATTRIBUTE(aligned) && CMETA_HAS_ATTRIBUTE(visibility)
#define CMETA_SECTION_BACKEND CMETA_SECTION_ELF
#define CMETA_ATTR_SECTION(name) __attribute__((section(name)))
#define CMETA_ATTR_ALIGNED(size) __attribute__((aligned(size)))
#elif defined(__APPLE__) && defined(__MACH__) && CMETA_HAS_ATTRIBUTE(section) && \
    CMETA_HAS_ATTRIBUTE(used) && CMETA_HAS_ATTRIBUTE(aligned) && CMETA_HAS_ATTRIBUTE(visibility)
#define CMETA_SECTION_BACKEND CMETA_SECTION_MACHO
#define CMETA_ATTR_SECTION(name) __attribute__((section(name)))
#define CMETA_ATTR_ALIGNED(size) __attribute__((aligned(size)))
#else
#define CMETA_SECTION_BACKEND CMETA_SECTION_NONE
#endif
#if CMETA_HAS_ATTRIBUTE(used)
#define CMETA_ATTR_USED __attribute__((used))
#endif
#if CMETA_HAS_ATTRIBUTE(retain)
#define CMETA_ATTR_RETAIN __attribute__((retain))
#endif
#if CMETA_HAS_ATTRIBUTE(visibility)
#define CMETA_ATTR_HIDDEN __attribute__((visibility("hidden")))
#endif

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

/* Native type syntax is distinct from CMETA_TYPEOF's semantic descriptor.
 * Query fixed types only: GNU typeof may evaluate variably modified operands.
 * C++ references are removed, but cv qualifiers, arrays and functions remain. */
#ifdef __cplusplus
#define CMETA_HAS_NATIVE_TYPEOF 1
#define CMETA_NATIVE_TYPEOF(expression) \
    typename std::remove_reference<decltype((expression))>::type
#elif defined(__clang__)
#if defined(__is_identifier)
#define CMETA_HAS_NATIVE_TYPEOF (!__is_identifier(__typeof__))
#else
#define CMETA_HAS_NATIVE_TYPEOF 0
#endif
#elif defined(__GNUC__)
#define CMETA_HAS_NATIVE_TYPEOF 1
#else
#define CMETA_HAS_NATIVE_TYPEOF 0
#endif
#if !defined(__cplusplus) && CMETA_HAS_NATIVE_TYPEOF
#define CMETA_NATIVE_TYPEOF(expression) __typeof__(expression)
#endif

/* Compare native expression types, including top-level qualifiers. Wrapping
 * each C type in a pointer prevents types_compatible_p from dropping those
 * qualifiers. C compatibility and C++ identity retain their language rules. */
#ifdef __cplusplus
#define CMETA_HAS_SAME_TYPE 1
#define CMETA_SAME_TYPE(a,b) \
    (std::is_same<CMETA_NATIVE_TYPEOF(a),CMETA_NATIVE_TYPEOF(b)>::value)
#elif CMETA_HAS_NATIVE_TYPEOF && CMETA_HAS_BUILTIN(__builtin_types_compatible_p)
#define CMETA_HAS_SAME_TYPE 1
#define CMETA_SAME_TYPE(a,b) \
    __builtin_types_compatible_p(CMETA_NATIVE_TYPEOF(a) *,CMETA_NATIVE_TYPEOF(b) *)
#else
#define CMETA_HAS_SAME_TYPE 0
#endif

/* A named local value initialized exactly once. Do not emulate __auto_type
 * with typeof(expr) name = expr: variably modified operands can run twice. */
#ifdef __cplusplus
#define CMETA_HAS_AUTO 1
#define CMETA_AUTO(name,expression) auto name = (expression)
#elif defined(__clang__)
#if defined(__is_identifier)
#define CMETA_HAS_AUTO (!__is_identifier(__auto_type))
#else
#define CMETA_HAS_AUTO 0
#endif
#elif defined(__GNUC__) && (__GNUC__ * 100 + __GNUC_MINOR__ >= 409)
#define CMETA_HAS_AUTO 1
#else
#define CMETA_HAS_AUTO 0
#endif
#if !defined(__cplusplus) && CMETA_HAS_AUTO
#define CMETA_AUTO(name,expression) __extension__ __auto_type name = (expression)
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
