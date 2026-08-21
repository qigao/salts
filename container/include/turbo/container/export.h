#ifndef TURBO_CONTAINER_EXPORT_H
#define TURBO_CONTAINER_EXPORT_H

#ifdef __cplusplus
#define CONTAINER_EXTERN_C extern "C"
#else
#define CONTAINER_EXTERN_C extern
#endif

#if defined(_WIN32) && defined(TURBO_CONTAINER_SHARED)
#if defined(TURBO_CONTAINER_BUILDING)
#define CONTAINER_API __declspec(dllexport)
#else
#define CONTAINER_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) && __GNUC__ >= 4
#define CONTAINER_API __attribute__((visibility("default")))
#else
#define CONTAINER_API
#endif

#endif /* TURBO_CONTAINER_EXPORT_H */
