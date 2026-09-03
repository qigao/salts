#ifndef SALTS_COROUTINE_MODULE_H
#define SALTS_COROUTINE_MODULE_H

#ifndef SALTS_COROUTINE_API
  #if !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
    #define SALTS_COROUTINE_API __attribute__((visibility("default")))
  #else
    #define SALTS_COROUTINE_API
  #endif
#endif

#ifndef SALTS_COROUTINE_C_API
  #ifdef __cplusplus
    #define SALTS_COROUTINE_C_API extern "C" SALTS_COROUTINE_API
  #else
    #define SALTS_COROUTINE_C_API SALTS_COROUTINE_API
  #endif
#endif

#endif /* SALTS_COROUTINE_MODULE_H */
