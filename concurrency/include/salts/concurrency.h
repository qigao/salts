#ifndef SALTS_CONCURRENCY_MODULE_H
#define SALTS_CONCURRENCY_MODULE_H

#ifndef SALTS_CONCURRENCY_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_CONCURRENCY_API __attribute__((visibility("default")))
  #else
    #define SALTS_CONCURRENCY_API
  #endif
#endif

#ifndef SALTS_CONCURRENCY_C_API
  #ifdef __cplusplus
    #define SALTS_CONCURRENCY_C_API extern "C" SALTS_CONCURRENCY_API
  #else
    #define SALTS_CONCURRENCY_C_API SALTS_CONCURRENCY_API
  #endif
#endif

#endif /* SALTS_CONCURRENCY_MODULE_H */
