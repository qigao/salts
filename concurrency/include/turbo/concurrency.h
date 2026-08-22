#ifndef TURBO_CONCURRENCY_MODULE_H
#define TURBO_CONCURRENCY_MODULE_H

#ifndef TURBO_CONCURRENCY_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define TURBO_CONCURRENCY_API __attribute__((visibility("default")))
  #else
    #define TURBO_CONCURRENCY_API
  #endif
#endif

#ifndef TURBO_CONCURRENCY_C_API
  #ifdef __cplusplus
    #define TURBO_CONCURRENCY_C_API extern "C" TURBO_CONCURRENCY_API
  #else
    #define TURBO_CONCURRENCY_C_API TURBO_CONCURRENCY_API
  #endif
#endif

#endif /* TURBO_CONCURRENCY_MODULE_H */
