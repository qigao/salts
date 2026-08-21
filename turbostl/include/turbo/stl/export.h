#ifndef TURBO_STL_EXPORT_H
#define TURBO_STL_EXPORT_H

#ifdef __cplusplus
#define TURBO_STL_EXTERN_C extern "C"
#else
#define TURBO_STL_EXTERN_C extern
#endif

#if defined(_WIN32) && defined(TURBO_STL_SHARED)
#if defined(TURBO_STL_BUILDING)
#define TURBO_STL_API __declspec(dllexport)
#else
#define TURBO_STL_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define TURBO_STL_API __attribute__((visibility("default")))
#else
#define TURBO_STL_API
#endif

#endif /* TURBO_STL_EXPORT_H */
